/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include <fatal/type/benchmark/trie/trie_benchmark.h>

#include <fatal/benchmark/driver.h>

namespace fatal {

CREATE_BENCHMARK(n30_len10,
  s10_00, s10_01, s10_02, s10_03, s10_04,
  s10_05, s10_06, s10_07, s10_08, s10_09,
  s10_10, s10_11, s10_12, s10_13, s10_14,
  s10_15, s10_16, s10_17, s10_18, s10_19,
  s10_20, s10_21, s10_22, s10_23, s10_24,
  s10_25, s10_26, s10_27, s10_28, s10_29
);

} // namespace fatal {
