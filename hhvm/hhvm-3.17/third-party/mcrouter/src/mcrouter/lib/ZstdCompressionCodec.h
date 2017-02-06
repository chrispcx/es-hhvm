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

#include <folly/Format.h>

#if FOLLY_HAVE_LIBZSTD
#include <zstd.h>

#include "mcrouter/lib/Compression.h"
#include "mcrouter/lib/IovecCursor.h"
#include "mcrouter/lib/IOBufUtil.h"

namespace facebook {
namespace memcache {

class ZstdCompressionCodec : public CompressionCodec {
 public:
  ZstdCompressionCodec(
      std::unique_ptr<folly::IOBuf> dictionary,
      uint32_t id,
      FilteringOptions codecFilteringOptions,
      uint32_t codecCompressionLevel);

  std::unique_ptr<folly::IOBuf> compress(const struct iovec* iov, size_t iovcnt)
      override final;
  std::unique_ptr<folly::IOBuf> uncompress(
      const struct iovec* iov,
      size_t iovcnt,
      size_t uncompressedLength = 0) override final;

 private:
  template <class T>
  using UPtr = std::unique_ptr<T, size_t(*)(T*)>;

  const std::unique_ptr<folly::IOBuf> dictionary_;
  int compressionLevel_{1};

  UPtr<ZSTD_CCtx> zstdCContext_;
  UPtr<ZSTD_DCtx> zstdDContext_;
  UPtr<ZSTD_CDict> zstdCDict_;
  UPtr<ZSTD_DDict> zstdDDict_;
};

} // memcache
} // facebook
#endif // FOLLY_HAVE_LIBZSTD
