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

#include <thrift/lib/cpp2/fatal/legacy_reflection.h>

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/fatal/debug.h>
#include <thrift/lib/cpp2/fatal/pretty_print.h>
#include <thrift/lib/thrift/gen-cpp2/reflection_fatal_types.h>
#include <thrift/test/gen-cpp2/fatal_legacy_reflection_types.h>
#include <thrift/test/gen-cpp2/fatal_legacy_reflection_fatal_types.h>

using namespace apache::thrift;
using namespace apache::thrift::reflection;
using namespace apache::thrift::test;

namespace {

class FatalLegacyReflectionTest : public testing::Test {};

}

TEST_F(FatalLegacyReflectionTest, schema) {
  Schema expected;
  SampleStruct::_reflection_register(expected);
  auto actual = legacy_reflection<SampleStruct>::schema();
  EXPECT_TRUE(
      debug_equals(expected, actual, make_debug_output_callback(LOG(INFO))));
}

TEST_F(FatalLegacyReflectionTest, name) {
  constexpr auto actual = legacy_reflection<SampleStruct>::name();
  EXPECT_EQ("struct fatal_legacy_reflection.SampleStruct", actual);
}

TEST_F(FatalLegacyReflectionTest, id) {
  EXPECT_EQ(
      SampleStruct::_reflection_id,
      legacy_reflection<SampleStruct>::id());
}
