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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <folly/fibers/FiberManager.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Optional.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/async.h"
#include "mcrouter/awriter.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Routes a request to a single ProxyClient.
 * This is the lowest level in Mcrouter's RouteHandle tree.
 */
template <class RouteHandleIf>
class DestinationRoute {
 public:
  std::string routeName() const {
    return folly::sformat(
        "host|pool={}|id={}|ap={}|timeout={}ms",
        poolName_,
        indexInPool_,
        destination_->accessPoint()->toString(),
        timeout_.count());
  }

  std::string keyWithFailoverTag(folly::StringPiece fullKey) const {
    const char* const kFailoverTag = ":failover=1";
    return folly::to<std::string>(fullKey, kFailoverTag);
  }

  /**
   * @param destination The destination where the request is to be sent
   */
  DestinationRoute(std::shared_ptr<ProxyDestination> destination,
                   std::string poolName,
                   size_t indexInPool,
                   std::chrono::milliseconds timeout,
                   bool keepRoutingPrefix) :
      destination_(std::move(destination)),
      poolName_(std::move(poolName)),
      indexInPool_(indexInPool),
      timeout_(timeout),
      keepRoutingPrefix_(keepRoutingPrefix) {
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    auto* ctx = fiber_local::getTraverseCtx();
    if (ctx) {
      ctx->recordDestination(poolName_, indexInPool_,
                             *destination_->accessPoint());
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, DeleteLikeT<Request> = 0) const {
    auto reply = routeWithDestination(req);
    if (isFailoverErrorResult(reply.result()) && spool(req)) {
      reply = createReply(DefaultReply, req);
      reply.setDestination(destination_->accessPoint());
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> route(const Request& req,
                        OtherThanT<Request, DeleteLike<>> = 0) const {
    return routeWithDestination(req);
  }

 private:
  const std::shared_ptr<ProxyDestination> destination_;
  const std::string poolName_;
  const size_t indexInPool_;
  const std::chrono::milliseconds timeout_;
  const bool keepRoutingPrefix_;
  size_t pendingShadowReqs_{0};

  template <class Request>
  ReplyT<Request> routeWithDestination(const Request& req) const {
    auto reply = checkAndRoute(req);
    reply.setDestination(destination_->accessPoint());
    return reply;
  }

  template <class Request>
  ReplyT<Request> checkAndRoute(const Request& req) const {
    auto& ctx = fiber_local::getSharedCtx();
    if (!destination_->may_send()) {
      return constructAndLog(req, *ctx, TkoReply);
    }

    if (destination_->shouldDrop<Request>()) {
      return constructAndLog(req, *ctx, BusyReply);
    }

    if (ctx->recording()) {
      ctx->recordDestination(poolName_, indexInPool_,
                             *destination_->accessPoint());
      return constructAndLog(req, *ctx, DefaultReply, req);
    }

    auto proxy = &ctx->proxy();
    auto requestClass = fiber_local::getRequestClass();
    if (requestClass.is(RequestClass::kShadow)) {
      if (proxy->router().opts().target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >=
          proxy->router().opts().target_max_shadow_requests) {
        return constructAndLog(req, *ctx, ErrorReply);
      }
      auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
      ++mutableCounter;
    }

    SCOPE_EXIT {
      if (requestClass.is(RequestClass::kShadow)) {
        auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
        --mutableCounter;
      }
    };

    return doRoute(req, *ctx);
  }

  template <class Request, class... Args>
  ReplyT<Request> constructAndLog(const Request& req,
                                  ProxyRequestContext& ctx,
                                  Args&&... args) const {
    auto now = nowUs();
    auto reply = createReply<Request>(std::forward<Args>(args)...);
    ctx.onReplyReceived(poolName_,
                        *destination_->accessPoint(),
                        folly::StringPiece(),
                        req,
                        reply,
                        now,
                        now,
                        fiber_local::getReplyStatsContext());
    return reply;
  }

  template <class Request>
  ReplyT<Request> doRoute(const Request& req,
                          ProxyRequestContext& ctx) const {
    DestinationRequestCtx dctx(nowUs());
    folly::Optional<Request> newReq;
    folly::StringPiece strippedRoutingPrefix;
    if (!keepRoutingPrefix_ && !req.key().routingPrefix().empty()) {
      newReq.emplace(req);
      newReq->key().stripRoutingPrefix();
      strippedRoutingPrefix = req.key().routingPrefix();
    }

    if (fiber_local::getFailoverTag()) {
      if (!newReq) {
        newReq.emplace(req);
      }
      auto newKey = keyWithFailoverTag(newReq->key().fullKey());
      /* It's always safe to not append a failover tag */
      if (newKey.size() <= MC_KEY_MAX_LEN) {
        newReq->key() = std::move(newKey);
      }
    }

    const auto& reqToSend = newReq ? *newReq : req;
    auto reply = destination_->send(reqToSend, dctx, timeout_);
    ctx.onReplyReceived(poolName_,
                        *destination_->accessPoint(),
                        strippedRoutingPrefix,
                        reqToSend,
                        reply,
                        dctx.startTime,
                        dctx.endTime,
                        fiber_local::getReplyStatsContext());
    return reply;
  }

  template <class Request>
  bool spool(const Request& req) const {
    auto asynclogName = fiber_local::getAsynclogName();
    if (asynclogName.empty()) {
      return false;
    }

    folly::StringPiece key = keepRoutingPrefix_ ?
      req.key().fullKey() :
      req.key().keyWithoutRoute();

    auto proxy = &fiber_local::getSharedCtx()->proxy();
    auto& ap = *destination_->accessPoint();
    folly::fibers::Baton b;
    auto res = proxy->router().asyncWriter().run(
      [&b, &ap, proxy, key, asynclogName] () {
        asynclog_delete(proxy, ap, key, asynclogName);
        b.post();
      }
    );
    if (!res) {
      MC_LOG_FAILURE(proxy->router().opts(),
                     memcache::failure::Category::kOutOfResources,
                     "Could not enqueue asynclog request (key {}, pool {})",
                     key, asynclogName);
    } else {
      /* Don't reply to the user until we safely logged the request to disk */
      b.wait();
      proxy->stats().increment(asynclog_requests_stat);
    }
    return true;
  }
};

}}}  // facebook::memcache::mcrouter
