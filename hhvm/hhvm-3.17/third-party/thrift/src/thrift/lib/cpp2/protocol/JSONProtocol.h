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

#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/Cursor.h>
#include <thrift/lib/cpp2/protocol/Protocol.h>
#include <thrift/lib/cpp2/protocol/JSONProtocolCommon.h>

namespace apache { namespace thrift {

class JSONProtocolReader;

class JSONProtocolWriter : public JSONProtocolWriterCommon {

 public:
  static const int32_t VERSION_1 = 0x80010000;

  using ProtocolReader = JSONProtocolReader;

  using JSONProtocolWriterCommon::JSONProtocolWriterCommon;

  static constexpr ProtocolType protocolType() {
    return ProtocolType::T_JSON_PROTOCOL;
  }

  inline uint32_t writeStructBegin(const char* name);
  inline uint32_t writeStructEnd();
  inline uint32_t writeFieldBegin(const char* name,
                                  TType fieldType,
                                  int16_t fieldId);
  inline uint32_t writeFieldEnd();
  inline uint32_t writeFieldStop();
  inline uint32_t writeMapBegin(TType keyType,
                                TType valType,
                                uint32_t size);
  inline uint32_t writeMapEnd();
  inline uint32_t writeListBegin(TType elemType, uint32_t size);
  inline uint32_t writeListEnd();
  inline uint32_t writeSetBegin(TType elemType, uint32_t size);
  inline uint32_t writeSetEnd();
  inline uint32_t writeBool(bool value);

  /**
   * Functions that return the serialized size
   */

  inline uint32_t serializedMessageSize(const std::string& name) const;
  inline uint32_t serializedFieldSize(const char* name,
                                      TType fieldType,
                                      int16_t fieldId) const;
  inline uint32_t serializedStructSize(const char* name) const;
  inline uint32_t serializedSizeMapBegin(TType keyType,
                                         TType valType,
                                         uint32_t size) const;
  inline uint32_t serializedSizeMapEnd() const;
  inline uint32_t serializedSizeListBegin(TType elemType,
                                            uint32_t size) const;
  inline uint32_t serializedSizeListEnd() const;
  inline uint32_t serializedSizeSetBegin(TType elemType,
                                           uint32_t size) const;
  inline uint32_t serializedSizeSetEnd() const;
  inline uint32_t serializedSizeStop() const;
  inline uint32_t serializedSizeBool(bool = false) const;
};

class JSONProtocolReader : public JSONProtocolReaderCommon {

 public:
  static const int32_t VERSION_MASK = 0xffff0000;
  static const int32_t VERSION_1 = 0x80010000;

  using ProtocolWriter = JSONProtocolWriter;

  using JSONProtocolReaderCommon::JSONProtocolReaderCommon;

  static constexpr ProtocolType protocolType() {
    return ProtocolType::T_JSON_PROTOCOL;
  }

  inline uint32_t readStructBegin(std::string& name);
  inline uint32_t readStructEnd();
  inline uint32_t readFieldBegin(std::string& name,
                                 TType& fieldType,
                                 int16_t& fieldId);
  inline uint32_t readFieldEnd();
  inline uint32_t readMapBegin(TType& keyType,
                               TType& valType,
                               uint32_t& size);
  inline uint32_t readMapEnd();
  inline uint32_t readListBegin(TType& elemType, uint32_t& size);
  inline uint32_t readListEnd();
  inline uint32_t readSetBegin(TType& elemType, uint32_t& size);
  inline uint32_t readSetEnd();
  inline uint32_t readBool(bool& value);
  inline uint32_t readBool(std::vector<bool>::reference value);
  inline bool peekMap();
  inline bool peekSet();
  inline bool peekList();
};

}} // apache::thrift

#include "JSONProtocol.tcc"
