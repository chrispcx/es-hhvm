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

#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class Proxy;

/**
 * Creates additional route handles for McRouteHandleProvider.
 */
class McExtraRouteHandleProvider : public ExtraRouteHandleProviderIf {
 public:
  virtual McrouterRouteHandlePtr makeShadow(
      Proxy& proxy,
      McrouterRouteHandlePtr destination,
      McrouterShadowData data,
      folly::StringPiece shadowPolicy) override;

  virtual std::vector<McrouterRouteHandlePtr>
  tryCreate(RouteHandleFactory<McrouterRouteHandleIf>& factory,
            folly::StringPiece type,
            const folly::dynamic& json) override;

  virtual ~McExtraRouteHandleProvider() {}
};

}}}  // facebook::memcache::mcrouter
