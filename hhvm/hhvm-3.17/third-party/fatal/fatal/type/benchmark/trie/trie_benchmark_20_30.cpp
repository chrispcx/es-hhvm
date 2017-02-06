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

CREATE_BENCHMARK(n20_len30,
  s30_00, s30_01, s30_02, s30_03, s30_04,
  s30_05, s30_06, s30_07, s30_08, s30_09,
  s30_10, s30_11, s30_12, s30_13, s30_14,
  s30_15, s30_16, s30_17, s30_18, s30_19
);

} // namespace fatal {
