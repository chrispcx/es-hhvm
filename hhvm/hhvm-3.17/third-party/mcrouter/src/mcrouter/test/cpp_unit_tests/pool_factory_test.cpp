/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <stdexcept>

#include <gtest/gtest.h>

#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/json.h>
#include <folly/Range.h>

#include "mcrouter/ConfigApiIf.h"
#include "mcrouter/PoolFactory.h"

using namespace facebook::memcache::mcrouter;

namespace {

class MockConfigApi : public ConfigApiIf {
 public:
  MockConfigApi() = default;

  explicit MockConfigApi(folly::StringKeyedUnorderedMap<std::string> pools)
    : pools_(std::move(pools)) {
  }

  bool get(ConfigType type, const std::string& path,
           std::string& contents) override final {
    ++getCalls_;
    if (type != ConfigType::Pool) {
      return false;
    }
    auto it = pools_.find(path);
    if (it != pools_.end()) {
      contents = it->second;
      return true;
    }
    return false;
  }

  bool getConfigFile(std::string& config, std::string& path) override final {
    config = "{}";
    path = "{}";
    return true;
  }

  size_t getCalls() const {
    return getCalls_;
  }
 private:
  folly::StringKeyedUnorderedMap<std::string> pools_;
  size_t getCalls_{0};
};

} // anonymous

TEST(PoolFactory, inherit_loop) {
  MockConfigApi api;
  PoolFactory factory(folly::parseJson(R"({
    "pools": {
      "A": {
        "inherit": "B"
      },
      "B": {
        "inherit": "C"
      },
      "C": {
        "inherit": "A"
      }
    }
  })"), api);
  try {
    factory.parsePool("A");
  } catch (const std::logic_error& e) {
    EXPECT_TRUE(folly::StringPiece(e.what()).contains("Cycle")) << e.what();
    return;
  }
  FAIL() << "No exception thrown on inherit cycle";
}

TEST(PoolFactory, inherit_cache) {
  MockConfigApi api(folly::StringKeyedUnorderedMap<std::string>{
    { "api_pool", "{ \"servers\": [ \"localhost:1234\" ] }" }
  });
  PoolFactory factory(folly::parseJson(R"({
    "pools": {
      "A": {
        "inherit": "api_pool",
        "server_timeout": 5
      },
      "B": {
        "inherit": "api_pool",
        "server_timeout": 10
      },
      "C": {
        "inherit": "A",
        "server_timeout": 15
      }
    }
  })"), api);
  auto poolA = factory.parsePool("A");
  EXPECT_EQ("A", poolA.name.str());
  EXPECT_EQ(5, poolA.json["server_timeout"].getInt());
  auto poolB = factory.parsePool("B");
  EXPECT_EQ("B", poolB.name.str());
  EXPECT_EQ(10, poolB.json["server_timeout"].getInt());
  auto poolC = factory.parsePool("C");
  EXPECT_EQ("C", poolC.name.str());
  EXPECT_EQ(15, poolC.json["server_timeout"].getInt());
  EXPECT_EQ(api.getCalls(), 1);
}
