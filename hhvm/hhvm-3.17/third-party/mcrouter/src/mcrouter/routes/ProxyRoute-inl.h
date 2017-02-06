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

#include <folly/Optional.h>

#include "mcrouter/McrouterInstance.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/lib/network/gen/MemcacheRouteHandleIf.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/RootRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr rh,
                                         BigValueRouteOptions options);
McrouterRouteHandlePtr createLoggingRoute(McrouterRouteHandlePtr rh);

namespace detail {

template <class RouteHandleIf>
typename std::enable_if<
    std::is_same<RouteHandleIf, MemcacheRouteHandleIf>::value,
    std::shared_ptr<RouteHandleIf>>::type
wrapWithBigValueRoute(
    std::shared_ptr<RouteHandleIf> ch,
    const McrouterOptions& routerOpts) {
  BigValueRouteOptions options(
      routerOpts.big_value_split_threshold, routerOpts.big_value_batch_size);
  return makeBigValueRoute(std::move(ch), std::move(options));
}

template <class RouteHandleIf>
typename std::enable_if<
    !std::is_same<RouteHandleIf, MemcacheRouteHandleIf>::value,
    std::shared_ptr<RouteHandleIf>>::type
wrapWithBigValueRoute(
    std::shared_ptr<RouteHandleIf> ch,
    const McrouterOptions& routerOpts) {
  return std::move(ch);
}

} // detail

template <class RouteHandleIf>
ProxyRoute<RouteHandleIf>::ProxyRoute(
    Proxy* proxy,
    const RouteSelectorMap<RouteHandleIf>& routeSelectors)
    : proxy_(proxy),
      root_(makeMcrouterRouteHandle<RootRoute>(proxy_, routeSelectors)) {
  if (proxy_->getRouterOptions().big_value_split_threshold != 0) {
    root_ = detail::wrapWithBigValueRoute(
        std::move(root_), proxy_->getRouterOptions());
  }
  if (proxy_->getRouterOptions().enable_logging_route) {
    root_ = createLoggingRoute(std::move(root_));
  }
}

template <class RouteHandleIf>
std::vector<std::shared_ptr<RouteHandleIf>>
ProxyRoute<RouteHandleIf>::getAllDestinations() const {
  std::vector<std::shared_ptr<RouteHandleIf>> rh;
  for (auto& it : proxy_->getConfig()->getPools()) {
    rh.insert(rh.end(), it.second.begin(), it.second.end());
  }
  return rh;
}

} // mcrouter
} // memcache
} // facebook
