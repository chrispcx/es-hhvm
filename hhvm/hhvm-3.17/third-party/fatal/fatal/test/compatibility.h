/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#ifndef FATAL_INCLUDE_fatal_test_compatibility_h
#define FATAL_INCLUDE_fatal_test_compatibility_h

#include <fatal/test/test.h>

// for internal use only

namespace fatal {
namespace test {

#define TEST FATAL_TEST

#define WARN_UNREACHABLE FATAL_WARN_UNREACHABLE
#define WARN_TRUE FATAL_WARN_TRUE
#define WARN_FALSE FATAL_WARN_FALSE
#define WARN_NULL FATAL_WARN_NULL
#define WARN_NOT_NULL FATAL_WARN_NOT_NULL
#define WARN_NO_THROW FATAL_WARN_NO_THROW
#define WARN_THROW FATAL_WARN_THROW
#define WARN_EQ FATAL_WARN_EQ
#define WARN_NE FATAL_WARN_NE
#define WARN_LT FATAL_WARN_LT
#define WARN_LE FATAL_WARN_LE
#define WARN_GT FATAL_WARN_GT
#define WARN_GE FATAL_WARN_GE

#define EXPECT_UNREACHABLE FATAL_EXPECT_UNREACHABLE
#define EXPECT_TRUE FATAL_EXPECT_TRUE
#define EXPECT_FALSE FATAL_EXPECT_FALSE
#define EXPECT_NULL FATAL_EXPECT_NULL
#define EXPECT_NOT_NULL FATAL_EXPECT_NOT_NULL
#define EXPECT_NO_THROW FATAL_EXPECT_NO_THROW
#define EXPECT_THROW FATAL_EXPECT_THROW
#define EXPECT_EQ FATAL_EXPECT_EQ
#define EXPECT_NE FATAL_EXPECT_NE
#define EXPECT_LT FATAL_EXPECT_LT
#define EXPECT_LE FATAL_EXPECT_LE
#define EXPECT_GT FATAL_EXPECT_GT
#define EXPECT_GE FATAL_EXPECT_GE
#define EXPECT_SAME FATAL_EXPECT_SAME

#define ASSERT_UNREACHABLE FATAL_ASSERT_UNREACHABLE
#define ASSERT_TRUE FATAL_ASSERT_TRUE
#define ASSERT_FALSE FATAL_ASSERT_FALSE
#define ASSERT_NULL FATAL_ASSERT_NULL
#define ASSERT_NOT_NULL FATAL_ASSERT_NOT_NULL
#define ASSERT_NO_THROW FATAL_ASSERT_NO_THROW
#define ASSERT_THROW FATAL_ASSERT_THROW
#define ASSERT_EQ FATAL_ASSERT_EQ
#define ASSERT_NE FATAL_ASSERT_NE
#define ASSERT_LT FATAL_ASSERT_LT
#define ASSERT_LE FATAL_ASSERT_LE
#define ASSERT_GT FATAL_ASSERT_GT
#define ASSERT_GE FATAL_ASSERT_GE

} // namespace test {
} // namespace fatal {

#endif // FATAL_INCLUDE_fatal_test_compatibility_h
