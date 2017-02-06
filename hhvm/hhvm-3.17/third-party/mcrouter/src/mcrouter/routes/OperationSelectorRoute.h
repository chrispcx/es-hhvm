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
#include <vector>

#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/Operation.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* RouteHandle that can send to a different target based on McOperation id */
template <class RouteHandleIf>
class OperationSelectorRoute {
 public:
  static std::string routeName() { return "operation-selector"; }

  OperationSelectorRoute(
    std::vector<std::shared_ptr<RouteHandleIf>> operationPolicies,
    std::shared_ptr<RouteHandleIf>&& defaultPolicy)
      : operationPolicies_(std::move(operationPolicies)),
        defaultPolicy_(std::move(defaultPolicy)) {
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    static constexpr int op = OpFromType<Request, RequestOpMapping>::value;

    if (operationPolicies_[op]) {
      t(*operationPolicies_[op], req);
    } else if (defaultPolicy_) {
      t(*defaultPolicy_, req);
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    static constexpr int op = OpFromType<Request, RequestOpMapping>::value;

    if (operationPolicies_[op]) {
      return operationPolicies_[op]->route(req);
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req);
    }

    return ReplyT<Request>();
  }

private:
  const std::vector<std::shared_ptr<RouteHandleIf>> operationPolicies_;
  const std::shared_ptr<RouteHandleIf> defaultPolicy_;
};

}}}  // facebook::memcache::mcrouter
