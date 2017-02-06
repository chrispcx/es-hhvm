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
#include <thrift/lib/cpp2/test/optionals/without_folly_optional/gen-cpp2/FollyOptionals_types.h>
#include <thrift/lib/cpp2/test/optionals/without_folly_optional/gen-cpp2/FollyOptionals_types_custom_protocol.h>
#include <thrift/lib/cpp/util/ThriftSerializer.h>
#include <thrift/lib/cpp2/protocol/SimpleJSONProtocol.h>
#include <gtest/gtest.h>

using namespace apache::thrift;

template <class T>
static std::string objToJSON(T& obj) {
  SimpleJSONProtocolWriter writer;
  const auto size = obj.serializedSize(&writer);
  folly::IOBufQueue queue(IOBufQueue::cacheChainLength());
  writer.setOutput(&queue, size);
  obj.write(&writer);
  auto buf = queue.move();
  auto ret = buf->moveToFbString().toStdString();
  return ret;
}

template <class T>
static T jsonToObj(const std::string& json) {
  SimpleJSONProtocolReader reader;
  T ret;
  ret.__clear();
  auto iobuf = folly::IOBuf::copyBuffer(json);
  reader.setInput(iobuf.get());
  ret.read(&reader);
  return ret;
}

TEST(TestWithoutFollyOptionals, SerDesTests) {
  std::string json1;
  std::string json2;

  cpp2::HasOptionals obj1;
  cpp2::HasOptionals obj2;
  obj1.__clear();
  obj2.__clear();

  // first try with only the required fields, leave all optionals empty
  obj1.int64Req = 42;
  obj1.stringReq = "hello";
  obj1.setReq = std::set<int64_t>{10, 20, 30};
  obj1.listReq = std::vector<int64_t>{40, 50, 60};
  obj1.mapReq = std::map<int64_t, int64_t>{{100, 101}, {102, 103}};
  obj1.enumReq = cpp2::HasOptionalsTestEnum::FOO;
  obj1.structReq = cpp2::HasOptionalsExtra();
  obj1.structReq.__clear();
  obj1.structReq.extraInt64Req = 69;
  obj1.structReq.extraStringReq = "world";
  obj1.structReq.extraSetReq = std::set<int64_t>{210, 220, 230};
  obj1.structReq.extraListReq = std::vector<int64_t>{240, 250, 260};
  obj1.structReq.extraMapReq =
      std::map<int64_t, int64_t>{{1000, 1001}, {1002, 1003}};
  obj1.structReq.extraEnumReq = cpp2::HasOptionalsTestEnum::BAR;
  json1 = objToJSON(obj1);

  obj2 = jsonToObj<cpp2::HasOptionals>(json1);

  EXPECT_EQ(obj1, obj2);
  json2 = objToJSON(obj2);
  EXPECT_EQ(json1, json2);

  // now set optionals
  obj1.int64Opt = 42;
  obj1.stringOpt = "helloOPTIONAL";
  obj1.setOpt = std::set<int64_t>{10, 20, 30};
  obj1.listOpt = std::vector<int64_t>{40, 50, 60};
  obj1.mapOpt = std::map<int64_t, int64_t>{{100, 101}, {102, 103}};
  obj1.enumOpt = cpp2::HasOptionalsTestEnum::FOO;
  obj1.structOpt = cpp2::HasOptionalsExtra();
  obj1.structOpt.__clear();
  obj1.structOpt.extraInt64Opt = 69;
  obj1.structOpt.extraStringOpt = "world";
  obj1.structOpt.extraSetOpt = std::set<int64_t>{210, 220, 230};
  obj1.structOpt.extraListOpt = std::vector<int64_t>{240, 250, 260};
  obj1.structOpt.extraMapOpt =
      std::map<int64_t, int64_t>{{1000, 1001}, {1002, 1003}};
  obj1.structOpt.extraEnumOpt = cpp2::HasOptionalsTestEnum::BAR;

  // Note: we did NOT set all the __isset's for the above!
  // Verify optionals WITHOUT isset are not serialized.
  json1 = objToJSON(obj1);
  EXPECT_EQ(std::string::npos, json1.find("helloOPTIONAL"));

  // ok, set the __isset's properly
  obj1.__isset.int64Opt = true;
  obj1.__isset.stringOpt = true;
  obj1.__isset.setOpt = true;
  obj1.__isset.listOpt = true;
  obj1.__isset.mapOpt = true;
  obj1.__isset.enumOpt = true;
  obj1.__isset.structOpt = true;
  obj1.structOpt.__isset.extraInt64Opt = true;
  obj1.structOpt.__isset.extraStringOpt = true;
  obj1.structOpt.__isset.extraSetOpt = true;
  obj1.structOpt.__isset.extraListOpt = true;
  obj1.structOpt.__isset.extraMapOpt = true;
  obj1.structOpt.__isset.extraEnumOpt = true;

  json1 = objToJSON(obj1);
  EXPECT_NE(std::string::npos, json1.find("helloOPTIONAL"));
  obj2 = jsonToObj<cpp2::HasOptionals>(json1);
  EXPECT_EQ(obj1, obj2);
  json2 = objToJSON(obj2);
  EXPECT_EQ(json1, json2);
}

TEST(TestWithoutFollyOptionals, EqualityTests) {
  cpp2::HasOptionals obj1;
  cpp2::HasOptionals obj2;
  obj1.__clear();
  obj2.__clear();

  // for each of the fields:
  // * set a required field, expect equal.
  // * set an optional field on one; expect not equal.
  // * the the optional field on the other one; equal again.

  // both completely empty
  EXPECT_EQ(obj1, obj2);

  obj1.int64Req = 1;
  obj2.int64Req = 1;
  EXPECT_EQ(obj1, obj2);
  obj1.int64Opt = 2;
  obj1.__isset.int64Opt = true;
  EXPECT_NE(obj1, obj2);
  obj2.int64Opt = 2;
  obj2.__isset.int64Opt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.stringReq = "hello";
  obj2.stringReq = "hello";
  EXPECT_EQ(obj1, obj2);
  obj1.stringOpt = "aloha";
  obj1.__isset.stringOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.stringOpt = "aloha";
  obj2.__isset.stringOpt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.setReq = std::set<int64_t>{1, 2};
  obj2.setReq = std::set<int64_t>{1, 2};
  EXPECT_EQ(obj1, obj2);
  obj1.setOpt = std::set<int64_t>{3, 4};
  obj1.__isset.setOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.setOpt = std::set<int64_t>{3, 4};
  obj2.__isset.setOpt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.listReq = std::vector<int64_t>{5, 6};
  obj2.listReq = std::vector<int64_t>{5, 6};
  EXPECT_EQ(obj1, obj2);
  obj1.listOpt = std::vector<int64_t>{7, 8};
  obj1.__isset.listOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.listOpt = std::vector<int64_t>{7, 8};
  obj2.__isset.listOpt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.mapReq = std::map<int64_t, int64_t>{{9, 10}, {11, 12}};
  obj2.mapReq = std::map<int64_t, int64_t>{{9, 10}, {11, 12}};
  EXPECT_EQ(obj1, obj2);
  obj1.mapOpt = std::map<int64_t, int64_t>{{13, 14}, {15, 16}};
  obj1.__isset.mapOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.mapOpt = std::map<int64_t, int64_t>{{13, 14}, {15, 16}};
  obj2.__isset.mapOpt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.enumReq = cpp2::HasOptionalsTestEnum::FOO;
  obj2.enumReq = cpp2::HasOptionalsTestEnum::FOO;
  EXPECT_EQ(obj1, obj2);
  obj1.enumOpt = cpp2::HasOptionalsTestEnum::BAR;
  obj1.__isset.enumOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.enumOpt = cpp2::HasOptionalsTestEnum::BAR;
  obj2.__isset.enumOpt = true;
  EXPECT_EQ(obj1, obj2);

  obj1.structReq = cpp2::HasOptionalsExtra();
  obj1.structReq.__clear();
  obj2.structReq = cpp2::HasOptionalsExtra();
  obj2.structReq.__clear();
  EXPECT_EQ(obj1, obj2);
  obj1.structOpt = cpp2::HasOptionalsExtra();
  obj1.structOpt.__clear();
  obj1.__isset.structOpt = true;
  EXPECT_NE(obj1, obj2);
  obj2.structOpt = cpp2::HasOptionalsExtra();
  obj2.structOpt.__clear();
  obj2.__isset.structOpt = true;
  EXPECT_EQ(obj1, obj2);

  // just one more test: try required/optional fields in the optional struct
  // to verify that recursive checking w/ optional fields works.
  // Don't bother testing all the nested struct's fields, this is enough.
  obj1.structOpt.extraInt64Req = 666;
  obj2.structOpt.extraInt64Req = 666;
  EXPECT_EQ(obj1, obj2);
  obj1.structOpt.extraInt64Opt = 13;
  obj1.structOpt.__isset.extraInt64Opt = true;
  EXPECT_NE(obj1, obj2);
  obj2.structOpt.extraInt64Opt = 13;
  obj2.structOpt.__isset.extraInt64Opt = true;
  EXPECT_EQ(obj1, obj2);
}
