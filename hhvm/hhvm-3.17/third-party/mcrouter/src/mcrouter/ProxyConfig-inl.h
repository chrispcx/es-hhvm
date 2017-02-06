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

#include <folly/Conv.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "mcrouter/PoolFactory.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/routes/PrefixSelectorRoute.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouteHandleIf>
ProxyConfig<RouteHandleIf>::ProxyConfig(
    Proxy& proxy,
    const folly::dynamic& json,
    std::string configMd5Digest,
    PoolFactory& poolFactory)
    : configMd5Digest_(std::move(configMd5Digest)) {
  McRouteHandleProvider provider(proxy, poolFactory);
  RouteHandleFactory<RouteHandleIf> factory(provider, proxy.getId());

  checkLogic(json.isObject(), "Config is not an object");

  if (auto jNamedHandles = json.get_ptr("named_handles")) {
    if (jNamedHandles->isArray()) {
      for (const auto& it : *jNamedHandles) {
        factory.create(it);
      }
    } else if (jNamedHandles->isObject()) {
      for (const auto& it : jNamedHandles->items()) {
        factory.addNamed(it.first.stringPiece(), it.second);
      }
    } else {
      throwLogic("named_handles is {} expected array/object",
                 jNamedHandles->typeName());
    }
  }

  RouteSelectorMap<RouteHandleIf> routeSelectors;

  auto jRoute = json.get_ptr("route");
  auto jRoutes = json.get_ptr("routes");
  checkLogic(!jRoute || !jRoutes,
             "Invalid config: both 'route' and 'routes' are specified");
  if (jRoute) {
    routeSelectors[proxy.getRouterOptions().default_route] =
        std::make_shared<PrefixSelectorRoute<RouteHandleIf>>(factory, *jRoute);
  } else if (jRoutes) { // jRoutes
    checkLogic(jRoutes->isArray() || jRoutes->isObject(),
               "Config: routes is not array/object");
    if (jRoutes->isArray()) {
      for (const auto& it : *jRoutes) {
        checkLogic(it.isObject(), "RoutePolicy is not an object");
        auto jCurRoute = it.get_ptr("route");
        auto jAliases = it.get_ptr("aliases");
        checkLogic(jCurRoute, "RoutePolicy: no route");
        checkLogic(jAliases, "RoutePolicy: no aliases");
        checkLogic(jAliases->isArray(), "RoutePolicy: aliases is not an array");
        auto routeSelector =
            std::make_shared<PrefixSelectorRoute<RouteHandleIf>>(
                factory, *jCurRoute);
        for (const auto& alias : *jAliases) {
          checkLogic(alias.isString(), "RoutePolicy: alias is not a string");
          routeSelectors[alias.stringPiece()] = routeSelector;
        }
      }
    } else { // object
      for (const auto& it : jRoutes->items()) {
        checkLogic(it.first.isString(), "RoutePolicy: alias is not a string");
        routeSelectors[it.first.stringPiece()] =
            std::make_shared<PrefixSelectorRoute<RouteHandleIf>>(
                factory, it.second);
      }
    }
  } else {
    throwLogic("No route/routes in config");
  }

  asyncLogRoutes_ = provider.releaseAsyncLogRoutes();
  pools_ = provider.releasePools();
  accessPoints_ = provider.releaseAccessPoints();
  proxyRoute_ = std::make_shared<ProxyRoute<RouteHandleIf>>(
      &proxy, routeSelectors);
  serviceInfo_ = std::make_shared<ServiceInfo<RouteHandleIf>>(&proxy, *this);
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf>
ProxyConfig<RouteHandleIf>::getRouteHandleForAsyncLog(
    folly::StringPiece asyncLogName) const {
  return tryGet(asyncLogRoutes_, asyncLogName);
}

template <class RouteHandleIf>
size_t ProxyConfig<RouteHandleIf>::calcNumClients() const {
  size_t result = 0;
  for (const auto& it : pools_) {
    result += it.second.size();
  }
  return result;
}

}}} // facebook::memcache::mcrouter
