/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <limits>
#include <stdint.h>
#include <string>
#include <type_traits>
#include <utility>

#include <folly/io/IOBuf.h>
#include <folly/small_vector.h>

#include "mcrouter/lib/carbon/CarbonProtocolCommon.h"
#include "mcrouter/lib/carbon/CarbonQueueAppender.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/carbon/SerializationTraits.h"
#include "mcrouter/lib/carbon/Util.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace carbon {

class CarbonProtocolWriter {
 public:
  explicit CarbonProtocolWriter(CarbonQueueAppenderStorage& storage)
      : appender_(&storage, 0 /* unused */) {}

  // The writeField() member functions serialize the field header (field type
  // and field ID information) followed by the serialized value.
  // For scalar types, no serialization is done when the field value is zero.
  // For binary types, no serialization is done when the binary data has zero
  // length.

  void writeField(const int16_t id, const bool b) {
    if (!b) {
      return;
    }
    writeFieldHeader(b ? FieldType::True : FieldType::False, id);
  }

  void writeField(const int16_t id, const char c) {
    if (!c) {
      return;
    }
    writeFieldHeader(FieldType::Int8, id);
    writeRaw(c);
  }

  void writeField(const int16_t id, const int8_t i) {
    if (!i) {
      return;
    }
    writeFieldHeader(FieldType::Int8, id);
    writeRaw(i);
  }

  void writeField(const int16_t id, const int16_t i) {
    if (!i) {
      return;
    }
    writeFieldHeader(FieldType::Int16, id);
    writeRaw(i);
  }

  void writeField(const int16_t id, const int32_t i) {
    if (!i) {
      return;
    }
    writeFieldHeader(FieldType::Int32, id);
    writeRaw(i);
  }

  void writeField(const int16_t id, const int64_t i) {
    if (!i) {
      return;
    }
    writeFieldHeader(FieldType::Int64, id);
    writeRaw(i);
  }

  void writeField(const int16_t id, const uint8_t ui) {
    if (!ui) {
      return;
    }
    writeFieldHeader(FieldType::Int8, id);
    writeRaw(ui);
  }

  void writeField(const int16_t id, const uint16_t ui) {
    if (!ui) {
      return;
    }
    writeFieldHeader(FieldType::Int16, id);
    writeRaw(ui);
  }

  void writeField(const int16_t id, const uint32_t ui) {
    if (!ui) {
      return;
    }
    writeFieldHeader(FieldType::Int32, id);
    writeRaw(ui);
  }

  void writeField(const int16_t id, const uint64_t ui) {
    if (!ui) {
      return;
    }
    writeFieldHeader(FieldType::Int64, id);
    writeRaw(ui);
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, void>::type writeField(
      const int16_t id,
      const T e) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    writeField(id, static_cast<UnderlyingType>(e));
  }

  void writeField(const int16_t id, const float f) {
    static_assert(
        sizeof(float) == sizeof(uint32_t),
        "Carbon doubles can only be used on platforms where sizeof(float)"
        " == sizeof(uint32_t)");
    static_assert(
        std::numeric_limits<float>::is_iec559,
        "Carbon floats may only be used on platforms using IEC 559 floats");

    if (f == 0.0) {
      return;
    }
    writeFieldHeader(FieldType::Float, id);
    writeRaw(f);
  }

  void writeField(const int16_t id, const double d) {
    static_assert(
        sizeof(double) == sizeof(uint64_t),
        "Carbon doubles can only be used on platforms where sizeof(double)"
        " == sizeof(uint64_t)");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "Carbon doubles may only be used on platforms using IEC 559 doubles");

    if (d == 0.0) {
      return;
    }
    writeFieldHeader(FieldType::Double, id);
    writeRaw(d);
  }

  void writeField(const int16_t id, const Result res) {
    static_assert(
        sizeof(Result) == sizeof(mc_res_t),
        "Carbon currently assumes sizeof(Result) == sizeof(mc_res_t)");
    // Note that this actually narrows mc_res_t from int to int16_t
    writeField(id, static_cast<int16_t>(res));
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, std::string, folly::IOBuf>::value, void>::type
      writeField(const int16_t id, const T& t) {
    if (t.empty()) {
      return;
    }
    writeFieldHeader(FieldType::Binary, id);
    writeRaw(t);
  }

  template <class T>
  void writeField(const int16_t id, const std::vector<T>& v) {
    facebook::memcache::checkRuntime(
        v.size() <= std::numeric_limits<uint32_t>::max(),
        "Input to writeField() for vector too long (len = {})",
        v.size());
    writeFieldHeader(FieldType::List, id);
    writeRaw(v);
  }

  // Serialize user-provided types that have suitable specializations of
  // carbon::SerializationTraits<>.
  template <class T>
  typename std::enable_if<detail::SerializationTraitsDefined<T>::value, void>::
      type
      writeField(const int16_t id, const T& data) {
    if (!SerializationTraits<T>::isEmpty(data)) {
      writeFieldHeader(FieldType::Binary, id);
      SerializationTraits<T>::write(data, *this);
    }
  }

  // Serialize Carbon-generated structure members and mixins
  template <class T>
  typename std::enable_if<IsCarbonStruct<T>::value, void>::type
  writeField(const int16_t id, const T& data) {
    writeFieldHeader(FieldType::Struct, id);
    writeRaw(data);
  }

  // Bookkeeping member functions
  void writeStructBegin() {
    nestedStructFieldIds_.push_back(lastFieldId_);
    lastFieldId_ = 0;
  }

  void writeStructEnd() {
    lastFieldId_ = nestedStructFieldIds_.back();
    nestedStructFieldIds_.pop_back();
  }

  void writeStop() {
    writeByte(FieldType::Stop);
  }

  void writeListSizeAndInnerType(
      const uint32_t size,
      const FieldType fieldType) {
    if (size < 0x0f) {
      writeByte((size << 4) | static_cast<uint8_t>(fieldType));
    } else {
      writeByte(0xf0 | static_cast<uint8_t>(fieldType));
      writeVarint(size);
    }
  }

  void writeFieldHeader(const FieldType type, const int16_t id) {
    const auto typeByte = static_cast<uint8_t>(type);
    if (id > lastFieldId_ && (id - lastFieldId_) <= 0xf) {
      const auto delta = static_cast<uint8_t>(id - lastFieldId_);
      writeByte((delta << 4) | typeByte);
    } else {
      writeByte(typeByte);
      writeTwoBytes(static_cast<uint16_t>(id));
    }
    lastFieldId_ = id;
  }

  void writeRaw(const std::string& s) {
    const size_t len = s.size();
    facebook::memcache::checkRuntime(
        len <= std::numeric_limits<uint32_t>::max(),
        "Input to writeRaw() too long (len = {})",
        len);
    writeVarint(static_cast<uint32_t>(len));
    appender_.push(reinterpret_cast<const uint8_t*>(s.data()), len);
  }

  void writeRaw(const folly::IOBuf& buf) {
    const auto len = buf.computeChainDataLength();
    facebook::memcache::checkRuntime(
        len <= std::numeric_limits<uint32_t>::max(),
        "Input to writeRaw() too long (len = {})",
        len);
    writeVarint(static_cast<uint32_t>(len));
    appender_.insert(buf);
  }

  void writeRaw(const bool b) {
    writeByte(b ? FieldType::True : FieldType::False);
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, char, int8_t, uint8_t>::value, void>::type
      writeRaw(const T value) {
    writeByte(value);
  }

  template <class T>
  typename std::enable_if<
      folly::
          IsOneOf<T, int16_t, int32_t, int64_t, uint16_t, uint32_t, uint64_t>::
              value,
      void>::type
  writeRaw(const T value) {
    writeZigzagVarint(value);
  }

  void writeRaw(const float f) {
    uint32_t bits;
    std::memcpy(std::addressof(bits), std::addressof(f), sizeof(bits));
    appender_.writeBE(bits);
  }

  void writeRaw(const double d) {
    uint64_t bits;
    std::memcpy(std::addressof(bits), std::addressof(d), sizeof(bits));
    appender_.writeBE(bits);
  }

  template <class T>
  void writeRaw(const std::vector<T>& v) {
    facebook::memcache::checkRuntime(
        v.size() <= std::numeric_limits<uint32_t>::max(),
        "Input to writeRaw() for vector too long (len = {})",
        v.size());
    writeListSizeAndInnerType(static_cast<uint32_t>(v.size()), FieldType::List);
    for (const auto& e : v) {
      writeRaw(e);
    }
  }

  template <class T>
  typename std::enable_if<IsCarbonStruct<T>::value, void>::type writeRaw(
      const T& data) {
    data.serialize(*this);
  }

  template <class T>
  typename std::enable_if<detail::SerializationTraitsDefined<T>::value, void>::
      type
      writeRaw(const T& data) {
    SerializationTraits<T>::write(data, *this);
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, void>::type writeRaw(
      const T e) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    writeRaw(static_cast<UnderlyingType>(e));
  }

 private:
  template <class T>
  void doWriteVarint(T val) {
    constexpr uint8_t kMaxIters = (sizeof(T) * 8 + 6) / 7;

    static_assert(
        std::is_unsigned<T>::value,
        "doWriteVarint should only be called with unsigned types");

    static_assert(
        sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "writeVarint() may only be used with 16-, 32-, or 64-bit integers");

    uint8_t iter = 0;
    // While loop should consume at most (kMaxIters - 1) iterations
    while (val >= 0x80 && ++iter < kMaxIters) {
      uint8_t byte = 0x80 | (static_cast<uint8_t>(val) & 0x7f);
      appender_.write(byte);
      val >>= 7;
    }
    facebook::memcache::checkRuntime(
        val < 0x80, "writeVarint() called on invalid varint");
    appender_.write(static_cast<uint8_t>(val));
  }

  template <class T>
  typename std::enable_if<std::numeric_limits<T>::is_integer>::type writeVarint(
      T val) {
    static_assert(
        sizeof(T) <= sizeof(uint64_t),
        "argument to writeVarint() can be no larger than uint64_t");

    using UnsignedT = typename std::make_unsigned<T>::type;
    doWriteVarint(static_cast<UnsignedT>(val));
  }

  template <class T>
  typename std::enable_if<std::numeric_limits<T>::is_integer>::type
  writeZigzagVarint(T val) {
    static_assert(
        sizeof(T) <= sizeof(uint64_t),
        "argument to writeZigzagVarint() can be no larger than uint64_t");

    using SignedT = typename std::make_signed<T>::type;

    const auto sval = static_cast<SignedT>(val);
    doWriteVarint(util::zigzag(sval));
  }

  void writeByte(const uint8_t byte) {
    appender_.write(byte);
  }

  void writeByte(const FieldType type) {
    writeByte(static_cast<uint8_t>(type));
  }

  void writeTwoBytes(const uint16_t bytes) {
    appender_.write(bytes);
  }

  CarbonQueueAppender appender_;
  folly::small_vector<int16_t, detail::kDefaultStackSize> nestedStructFieldIds_;
  int16_t lastFieldId_{0};
};

} // carbon
