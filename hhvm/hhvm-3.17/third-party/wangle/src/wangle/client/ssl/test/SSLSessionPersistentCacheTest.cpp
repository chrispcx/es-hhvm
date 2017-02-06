/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Format.h>
#include <folly/DynamicConverter.h>
#include <folly/Memory.h>

#include <gmock/gmock.h>

#include <gtest/gtest.h>

#include <vector>

#include <wangle/client/ssl/SSLSessionPersistentCache.h>
#include <wangle/client/persistence/test/TestUtil.h>
#include <wangle/client/ssl/test/TestUtil.h>

using namespace testing;
using namespace std::chrono;
using namespace wangle;

namespace wangle {

class MockTimeUtil : public SSLSessionPersistentCache::TimeUtil {
  public:
    void advance(std::chrono::milliseconds ms) {
      t_ += ms;
    }

    std::chrono::time_point<std::chrono::system_clock> now() const override {
      return t_;
    }

  private:
    std::chrono::time_point<std::chrono::system_clock> t_;
};

class SSLSessionPersistentCacheTest : public Test {
  public:
  SSLSessionPersistentCacheTest():
    filename_(getPersistentCacheFilename()) {}

  void SetUp() override {
    for (auto& it : getSessions()) {
      sessions_.emplace_back(it.first, it.second);
    }
    sessionWithTicket_ = getSessionWithTicket();
    sessions_.emplace_back(sessionWithTicket_.first, sessionWithTicket_.second);

    // Create the cache fresh for each test
    mockTimeUtil_ = new MockTimeUtil();
    cache_.reset(new SSLSessionPersistentCache(filename_,
                                      50, std::chrono::seconds(150)));
    cache_->setTimeUtil(
        std::unique_ptr<SSLSessionPersistentCache::TimeUtil>(mockTimeUtil_));
  }

  void TearDown() override {
    for (const auto& it : sessions_) {
      SSL_SESSION_free(it.first);
    }
    sessions_.clear();
    cache_.reset();
    EXPECT_TRUE(unlink(filename_.c_str()) != -1);
  }

 protected:
  void verifyEntryInCache(const std::string& hostname,
      std::pair<SSL_SESSION*, size_t> session,
      bool inCache = true) {
    auto s = cache_->getSSLSession(hostname);
    if (inCache) {
      ASSERT_TRUE(s != nullptr);
      ASSERT_TRUE(
          isSameSession(
            session,
            std::make_pair(s.get(), session.second)));
    } else {
      ASSERT_FALSE(s);
    }
  }

 protected:
  std::string filename_;
  MockTimeUtil *mockTimeUtil_;
  std::unique_ptr<SSLSessionPersistentCache> cache_;
  std::vector<std::pair<SSL_SESSION*, size_t>> sessions_;
  std::pair<SSL_SESSION*, size_t> sessionWithTicket_;
};

TEST_F(SSLSessionPersistentCacheTest, Basic) {
  for (size_t i = 0; i < sessions_.size(); ++i) {
    auto hostname = folly::sformat("host{}", i);

    // The session data does not exist before set.
    ASSERT_EQ(i, cache_->size());
    ASSERT_FALSE(cache_->getSSLSession(hostname));

    cache_->setSSLSession(hostname, createPersistentTestSession(sessions_[i]));

    // The session data should exist before set.
    ASSERT_EQ(i + 1, cache_->size());
    verifyEntryInCache(hostname, sessions_[i]);
  }

  // The previously inserted sessions shouldn't have changed. Then remove them
  // one by one and verify they are not in cache after the removal.
  for (size_t i = 0; i < sessions_.size(); ++i) {
    auto hostname = folly::sformat("host{}", i);
    verifyEntryInCache(hostname, sessions_[i]);
    cache_->removeSSLSession(hostname);
    verifyEntryInCache(hostname, sessions_[i], false);
  }
}

TEST_F(SSLSessionPersistentCacheTest, BadSession) {
  std::string badHost = "bad";

  // Insert bad session to an empty cache.
  cache_->setSSLSession(badHost, SSLSessionPtr(nullptr));
  ASSERT_FALSE(cache_->getSSLSession(badHost));
  ASSERT_EQ(0, cache_->size());

  cache_->setSSLSession("host0", createPersistentTestSession(sessions_[0]));
  cache_->setSSLSession("host1", createPersistentTestSession(sessions_[1]));

  // Insert bad session to non-empty cache
  cache_->setSSLSession(badHost, SSLSessionPtr(nullptr));
  ASSERT_FALSE(cache_->getSSLSession(badHost));
  ASSERT_EQ(2, cache_->size());

  verifyEntryInCache("host0", sessions_[0]);
  verifyEntryInCache("host1", sessions_[1]);
}

TEST_F(SSLSessionPersistentCacheTest, Overwrite) {
  cache_->setSSLSession("host0", createPersistentTestSession(sessions_[0]));
  cache_->setSSLSession("host1", createPersistentTestSession(sessions_[1]));

  {
    // Overwrite host1 with a nullptr, the cache shouldn't have changed.
    cache_->setSSLSession("host1", SSLSessionPtr(nullptr));
    verifyEntryInCache("host0", sessions_[0]);
    verifyEntryInCache("host1", sessions_[1]);
  }

  {
    // Valid overwrite.
    cache_->setSSLSession("host1", createPersistentTestSession(sessions_[3]));
    verifyEntryInCache("host0", sessions_[0]);
    verifyEntryInCache("host1", sessions_[3]);
  }

}

TEST_F(SSLSessionPersistentCacheTest, SessionTicketTimeout) {
  std::string myhost("host3");
  cache_->setSSLSession(
      myhost,
      createPersistentTestSession(sessionWithTicket_));

  // First verify element is successfully added to the cache
  auto s = cache_->getSSLSession(myhost);
  ASSERT_TRUE(s != nullptr);
  ASSERT_TRUE(s->tlsext_ticklen > 0);
  verifyEntryInCache(myhost, sessions_[3]);

  // Verify that if element is added to cache within
  // tlsext_tick_lifetime_hint seconds ago, we can retrieve it

  // advance current time by slightly less than tlsext_tick_lifetime_hint
  // Ticket should still be in cache
  long lifetime_seconds = s->tlsext_tick_lifetime_hint;
  mockTimeUtil_->advance(
      duration_cast<milliseconds>(seconds(lifetime_seconds - 10)));

  verifyEntryInCache(myhost, sessions_[3]);

  // advance current time to >= tlsext_tick_lifetime_hint
  // Ticket should not be in cache
  mockTimeUtil_->advance(duration_cast<milliseconds>(seconds(15)));
  ASSERT_FALSE(cache_->getSSLSession(myhost));
}

}
