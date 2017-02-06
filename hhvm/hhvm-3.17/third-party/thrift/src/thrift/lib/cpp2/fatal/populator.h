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

#pragma once

#include <thrift/lib/cpp2/fatal/container_traits.h>
#include <thrift/lib/cpp2/fatal/reflection.h>
#include <thrift/lib/cpp2/fatal/serializer.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <random>
#include <type_traits>
#include <vector>

#include <folly/io/Cursor.h>

#include <fatal/type/array.h>
#include <fatal/type/convert.h>

namespace apache { namespace thrift { namespace populator {

struct populator_opts {
  template <typename Int = std::size_t>
  struct range {
    Int min;
    Int max;
    range(Int min, Int max) : min(min), max(max) {
      assert(min <= max);
    }
  };

  range<> list_len = range<>(0, 0xFF);
  range<> set_len  = range<>(0, 0xFF);
  range<> map_len  = range<>(0, 0xFF);
  range<> bin_len  = range<>(0, 0xFF);
  range<> str_len  = range<>(0, 0xFF);
};

namespace detail {
// generate a value of type Int within [range.min, range.max]
template <typename Int, typename Rng>
Int rand_in_range(
  Rng& rng,
  populator_opts::range<Int> const& range)
{
  // uniform_int_distribution undefined for char,
  // use the next larger type if it's small
  using int_type = typename std::conditional<
    (sizeof(Int) > 1),
      Int,
      typename std::conditional<
        std::numeric_limits<Int>::is_signed,
          signed short,
          unsigned short
      >::type
    >::type;

  std::uniform_int_distribution<int_type> gen(range.min, range.max);
  int_type tmp = gen(rng);
  return static_cast<Int>(tmp);
}

// bring in an identifier from detail namespace in serializer.h
using ::apache::thrift::detail::enable_if_smart_pointer;
using ::apache::thrift::detail::disable_if_smart_pointer;
using ::apache::thrift::detail::deref;
using ::apache::thrift::detail::extract_descriptor_fid;
using ::apache::thrift::detail::is_required_field;

template <field_id_t... Ids>
using field_id_sequence = fatal::constant_sequence<field_id_t, Ids...>;

} // namespace detail

template <typename TypeClass, typename Type, typename Enable = void>
struct populator_methods;

template <typename Int>
struct populator_methods<type_class::integral, Int> {
  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Int& out) {
    using limits = std::numeric_limits<Int>;
    out = detail::rand_in_range(
      rng, populator_opts::range<Int>(limits::min(), limits::max())
    );
    DVLOG(4) << "generated int: " << out;
  }
};

template <typename Fp>
struct populator_methods<type_class::floating_point, Fp> {
  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Fp& out) {
    std::uniform_real_distribution<Fp> gen;
    out = gen(rng);
    DVLOG(4) << "generated real: " << out;
  }
};

template <>
struct populator_methods<type_class::string, std::string> {
  template <typename Rng>
  static void populate(
    Rng& rng,
    populator_opts const& opts,
    std::string& str)
  {
    using larger_char = typename std::conditional<
      std::numeric_limits<char>::is_signed,
      int, unsigned>::type;

    // all printable chars (see `man ascii`)
    std::uniform_int_distribution<larger_char> char_gen(0x20, 0x7E);

    const std::size_t length = detail::rand_in_range(rng, opts.str_len);

    str = std::string(length, 0);
    std::generate_n(str.begin(), length, [&]() {
      return static_cast<char>(char_gen(rng));
    });

    DVLOG(4) << "generated string of len" << length;
  }
};

template <typename Rng, typename Binary, typename WriteFunc>
void generate_bytes(
  Rng& rng,
  Binary& bin,
  const std::size_t length,
  WriteFunc const& write_func)
{
  std::uniform_int_distribution<unsigned> byte_gen(0, 0xFF);
  for(std::size_t i = 0; i < length; i++) {
    write_func(static_cast<uint8_t>(byte_gen(rng)));
  }
  DVLOG(4) << "generated binary of length " << length;
}

template <>
struct populator_methods<type_class::binary, std::string> {
  template <typename Rng>
  static void populate(
    Rng& rng,
    populator_opts const& opts,
    std::string& bin)
  {
    auto const length = detail::rand_in_range(rng, opts.bin_len);
    bin = std::string(length, 0);
    auto iter = bin.begin();
    generate_bytes(rng, bin, length, [&](uint8_t c) {
      *iter++ = c;
    });
  }
};

template <>
  struct populator_methods<type_class::binary, folly::IOBuf> {
  template <typename Rng>
  static void populate(
    Rng& rng,
    populator_opts const& opts,
    folly::IOBuf& bin)
  {
    auto const length = detail::rand_in_range(rng, opts.bin_len);
    bin = folly::IOBuf(folly::IOBuf::CREATE, length);
    bin.append(length);
    folly::io::RWUnshareCursor range(&bin);
    generate_bytes(rng, range, length, [&](uint8_t c) {
      range.write<uint8_t>(c);
    });
  }
};

template <>
struct populator_methods<type_class::binary, std::unique_ptr<folly::IOBuf>> {
  template <typename Rng>
  static void populate(
    Rng& rng,
    populator_opts const& opts,
    std::unique_ptr<folly::IOBuf>& bin)
  {
    bin = std::make_unique<folly::IOBuf>();
    return populator_methods<type_class::binary, folly::IOBuf>
      ::populate(rng, opts, *bin);
  }
};

// handle dereferencing smart pointers
template <
  typename TypeClass,
  typename PtrType
>
struct populator_methods <
  TypeClass,
  PtrType,
  detail::enable_if_smart_pointer<PtrType>
>
{
  using element_type = typename PtrType::element_type;
  using type_methods = populator_methods<TypeClass, element_type>;

  template <typename Rng>
  static void populate(
    Rng& rng,
    populator_opts const& opts,
    PtrType& out)
  {
    return type_methods::populate(rng, opts, *out);
  }
};

// Enumerations
template<typename Type>
struct populator_methods<type_class::enumeration, Type> {

  using int_type = typename std::underlying_type<Type>::type;
  using int_methods = populator_methods<type_class::integral, int_type>;

  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Type& out) {
    int_type tmp;
    int_methods::populate(rng, opts, tmp);
    out = static_cast<Type>(tmp);
  }
};

// Lists
template<typename ElemClass, typename Type>
struct populator_methods<type_class::list<ElemClass>, Type> {
  using elem_type   = typename Type::value_type;
  using elem_tclass = ElemClass;
  static_assert(!std::is_same<elem_tclass, type_class::unknown>(),
    "Unable to serialize unknown list element");

  using elem_methods = populator_methods<elem_tclass, elem_type>;

  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Type& out) {
    std::uint32_t list_size = detail::rand_in_range(rng, opts.list_len);
    out = Type();

    DVLOG(3) << "populating list size " << list_size;

    out.resize(list_size);
    for(decltype(list_size) i = 0; i < list_size; i++) {
      elem_methods::populate(rng, opts, out[i]);
    }
  }
};

// Sets
template <typename ElemClass, typename Type>
struct populator_methods<type_class::set<ElemClass>, Type> {

  // TODO: fair amount of shared code bewteen this and specialization for
  // type_class::list
  using elem_type   = typename Type::value_type;
  using elem_tclass = ElemClass;
  static_assert(!std::is_same<elem_tclass, type_class::unknown>(),
    "Unable to serialize unknown type");
  using elem_methods = populator_methods<elem_tclass, elem_type>;

  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Type& out) {
    std::uint32_t set_size = detail::rand_in_range(rng, opts.set_len);

    DVLOG(3) << "populating set size " << set_size;
    out = Type();

    for(decltype(set_size) i = 0; i < set_size; i++) {
      elem_type tmp;
      elem_methods::populate(rng, opts, tmp);
      out.insert(std::move(tmp));
    }
  }
};

// Maps
template <typename KeyClass, typename MappedClass, typename Type>
struct populator_methods<type_class::map<KeyClass, MappedClass>, Type> {

  using key_type    = typename Type::key_type;
  using key_tclass  = KeyClass;

  using mapped_type   = typename Type::mapped_type;
  using mapped_tclass = MappedClass;

  static_assert(!std::is_same<key_tclass, type_class::unknown>(),
    "Unable to serialize unknown key type in map");
  static_assert(!std::is_same<mapped_tclass, type_class::unknown>(),
    "Unable to serialize unknown mapped type in map");

    using key_methods    = populator_methods<key_tclass, key_type>;
    using mapped_methods = populator_methods<mapped_tclass, mapped_type>;

  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Type& out) {
    std::uint32_t map_size = detail::rand_in_range(rng, opts.map_len);

    DVLOG(3) << "populating map size " << map_size;
    out = Type();

    for(decltype(map_size) i = 0; i < map_size; i++) {
      key_type key_tmp;
      key_methods::populate(rng, opts, key_tmp);
      mapped_methods::populate(rng, opts, out[std::move(key_tmp)]);
    }
  }
};

// specialization for variants (Thrift unions)
template <typename Union>
struct populator_methods<type_class::variant, Union> {
  using traits = fatal::variant_traits<Union>;

private:
  struct write_member_by_fid {
    template <typename Fid, std::size_t Index, typename Rng>
    void operator ()(
      fatal::indexed<Fid, Index>,
      Rng& rng,
      populator_opts const& opts,
      Union& obj
    ) {
      using descriptor = fatal::get<
        typename traits::descriptors, Fid, detail::extract_descriptor_fid>;
      using methods = populator_methods<
        typename descriptor::metadata::type_class,
        typename descriptor::type>;

      assert(Fid::value == descriptor::metadata::id::value);

      DVLOG(3) << "writing union field "
        << fatal::z_data<typename descriptor::metadata::name>()
        << ", fid: " << descriptor::metadata::id::value;

      typename descriptor::type tmp;
      typename descriptor::setter setter;

      methods::populate(
        rng,
        opts,
        tmp
      );
      setter(obj, std::move(tmp));
    }
  };

public:
  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Union& out) {
    DVLOG(3) << "begin writing union: "
      << fatal::z_data<typename traits::name>()
      << ", type: " << out.getType();

    // array of all possible FIDs of this union
    using fids_seq = fatal::sort<
      fatal::as_sequence<
        fatal::transform<
          typename traits::descriptors,
          detail::extract_descriptor_fid
        >,
        fatal::sequence,
        field_id_t
      >
    >;

    // std::array of field_id_t
    auto const range = populator_opts::range<std::size_t>(
      0,
      fatal::size<fids_seq>::value - !fatal::empty<fids_seq>::value
    );
    auto const selected = detail::rand_in_range(rng, range);

    fatal::sorted_search<fids_seq>(
      fatal::as_array<fids_seq>::data[selected],
      write_member_by_fid(),
      rng,
      opts,
      out
    );
    DVLOG(3) << "end writing union";
  }
};

// specialization for structs
template <typename Struct>
struct populator_methods<type_class::structure, Struct> {
private:
  using traits = apache::thrift::reflect_struct<Struct>;

  using all_fields = fatal::partition<
    typename traits::members,
    detail::is_required_field
  >;
  using required_fields = fatal::first<all_fields>;
  using optional_fields = fatal::second<all_fields>;

  using isset_array = std::array<bool, fatal::size<required_fields>::value>;

  template <
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality optional,
    typename Enable = void
  >
  struct field_populator;

  // generic field writer
  template <
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality opt
  >
  struct field_populator
  <
    Member, TypeClass, MemberType, Methods, opt,
    detail::disable_if_smart_pointer<MemberType>
  > {
    template <typename Rng>
    static void populate(
      Rng& rng,
      populator_opts const& opts,
      MemberType& out)
    {
      Methods::populate(rng, opts, out);
    }
  };

  // writer for default/required ref structs
  template <
    typename Member,
    typename PtrType,
    typename Methods,
    optionality opt
  >
  struct field_populator <
    Member, type_class::structure, PtrType, Methods, opt,
    detail::enable_if_smart_pointer<PtrType>
  > {
    using struct_type  = typename PtrType::element_type;

    template <typename Rng>
    static void populate(
      Rng& rng,
      populator_opts const& opts,
      PtrType& out)
    {
      // `in` is a pointer to a struct.
      // if not present, and this isn't an optional field,
      // populate out an empty struct
      // TODO: always populate this field
      field_populator<
        Member,
        type_class::structure,
        struct_type,
        Methods,
        opt
      >::populate(rng, opts, detail::deref<PtrType>::clear_and_get(out));
    }
  };

  // writer for optional ref structs
  // 50/50 chance that they will be populated
  template <
    typename Member,
    typename PtrType,
    typename Methods
  >
  struct field_populator <
    Member, type_class::structure, PtrType, Methods, optionality::optional,
    detail::enable_if_smart_pointer<PtrType>
  > {
    template <typename Rng>
    static void populate(
      Rng& rng,
      populator_opts const& opts,
      PtrType& out)
    {
      auto const range = populator_opts::range<>(0, 1);
      if(detail::rand_in_range(rng, range)) {
        field_populator<
          Member,
          type_class::structure,
          PtrType,
          Methods,
          optionality::required
        >::populate(rng, opts, out);
      }
      else {
        out.reset();
      }
    }
  };

  struct member_populator {
    template <typename Member, std::size_t Index, typename Rng>
    void operator()(
      fatal::indexed<Member, Index>,
      Rng& rng,
      populator_opts const& opts,
      Struct& out)
    {
      using methods = populator_methods<
        typename Member::type_class,
        typename Member::type
      >;

      auto& got = Member::getter::ref(out);
      using member_type = typename std::decay<decltype(got)>::type;
      member_type tmp;

      DVLOG(3) << "populating member: "
        << fatal::z_data<typename Member::name>();

      field_populator<
        Member,
        typename Member::type_class,
        member_type,
        methods,
        Member::optional::value>::populate(rng, opts, got);
    }
  };

public:
  template <typename Rng>
  static void populate(Rng& rng, populator_opts const& opts, Struct& out) {
    fatal::foreach<typename traits::members>(
      member_populator(), rng, opts, out
    );
  }
};

/**
 * Entrypoints for using populator
 * Populates Thrift datatype with random data
 *
 * // C++
 * MyStruct a;
 * populator_opts opts;
 *
 * populate(a, opts);
 *
 * @author: Dylan Knutson <dymk@fb.com>
 */

template <typename Type, typename Rng>
void populate(Type& out, populator_opts const& opts, Rng& rng) {
  return populator_methods<reflect_type_class<Type>, Type>
    ::populate(rng, opts, out);
}

} } } // namespace apache::thrift::populator
