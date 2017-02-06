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
#include <utility>
#include <vector>

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ShadowSettings;

template <class RouteHandleIf>
using ShadowData = std::vector<std::pair<
    std::shared_ptr<RouteHandleIf>,
    std::shared_ptr<ShadowSettings>>>;

using McrouterShadowData = ShadowData<McrouterRouteHandleIf>;

}}}  // facebook::memcache::mcrouter
