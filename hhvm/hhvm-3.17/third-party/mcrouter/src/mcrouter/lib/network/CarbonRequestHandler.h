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

#include <mcrouter/lib/carbon/RequestReplyUtil.h>
#include <mcrouter/lib/fbi/cpp/util.h>
#include <mcrouter/lib/network/CarbonMessageDispatcher.h>

namespace carbon {

template <class OnRequest, class RequestList>
class CarbonRequestHandler
    : public facebook::memcache::CarbonMessageDispatcher<
          RequestList,
          CarbonRequestHandler<OnRequest, RequestList>,
          facebook::memcache::McServerRequestContext&&> {
 public:
  template <class... Args>
  explicit CarbonRequestHandler(Args&&... args)
      : onRequest_(std::forward<Args>(args)...) {}

  template <class Request>
  void onRequest(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req) {
    onRequestImpl(
        std::move(ctx),
        std::move(req),
        carbon::detail::CanHandleRequest::value<Request, OnRequest>());
  }

 private:
  OnRequest onRequest_;

  template <class Request>
  void onRequestImpl(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      std::true_type) {
    onRequest_.onRequest(std::move(ctx), std::move(req));
  }

  template <class Request>
  void onRequestImpl(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      std::false_type) {
    facebook::memcache::throwRuntime(
        "onRequest for {} not defined", typeid(Request).name());
  }
};

} // carbon
