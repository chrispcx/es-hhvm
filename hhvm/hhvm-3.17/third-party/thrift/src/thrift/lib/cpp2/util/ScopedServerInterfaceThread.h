/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef THRIFT_UTIL_SCOPEDSERVEREVENTBASETHREAD_H
#define THRIFT_UTIL_SCOPEDSERVEREVENTBASETHREAD_H

#include <memory>
#include <folly/io/async/EventBase.h>
#include <folly/SocketAddress.h>
#include <thrift/lib/cpp/util/ScopedServerThread.h>

namespace apache { namespace thrift {

class AsyncProcessorFactory;
class ThriftServer;

/**
 * ScopedServerInterfaceThread spawns a thrift cpp2 server in a new thread.
 *
 * The server is stopped automatically when the instance is destroyed.
 */
class ScopedServerInterfaceThread {

 public:

  explicit ScopedServerInterfaceThread(
      std::shared_ptr<AsyncProcessorFactory> apf,
      const std::string& host = "::1",
      uint16_t port = 0);

  explicit ScopedServerInterfaceThread(
      std::shared_ptr<ThriftServer> ts);

  ThriftServer& getThriftServer() const;
  const folly::SocketAddress& getAddress() const;
  uint16_t getPort() const;

  template <class AsyncClientT>
  std::unique_ptr<AsyncClientT> newClient(folly::EventBase* eb) const;
  template <class AsyncClientT>
  std::unique_ptr<AsyncClientT> newClient(folly::EventBase& eb) const;

 private:

  std::shared_ptr<ThriftServer> ts_;
  util::ScopedServerThread sst_;

};

}}

#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread-inl.h>

#endif
