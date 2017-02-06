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

#include <memory>
#include <string>

#include <folly/fibers/FiberManager.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McRequestList.h"
#include "mcrouter/lib/RequestLoggerContext.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyRequestLogger.h"
#include "mcrouter/ProxyRequestPriority.h"

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

template <class RouteHandleIf>
class ProxyRoute;

class McrouterClient;
class ShardSplitter;

/**
 * This object is alive for the duration of user's request,
 * including any subrequests that might have been sent out.
 *
 * It starts it's life under a unique_ptr outside of proxy threads.
 * When handed off to a proxy thread and ready to execute,
 * we save the current configuration and convert it to shared
 * ownership.
 *
 * Records collected stats on destruction.
 */
class ProxyRequestContext {
public:
  using ClientCallback = std::function<
    void(folly::StringPiece, size_t, const AccessPoint&)>;
  using ShardSplitCallback = std::function<void(const ShardSplitter&)>;

  /**
   * A request with this context will not be sent/logged anywhere.
   *
   * @param clientCallback  If non-nullptr, called by DestinationRoute when
   *   the request would normally be sent to destination;
   *   also in traverse() of DestinationRoute.
   * @param shardSplitCallback  If non-nullptr, called by ShardSplitRoute
   *   in traverse() with itself as the argument.
   */
  static std::shared_ptr<ProxyRequestContext> createRecording(
      Proxy& proxy,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback = nullptr);

  /**
   * Same as createRecording(), but also notifies the baton
   * when this context is destroyed (i.e. all requests referencing it
   * finish executing).
   */
  static std::shared_ptr<ProxyRequestContext> createRecordingNotify(
      Proxy& proxy,
      folly::fibers::Baton& baton,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback = nullptr);

  virtual ~ProxyRequestContext();

  Proxy& proxy() {
    return proxy_;
  }

  bool recording() const noexcept {
    return recording_;
  }

  void recordDestination(folly::StringPiece poolName,
                         size_t index,
                         const AccessPoint& ap) const {
    if (recording_ && recordingState_->clientCallback) {
      recordingState_->clientCallback(poolName, index, ap);
    }
  }

  void recordShardSplitter(const ShardSplitter& splitter) const {
    if (recording_ && recordingState_->shardSplitCallback) {
      recordingState_->shardSplitCallback(splitter);
    }
  }

  uint64_t senderId() const;

  void setSenderIdForTest(uint64_t id);

  bool failoverDisabled() const {
    return failoverDisabled_;
  }

  ProxyRequestPriority priority() const {
    return priority_;
  }

  /**
   * Called once a reply is received to record a stats sample if required.
   */
  template <class Request>
  void onReplyReceived(const std::string& poolName,
                       const AccessPoint& ap,
                       folly::StringPiece strippedRoutingPrefix,
                       const Request& request,
                       const ReplyT<Request>& reply,
                       const int64_t startTimeUs,
                       const int64_t endTimeUs,
                       const ReplyStatsContext replyStatsContext) {
    if (recording_) {
      return;
    }

    RequestLoggerContext loggerContext(
        poolName,
        ap,
        strippedRoutingPrefix,
        request,
        reply,
        startTimeUs,
        endTimeUs,
        replyStatsContext);
    assert(logger_.hasValue());
    logger_->log<Request>(loggerContext);
    assert(additionalLogger_.hasValue());
    additionalLogger_->log(loggerContext);
  }

  /**
   * Continues processing current request.
   * Should be called only from the attached proxy thread.
   */
  virtual void startProcessing() {
    throw std::logic_error(
        "Calling startProcessing on an incomplete instance "
        "of ProxyRequestContext");
  }

  const std::string& userIpAddress() const noexcept {
    return userIpAddr_;
  }

  void setUserIpAddress(folly::StringPiece newAddr) noexcept {
    userIpAddr_ = newAddr.str();
  }

  /**
   * Returns the id of this requests.
   */
  uint64_t requestId() const;

 protected:
  bool replied_{false};

  ProxyRequestContext(Proxy& pr, ProxyRequestPriority priority__);

 private:
  const uint64_t requestId_;
  Proxy& proxy_;
  bool failoverDisabled_{false};

  /** If true, this is currently being processed by a proxy and
      we want to notify we're done on destruction. */
  bool processing_{false};

  bool recording_{false};

  std::shared_ptr<McrouterClient> requester_;

  struct RecordingState {
    ClientCallback clientCallback;
    ShardSplitCallback shardSplitCallback;
  };

  union {
    void* context_{nullptr};
    std::unique_ptr<RecordingState> recordingState_;
  };

  /**
   * The function that will be called when all replies (including async)
   * come back.
   * Guaranteed to be called after enqueueReply_ (right after in sync mode).
   */
  void (*reqComplete_)(ProxyRequestContext& preq){nullptr};

  folly::Optional<ProxyRequestLogger> logger_;
  folly::Optional<AdditionalProxyRequestLogger> additionalLogger_;

  uint64_t senderIdForTest_{0};

  ProxyRequestPriority priority_{ProxyRequestPriority::kCritical};

  std::string userIpAddr_;

  enum RecordingT { Recording };
  ProxyRequestContext(
      RecordingT,
      Proxy& pr,
      ClientCallback clientCallback,
      ShardSplitCallback shardSplitCallback);

  ProxyRequestContext(const ProxyRequestContext&) = delete;
  ProxyRequestContext(ProxyRequestContext&&) noexcept = delete;
  ProxyRequestContext& operator=(const ProxyRequestContext&) = delete;
  ProxyRequestContext& operator=(ProxyRequestContext&&) = delete;

 public:
  /* Do not use for new code */
  class LegacyPrivateAccessor {
   public:
    using ReqCompleteFunc = void (*)(ProxyRequestContext&);

    static ReqCompleteFunc& reqComplete(ProxyRequestContext& preq) {
      return preq.reqComplete_;
    }

    static void*& context(ProxyRequestContext& preq) {
      assert(!preq.recording_);
      return preq.context_;
    }

    static bool& failoverDisabled(ProxyRequestContext& preq) {
      return preq.failoverDisabled_;
    }
  };

private:
  friend class McrouterClient;
  friend class Proxy;
};

template <class RouteHandleIf, class Request>
class ProxyRequestContextTyped : public ProxyRequestContext {
 public:
  using Type = ProxyRequestContextTyped<RouteHandleIf, Request>;
  /**
   * Sends the reply for this proxy request.
   * @param newReply the message that we are sending out as the reply
   *   for the request we are currently handling
   */
  void sendReply(ReplyT<Request>&& reply);

  /**
   * DEPRECATED. Convenience method, that constructs reply and calls
   * non-template method.
   *
   * WARNING: This function can be dangerous with new typed requests.
   * For typed requests,
   *   ctx->sendReply(mc_res_local_error, "Error message")
   * does the right thing, while
   *   ctx->sendReply(mc_res_found, "value")
   * does the wrong thing.
   */
  template <class... Args>
  void sendReply(Args&&... args) {
    sendReply(ReplyT<Request>(std::forward<Args>(args)...));
  }

  void startProcessing() override final;

  const ProxyConfig<RouteHandleIf>& proxyConfig() const {
    assert(!recording());
    return *config_;
  }

  ProxyRoute<RouteHandleIf>& proxyRoute() const {
    assert(!recording());
    return config_->proxyRoute();
  }

  /**
   * Internally converts the context into one ready to route.
   * Config pointer is saved to keep the config alive, and
   * ownership is changed to shared so that all subrequests
   * keep track of this context.
   */
  static std::shared_ptr<Type> process(
      std::unique_ptr<Type> preq,
      std::shared_ptr<const ProxyConfig<RouteHandleIf>> config);

 protected:
  ProxyRequestContextTyped(
      Proxy& pr,
      const Request& req,
      ProxyRequestPriority priority__)
      : ProxyRequestContext(pr, priority__), req_(&req) {}

  virtual void sendReplyImpl(ReplyT<Request>&& reply) = 0;

  std::shared_ptr<const ProxyConfig<RouteHandleIf>> config_;

  // It's guaranteed to point to an existing request until we call user callback
  // (i.e. replied_ changes to true), after that it's nullptr.
  const Request* req_;
};

/**
 * Creates a new proxy request context
 */
template <class Request, class F>
std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
createProxyRequestContext(
    Proxy& pr,
    const Request& req,
    F&& f,
    ProxyRequestPriority priority = ProxyRequestPriority::kCritical);
}}}  // facebook::memcache::mcrouter

#include "ProxyRequestContext-inl.h"
