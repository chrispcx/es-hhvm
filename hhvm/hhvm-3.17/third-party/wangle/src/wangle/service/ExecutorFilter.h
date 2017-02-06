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

#include <wangle/service/Service.h>

namespace wangle {

/**
 * A service that runs all requests through an executor.
 */
template <typename Req, typename Resp = Req>
class ExecutorFilter : public ServiceFilter<Req, Resp> {
 public:
 explicit ExecutorFilter(
   std::shared_ptr<folly::Executor> exe,
   std::shared_ptr<Service<Req, Resp>> service)
      : ServiceFilter<Req, Resp>(service)
      , exe_(exe) {}

 folly::Future<Resp> operator()(Req req) override {
   return via(exe_.get()).then([ req = std::move(req), this ]() mutable {
     return (*this->service_)(std::move(req));
   });
  }

 private:
  std::shared_ptr<folly::Executor> exe_;
};

} // namespace wangle
