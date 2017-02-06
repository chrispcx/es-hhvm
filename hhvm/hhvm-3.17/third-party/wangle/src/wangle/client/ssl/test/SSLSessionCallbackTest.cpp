/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/SSLContext.h>
#include <gtest/gtest.h>
#include <wangle/client/ssl/SSLSessionCallbacks.h>
#include <wangle/client/ssl/test/TestUtil.h>
#include <vector>
#include <map>

using namespace testing;
using namespace wangle;

using folly::SSLContext;

// One time use cache for testing that uses size_t as the cache key
class FakeSessionCallbacks : public SSLSessionCallbacks {
 public:
  void setSSLSession(
    const std::string& key,
    SSLSessionPtr session) noexcept override {
    cache_.emplace(key, std::move(session));
  }

  SSLSessionPtr getSSLSession(const std::string& key) const noexcept override {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      return SSLSessionPtr(nullptr);
    }
    auto sess = std::move(it->second);
    cache_.erase(it);
    return sess;
  }

  bool removeSSLSession(const std::string&) noexcept override {
    return true;
  }

  size_t size() const override {
    return cache_.size();
  }

 private:
   mutable std::map<std::string, SSLSessionPtr> cache_;
};


TEST(SSLSessionCallbackTest, AttachMultiple) {
  SSLContext c1;
  SSLContext c2;
  FakeSessionCallbacks cb;
  FakeSessionCallbacks::attachCallbacksToContext(c1.getSSLCtx(), &cb);
  FakeSessionCallbacks::attachCallbacksToContext(c2.getSSLCtx(), &cb);

  auto cb1 = FakeSessionCallbacks::getCacheFromContext(c1.getSSLCtx());
  auto cb2 = FakeSessionCallbacks::getCacheFromContext(c2.getSSLCtx());
  EXPECT_EQ(cb1, cb2);

  FakeSessionCallbacks::detachCallbacksFromContext(c1.getSSLCtx(), cb1);
  EXPECT_FALSE(FakeSessionCallbacks::getCacheFromContext(c1.getSSLCtx()));

  FakeSessionCallbacks unused;
  FakeSessionCallbacks::detachCallbacksFromContext(c2.getSSLCtx(), &unused);
  cb2 = FakeSessionCallbacks::getCacheFromContext(c2.getSSLCtx());
  EXPECT_EQ(&cb, cb2);
}
