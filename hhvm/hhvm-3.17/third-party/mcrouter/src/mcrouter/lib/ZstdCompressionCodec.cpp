/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ZstdCompressionCodec.h"

#if FOLLY_HAVE_LIBZSTD

namespace facebook {
namespace memcache {

ZstdCompressionCodec::ZstdCompressionCodec(
    std::unique_ptr<folly::IOBuf> dictionary,
    uint32_t id,
    FilteringOptions codecFilteringOptions,
    uint32_t codecCompressionLevel)
    : CompressionCodec(
          CompressionCodecType::ZSTD,
          id,
          codecFilteringOptions,
          codecCompressionLevel),
      dictionary_(std::move(dictionary)),
      compressionLevel_(codecCompressionLevel),
      zstdCContext_(ZSTD_createCCtx(), ZSTD_freeCCtx),
      zstdDContext_(ZSTD_createDCtx(), ZSTD_freeDCtx),
      zstdCDict_(
          ZSTD_createCDict(
              reinterpret_cast<const char*>(dictionary_->data()),
              dictionary_->length(),
              compressionLevel_),
          ZSTD_freeCDict),
      zstdDDict_(
          ZSTD_createDDict(
              reinterpret_cast<const char*>(dictionary_->data()),
              dictionary_->length()),
          ZSTD_freeDDict) {
  if (zstdCDict_ == nullptr || zstdDDict_ == nullptr) {
    throw std::runtime_error("ZSTD codec: Failed to load dictionary.");
  }

  if (zstdCContext_ == nullptr || zstdDContext_ == nullptr) {
    throw std::runtime_error("ZSTD codec: Failed to create context.");
  }
}

std::unique_ptr<folly::IOBuf> ZstdCompressionCodec::compress(
    const struct iovec* iov,
    size_t iovcnt) {
  assert(iov);
  folly::IOBuf data =
      coalesceIovecs(iov, iovcnt, IovecCursor::computeTotalLength(iov, iovcnt));
  auto bytes = data.coalesce();
  size_t compressBound = ZSTD_compressBound(bytes.size());
  auto buffer = folly::IOBuf::create(compressBound);

  int compressedSize = ZSTD_compress_usingCDict(
      zstdCContext_.get(),
      reinterpret_cast<char*>(buffer->writableTail()),
      compressBound,
      reinterpret_cast<const char*>(bytes.data()),
      bytes.size(),
      zstdCDict_.get());

  if (ZSTD_isError(compressedSize)) {
    throw std::runtime_error(folly::sformat(
        "ZSTD codec: Failed to compress. Error: {}",
        ZSTD_getErrorName(compressedSize)));
  }

  buffer->append(compressedSize);
  return buffer;
}

std::unique_ptr<folly::IOBuf> ZstdCompressionCodec::uncompress(
    const struct iovec* iov,
    size_t iovcnt,
    size_t uncompressedLength) {
  folly::IOBuf data =
      coalesceIovecs(iov, iovcnt, IovecCursor::computeTotalLength(iov, iovcnt));
  auto bytes = data.coalesce();
  auto buffer = folly::IOBuf::create(uncompressedLength);

  int bytesWritten = ZSTD_decompress_usingDDict(
      zstdDContext_.get(),
      reinterpret_cast<char*>(buffer->writableTail()),
      buffer->capacity(),
      reinterpret_cast<const char*>(bytes.data()),
      bytes.size(),
      zstdDDict_.get());

  assert(ZSTD_isError(bytesWritten) || bytesWritten == uncompressedLength);
  if (ZSTD_isError(bytesWritten)) {
    throw std::runtime_error(folly::sformat(
        "ZSTD codec: decompression returned invalid value. Error: {} ",
        ZSTD_getErrorName(bytesWritten)));
  }

  buffer->append(bytesWritten);
  return buffer;
}

} // memcache
} // facebook
#endif // FOLLY_HAVE_LIBZSTD
