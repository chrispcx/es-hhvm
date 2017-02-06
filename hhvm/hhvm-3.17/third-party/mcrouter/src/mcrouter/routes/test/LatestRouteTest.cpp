/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <folly/dynamic.h>
#include <folly/Hash.h>

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeLatestRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> targets,
  size_t id);

}}}  // facebook::memcache::mcrouter

TEST(latestRouteTest, one) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object("failover_count", 3);
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);

  auto first = replyFor(*rh, "key")[0] - 'a';

  /* While first is good, will keep sending to it */
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  test_handles[first]->setTko();
  /* first is TKO, send to other one */
  auto second = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, second);
  test_handles[first]->unsetTko();
  test_handles[second]->setTko();
  /* first is not TKO */
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  test_handles[first]->setTko();
  /* first and second are now TKO */
  auto third = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, third);
  EXPECT_NE(second, third);
  test_handles[third]->setTko();
  /* three boxes are now TKO, we hit the failover limit */
  auto reply = rh->route(McGetRequest("key"));
  EXPECT_EQ(mc_res_tko, reply.result());
}

TEST(latestRouteTest, weights) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object;
  settings["failover_count"] = 3;
  settings["thread_local_failover"] = true;
  settings["weights"] = folly::dynamic::array(.25, .5, .75, 1);
  std::vector<size_t> hits_per_index;
  hits_per_index.resize(4);
  for (int i = 0; i < 10000; i++) {
    auto rh = makeLatestRoute(settings, get_route_handles(test_handles),
                              /* threadId */ i);
    auto index = replyFor(*rh, "key")[0] - 'a';
    hits_per_index[index]++;
  }
  EXPECT_NEAR(hits_per_index[0], 1000, 50);
  EXPECT_NEAR(hits_per_index[1], 2000, 100);
  EXPECT_NEAR(hits_per_index[2], 3000, 150);
  EXPECT_NEAR(hits_per_index[3], 4000, 200);
}

TEST(latestRouteTest, thread_local_failover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object;
  settings["failover_count"] = 3;
  settings["thread_local_failover"] = true;
  // verify we don't always get the same index

  auto rh = makeLatestRoute(settings, get_route_handles(test_handles),
                            /* threadId */ 0);
  auto last_thread_reply = replyFor(*rh, "key");
  auto replies_differ = false;
  for (int i = 1; i < 10; i++) {
    rh = makeLatestRoute(settings, get_route_handles(test_handles),
                              /* threadId */ i);
    auto thread_reply = replyFor(*rh, "key");
    if (thread_reply != last_thread_reply) {
      replies_differ = true;
      break;
    }
    last_thread_reply = thread_reply;
  }
  EXPECT_TRUE(replies_differ);

  // Disable thread_local_failover
  settings["thread_local_failover"] = false;
  rh = makeLatestRoute(settings, get_route_handles(test_handles),
                            /* threadId */ 0);
  last_thread_reply = replyFor(*rh, "key");
  for (int i = 1; i < 10; i++) {
    auto rh = makeLatestRoute(settings, get_route_handles(test_handles),
                              /* threadId */ i);
    auto thread_reply = replyFor(*rh, "key");
    EXPECT_EQ(thread_reply, last_thread_reply);
    last_thread_reply = thread_reply;
  }
}

TEST(latestRouteTest, leasePairingNoName) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object(
      "enable_lease_pairing", true)("failover_count", 3);

  EXPECT_ANY_THROW({
    auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);
  });
}

TEST(latestRouteTest, leasePairingWithName) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object(
      "enable_lease_pairing", true)("name", "01")("failover_count", 3);

  // Should not throw, as the name was provided
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);
}
