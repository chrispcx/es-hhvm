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

#include <chrono>
#include <unordered_map>

#include <folly/IntrusiveList.h>
#include <folly/SocketAddress.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"

namespace facebook { namespace memcache {

/**
 * Wrapper around ClientServerMcParser that also tracks of information
 * useful for sniffer (e.g. socket addresses, keys for replies).
 *
 * @param Callback  Callback containing two functions:
 *                  void requestReady(msgId, request, from, to, protocol);
 *                  void replyReady(msgId, reply, key, from, to, protocol);
 */
template <class Callback>
class SnifferParser {
 public:
  explicit SnifferParser(Callback& cb) noexcept;

  ClientServerMcParser<SnifferParser>& parser() {
    return parser_;
  }

  void setAddresses(folly::SocketAddress fromAddress,
                    folly::SocketAddress toAddress) {
    fromAddress_ = std::move(fromAddress);
    toAddress_ = std::move(toAddress);
  }

  // See the comments on currentMsgStartTimeUs_ for information about when
  // this gets set.
  void setCurrentMsgStartTime(uint64_t msgStartTimeUs) {
    currentMsgStartTimeUs_ = msgStartTimeUs;
  }

 private:
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  // Holds the id of the request and the key of the matching message.
  struct Item {
    Item(uint64_t id, std::string k, uint64_t msgStartTimeUs, TimePoint now)
        : reqId(id),
          key(std::move(k)),
          msgStartTimeUs(msgStartTimeUs),
          created(now) { }

    uint64_t reqId;
    std::string key;
    // time when the item was sent through mcrouter
    uint64_t msgStartTimeUs;
    // time when the item was created in mcpiper
    TimePoint created;

    folly::IntrusiveListHook listHook;
  };

  // Callback called when a message is ready
  Callback& callback_;
  // The parser itself.
  ClientServerMcParser<SnifferParser> parser_;
  // Addresses of current message.
  folly::SocketAddress fromAddress_;
  folly::SocketAddress toAddress_;
  // Map (msgId -> key) of messages that haven't been paired yet.
  std::unordered_map<uint64_t, Item> msgs_;
  // Keeps an in-order list of what should be invalidated.
  folly::IntrusiveList<Item, &Item::listHook> evictionQueue_;
  // Start time of the currently parsed message.
  // Between parsing the header and the message body, it temporarily holds the
  // sender-side start time of the message that we deserialized from the header.
  // It is necessary to store here because we want to save it with the item,
  // despite the Item being parsed separately from the header.
  uint64_t currentMsgStartTimeUs_;

  void evictOldItems(TimePoint now);

  // ClientServerMcParser callbacks
  template <class Request>
  void requestReady(uint64_t msgId, Request&& request);
  template <class Reply>
  void replyReady(
      uint64_t msgId,
      Reply&& reply,
      ReplyStatsContext replyStatsContext);

  friend class ClientServerMcParser<SnifferParser>;
};

}} // facebook::memcache

#include "SnifferParser-inl.h"
