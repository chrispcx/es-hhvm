/*
 * Copyright 2016 Facebook, Inc.
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

#include <thrift/lib/cpp2/fatal/merge.h>
#include <thrift/lib/cpp2/fatal/pretty_print.h>

#include <thrift/test/gen-cpp2/fatal_merge_constants.h>
#include <thrift/test/gen-cpp2/fatal_merge_types.h>
#include <thrift/test/gen-cpp2/fatal_merge_fatal_types.h>

#include <gtest/gtest.h>

using namespace apache::thrift::test;

namespace {

class FatalMergeTest : public testing::Test {};

}

namespace apache { namespace thrift { namespace test {

static std::ostream& operator<<(std::ostream& o, const Basic& v) {
  return o << apache::thrift::pretty_string(v);
}

static std::ostream& operator<<(std::ostream& o, const Nested& v) {
  return o << apache::thrift::pretty_string(v);
}

}}}

#define TEST_GROUP(name, constant) \
  TEST_F(FatalMergeTest, name##_copy) { \
    const auto& example = fatal_merge_constants::constant(); \
    auto src = example.src, dst = example.dst; \
    apache::thrift::merge_into(src, dst); \
    EXPECT_EQ(example.exp, dst); \
    EXPECT_EQ(example.src, src); \
  } \
  TEST_F(FatalMergeTest, name##_copy_legacy) { \
    const auto& example = fatal_merge_constants::constant(); \
    auto src = example.src, dst = example.dst; \
    apache::thrift::merge(src, dst); \
    EXPECT_EQ(example.exp, dst); \
    EXPECT_EQ(example.src, src); \
  } \
  TEST_F(FatalMergeTest, name##_move) { \
    const auto& example = fatal_merge_constants::constant(); \
    auto src = example.src, dst = example.dst; \
    apache::thrift::merge_into(std::move(src), dst); \
    EXPECT_EQ(example.exp, dst); \
    EXPECT_EQ(example.nil, src); \
  } \
  TEST_F(FatalMergeTest, name##_move_legacy) { \
    const auto& example = fatal_merge_constants::constant(); \
    auto src = example.src, dst = example.dst; \
    apache::thrift::merge(std::move(src), dst); \
    EXPECT_EQ(example.exp, dst); \
    EXPECT_EQ(example.nil, src); \
  } \

TEST_GROUP(enumeration, kEnumExample);
TEST_GROUP(structure, kBasicExample);
TEST_GROUP(list, kBasicListExample);
TEST_GROUP(set, kBasicSetExample);
TEST_GROUP(map, kBasicMapExample);
TEST_GROUP(nested_structure, kNestedExample);
