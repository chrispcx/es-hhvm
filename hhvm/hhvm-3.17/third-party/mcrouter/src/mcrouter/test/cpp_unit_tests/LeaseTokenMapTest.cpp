/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <chrono>
#include <thread>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/Optional.h>

#include "mcrouter/LeaseTokenMap.h"

using namespace facebook::memcache::mcrouter;

namespace {

void assertQueryTrue(LeaseTokenMap& map, std::string routeName,
                     uint64_t specialToken, LeaseTokenMap::Item expectedItem) {
  auto item = map.query(routeName, specialToken);
  EXPECT_TRUE(item.hasValue());
  EXPECT_EQ(item->originalToken, expectedItem.originalToken);
  EXPECT_EQ(item->routeHandleChildIndex, expectedItem.routeHandleChildIndex);
}

void assertQueryFalse(LeaseTokenMap& map, std::string routeName,
                      uint64_t specialToken) {
  auto item = map.query(routeName, specialToken);
  EXPECT_FALSE(item.hasValue());
}

} // anonymous namespace

TEST(LeaseTokenMap, sanity) {
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  EXPECT_EQ(map.size(), 0);

  auto tkn1 = map.insert("route01", {10, 1});
  auto tkn2 = map.insert("route01", {20, 2});
  auto tkn3 = map.insert("route01", {30, 3});

  EXPECT_EQ(map.size(), 3);

  assertQueryTrue(map, "route01", tkn1, {10, 1});
  assertQueryTrue(map, "route01", tkn2, {20, 2});
  assertQueryTrue(map, "route01", tkn3, {30, 3});

  EXPECT_EQ(map.size(), 0); // read all data from map.
  assertQueryFalse(map, "route01", 1); // "existing" id but without magic.
  assertQueryFalse(map, "route01", 10); // unexisting id.
  assertQueryFalse(map, "route01", 0x7aceb00c00000006); // unexisting token.
}

TEST(LeaseTokenMap, magicConflict) {
  // If we are unlucky enough to have an originalToken (i.e. token returned
  // by memcached) that contains our "magic", LeaseTokenMap should handle it
  // gracefully.

  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  EXPECT_EQ(map.size(), 0);

  uint64_t originalToken = 0x7aceb00c0000000A;
  uint64_t specialToken = map.insert("route01", {originalToken, 1});

  EXPECT_EQ(map.size(), 1);
  assertQueryTrue(map, "route01", specialToken, {originalToken, 1});
  assertQueryFalse(map, "route01", originalToken);
  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, nestedRoutes) {
  // Simulates the following routing:
  // proxyRoute -> failover:route02 -> failover:route01 -> destinationRoute

  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  // LEASE-GET
  // get token 17 from memcached
  const uint64_t memcachedToken = 17;
  // in route failover:route01, insert memcachedToken into map and return
  // specialToken1
  const uint64_t specialToken1 = map.insert("route01", {memcachedToken, 1});
  // In route failover:route02, insert specialToken1 into map and return
  // specialToken2
  const uint64_t specialToken2 = map.insert("route02", {specialToken1,  2});
  // The client will receice specialToken2

  // LEASE-SET
  // We will go first to failover:route02 with specialToken2
  assertQueryTrue(map, "route02", specialToken2, {specialToken1, 2});
  assertQueryTrue(map, "route01", specialToken1, {memcachedToken, 1});
}

TEST(LeaseTokenMap, shrink) {
  size_t tokenTtl = 100;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (size_t i = 0; i < 1000; ++i) {
    map.insert("route01", {i * 10, i});
  }

  // Allow time for the map to shrink.
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(tokenTtl * 5));

  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, stress) {
  size_t tokenTtl = 1000;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (size_t i = 0; i < 5000; ++i) {
    uint64_t origToken = i * 10;
    uint64_t specToken = map.insert("route01", {origToken, i});

    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // leave some work for the shrink thread.
    if (i % 10 != 0) {
      assertQueryTrue(map, "route01", specToken, {origToken, i});
    }
  }
}
