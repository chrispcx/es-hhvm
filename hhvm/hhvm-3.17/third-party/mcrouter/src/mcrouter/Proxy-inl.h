/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

bool processGetServiceInfoRequest(
    const McGetRequest& req,
    std::shared_ptr<ProxyRequestContextTyped<
        McrouterRouteHandleIf, McGetRequest>>& ctx);

template <class Request>
bool processGetServiceInfoRequest(
    const Request&,
    std::shared_ptr<
        ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>&) {
  return false;
}

template <class GetRequest>
bool processGetServiceInfoRequestImpl(
    const GetRequest& req,
    std::shared_ptr<
        ProxyRequestContextTyped<McrouterRouteHandleIf, GetRequest>>& ctx,
    GetLikeT<GetRequest> = 0);

} // detail

template <class Request>
Proxy::WaitingRequest<Request>::WaitingRequest(
    const Request& req,
    std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
        ctx)
    : req_(req), ctx_(std::move(ctx)) {}

template <class Request>
void Proxy::WaitingRequest<Request>::process(Proxy* proxy) {
  // timePushedOnQueue_ is nonnegative only if waiting-requests-timeout is
  // enabled
  if (timePushedOnQueue_ >= 0) {
    const auto durationInQueueUs = nowUs() - timePushedOnQueue_;

    if (durationInQueueUs >
        1000 * static_cast<int64_t>(
          proxy->getRouterOptions().waiting_request_timeout_ms)) {
      ctx_->sendReply(mc_res_busy);
      return;
    }
  }

  proxy->processRequest(req_, std::move(ctx_));
}

template <class Request>
typename std::enable_if<TRequestListContains<Request>::value, void>::type
Proxy::routeHandlesProcessRequest(
    const Request& req,
    std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
        uctx) {
  auto sharedCtx =
      ProxyRequestContextTyped<McrouterRouteHandleIf, Request>::process(
          std::move(uctx), getConfig());

  if (detail::processGetServiceInfoRequest(req, sharedCtx)) {
    return;
  }

  auto funcCtx = sharedCtx;

  fiberManager_.addTaskFinally(
      [&req, ctx = std::move(funcCtx)]() mutable {
        try {
          auto& proute = ctx->proxyRoute();
          fiber_local::setSharedCtx(std::move(ctx));
          return proute.route(req);
        } catch (const std::exception& e) {
          auto err = folly::sformat(
              "Error routing request of type {}!"
              " Exception: {}",
              typeid(Request).name(), e.what());
          ReplyT<Request> reply(mc_res_local_error);
          reply.message() = std::move(err);
          return reply;
        }
      },
      [ctx = std::move(sharedCtx)](
          folly::Try<ReplyT<Request>>&& reply) {
        ctx->sendReply(std::move(*reply));
      });
}

template <class Request>
typename std::enable_if<!TRequestListContains<Request>::value, void>::type
Proxy::routeHandlesProcessRequest(
    const Request&,
    std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
        uctx) {
  auto err = folly::sformat(
      "Couldn't route request of type {} "
      "because the operation is not supported by RouteHandles "
      "library!",
      typeid(Request).name());
  uctx->sendReply(mc_res_local_error, err);
}

template <class Request>
void Proxy::processRequest(
    const Request& req,
    std::unique_ptr<ProxyRequestContextTyped<McrouterRouteHandleIf, Request>>
        ctx) {
  assert(!ctx->processing_);
  ctx->processing_ = true;
  ++numRequestsProcessing_;
  stats().increment(proxy_reqs_processing_stat);
  bumpStats(req);

  routeHandlesProcessRequest(req, std::move(ctx));

  stats().increment(request_sent_stat);
  stats().increment(request_sent_count_stat);
}

template <class RouteHandleIf, class Request>
void Proxy::dispatchRequest(
    const Request& req,
    std::unique_ptr<ProxyRequestContextTyped<RouteHandleIf, Request>> ctx) {
  if (rateLimited(ctx->priority(), req)) {
    if (getRouterOptions().proxy_max_throttled_requests > 0 &&
        numRequestsWaiting_ >=
            getRouterOptions().proxy_max_throttled_requests) {
      ctx->sendReply(mc_res_busy);
      return;
    }
    auto& queue = waitingRequests_[static_cast<int>(ctx->priority())];
    auto w = folly::make_unique<WaitingRequest<Request>>(
        req, std::move(ctx));
    // Only enable timeout on waitingRequests_ queue when queue throttling is
    // enabled
    if (getRouterOptions().proxy_max_inflight_requests > 0 &&
        getRouterOptions().proxy_max_throttled_requests > 0 &&
        getRouterOptions().waiting_request_timeout_ms > 0) {
      w->setTimePushedOnQueue(nowUs());
    }
    queue.pushBack(std::move(w));
    ++numRequestsWaiting_;
    stats().increment(proxy_reqs_waiting_stat);
  } else {
    processRequest(req, std::move(ctx));
  }
}

template <>
inline void Proxy::bumpStats(const McStatsRequest&) {
  stats().increment(cmd_stats_stat);
  stats().increment(cmd_stats_count_stat);
}

template <>
inline void Proxy::bumpStats(const McCasRequest&) {
  stats().increment(cmd_cas_stat);
  stats().increment(cmd_cas_count_stat);
}

template <>
inline void Proxy::bumpStats(const McGetRequest&) {
  stats().increment(cmd_get_stat);
  stats().increment(cmd_get_count_stat);
}

template <>
inline void Proxy::bumpStats(const McGetsRequest&) {
  stats().increment(cmd_gets_stat);
  stats().increment(cmd_gets_count_stat);
}

template <>
inline void Proxy::bumpStats(const McMetagetRequest&) {
  stats().increment(cmd_meta_stat);
}

template <>
inline void Proxy::bumpStats(const McAddRequest&) {
  stats().increment(cmd_add_stat);
  stats().increment(cmd_add_count_stat);
}

template <>
inline void Proxy::bumpStats(const McReplaceRequest&) {
  stats().increment(cmd_replace_stat);
  stats().increment(cmd_replace_count_stat);
}

template <>
inline void Proxy::bumpStats(const McSetRequest&) {
  stats().increment(cmd_set_stat);
  stats().increment(cmd_set_count_stat);
}

template <>
inline void Proxy::bumpStats(const McIncrRequest&) {
  stats().increment(cmd_incr_stat);
  stats().increment(cmd_incr_count_stat);
}

template <>
inline void Proxy::bumpStats(const McDecrRequest&) {
  stats().increment(cmd_decr_stat);
  stats().increment(cmd_decr_count_stat);
}

template <>
inline void Proxy::bumpStats(const McDeleteRequest&) {
  stats().increment(cmd_delete_stat);
  stats().increment(cmd_delete_count_stat);
}

template <>
inline void Proxy::bumpStats(const McLeaseSetRequest&) {
  stats().increment(cmd_lease_set_stat);
  stats().increment(cmd_lease_set_count_stat);
}

template <>
inline void Proxy::bumpStats(const McLeaseGetRequest&) {
  stats().increment(cmd_lease_get_stat);
  stats().increment(cmd_lease_get_count_stat);
}

template <class Request>
inline void Proxy::bumpStats(const Request&) {
  stats().increment(cmd_other_stat);
  stats().increment(cmd_other_count_stat);
}

template <>
inline bool Proxy::rateLimited(
    ProxyRequestPriority priority,
    const McStatsRequest&) const {
  return false;
}

template <>
inline bool Proxy::rateLimited(
    ProxyRequestPriority priority,
    const McVersionRequest&) const {
  return false;
}

template <class Request>
inline bool Proxy::rateLimited(ProxyRequestPriority priority, const Request&)
    const {
  if (!getRouterOptions().proxy_max_inflight_requests) {
    return false;
  }

  if (waitingRequests_[static_cast<int>(priority)].empty() &&
      numRequestsProcessing_ < getRouterOptions().proxy_max_inflight_requests) {
    return false;
  }

  return true;
}
} // mcrouter
} // memcache
} // facebook
