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
#ifndef THRIFT_FATAL_REFLECTION_INL_PRE_H_
#define THRIFT_FATAL_REFLECTION_INL_PRE_H_ 1

#if !defined THRIFT_FATAL_REFLECTION_H_
# error "This file must be included from reflection.h"
#endif

namespace apache { namespace thrift { namespace detail {

template <typename, typename, bool IsTry, typename Default = void>
struct reflect_module_tag_selector {
  using type = Default;
  static_assert(
    IsTry,
    "given type has no reflection metadata or is not a struct, enum or union"
  );
};

template <typename> struct reflect_module_tag_get;
template <typename, typename> struct reflect_module_tag_try_get;
template <typename> struct reflect_type_class_impl;

struct reflection_metadata_tag {};
struct struct_traits_metadata_tag {};

namespace reflection_impl {

template <typename, typename, bool> struct is_set;
template <typename, typename, bool> struct mark_set;
template <typename, typename, bool> struct unmark_set;

struct variant_member_name {
  template <typename Descriptor>
  using apply = typename Descriptor::metadata::name;
};

struct variant_member_field_id {
  template <typename Descriptor>
  using apply = typename Descriptor::metadata::id;
};

}} // detail::reflection_impl

#define THRIFT_REGISTER_REFLECTION_METADATA(Tag, ...) \
  FATAL_REGISTER_TYPE( \
    ::apache::thrift::detail::reflection_metadata_tag, \
    Tag, \
    ::apache::thrift::reflected_module< \
      __VA_ARGS__ \
    > \
  )

#define THRIFT_REGISTER_STRUCT_TRAITS(Struct, ...) \
  FATAL_REGISTER_TYPE( \
    ::apache::thrift::detail::struct_traits_metadata_tag, \
    Struct, \
    ::apache::thrift::reflected_struct< \
      Struct, \
      __VA_ARGS__ \
    > \
  )

template <typename = void> struct reflected_annotations;

}} // apache::thrift

#endif // THRIFT_FATAL_REFLECTION_INL_PRE_H_
