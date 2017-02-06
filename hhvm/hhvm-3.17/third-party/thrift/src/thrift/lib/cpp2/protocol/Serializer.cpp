/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace apache { namespace thrift {

std::unique_ptr<folly::IOBuf> serializeError(
  int protId, TApplicationException obj, folly::IOBuf* buf) {
  switch(protId) {
    case apache::thrift::protocol::T_BINARY_PROTOCOL:
    {
      return serializeErrorProtocol<BinaryProtocolReader,
        BinaryProtocolWriter>(obj, std::move(buf));
      break;
    }
    case apache::thrift::protocol::T_COMPACT_PROTOCOL:
    {
      return serializeErrorProtocol<CompactProtocolReader,
        CompactProtocolWriter>(obj, std::move(buf));
      break;
    }
    default:
    {
      LOG(ERROR) << "Invalid protocol from client";
    }
  }

  return nullptr;
}

std::unique_ptr<folly::IOBuf> serializeError(int protId,
                                             TApplicationException obj,
                                             const std::string& fname,
                                             int32_t protoSeqId) {
  switch(protId) {
    case apache::thrift::protocol::T_BINARY_PROTOCOL:
    {
      return serializeErrorProtocol<BinaryProtocolWriter>(obj, fname,
          protoSeqId);
      break;
    }
    case apache::thrift::protocol::T_COMPACT_PROTOCOL:
    {
      return serializeErrorProtocol<CompactProtocolWriter>(obj, fname,
          protoSeqId);
      break;
    }
    default:
    {
      LOG(ERROR) << "Invalid protocol from client";
    }
  }

  return nullptr;
}

}} // namespace apache::thrift
