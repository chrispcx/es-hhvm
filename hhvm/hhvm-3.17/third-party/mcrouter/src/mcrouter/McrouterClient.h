/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/IntrusiveList.h>
#include <folly/Range.h>

#include "mcrouter/lib/CacheClientStats.h"
#include "mcrouter/lib/fbi/counting_sem.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterClient;
class McrouterInstance;
class Proxy;
class ProxyRequestContext;

/**
 * A mcrouter client is used to communicate with a mcrouter instance.
 * Typically a client is long lived. Request sent through a single client
 * will be sent to the same mcrouter thread that's determined once on creation.
 *
 * Create via McrouterInstance::createClient().
 */
class McrouterClient {
 private:
  struct Disconnecter {
    void operator() (McrouterClient* client) {
      client->disconnected_ = true;
      /* We only access self_ when we need to send a request, which only
         the user can do. Since the user is destroying the pointer,
         there could be no concurrent send and this write is safe.

         Note: not client->self_.reset(), since this could destroy client
         from inside the call to reset(), destroying self_ while the method
         is still running. */
      auto stolenPtr = std::move(client->self_);
    }
  };
  folly::IntrusiveListHook hook_;

 public:
  using Pointer = std::unique_ptr<McrouterClient, Disconnecter>;
  using Queue = folly::IntrusiveList<McrouterClient,
                                     &McrouterClient::hook_>;

  /**
   * Asynchronously send a single request with the given operation.
   *
   * @param callback  the callback to call when request is completed,
   *                  should be callable
   *                    f(const Request& request, ReplyT<Request>&& reply)
   *
   *                  result mc_res_unknown means that the request was canceled.
   *                  It will be moved into a temporary storage before being
   *                  called. Will be destroyed only after callback is called,
   *                  but may be delayed, until all sub-requests are processed.
   *
   * @return true iff the request was scheduled to be sent / was sent,
   *         false if some error happened (e.g. McrouterInstance was destroyed).
   *
   * Note: the caller is responsible for keeping the request alive until the
   *       callback is called.
   */
  template <class Request, class F>
  /* Don't attempt instantiation when we want the other overload of send() */
  bool send(const Request& req,
            F&& callback,
            folly::StringPiece ipAddr = folly::StringPiece());

  /**
   * Multi requests version of send.
   * @param callback  callback to call for each request, should be callable
   *                    f(const Request& request, ReplyT<Request>&& reply)
   *                  Note: callback should be copyable.
   *
   * @return true iff the requests were scheduled for sending,
   *         false otherwise (e.g. McrouterInstance was destroyed).
   *
   * Note: the caller is responsible for keeping requests alive until the
   *       callback is called for each of them.
   * Note: the order in which callbacks will be called is undefined, but
   *       it's guaranteed that the callback is called exactly once for
   *       each request.
   */
  template <class InputIt, class F>
  bool send(
      InputIt begin,
      InputIt end,
      F&& callback,
      folly::StringPiece ipAddr = folly::StringPiece());

  CacheClientCounters getStatCounters() noexcept {
    return stats_.getCounters();
  }

  /**
   * Unique client id. Ids are not re-used for the lifetime of the process.
   */
  uint64_t clientId() const {
    return clientId_;
  }

  /**
   * Override default proxy assignment.
   */
  void setProxy(Proxy* proxy) {
    proxy_ = proxy;
  }

  McrouterClient(const McrouterClient&) = delete;
  McrouterClient(McrouterClient&&) noexcept = delete;
  McrouterClient& operator=(const McrouterClient&) = delete;
  McrouterClient& operator=(McrouterClient&&) = delete;

  ~McrouterClient();

 private:
  std::weak_ptr<McrouterInstance> router_;
  bool sameThread_{false};

  Proxy* proxy_{nullptr};

  CacheClientStats stats_;

  /// Maximum allowed requests in flight (unlimited if 0)
  const unsigned int maxOutstanding_;
  /// If true, error is immediately returned when maxOutstanding_ limit is hit
  /// If false, sender thread is blocked when maxOutstanding_ limit is hit
  const bool maxOutstandingError_;
  counting_sem_t outstandingReqsSem_;

  /**
   * Automatically-assigned client id, used for QOS for different clients
   * sharing the same connection.
   */
  uint64_t clientId_;

  /**
   * The user let go of the McrouterClient::Pointer, and the object
   * is pending destruction when all requests complete.
   */
  std::atomic<bool> disconnected_{false};

  /**
   * The ownership is shared between the user and the outstanding requests.
   */
  std::shared_ptr<McrouterClient> self_;

  McrouterClient(
    std::weak_ptr<McrouterInstance> router,
    size_t maximum_outstanding,
    bool maximum_outstanding_error,
    bool sameThread);

  static Pointer create(
    std::weak_ptr<McrouterInstance> router,
    size_t maximum_outstanding,
    bool maximum_outstanding_error,
    bool sameThread);

  /**
   * Batch send requests.
   *
   * @param nreqs          number of requests to be sent.
   * @param makeNextPreq   proxy request generator.
   * @param failRemaining  will be called if all remaining requests should be
   *                       canceled due to maxOutstandingError_ flag
   */
  template <class F, class G>
  bool sendMultiImpl(size_t nreqs, F&& makeNextPreq, G&& failRemaining);

  void sendRemoteThread(std::unique_ptr<ProxyRequestContext> req);
  void sendSameThread(std::unique_ptr<ProxyRequestContext> req);

  friend class McrouterInstance;
  friend class ProxyRequestContext;
};

}}} // facebook::memcache::mcrouter

#include "McrouterClient-inl.h"
