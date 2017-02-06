/*
 * Copyright 2014 Facebook, Inc.
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

#include <thrift/lib/cpp/async/TAsyncSocketFactory.h>

#include <folly/Memory.h>

namespace apache { namespace thrift { namespace async {

TAsyncSocketFactory::TAsyncSocketFactory(folly::EventBase* eventBase) :
 eventBase_(eventBase) {
}

TAsyncSocketFactory::~TAsyncSocketFactory() {
}

TAsyncSocket::UniquePtr TAsyncSocketFactory::make() const {
  return TAsyncSocket::UniquePtr(new TAsyncSocket(eventBase_));
}

TAsyncSocket::UniquePtr TAsyncSocketFactory::make(int fd) const {
  return TAsyncSocket::UniquePtr(new TAsyncSocket(eventBase_, fd));
}

}}}
