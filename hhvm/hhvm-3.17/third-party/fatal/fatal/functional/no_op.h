/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#ifndef FATAL_INCLUDE_fatal_functional_no_op_h
#define FATAL_INCLUDE_fatal_functional_no_op_h

namespace fatal {
namespace fn {

/**
 * A callable that accepts any parameters and does nothing.
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */
struct no_op {
  template <typename... UArgs>
  void operator ()(UArgs &&...) const noexcept {}
};

} // namespace fn {
} // namespace fatal {

#endif // FATAL_INCLUDE_fatal_functional_no_op_h
