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
#ifndef THRIFT_FATAL_FLATTEN_GETTERS_INL_PRE_H_
#define THRIFT_FATAL_FLATTEN_GETTERS_INL_PRE_H_ 1

#include <fatal/type/logical.h>

namespace apache { namespace thrift {
namespace detail { namespace flatten_getters_impl {

// default filter
struct f {
  template <typename T>
  using apply = fatal::negate<
    std::is_same<type_class::structure, typename T::type_class>
  >;
};

template <typename, typename, typename> struct s;

}} // detail::flatten_getters_impl
}} // apache::thrift

#endif // THRIFT_FATAL_FLATTEN_GETTERS_INL_PRE_H_
