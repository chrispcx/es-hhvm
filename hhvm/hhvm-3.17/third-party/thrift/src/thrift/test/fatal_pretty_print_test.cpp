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

#include <thrift/lib/cpp2/fatal/pretty_print.h>

#include <thrift/test/gen-cpp2/reflection_fatal_types.h>

#include <sstream>
#include <type_traits>

#include <gtest/gtest.h>

using output_result = std::false_type;

namespace test_cpp2 {
namespace cpp_reflection {

#define TEST_IMPL(Expected, ...) \
  do { \
    std::ostringstream out; \
    apache::thrift::pretty_print(out, __VA_ARGS__); \
    auto const actual = out.str(); \
    \
    if (output_result::value) { \
      std::cout << "actual output: " << actual \
        << std::endl << "expected output: " << Expected \
        << std::endl; \
    } \
    \
    EXPECT_EQ(Expected, actual); \
  } while (false)

TEST(fatal_pretty_print, pretty_print) {
  structA a1;
  a1.a = 99;
  a1.b = "abc";
  structA a2;
  a2.a = 1001;
  a2.b = "foo";
  structA a3;
  a3.a = 654;
  a3.b = "bar";
  structA a4;
  a4.a = 9791;
  a4.b = "baz";
  structA a5;
  a5.a = 111;
  a5.b = "gaz";

  structB b1;
  b1.c = 1.23;
  b1.d = true;
  structB b2;
  b2.c = 9.8;
  b2.d = false;
  structB b3;
  b3.c = 10.01;
  b3.d = true;
  structB b4;
  b4.c = 159.73;
  b4.d = false;
  structB b5;
  b5.c = 468.02;
  b5.d = true;

  structC c1;
  c1.a = 47;
  c1.b = "hello, world";
  c1.c = 132.98;
  c1.d = true;

  c1.e = enum1::field1;
  c1.f = enum2::field0_2;
  c1.g.set_us("this is a test");

  // c1.h intentionally left empty
  c1.i.set_a(a1);

  // c1.j intentionally left empty
  c1.j1 = {2, 4, 6, 8};
  c1.j2 = {enum1::field0, enum1::field1, enum1::field2};
  c1.j3 = {a1, a2, a3, a4};

  // c1.k intentionally left empty
  c1.k1 = {3, 5, 7, 9};
  c1.k2 = {enum2::field0_2, enum2::field1_2, enum2::field2_2};
  c1.k3 = {b1, b2, b3, b4};

  // c1.l intentionally left empty
  c1.l1 = {{2, 3}, {4, 5}, {6, 7}, {8, 9}};
  c1.l2 = {{12, enum1::field0}, {34, enum1::field1}, {56, enum1::field2}};
  c1.l3 = {{89, b1}, {78, b2}, {67, b3}, {56, b4}};

  c1.m1 = {{enum1::field0, 3}, {enum1::field1, 5}, {enum1::field2, 7}};
  c1.m2 = {
    {enum1::field0, enum2::field0_2},
    {enum1::field1, enum2::field1_2},
    {enum1::field2, enum2::field2_2}
  };
  c1.m3 = {{enum1::field0, b1}, {enum1::field1, b2}, {enum1::field2, b3}};

  c1.n1["abc"] = 3;
  c1.n1["def"] = 5;
  c1.n1["ghi"] = 7;
  c1.n1["jkl"] = 9;
  c1.n2["mno"] = enum1::field0;
  c1.n2["pqr"] = enum1::field1;
  c1.n2["stu"] = enum1::field2;
  c1.n3["vvv"] = b1;
  c1.n3["www"] = b2;
  c1.n3["xxx"] = b3;
  c1.n3["yyy"] = b4;

  c1.o1[a1] = 3;
  c1.o1[a2] = 5;
  c1.o1[a3] = 7;
  c1.o1[a4] = 9;
  c1.o2[a1] = enum1::field0;
  c1.o2[a2] = enum1::field1;
  c1.o2[a3] = enum1::field2;
  c1.o3[a1] = b1;
  c1.o3[a2] = b2;
  c1.o3[a3] = b3;
  c1.o3[a4] = b4;

  TEST_IMPL(
    "<struct>{\n"
    "  a: 47,\n"
    "  b: \"hello, world\",\n"
    "  c: 132.98,\n"
    "  d: true,\n"
    "  e: field1,\n"
    "  f: field0_2,\n"
    "  g: <variant>{\n"
    "    us: \"this is a test\"\n"
    "  },\n"
    "  h: <variant>{},\n"
    "  i: <variant>{\n"
    "    a: <struct>{\n"
    "      a: 99,\n"
    "      b: \"abc\"\n"
    "    }\n"
    "  },\n"
    "  j: <list>[],\n"
    "  j1: <list>[\n"
    "    0: 2,\n"
    "    1: 4,\n"
    "    2: 6,\n"
    "    3: 8\n"
    "  ],\n"
    "  j2: <list>[\n"
    "    0: field0,\n"
    "    1: field1,\n"
    "    2: field2\n"
    "  ],\n"
    "  j3: <list>[\n"
    "    0: <struct>{\n"
    "      a: 99,\n"
    "      b: \"abc\"\n"
    "    },\n"
    "    1: <struct>{\n"
    "      a: 1001,\n"
    "      b: \"foo\"\n"
    "    },\n"
    "    2: <struct>{\n"
    "      a: 654,\n"
    "      b: \"bar\"\n"
    "    },\n"
    "    3: <struct>{\n"
    "      a: 9791,\n"
    "      b: \"baz\"\n"
    "    }\n"
    "  ],\n"
    "  k: <set>{},\n"
    "  k1: <set>{\n"
    "    3,\n"
    "    5,\n"
    "    7,\n"
    "    9\n"
    "  },\n"
    "  k2: <set>{\n"
    "    field0_2,\n"
    "    field1_2,\n"
    "    field2_2\n"
    "  },\n"
    "  k3: <set>{\n"
    "    <struct>{\n"
    "      c: 1.23,\n"
    "      d: true\n"
    "    },\n"
    "    <struct>{\n"
    "      c: 9.8,\n"
    "      d: false\n"
    "    },\n"
    "    <struct>{\n"
    "      c: 10.01,\n"
    "      d: true\n"
    "    },\n"
    "    <struct>{\n"
    "      c: 159.73,\n"
    "      d: false\n"
    "    }\n"
    "  },\n"
    "  l: <map>{},\n"
    "  l1: <map>{\n"
    "    2: 3,\n"
    "    4: 5,\n"
    "    6: 7,\n"
    "    8: 9\n"
    "  },\n"
    "  l2: <map>{\n"
    "    12: field0,\n"
    "    34: field1,\n"
    "    56: field2\n"
    "  },\n"
    "  l3: <map>{\n"
    "    56: <struct>{\n"
    "      c: 159.73,\n"
    "      d: false\n"
    "    },\n"
    "    67: <struct>{\n"
    "      c: 10.01,\n"
    "      d: true\n"
    "    },\n"
    "    78: <struct>{\n"
    "      c: 9.8,\n"
    "      d: false\n"
    "    },\n"
    "    89: <struct>{\n"
    "      c: 1.23,\n"
    "      d: true\n"
    "    }\n"
    "  },\n"
    "  m1: <map>{\n"
    "    field0: 3,\n"
    "    field1: 5,\n"
    "    field2: 7\n"
    "  },\n"
    "  m2: <map>{\n"
    "    field0: field0_2,\n"
    "    field1: field1_2,\n"
    "    field2: field2_2\n"
    "  },\n"
    "  m3: <map>{\n"
    "    field0: <struct>{\n"
    "      c: 1.23,\n"
    "      d: true\n"
    "    },\n"
    "    field1: <struct>{\n"
    "      c: 9.8,\n"
    "      d: false\n"
    "    },\n"
    "    field2: <struct>{\n"
    "      c: 10.01,\n"
    "      d: true\n"
    "    }\n"
    "  },\n"
    "  n1: <map>{\n"
    "    \"abc\": 3,\n"
    "    \"def\": 5,\n"
    "    \"ghi\": 7,\n"
    "    \"jkl\": 9\n"
    "  },\n"
    "  n2: <map>{\n"
    "    \"mno\": field0,\n"
    "    \"pqr\": field1,\n"
    "    \"stu\": field2\n"
    "  },\n"
    "  n3: <map>{\n"
    "    \"vvv\": <struct>{\n"
    "      c: 1.23,\n"
    "      d: true\n"
    "    },\n"
    "    \"www\": <struct>{\n"
    "      c: 9.8,\n"
    "      d: false\n"
    "    },\n"
    "    \"xxx\": <struct>{\n"
    "      c: 10.01,\n"
    "      d: true\n"
    "    },\n"
    "    \"yyy\": <struct>{\n"
    "      c: 159.73,\n"
    "      d: false\n"
    "    }\n"
    "  },\n"
    "  o1: <map>{\n"
    "    <struct>{\n"
    "      a: 99,\n"
    "      b: \"abc\"\n"
    "    }: 3,\n"
    "    <struct>{\n"
    "      a: 654,\n"
    "      b: \"bar\"\n"
    "    }: 7,\n"
    "    <struct>{\n"
    "      a: 1001,\n"
    "      b: \"foo\"\n"
    "    }: 5,\n"
    "    <struct>{\n"
    "      a: 9791,\n"
    "      b: \"baz\"\n"
    "    }: 9\n"
    "  },\n"
    "  o2: <map>{\n"
    "    <struct>{\n"
    "      a: 99,\n"
    "      b: \"abc\"\n"
    "    }: field0,\n"
    "    <struct>{\n"
    "      a: 654,\n"
    "      b: \"bar\"\n"
    "    }: field2,\n"
    "    <struct>{\n"
    "      a: 1001,\n"
    "      b: \"foo\"\n"
    "    }: field1\n"
    "  },\n"
    "  o3: <map>{\n"
    "    <struct>{\n"
    "      a: 99,\n"
    "      b: \"abc\"\n"
    "    }: <struct>{\n"
    "      c: 1.23,\n"
    "      d: true\n"
    "    },\n"
    "    <struct>{\n"
    "      a: 654,\n"
    "      b: \"bar\"\n"
    "    }: <struct>{\n"
    "      c: 10.01,\n"
    "      d: true\n"
    "    },\n"
    "    <struct>{\n"
    "      a: 1001,\n"
    "      b: \"foo\"\n"
    "    }: <struct>{\n"
    "      c: 9.8,\n"
    "      d: false\n"
    "    },\n"
    "    <struct>{\n"
    "      a: 9791,\n"
    "      b: \"baz\"\n"
    "    }: <struct>{\n"
    "      c: 159.73,\n"
    "      d: false\n"
    "    }\n"
    "  }\n"
    "}",
    c1
  );

  TEST_IMPL(
    "===><struct>{\n"
    "===>*-=.|a: 47,\n"
    "===>*-=.|b: \"hello, world\",\n"
    "===>*-=.|c: 132.98,\n"
    "===>*-=.|d: true,\n"
    "===>*-=.|e: field1,\n"
    "===>*-=.|f: field0_2,\n"
    "===>*-=.|g: <variant>{\n"
    "===>*-=.|*-=.|us: \"this is a test\"\n"
    "===>*-=.|},\n"
    "===>*-=.|h: <variant>{},\n"
    "===>*-=.|i: <variant>{\n"
    "===>*-=.|*-=.|a: <struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 99,\n"
    "===>*-=.|*-=.|*-=.|b: \"abc\"\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|},\n"
    "===>*-=.|j: <list>[],\n"
    "===>*-=.|j1: <list>[\n"
    "===>*-=.|*-=.|0: 2,\n"
    "===>*-=.|*-=.|1: 4,\n"
    "===>*-=.|*-=.|2: 6,\n"
    "===>*-=.|*-=.|3: 8\n"
    "===>*-=.|],\n"
    "===>*-=.|j2: <list>[\n"
    "===>*-=.|*-=.|0: field0,\n"
    "===>*-=.|*-=.|1: field1,\n"
    "===>*-=.|*-=.|2: field2\n"
    "===>*-=.|],\n"
    "===>*-=.|j3: <list>[\n"
    "===>*-=.|*-=.|0: <struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 99,\n"
    "===>*-=.|*-=.|*-=.|b: \"abc\"\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|1: <struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 1001,\n"
    "===>*-=.|*-=.|*-=.|b: \"foo\"\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|2: <struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 654,\n"
    "===>*-=.|*-=.|*-=.|b: \"bar\"\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|3: <struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 9791,\n"
    "===>*-=.|*-=.|*-=.|b: \"baz\"\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|],\n"
    "===>*-=.|k: <set>{},\n"
    "===>*-=.|k1: <set>{\n"
    "===>*-=.|*-=.|3,\n"
    "===>*-=.|*-=.|5,\n"
    "===>*-=.|*-=.|7,\n"
    "===>*-=.|*-=.|9\n"
    "===>*-=.|},\n"
    "===>*-=.|k2: <set>{\n"
    "===>*-=.|*-=.|field0_2,\n"
    "===>*-=.|*-=.|field1_2,\n"
    "===>*-=.|*-=.|field2_2\n"
    "===>*-=.|},\n"
    "===>*-=.|k3: <set>{\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 1.23,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 9.8,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 10.01,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 159.73,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|},\n"
    "===>*-=.|l: <map>{},\n"
    "===>*-=.|l1: <map>{\n"
    "===>*-=.|*-=.|2: 3,\n"
    "===>*-=.|*-=.|4: 5,\n"
    "===>*-=.|*-=.|6: 7,\n"
    "===>*-=.|*-=.|8: 9\n"
    "===>*-=.|},\n"
    "===>*-=.|l2: <map>{\n"
    "===>*-=.|*-=.|12: field0,\n"
    "===>*-=.|*-=.|34: field1,\n"
    "===>*-=.|*-=.|56: field2\n"
    "===>*-=.|},\n"
    "===>*-=.|l3: <map>{\n"
    "===>*-=.|*-=.|56: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 159.73,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|67: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 10.01,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|78: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 9.8,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|89: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 1.23,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|},\n"
    "===>*-=.|m1: <map>{\n"
    "===>*-=.|*-=.|field0: 3,\n"
    "===>*-=.|*-=.|field1: 5,\n"
    "===>*-=.|*-=.|field2: 7\n"
    "===>*-=.|},\n"
    "===>*-=.|m2: <map>{\n"
    "===>*-=.|*-=.|field0: field0_2,\n"
    "===>*-=.|*-=.|field1: field1_2,\n"
    "===>*-=.|*-=.|field2: field2_2\n"
    "===>*-=.|},\n"
    "===>*-=.|m3: <map>{\n"
    "===>*-=.|*-=.|field0: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 1.23,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|field1: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 9.8,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|field2: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 10.01,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|},\n"
    "===>*-=.|n1: <map>{\n"
    "===>*-=.|*-=.|\"abc\": 3,\n"
    "===>*-=.|*-=.|\"def\": 5,\n"
    "===>*-=.|*-=.|\"ghi\": 7,\n"
    "===>*-=.|*-=.|\"jkl\": 9\n"
    "===>*-=.|},\n"
    "===>*-=.|n2: <map>{\n"
    "===>*-=.|*-=.|\"mno\": field0,\n"
    "===>*-=.|*-=.|\"pqr\": field1,\n"
    "===>*-=.|*-=.|\"stu\": field2\n"
    "===>*-=.|},\n"
    "===>*-=.|n3: <map>{\n"
    "===>*-=.|*-=.|\"vvv\": <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 1.23,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|\"www\": <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 9.8,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|\"xxx\": <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 10.01,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|\"yyy\": <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 159.73,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|},\n"
    "===>*-=.|o1: <map>{\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 99,\n"
    "===>*-=.|*-=.|*-=.|b: \"abc\"\n"
    "===>*-=.|*-=.|}: 3,\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 654,\n"
    "===>*-=.|*-=.|*-=.|b: \"bar\"\n"
    "===>*-=.|*-=.|}: 7,\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 1001,\n"
    "===>*-=.|*-=.|*-=.|b: \"foo\"\n"
    "===>*-=.|*-=.|}: 5,\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 9791,\n"
    "===>*-=.|*-=.|*-=.|b: \"baz\"\n"
    "===>*-=.|*-=.|}: 9\n"
    "===>*-=.|},\n"
    "===>*-=.|o2: <map>{\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 99,\n"
    "===>*-=.|*-=.|*-=.|b: \"abc\"\n"
    "===>*-=.|*-=.|}: field0,\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 654,\n"
    "===>*-=.|*-=.|*-=.|b: \"bar\"\n"
    "===>*-=.|*-=.|}: field2,\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 1001,\n"
    "===>*-=.|*-=.|*-=.|b: \"foo\"\n"
    "===>*-=.|*-=.|}: field1\n"
    "===>*-=.|},\n"
    "===>*-=.|o3: <map>{\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 99,\n"
    "===>*-=.|*-=.|*-=.|b: \"abc\"\n"
    "===>*-=.|*-=.|}: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 1.23,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 654,\n"
    "===>*-=.|*-=.|*-=.|b: \"bar\"\n"
    "===>*-=.|*-=.|}: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 10.01,\n"
    "===>*-=.|*-=.|*-=.|d: true\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 1001,\n"
    "===>*-=.|*-=.|*-=.|b: \"foo\"\n"
    "===>*-=.|*-=.|}: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 9.8,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|},\n"
    "===>*-=.|*-=.|<struct>{\n"
    "===>*-=.|*-=.|*-=.|a: 9791,\n"
    "===>*-=.|*-=.|*-=.|b: \"baz\"\n"
    "===>*-=.|*-=.|}: <struct>{\n"
    "===>*-=.|*-=.|*-=.|c: 159.73,\n"
    "===>*-=.|*-=.|*-=.|d: false\n"
    "===>*-=.|*-=.|}\n"
    "===>*-=.|}\n"
    "===>}",
    c1,
    "*-=.|",
    "===>"
  );
}

} // namespace cpp_reflection {
} // namespace test_cpp2 {
