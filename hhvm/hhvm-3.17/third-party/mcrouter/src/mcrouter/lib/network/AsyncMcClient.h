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

#include <chrono>
#include <functional>
#include <utility>

#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"
#include "mcrouter/lib/Operation.h"

namespace folly {
class EventBase;
} // folly

namespace facebook { namespace memcache {

class AsyncMcClientImpl;

/**
 * A class for network communication with memcache protocol.
 *
 * This class serves as a public interface and gateway to the client
 * implementation. It guarantees that all requests will be processed even after
 * this client was destroyed (i.e. the base client will be kept alive as long
 * as we have at least one request, but it will be impossible to send more
 * requests).
 */
class AsyncMcClient {
 public:

  AsyncMcClient(folly::EventBase& eventBase,
                ConnectionOptions options);

  /**
   * Close connection and fail all outstanding requests immediately.
   */
  void closeNow();

  /**
   * Set status callbacks for the underlying connection.
   *
   * @param onUp  will be called whenever client successfully connects to the
   *              server. Will be called immediately if we're already connected.
   *              Can be nullptr.
   * @param onDown  will be called whenever connection goes down. Will not be
   *                called if the connection is already DOWN.
   *                Can be nullptr.
   * Note: those callbacks may be called even after the client was destroyed.
   *       This will happen in case when the client is destroyed and there are
   *       some requests left, for wich reply callback wasn't called yet.
   */
  void setStatusCallbacks(
    std::function<void()> onUp,
    std::function<void(bool aborting)> onDown);

  /**
   * Set callbacks for when requests state change.
   *
   * @param onStateChange   Will be called whenever a request changes state.
   *                        pendingDiff and inflightDiff will hold the
   *                        difference in the number of pending and inflight
   *                        requests, respectively.
   * @param onWrite           Will be called everytime AsyncMcClient is about to
   *                          write data to network. The numToSend argument
   *                          holds the number of requests that will be sent in
   *                          a single batch.
   */
  void setRequestStatusCallbacks(
      std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
      std::function<void(int numToSend)> onWrite);

  /**
   * Set callback for reply stats updates.
   *
   * @param replyStatsCallback    Will be called whenever a reply is received.
   *                              Called within the fiber.
   */
  void setReplyStatsCallback(
      std::function<void(ReplyStatsContext)> replyStatsCallback);

  /**
   * Send request synchronously (i.e. blocking call).
   * Note: it must be called only from fiber context. It will block the current
   *       stack and will send request only when we loop EventBase.
   */
  template <class Request>
  ReplyT<Request> sendSync(const Request& request,
                           std::chrono::milliseconds timeout);

  /**
   * Set throttling options.
   *
   * @param maxInflight  max number of requests that can be waiting for the
   *                     network reply (0 means unlimited).
   * @param maxPending  max number of requests that can be waiting to be
   *                    sent over the network (0 means unlimited). If on attempt
   *                    to send a new request we're going to exceed this limit,
   *                    then that request would fail and the callback would be
   *                    called with a proper error code immediately
   *                    (e.g. local error).
   *                    Also user should not expect to be able to put more
   *                    than maxPending requests into the queue at once (i.e.
   *                    user should not expect to be able to send
   *                    maxPending+maxInflight requests at once).
   * Note: will not affect already sent or pending requests. None of them would
   *       be dropped.
   */
  void setThrottle(size_t maxInflight, size_t maxPending);

  /**
   * Get the number of requests in pending queue. Those requests have not been
   * sent to the network yet, this means that in case of remote error we can
   * still try to send them.
   */
  size_t getPendingRequestCount() const;

  /**
   * Get the number of requests in inflight queue. This amounts for requests
   * that are currently been written to the socket and requests that were
   * already sent to the server and are waiting for replies. Those requests
   * might be already processed by the server, thus they wouldn't be
   * retransmitted in case of error.
   */
  size_t getInflightRequestCount() const;

  /**
   * Update send and connect timeout. If new value is larger than current
   * it is ignored.
   */
  void updateWriteTimeout(std::chrono::milliseconds timeout);

  /**
   * @return        The transport used to manage socket
   */
  const folly::AsyncTransportWrapper* getTransport();

  /**
   * @return Retransmits per packet used to detect lossy connections
   */
  double getRetransmissionInfo();

  /**
   * Get the drop probability
   */
  template <class Request>
  double getDropProbability() const;

 private:
  std::shared_ptr<AsyncMcClientImpl> base_;
};

}} // facebook::memcache

#include "AsyncMcClient-inl.h"
