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
#ifndef THRIFT_FATAL_PRETTY_PRINT_H_
#define THRIFT_FATAL_PRETTY_PRINT_H_ 1

#include <thrift/lib/cpp2/fatal/indenter.h>
#include <thrift/lib/cpp2/fatal/reflection.h>

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

/**
 * READ ME FIRST: this header provides pretty printers for Thrift types.
 *
 * Please refer to the top of `thrift/lib/cpp2/fatal/reflection.h` on how to
 * enable compile-time reflection for Thrift types. The present header relies on
 * it for its functionality.
 *
 * TROUBLESHOOTING:
 *  - make sure you've followed the instructions on `reflection.h` to enable
 *    generation of compile-time reflection;
 *  - make sure you've included the metadata for your Thrift types, as specified
 *    in `reflection.h`.
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */

#include <thrift/lib/cpp2/fatal/internal/pretty_print-inl-pre.h>

namespace apache { namespace thrift {

/**
 * Pretty-prints an object to the given output stream using Thrift's reflection
 * support.
 *
 * All Thrift types are required to be generated using the 'fatal' cpp2 flag,
 * otherwise compile-time reflection metadata won't be available.
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */
template <typename OutputStream, typename T>
void pretty_print(
  OutputStream &&out,
  T &&what,
  std::string indentation = "  ",
  std::string margin = std::string()
) {
  using impl = detail::pretty_print_impl<
    reflect_type_class<typename std::decay<T>::type>
  >;

  auto indenter = make_indenter(out, std::move(indentation), std::move(margin));
  impl::print(indenter, std::forward<T>(what));
}

/**
 * Pretty-prints an object to a string using Thrift's reflection support.
 *
 * All Thrift types are required to be generated using the 'fatal' cpp2 flag,
 * otherwise compile-time reflection metadata won't be available.
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */
template <typename... Args>
std::string pretty_string(Args &&...args) {
  std::ostringstream out;
  pretty_print(out, std::forward<Args>(args)...);
  return out.str();
}

}} // apache::thrift

#include <thrift/lib/cpp2/fatal/internal/pretty_print-inl-post.h>

#endif // THRIFT_FATAL_PRETTY_PRINT_H_
