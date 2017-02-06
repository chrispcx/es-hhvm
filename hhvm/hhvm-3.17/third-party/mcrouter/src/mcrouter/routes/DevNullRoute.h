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

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Same as NullRoute, but with Mcrouter stats reporting.
 */
template <class RouteHandleIf>
class DevNullRoute {
 public:
  static std::string routeName() { return "devnull"; }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const { }

  template <class Request>
  static ReplyT<Request> route(const Request& req) {
    auto& ctx = fiber_local::getSharedCtx();
    ctx->proxy().stats().increment(dev_null_requests_stat);
    return createReply(DefaultReply, req);
  }
};

}}}  // facebook::memcache::mcrouter
