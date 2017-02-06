/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <folly/json.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myBinaryStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myStringStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/mySetStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myDoubleStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myByteStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myI16Struct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myI32Struct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myBoolStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myComplexStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myMixedStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myMapStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myEmptyStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myKeyStruct_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myCombinedStructs_types.h>
#include <thrift/test/JsonToThriftTest/gen-cpp/myDoubleListStruct_types.h>

#include <thrift/lib/cpp/Thrift.h>

#include <memory>
#include <thrift/lib/cpp/transport/TBufferTransports.h>
#include <thrift/lib/cpp/protocol/TJSONProtocol.h>
#include <thrift/lib/cpp/protocol/TSimpleJSONProtocol.h>
#include <thrift/lib/cpp/util/ThriftSerializer.h>
#include <fstream>
#include <iostream>
#include <limits>

#include <gtest/gtest.h>

using namespace std;

using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TJSONProtocol;
using apache::thrift::protocol::TSimpleJSONProtocol;
using apache::thrift::transport::TMemoryBuffer;

template <typename T>
static void testSimpleJSON(T dataStruct){
   std::string simpleJsonText = serializeJSON(dataStruct);

   T parsedStruct;
   deserializeJSON(parsedStruct, simpleJsonText);

   EXPECT_TRUE(parsedStruct == dataStruct);
}

template <typename T>
static std::string serializeJSON(T dataStruct,
                   string fileName = string()) {

    std::shared_ptr<TMemoryBuffer> buffer =
        std::make_shared<TMemoryBuffer>();

    TProtocol* prot = new TSimpleJSONProtocol(buffer);

    std::shared_ptr<TProtocol> protocol(prot);

    dataStruct.write(protocol.get());

    uint8_t* buf = nullptr;
    uint32_t size;
    buffer->getBuffer(&buf, &size);

    if (!fileName.empty()){
        string jsonFileName = fileName + ".json";
        cout << " Writing JSON to " << fileName;
        FILE* outFile = fopen(jsonFileName.c_str(), "w");
        fwrite(buf, sizeof(uint8_t), size, outFile);
        fclose(outFile);
    }

    std::string ret((char*)buf, size);
    return ret;
}

template <typename T>
static void deserializeJSON(T& dataStruct,
                   const string& json) {

    std::shared_ptr<TMemoryBuffer> buffer =
        std::make_shared<TMemoryBuffer>(
            (uint8_t *) json.c_str(), json.length());

    TProtocol* prot = new TSimpleJSONProtocol(buffer);

    std::shared_ptr<TProtocol> protocol(prot);

    uint32_t numRead = dataStruct.read(protocol.get());

    EXPECT_TRUE(numRead == json.length());
}

TEST(SimpleJSONToThriftTest, SimpleJSON_ComplexSerialization) {
   myComplexStruct thriftComplexObj;
   myMixedStruct thriftMixedObj;

   mySimpleStruct thriftSimpleObj;
   thriftSimpleObj.a = true;
   thriftSimpleObj.b = 120;
   thriftSimpleObj.c = 9990;
   thriftSimpleObj.d = -9990;
   thriftSimpleObj.e = -1;
   thriftSimpleObj.f = 0.9;
   thriftSimpleObj.g = "Simple String";

   mySuperSimpleStruct superSimple;
   superSimple.a = 121;
   thriftMixedObj.a.push_back(18);
   thriftMixedObj.b.push_back(superSimple);
   thriftMixedObj.c.insert(std::make_pair("flame",-8));
   thriftMixedObj.c.insert(std::make_pair("fire",-191));
   thriftMixedObj.d.insert(std::make_pair("key1",superSimple));
   thriftMixedObj.e.insert(88);thriftMixedObj.e.insert(89);

   thriftComplexObj.a = thriftSimpleObj;
   thriftComplexObj.b.push_back(25);
   thriftComplexObj.b.push_back(24);

   for(int i=0;i<3;i++) {
      mySimpleStruct obj;
      obj.a = true;
      obj.b = 80 + i;
      obj.c = 7000+i;
      obj.e = -i;
      obj.f = -0.5 * i;
      string elmName = "element" + folly::to<std::string>(i+1);
      obj.g = elmName.c_str();
      thriftComplexObj.c.insert(std::make_pair(elmName, thriftSimpleObj));
   }

   thriftComplexObj.e = EnumTest::EnumTwo;

   testSimpleJSON(thriftMixedObj);
   testSimpleJSON(thriftComplexObj);
}

TEST(SimpleJSONToThriftTest, SimpleJSON_BasicSerialization) {
   mySimpleStruct thriftSimpleObj;
   myDoubleStruct thriftDoubleObj;
   myBoolStruct thriftBoolObj1, thriftBoolObj2;
   myByteStruct thriftByteObj;
   myStringStruct thriftStringObj;
   myI16Struct thriftI16Obj;
   myI32Struct thriftI32Obj;

   thriftSimpleObj.a = false;
   thriftSimpleObj.b = 87;
   thriftSimpleObj.c = 7880;
   thriftSimpleObj.d = -7880;
   thriftSimpleObj.e = -1;
   thriftSimpleObj.f = -0.1;
   thriftSimpleObj.g = "T-bone";

   thriftDoubleObj.a = 100.5;
   testSimpleJSON(thriftDoubleObj);
   thriftDoubleObj.a = numeric_limits<double>::infinity();
   testSimpleJSON(thriftDoubleObj);
   thriftDoubleObj.a = -numeric_limits<double>::infinity();
   testSimpleJSON(thriftDoubleObj);

   thriftBoolObj1.a = true;
   thriftBoolObj2.a = false;

   thriftByteObj.a = 115;
   thriftStringObj.a = "testing";

   thriftI16Obj.a = 4567;
   thriftI32Obj.a = 12131415;

   testSimpleJSON(thriftSimpleObj);
   testSimpleJSON(thriftBoolObj1);
   testSimpleJSON(thriftBoolObj2);
   testSimpleJSON(thriftByteObj);
   testSimpleJSON(thriftStringObj);
   testSimpleJSON(thriftI16Obj);
   testSimpleJSON(thriftI32Obj);
}

TEST(SimpleJSONToThriftTest, SimpleJSON_BasicSerializationNan) {
  myDoubleListStruct obj;
  std::vector<double> array = {NAN, -NAN, 0.3333333333};
  obj.l = array;

  auto jsonString = serializeJSON(obj);
  myDoubleListStruct parsedStruct;
  deserializeJSON(parsedStruct, jsonString);

  EXPECT_EQ(obj.l.size(), parsedStruct.l.size());
  for (int i = 0; i < obj.l.size(); ++i) {
    if (std::isnan(obj.l[i]) == std::isnan(parsedStruct.l[i])) {
      continue;
    }
    EXPECT_EQ(obj.l[i], parsedStruct.l[i]);
  }

  auto jsonString2 = serializeJSON(parsedStruct);

  // this checks that nan and -nan still have correct '-' information
  EXPECT_EQ(jsonString, jsonString2);
}

TEST(SimpleJSONToThriftTest, SimpleStructMissingNonRequiredField) {

  // jsonSimpleT1 is to test whether __isset is set properly, given
  // that all the required field has value: field a's value is missing
  string jsonSimpleT("{\"c\":16,\"d\":32,\"e\":64,\"b\":8,"
      "\"f\":0.99,\"g\":\"Hello\"}");
  mySimpleStruct thriftSimpleObj;

  deserializeJSON(thriftSimpleObj, jsonSimpleT);

  EXPECT_TRUE(!thriftSimpleObj.__isset.a);
  EXPECT_EQ(thriftSimpleObj.b, 8);
  EXPECT_TRUE(thriftSimpleObj.__isset.b);
  // field c doesn't have __isset field, since it is required.
  EXPECT_EQ(thriftSimpleObj.c, 16);
  EXPECT_EQ(thriftSimpleObj.d, 32);
  EXPECT_TRUE(thriftSimpleObj.__isset.d);
  EXPECT_EQ(thriftSimpleObj.e, 64);
  EXPECT_TRUE(thriftSimpleObj.__isset.e);
  EXPECT_EQ(thriftSimpleObj.f, 0.99);
  EXPECT_TRUE(thriftSimpleObj.__isset.f);
  EXPECT_EQ(thriftSimpleObj.g, "Hello");
  EXPECT_TRUE(thriftSimpleObj.__isset.g);
}

TEST(SimpleJSONToThriftTest, NegativeBoundaryCase) {

  string jsonByteTW("{\"a\":-129}");
  myByteStruct thriftByteObjW;
  try {
    deserializeJSON(thriftByteObjW, jsonByteTW);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonByteT("{\"a\":-128}");
  myByteStruct thriftByteObj;
  deserializeJSON(thriftByteObj, jsonByteT);
  EXPECT_EQ(thriftByteObj.a, -128);

  string jsonI16TW("{\"a\":-32769}");
  myI16Struct thriftI16ObjW;
  try {
    deserializeJSON(thriftI16ObjW, jsonI16TW);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonI16T("{\"a\":-32768}");
  myI16Struct thriftI16Obj;
  deserializeJSON(thriftI16Obj, jsonI16T);
  EXPECT_EQ(thriftI16Obj.a, -32768);

  string jsonI32TW("{\"a\":-2147483649}");
  myI32Struct thriftI32ObjW;
  try {
    deserializeJSON(thriftI32ObjW, jsonI32TW);
    cout << serializeJSON(thriftI32ObjW) << endl;
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonI32T("{\"a\":-2147483648}");
  myI32Struct thriftI32Obj;
  deserializeJSON(thriftI32Obj, jsonI32T);
  EXPECT_EQ(thriftI32Obj.a, -2147483648);
}

TEST(SimpleJSONToThriftTest, PassingWrongType) {

  string jsonI32T("{\"a\":\"hello\"}");
  myI32Struct thriftI32Obj;
  try {
    deserializeJSON(thriftI32Obj, jsonI32T);
    EXPECT_TRUE(false);
  } catch (std::exception &e) {
  }
}

TEST(SimpleJSONToThriftTest, Whitespace) {
  // tests if \n \r \t and space are ignored properly
  string jsonSimpleT("\n\r\t {\n\r\t \"c\"\n\r\t :\n\r\t 16,\"d\":32"
                     ",\"e\":64\t "
                     ", \n\r\t\"b\":\r\t\n 8"
                     ",\"f\": \n\r\t0.99\r"
                     ",\r\"g\" :  \"Hello\"\n\r\t "
                     "}\n\r\t ");

  mySimpleStruct thriftSimpleObj;
  deserializeJSON(thriftSimpleObj, jsonSimpleT);

  string jsonComplexT("{\"a\":" + jsonSimpleT + ","
                      "\"b\":\t\n\r [\n\t\r 3,2,1\r\t \n] \t\n\r,"
                      "\"c\":\n\r\t { \t\n\r \"key1\":" +
                      jsonSimpleT + "  ,     \"key2\": {\"c\":20,"
                      "\"d\":320,\"f\":0.001}}\r\r\t\t\n\n   \n}\r \t\n\t\t\t");
  myComplexStruct thriftComplexObj;

  deserializeJSON(thriftComplexObj, jsonComplexT);

  EXPECT_TRUE(!thriftComplexObj.a.__isset.a);
  EXPECT_EQ(thriftComplexObj.a.b, 8);
  EXPECT_TRUE(thriftComplexObj.a.__isset.b);
  // field c doesn't have __isset field, since it is required.
  EXPECT_EQ(thriftComplexObj.a.c, 16);
  EXPECT_EQ(thriftComplexObj.a.d, 32);
  EXPECT_TRUE(thriftComplexObj.a.__isset.d);
  EXPECT_EQ(thriftComplexObj.a.e, 64);
  EXPECT_TRUE(thriftComplexObj.a.__isset.e);
  EXPECT_EQ(thriftComplexObj.a.f, 0.99);
  EXPECT_TRUE(thriftComplexObj.a.__isset.f);
  EXPECT_EQ(thriftComplexObj.a.g, "Hello");
  EXPECT_TRUE(thriftComplexObj.a.__isset.g);

  EXPECT_EQ(thriftComplexObj.b[0], 3);
  EXPECT_EQ(thriftComplexObj.b[1], 2);
  EXPECT_EQ(thriftComplexObj.b[2], 1);

  EXPECT_TRUE(!thriftComplexObj.c["key1"].__isset.a);
  EXPECT_EQ(thriftComplexObj.c["key1"].b, 8);
  EXPECT_TRUE(thriftComplexObj.c["key1"].__isset.b);
  // field c doesn't have __isset field, since it is required.
  EXPECT_EQ(thriftComplexObj.c["key1"].c, 16);
  EXPECT_EQ(thriftComplexObj.c["key1"].d, 32);
  EXPECT_TRUE(thriftComplexObj.c["key1"].__isset.d);
  EXPECT_EQ(thriftComplexObj.c["key1"].e, 64);
  EXPECT_TRUE(thriftComplexObj.c["key1"].__isset.e);
  EXPECT_EQ(thriftComplexObj.c["key1"].f, 0.99);
  EXPECT_TRUE(thriftComplexObj.c["key1"].__isset.f);
  EXPECT_EQ(thriftComplexObj.c["key1"].g, "Hello");
  EXPECT_TRUE(thriftComplexObj.c["key1"].__isset.g);

  EXPECT_EQ(thriftComplexObj.c["key2"].c, 20);
  EXPECT_EQ(thriftComplexObj.c["key2"].d, 320);
  EXPECT_EQ(thriftComplexObj.c["key2"].f, 0.001);
}
//fields in JSON that are not present in the thrift type spec
TEST(SimpleJSONToThriftTest, MissingField) {
  string jsonSimpleT("{\"c\":16,\"d\":32,\"e\":64,\"b\":8,"
      "\"f\":0.99,\"g\":\"Hello\",\"extra\":12}");
  mySimpleStruct thriftSimpleObj;
  deserializeJSON(thriftSimpleObj, jsonSimpleT);

  EXPECT_TRUE(!thriftSimpleObj.__isset.a);
  EXPECT_EQ(thriftSimpleObj.b, 8);
  EXPECT_TRUE(thriftSimpleObj.__isset.b);
  // field c doesn't have __isset field, since it is required.
  EXPECT_EQ(thriftSimpleObj.c, 16);
  EXPECT_EQ(thriftSimpleObj.d, 32);
  EXPECT_TRUE(thriftSimpleObj.__isset.d);
  EXPECT_EQ(thriftSimpleObj.e, 64);
  EXPECT_TRUE(thriftSimpleObj.__isset.e);
  EXPECT_EQ(thriftSimpleObj.f, 0.99);
  EXPECT_TRUE(thriftSimpleObj.__isset.f);
  EXPECT_EQ(thriftSimpleObj.g, "Hello");
  EXPECT_TRUE(thriftSimpleObj.__isset.g);

  // checks that a map is skipped properly
  string jsonEmptyListT("{\"e\":[1, 0.13]}");
  myEmptyStruct thriftEmptyListObj;
  deserializeJSON(thriftEmptyListObj, jsonEmptyListT);

  // checks that a map is skipped properly
  string jsonEmptyMapT("{\"m\":{\"1\":2, \"3\":13}}");
  myEmptyStruct thriftEmptyMapObj;
  deserializeJSON(thriftEmptyMapObj, jsonEmptyMapT);

  // checks that all fields are skipped properly
  string jsonEmptyT("{\"a\": 1,\"b\":-0.1,\"c\":false,\"d\": true"
                     ",\"e\":[ 0.3,1],\"f\":{ \"g\":\"abc\",\"h\":\"def\"}"
                     ",\"i\":[[ ],[]],\"j\":{}}");
  myEmptyStruct thriftEmptyObj;
  deserializeJSON(thriftEmptyObj, jsonEmptyT);

  // checks that all fields are skipped properly
  string jsonNestedT("{\"a\":" + jsonEmptyT +
                      ",\"b\":[" + jsonEmptyT + "," + jsonEmptyT + "]"
                      ",\"c\":-123}");
  myNestedEmptyStruct thriftNestedObj;
  deserializeJSON(thriftNestedObj, jsonNestedT);
  EXPECT_TRUE(thriftNestedObj.__isset.a);
  EXPECT_TRUE(thriftNestedObj.__isset.b);
  EXPECT_TRUE(thriftNestedObj.__isset.c);
  EXPECT_EQ(thriftNestedObj.c, -123);
}

TEST(SimpleJSONToThriftTest, BoundaryCase) {

  // jsonSimpleT2 is to test whether the generated code throw exeption
  // if the required field doesn't have value : field c's value is
  // missing
  string jsonSimpleT("{\"a\":true,\"d\":32,\"e\":64,\"b\":8,"
      "\"f\":0.99,\"g\":\"Hello\"}");
  mySimpleStruct thriftSimpleObj;
  try {
    deserializeJSON(thriftSimpleObj, jsonSimpleT);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonByteTW("{\"a\":128}");
  myByteStruct thriftByteObjW;
  try {
    deserializeJSON(thriftByteObjW, jsonByteTW);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonByteT("{\"a\":127}");
  myByteStruct thriftByteObj;
  deserializeJSON(thriftByteObj, jsonByteT);
  EXPECT_EQ(thriftByteObj.a, 127);

  string jsonI16TW("{\"a\":32768}");
  myI16Struct thriftI16ObjW;
  try {
    deserializeJSON(thriftI16ObjW, jsonI16TW);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonI16T("{\"a\":32767}");
  myI16Struct thriftI16Obj;
  deserializeJSON(thriftI16Obj, jsonI16T);
  EXPECT_EQ(thriftI16Obj.a, 32767);

  string jsonI32TW("{\"a\":2147483648}");
  myI32Struct thriftI32ObjW;
  try {
    deserializeJSON(thriftI32ObjW, jsonI32TW);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }

  string jsonI32T("{\"a\":2147483647}");
  myI32Struct thriftI32Obj;
  deserializeJSON(thriftI32Obj, jsonI32T);
  EXPECT_EQ(thriftI32Obj.a, 2147483647);

  string jsonBoolTW("{\"a\":2}");
  myBoolStruct thriftBoolObjW;
  try {
    deserializeJSON(thriftBoolObjW, jsonBoolTW);
    EXPECT_TRUE(false);
  } catch (std::exception &e) {
  }

}

TEST(SimpleJSONToThriftTest, DoubleExponents) {
  string jsonDouble("{\"a\":21.47483647e9}");
  myDoubleStruct thriftDoubleObj;
  deserializeJSON(thriftDoubleObj, jsonDouble);
  EXPECT_EQ(thriftDoubleObj.a, 21.47483647e9);
}

TEST(SimpleJSONToThriftTest, ComplexTypeMissingRequiredFieldInMember) {

  string jsonT("{\"a\":true,\"c\":16,\"d\":32,\"e\":64,\"b\":8,"
      "\"f\":0.99,\"g\":\"Hello\"}");
  string jsonComplexT
    ("{\"a\":" + jsonT +  ",\"b\":[3,2,1],\"c\":{\"key1\":" +
    jsonT + ",\"key2\":{\"d\":320,\"f\":0.001}}}");

  myComplexStruct thriftComplexObj;
  try {
    deserializeJSON(thriftComplexObj, jsonComplexT);
    EXPECT_TRUE(false);
  } catch (apache::thrift::TException &e) {
  }
}

TEST(SimpleJSONToThriftTest, ComplexTypeTest) {

  string jsonT("{\"a\":true,\"c\":16,\"d\":32,\"e\":64,\"b\":8,"
      "\"f\":0.99,\"g\":\"Hello\"}");
  string jsonComplexT
    ("{\"a\":" + jsonT +  ",\"b\":[3,2,1],\"c\":{\"key1\":" +
    jsonT + ",\"key2\":{\"c\":20, \"d\":320,\"f\":0.001}}}");

  myComplexStruct thriftComplexObj;
  deserializeJSON(thriftComplexObj, jsonComplexT);

  EXPECT_EQ(thriftComplexObj.a.b, 8);
  EXPECT_EQ(thriftComplexObj.a.c, 16);
  EXPECT_EQ(thriftComplexObj.a.d, 32);
  EXPECT_EQ(thriftComplexObj.a.e, 64);
  EXPECT_EQ(thriftComplexObj.a.f, 0.99);
  EXPECT_EQ(thriftComplexObj.a.g, "Hello");

  EXPECT_EQ(thriftComplexObj.b[0], 3);
  EXPECT_EQ(thriftComplexObj.b[1], 2);
  EXPECT_EQ(thriftComplexObj.b[2], 1);

  EXPECT_EQ(thriftComplexObj.c["key1"].b, 8);
  EXPECT_EQ(thriftComplexObj.c["key1"].c, 16);
  EXPECT_EQ(thriftComplexObj.c["key1"].d, 32);
  EXPECT_EQ(thriftComplexObj.c["key1"].e, 64);
  EXPECT_EQ(thriftComplexObj.c["key1"].f, 0.99);
  EXPECT_EQ(thriftComplexObj.c["key1"].g, "Hello");
  EXPECT_EQ(thriftComplexObj.c["key2"].c, 20);
  EXPECT_EQ(thriftComplexObj.c["key2"].d, 320);
  EXPECT_EQ(thriftComplexObj.c["key2"].f, 0.001);
}

TEST(SimpleJSONToThriftTest, SetTypeTest) {

  string jsonT("{\"a\":[1,2,3]}");
  mySetStruct thriftSetObj;
  deserializeJSON(thriftSetObj, jsonT);
  EXPECT_TRUE(thriftSetObj.__isset.a);
  EXPECT_EQ(thriftSetObj.a.size(), 3);
  EXPECT_TRUE(thriftSetObj.a.find(2) != thriftSetObj.a.end());
  EXPECT_TRUE(thriftSetObj.a.find(5) ==  thriftSetObj.a.end());
}

TEST(SimpleJSONToThriftTest, MixedStructTest) {
  string jsonT("{\"a\":[1],\"b\":[{\"a\":1}],\"c\":{\"hello\":1},"
      "\"d\":{\"hello\":{\"a\":1}},\"e\":[1]}");
  myMixedStruct thriftMixedObj;
  deserializeJSON(thriftMixedObj, jsonT);
  EXPECT_EQ(thriftMixedObj.a[0], 1);
  EXPECT_EQ(thriftMixedObj.b[0].a, 1);
  EXPECT_EQ(thriftMixedObj.c["hello"], 1);
  EXPECT_EQ(thriftMixedObj.d["hello"].a, 1);
  EXPECT_TRUE(thriftMixedObj.e.find(1) != thriftMixedObj.e.end());
}

TEST(SimpleJSONToThriftTest, MapTypeTest) {
  string stringJson("\"stringMap\": {\"a\":\"A\", \"b\":\"B\"}");
  string boolJson("\"boolMap\": {\"true\":\"True\", \"false\":\"False\"}");
  string byteJson("\"byteMap\": {\"1\":\"one\", \"2\":\"two\"}");
  string doubleJson("\"doubleMap\": {\"0.1\":\"0.one\", \"0.2\":\"0.two\"}");
  string enumJson("\"enumMap\": {\"1\":\"male\", \"2\":\"female\"}");
  string json = "{" + stringJson + "," + boolJson + "," + byteJson + "," +
    doubleJson + "," + enumJson + "}";
  myMapStruct mapStruct;
  deserializeJSON(mapStruct, json);
  EXPECT_EQ(mapStruct.stringMap.size(), 2);
  EXPECT_EQ(mapStruct.stringMap["a"], "A");
  EXPECT_EQ(mapStruct.stringMap["b"], "B");
  EXPECT_EQ(mapStruct.boolMap.size(), 2);
  EXPECT_EQ(mapStruct.boolMap[true], "True");
  EXPECT_EQ(mapStruct.boolMap[false], "False");
  EXPECT_EQ(mapStruct.byteMap.size(), 2);
  EXPECT_EQ(mapStruct.byteMap[1], "one");
  EXPECT_EQ(mapStruct.byteMap[2], "two");
  EXPECT_EQ(mapStruct.doubleMap.size(), 2);
  EXPECT_EQ(mapStruct.doubleMap[0.1], "0.one");
  EXPECT_EQ(mapStruct.doubleMap[0.2], "0.two");
  EXPECT_EQ(mapStruct.enumMap.size(), 2);
  EXPECT_EQ(mapStruct.enumMap[Gender::MALE], "male");
  EXPECT_EQ(mapStruct.enumMap[Gender::FEMALE], "female");
}

TEST(SimpleJSONToThriftTest, EmptyStringTest) {
  string jsonT("{\"a\":\"\"}");
  myStringStruct thriftStringObj;
  deserializeJSON(thriftStringObj, jsonT);
  EXPECT_EQ(thriftStringObj.a, "");
}

TEST(SimpleJSONToThriftTest, BinaryTypeTest) {
  string jsonT("{\"a\":\"SSBsb3ZlIEJhc2U2NCEA\"}");
  myBinaryStruct thriftBinaryObj;
  deserializeJSON(thriftBinaryObj, jsonT);
  EXPECT_EQ(thriftBinaryObj.a, std::string("I love Base64!\0", 15));
}

TEST(SimpleJSONToThriftTest, CompoundTest) {
  SmallStruct struct1;
  struct1.bools = {};
  struct1.ints = {};

  SmallStruct struct2;
  struct1.bools = {true};
  struct1.ints = {1};

  SmallStruct struct3;
  struct1.bools = {false, true};
  struct1.ints = {1, 2};

  NestedStruct nester;
  nester.lists = {{}, {{}, {1}, {2, 3}}, {{4, 5, 6}}};
  nester.sets = {{}, {{}, {1}, {2, 3}}, {{4, 5, 6}}};
  nester.maps["abc"][1] = {struct1, struct1, struct2};
  nester.maps["abc"][2] = {struct1, struct2, struct3};
  nester.maps["edf"][-10] = {struct2, struct3, struct3};
  nester.maps["ghi"] = {};
  nester.maps["jkl"][0] = {};

  TestStruct stuff;
  stuff.i1 = 1;
  stuff.i2 = -2;
  stuff.i3 = 3;
  stuff.b1 = true;
  stuff.b2 = false;
  stuff.doubles = {0.0, 1.0, -2.0};
  stuff.ints = {0, 1, -2};
  stuff.m1 = {{"one", 1}, {"two", 2}, {"three", 3}};
  stuff.m2 = {{0, {}}, {1, {"one"}}, {2, {"one", "two"}}};
  stuff.structs = {struct1, struct2, struct2};
  stuff.n = nester;
  stuff.s = "hello \\u!@#$%^&*()\\r\\\\n\\'\"";

  testSimpleJSON(stuff);

  std::string text = ThriftSimpleJSONString(stuff);
  TestStruct deserialized;
  deserializeJSON(deserialized, text);

  EXPECT_TRUE(stuff == deserialized);
}

TEST(SimpleJSONToThriftTest, MapKeysTests) {
  myKeyStruct mapStruct;
  mapStruct.a[{}] = "";
  mapStruct.a[{1}] = "1";
  mapStruct.a[{1,2,3}] = "123";

  // currently the implementation does not throw errors on
  // map keys that are lists, maps, sets or structs
  // this may be a desireable feature later on
  try {
    testSimpleJSON(mapStruct);
  } catch (apache::thrift::TException &e) {
    EXPECT_TRUE(false);
  }
}
