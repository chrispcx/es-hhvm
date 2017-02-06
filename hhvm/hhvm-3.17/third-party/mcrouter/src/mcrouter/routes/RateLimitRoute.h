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
#include <vector>

#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/RateLimiter.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Requests sent through this route will be rate limited according
 * to settings in the RateLimiter passed to the constructor.
 *
 * See comments in TokenBucket.h for algorithm details.
 */
template <class RouteHandleIf>
class RateLimitRoute {
 public:
  std::string routeName() const {
    auto rlStr = rl_.toDebugStr();
    if (rlStr.empty()) {
      return "rate-limit";
    }
    return "rate-limit|" + rlStr;
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*target_, req);
  }

  RateLimitRoute(std::shared_ptr<RouteHandleIf> target, RateLimiter rl)
      : target_(std::move(target)),
        rl_(std::move(rl)) {
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    if (LIKELY(rl_.canPassThrough<Request>())) {
      return target_->route(req);
    }
    return createReply(DefaultReply, req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> target_;
  RateLimiter rl_;
};

}}}  // facebook::memcache::mcrouter
