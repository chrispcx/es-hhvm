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

#include <functional>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/network/ClientMcParser.h"
#include "mcrouter/lib/network/McParser.h"
#include "mcrouter/lib/network/ServerMcParser.h"
#include "mcrouter/lib/network/CarbonMessageDispatcher.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"
#include "mcrouter/tools/mcpiper/Config.h"

namespace folly {
class IOBuf;
} // folly

namespace facebook { namespace memcache {

constexpr size_t kReadBufferSizeMin = 256;
constexpr size_t kReadBufferSizeMax = 4096;

namespace detail {

template <class ReplyParser>
class ExpectNextDispatcher {
 public:
  explicit ExpectNextDispatcher(ReplyParser* parser)
    : replyParser_(parser) {}

  void dispatch(size_t typeId) {
    dispatcher_.dispatch(typeId, *this);
  }

  template <class M>
  static void processMsg(ExpectNextDispatcher& me) {
    assert(me.replyParser_);
    me.replyParser_->template expectNext<M>();
  }

  void setReplyParser(ReplyParser* parser) {
    replyParser_ = parser;
  }

 private:
  ReplyParser* replyParser_;
  CallDispatcher<McRequestList, ExpectNextDispatcher> dispatcher_;
};

} // detail

template <class Callback>
class ClientServerMcParser {
 public:
  class ReplyCallback {
   public:
    explicit ReplyCallback(Callback& callback)
      : callback_(callback) {}

    template <class Reply>
    void replyReady(
        Reply&& reply,
        uint64_t msgId,
        ReplyStatsContext replyStatsContext) {
      callback_.template replyReady<Reply>(
          msgId,
          std::move(reply),
          replyStatsContext);
    }

    bool nextReplyAvailable(uint64_t) {
      return true;
    }

    void parseError(mc_res_t, folly::StringPiece) {}

   private:
    Callback& callback_;
  };

  struct RequestCallback : public CarbonMessageDispatcher<
                               McRequestList,
                               RequestCallback,
                               const UmbrellaMessageInfo&> {
   public:
    template <class M>
    void onTypedMessage(M&& req,
                        const UmbrellaMessageInfo& headerInfo) {
      callback_.requestReady(headerInfo.reqId, std::move(req));
    }

    explicit RequestCallback(Callback& callback)
      : callback_(callback) {}

    template <class Request>
    void onRequest(Request&& req, bool noreply) {
      callback_.requestReady(0, std::move(req));
    }

    template <class Request>
    void umbrellaRequestReady(Request&& req, uint64_t msgId) {
      callback_.requestReady(msgId, std::move(req));
    }

    void caretRequestReady(const UmbrellaMessageInfo& headerInfo,
                           const folly::IOBuf& buffer) {
      this->dispatchTypedRequest(headerInfo, buffer, headerInfo);
    }

    void multiOpEnd() {}
    void parseError(mc_res_t, folly::StringPiece) {}

   private:
    Callback& callback_;
  };


  /**
   * Creates the client/server parser.
   *
   * @param callback  Callback function that will be called when a
   *                  request/reply is successfully parsed.
   */
  explicit ClientServerMcParser(Callback& callback)
    : replyCallback_(callback),
      requestCallback_(callback),
      replyParser_(folly::make_unique<ClientMcParser<ReplyCallback>>(
        replyCallback_, kReadBufferSizeMin, kReadBufferSizeMax)),
      requestParser_(folly::make_unique<ServerMcParser<RequestCallback>>(
        requestCallback_, kReadBufferSizeMin, kReadBufferSizeMax)),
      expectNextDispatcher_(replyParser_.get()) {}

  /**
   * Feed data into the parser. The callback will be called as soon
   * as a message is completely parsed.
   */
  void parse(folly::ByteRange data, uint32_t typeId, bool isFirstPacket);

  void reset() {
    replyParser_ = folly::make_unique<ClientMcParser<ReplyCallback>>(
        replyCallback_,
        kReadBufferSizeMin,
        kReadBufferSizeMax,
        false /* useJemallocNodumpAllocator */,
        getCompressionCodecMap());
    expectNextDispatcher_.setReplyParser(replyParser_.get());

    requestParser_ = folly::make_unique<ServerMcParser<RequestCallback>>(
        requestCallback_, kReadBufferSizeMin, kReadBufferSizeMax);
  }

  mc_protocol_t getProtocol() const {
    return protocol_;
  }

 private:
  ReplyCallback replyCallback_;
  RequestCallback requestCallback_;
  mc_protocol_t protocol_{mc_unknown_protocol};

  std::unique_ptr<ClientMcParser<ReplyCallback>> replyParser_;
  std::unique_ptr<ServerMcParser<RequestCallback>> requestParser_;

  detail::ExpectNextDispatcher<ClientMcParser<ReplyCallback>>
      expectNextDispatcher_;
};

}} // facebook::memcache

#include "ClientServerMcParser-inl.h"
