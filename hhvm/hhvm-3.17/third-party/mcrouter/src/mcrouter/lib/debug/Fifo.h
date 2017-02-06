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

#include <sys/uio.h>

#include <atomic>
#include <string>

namespace facebook {
namespace memcache {

/**
 * Writes data to a named pipe (fifo) for debugging purposes.
 * Instances of this file have a one-to-one mapping with actual FIFOs on disk.
 *
 * Notes:
 *  - Unless specified otherwise, methods of this class are thread-safe.
 *  - Life of Fifo is managed by FifoManager.
 */
class Fifo {
 public:
  ~Fifo();

  // non copyable
  Fifo(const Fifo& other) = delete;
  Fifo& operator=(Fifo&) = delete;

  // non movable
  Fifo(Fifo&&) noexcept = delete;
  Fifo& operator=(Fifo&&) noexcept = delete;

  /**
   * Tries to connect to the fifo (if not already connected).
   * Note: This method is not thread-safe.
   *
   * @return  True if the pipe is already connected or connection
   *          was established successfully. False otherwise.
   */
  bool tryConnect() noexcept;

  /**
   * Tells whether this fifo is connectted.
   */
  bool isConnected() const noexcept;

  /**
   * Writes data to the FIFO.
   *
   * Note: Writes are best effort. If, for example, the pipe is full, this
   * method will fail (return false).
   *
   * @param iov         Data of the message, to write to the pipe.
   * @param iovcnt      Size of iov.
   *
   * @return            True if the data was written. False otherwise.
   */
  bool write(const struct iovec* iov, size_t iovcnt) noexcept;
  bool write(void* buf, size_t len) noexcept;

 private:
  explicit Fifo(std::string path);

  // Path of the fifo
  const std::string path_;
  // Fifo file descriptor.
  std::atomic<int> fd_{-1};

  /**
   * Disconnects the pipe.
   */
  void disconnect() noexcept;

  friend class FifoManager;
};

} // memcache
} // facebook
