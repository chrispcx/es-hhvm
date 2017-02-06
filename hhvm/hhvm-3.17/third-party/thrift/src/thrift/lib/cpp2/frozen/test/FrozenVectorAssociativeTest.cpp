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
#include <gtest/gtest.h>
#include <thrift/lib/cpp/protocol/TCompactProtocol.h>
#include <thrift/lib/cpp/protocol/TDebugProtocol.h>
#include <thrift/lib/cpp/util/ThriftSerializer.h>
#include <thrift/lib/cpp2/frozen/FrozenTestUtil.h>
#include <thrift/lib/cpp2/frozen/VectorAssociative.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp/Example_types.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp2/Example_layouts.h>
#include <thrift/lib/cpp2/frozen/test/gen-cpp2/Example_types_custom_protocol.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

using namespace apache::thrift;
using namespace frozen;
using namespace util;

TEST(FrozenVectorTypes, VectorAsMap) {
  VectorAsMap<int, int> dm;
  dm.insert({9, 81});
  dm.insert({5, 25});
  dm.insert({3, 9});
  dm.insert({7, 49});
  dm.insert(dm.end(), {1, 1});
  // ensure it gets sorted
  auto fdm = freeze(dm);
  EXPECT_EQ(1, fdm[0].first());
  EXPECT_EQ(1, fdm[0].second());
  EXPECT_EQ(81, fdm.at(9));
  EXPECT_EQ(25, fdm.at(5));
  {
    auto found = fdm.find(3);
    ASSERT_NE(found, fdm.end());
    EXPECT_EQ(found->second(), 9);
  }
  {
    auto found = fdm.find(2);
    EXPECT_EQ(found, fdm.end());
  }
}

TEST(FrozenVectorTypes, VectorAsHashMap) {
  VectorAsHashMap<int, int> dm;
  dm.insert({1, 2});
  dm.insert(dm.end(), {3, 4});
  auto fdm = freeze(dm);
  EXPECT_EQ(2, fdm.at(1));
  EXPECT_EQ(4, fdm.at(3));
  {
    auto found = fdm.find(3);
    ASSERT_NE(found, fdm.end());
    EXPECT_EQ(found->second(), 4);
  }
  {
    auto found = fdm.find(2);
    EXPECT_EQ(found, fdm.end());
  }
}

TEST(FrozenVectorTypes, OptionalVectorAsHashMap) {
  folly::Optional<VectorAsHashMap<int, int>> dm;
  dm.emplace();
  dm->insert({1, 2});
  dm->insert(dm->end(), {3, 4});
  auto fdm = freeze(dm);
  EXPECT_EQ(2, fdm->at(1));
  EXPECT_EQ(4, fdm->at(3));
  {
    auto found = fdm->find(3);
    ASSERT_NE(found, fdm->end());
    EXPECT_EQ(found->second(), 4);
  }
  {
    auto found = fdm.value().find(3);
    ASSERT_NE(found, fdm.value().end());
    EXPECT_EQ(found->second(), 4);
  }
  {
    auto found = fdm->find(2);
    EXPECT_EQ(found, fdm->end());
  }
}

TEST(FrozenVectorTypes, VectorAsSet) {
  VectorAsSet<int> dm;
  dm.insert(3);
  dm.insert(dm.end(), 7);
  auto fdm = freeze(dm);
  EXPECT_EQ(1, fdm.count(3));
  EXPECT_EQ(1, fdm.count(7));
  EXPECT_EQ(0, fdm.count(4));
}

TEST(FrozenVectorTypes, VectorAsHashSet) {
  VectorAsHashSet<int> dm;
  dm.insert(3);
  dm.insert(dm.end(), 7);
  auto fdm = freeze(dm);
  EXPECT_EQ(1, fdm.count(3));
  EXPECT_EQ(1, fdm.count(7));
  EXPECT_EQ(0, fdm.count(4));
}

template<class TestType>
void populate(TestType& x) {
  x.aList.push_back(1);
  x.aSet.insert(2);
  x.aMap[3] = 4;
  x.aHashSet.insert(5);
  x.aHashMap[6] = 7;
}

bool areEqual(const example1::VectorTest& v1, const example2::VectorTest& v2){
  return v1.aList == v2.aList && v1.aMap == v2.aMap && v1.aSet == v2.aSet &&
      v1.aHashMap == v2.aHashMap && v1.aHashSet == v2.aHashSet;
}

namespace {

template <typename T>
typename std::enable_if<
    std::is_base_of<apache::thrift::TStructType<T>, T>::value>::type
serializeCompact(const T& obj, std::string* out) {
  // cpp1
  ThriftSerializerCompact<>().serialize(obj, out);
}

template <typename T>
typename std::enable_if<
    !std::is_base_of<apache::thrift::TStructType<T>, T>::value>::type
serializeCompact(const T& obj, std::string* out) {
  // cpp2
  folly::IOBufQueue bq(folly::IOBufQueue::cacheChainLength());
  CompactSerializer::serialize(obj, &bq);
  bq.appendToString(*out);
}

template <typename T>
typename std::enable_if<
    std::is_base_of<apache::thrift::TStructType<T>, T>::value>::type
deserializeCompact(const std::string& in, T* out) {
  // cpp1
  ThriftSerializerCompact<>().deserialize(in, out);
}

template <typename T>
typename std::enable_if<
    !std::is_base_of<apache::thrift::TStructType<T>, T>::value>::type
deserializeCompact(const std::string& in, T* out) {
  // cpp2
  CompactSerializer::deserialize(in, *out);
}
}

TEST(FrozenVectorTypes, CrossVersions) {
  example1::VectorTest input1, output1;
  example2::VectorTest input2, output2;

  populate(input1);
  populate(input2);

  std::string serialized1, serialized2;

  serializeCompact(input1, &serialized1);
  deserializeCompact(serialized1, &output2);

  serializeCompact(input2, &serialized2);
  deserializeCompact(serialized2, &output1);

  EXPECT_EQ(input1, output1);
  EXPECT_EQ(input2, output2);
}

template <class T>
class FrozenStructsWithVectors : public ::testing::Test {};
TYPED_TEST_CASE_P(FrozenStructsWithVectors);

TYPED_TEST_P(FrozenStructsWithVectors, Serializable) {
  TypeParam input, output;
  populate(input);
  std::string serialized;
  serializeCompact(input, &serialized);
  deserializeCompact(serialized, &output);
  EXPECT_EQ(input, output);
}

TYPED_TEST_P(FrozenStructsWithVectors, Freezable) {
  TypeParam input;
  populate(input);
  auto f = freeze(input);
  EXPECT_EQ(f.aList()[0], 1);
  EXPECT_EQ(f.aSet().count(1), 0);
  EXPECT_EQ(f.aSet().count(2), 1);
  EXPECT_EQ(f.aMap().getDefault(3, 9), 4);
  EXPECT_EQ(f.aMap().getDefault(4, 9), 9);
  EXPECT_EQ(f.aHashSet().count(5), 1);
  EXPECT_EQ(f.aHashSet().count(6), 0);
  EXPECT_EQ(f.aHashMap().getDefault(6, 9), 7);
  EXPECT_EQ(f.aHashMap().getDefault(7, 9), 9);
}

REGISTER_TYPED_TEST_CASE_P(FrozenStructsWithVectors, Freezable, Serializable);
typedef ::testing::Types<example1::VectorTest, example2::VectorTest> MyTypes;
INSTANTIATE_TYPED_TEST_CASE_P(CppVerions, FrozenStructsWithVectors, MyTypes);

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
