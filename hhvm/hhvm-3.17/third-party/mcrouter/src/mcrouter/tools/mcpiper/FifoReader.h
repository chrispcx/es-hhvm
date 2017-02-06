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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/regex.hpp>

#include <folly/io/async/AsyncPipe.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/IOBufQueue.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>

#include "mcrouter/lib/debug/ConnectionFifoProtocol.h"

namespace folly {
class EventBase;
} // folly

namespace facebook { namespace memcache {

class FifoReader;

/**
 * Function called when a message is completely read from the fifo.
 *
 * @param connectionId  Id of the connection.
 * @param packetId      Id of the packet.
 * @param from          Address of the endpoint that sent the message.
 * @param to            Address of the endpoint that received the message.
 * @param data          The data of the message.
 */
using MessageReadyFn = std::function<void(uint64_t connectionId,
                                          uint64_t packetId,
                                          folly::SocketAddress from,
                                          folly::SocketAddress to,
                                          uint32_t typeId,
                                          uint64_t msgStartTime,
                                          folly::ByteRange data)>;

class FifoReadCallback : public folly::AsyncReader::ReadCallback {
 public:
  FifoReadCallback(std::string fifoName,
                   const MessageReadyFn& messageReady) noexcept;

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override final;
  void readDataAvailable(size_t len) noexcept override final;
  void readEOF() noexcept override final;
  void readErr(const folly::AsyncSocketException& ex) noexcept override final;

 private:
  static constexpr uint64_t kMinSize{256};
  folly::IOBufQueue readBuffer_{folly::IOBufQueue::cacheChainLength()};
  const std::string fifoName_;
  const MessageReadyFn& messageReady_;

  // Indicates if there is a pending message, i.e. a header has being read
  // (pendingHeader_) but its data hasn't being processed yet
  folly::Optional<PacketHeader> pendingHeader_;

  // Addresses of the endpoints of the message currently being read.
  folly::SocketAddress from_;
  folly::SocketAddress to_;
  uint32_t typeId_{0};
  uint64_t msgStartTime_{0};

  void forwardMessage(const PacketHeader& header,
                      std::unique_ptr<folly::IOBuf> buf);

  void handleMessageHeader(MessageHeader msgHeader) noexcept;
};

/**
 * Manages all fifo readers in a directory.
 */
class FifoReaderManager {
 public:
  /**
   * Builds FifoReaderManager and starts watching "dir" for fifos
   * that match "filenamePattern".
   * If a fifo with a name that matches "filenamePattern" is found, a
   * folly:AsyncPipeReader for it is created and scheduled in "evb".
   *
   * @param evb             EventBase to run FifoReaderManager and
   *                        its FifoReaders.
   * @param messageReadyCb  Callback to be called when a message is completely
   *                        read from the fifo.
   * @param dir             Directory to watch.
   * @param filenamePattern Regex that file names must match.
   */
  FifoReaderManager(folly::EventBase& evb,
                    MessageReadyFn messageReady,
                    std::string dir,
                    std::unique_ptr<boost::regex> filenamePattern);

  // non-copyable
  FifoReaderManager(const FifoReaderManager&) = delete;
  FifoReaderManager& operator=(const FifoReaderManager&) = delete;

 private:
  using FifoReader = std::pair<folly::AsyncPipeReader::UniquePtr,
                               std::unique_ptr<FifoReadCallback>>;

  static constexpr size_t kPollDirectoryIntervalMs = 1000;
  folly::EventBase& evb_;
  MessageReadyFn messageReady_;
  const std::string directory_;
  const std::unique_ptr<boost::regex> filenamePattern_;
  std::unordered_map<std::string, FifoReader> fifoReaders_;

  std::vector<std::string> getMatchedFiles() const;
  void runScanDirectory();
};

}} // facebook::memcache
