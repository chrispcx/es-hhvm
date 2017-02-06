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

#include <thrift/lib/cpp2/protocol/JSONProtocolCommon.h>

#include <type_traits>

namespace {

class WrappedIOBufQueueAppender {
 public:
  explicit WrappedIOBufQueueAppender(folly::io::QueueAppender& out)
      : out_(out) {}

  void append(const char* s, const size_t n) {
    if (n == 0)
      return;
    out_.push(reinterpret_cast<const uint8_t*>(CHECK_NOTNULL(s)), n);
    length_ += n;
  }

  void push_back(const char c) {
    append(&c, 1);
  }

  WrappedIOBufQueueAppender& operator+=(const char c) {
    push_back(c);
    return *this;
  }

  size_t size() const {
    return length_;
  }

 private:
  folly::io::QueueAppender& out_;
  size_t length_ = 0;
};

}  // namespace

namespace folly {

template <>
struct IsSomeString<WrappedIOBufQueueAppender> : std::true_type {};

}  // namespace folly

namespace apache { namespace thrift {

// This table describes the handling for the first 0x30 characters
//  0 : escape using "\u00xx" notation
//  1 : just output index
// <other> : escape using "\<other>" notation
const uint8_t JSONProtocolWriterCommon::kJSONCharTable[0x30] = {
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    0,  0,  0,  0,  0,  0,  0,  0,'b','t','n',  0,'f','r',  0,  0, // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 1
    1,  1,'"',  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, // 2
};

constexpr folly::StringPiece JSONProtocolReaderCommon::kEscapeChars;

// The elements of this array must match up with the sequence of characters in
// kEscapeChars
const uint8_t JSONProtocolReaderCommon::kEscapeCharVals[8] = {
  '"', '\\', '/', '\b', '\f', '\n', '\r', '\t',
};

uint32_t JSONProtocolWriterCommon::writeJSONDoubleInternal(double dbl) {
  WrappedIOBufQueueAppender appender(out_);
  folly::toAppend(dbl, &appender);
  return appender.size();
}

uint32_t JSONProtocolWriterCommon::writeJSONIntInternal(int64_t num) {
  WrappedIOBufQueueAppender appender(out_);
  if (!context.empty() && context.back().type == ContextType::MAP &&
      context.back().meta % 2 == 1) {
    folly::toAppend('"', num, '"', &appender);
  } else {
    folly::toAppend(num, &appender);
  }
  return appender.size();
}

}}
