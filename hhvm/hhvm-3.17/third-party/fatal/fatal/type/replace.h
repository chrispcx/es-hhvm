/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#ifndef FATAL_INCLUDE_fatal_type_replace_h
#define FATAL_INCLUDE_fatal_type_replace_h

#include <fatal/type/impl/replace.h>

namespace fatal {

template <typename T>
using replace = impl_rp::in<T>;

} // namespace fatal {

#endif // FATAL_INCLUDE_fatal_type_replace_h
