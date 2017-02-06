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

#include <string>
#include <utility>

#include <folly/io/IOBuf.h>
#include <folly/Optional.h>

#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/McRequestList.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"
#include "mcrouter/lib/Compression.h"
#include "mcrouter/lib/CompressionCodecManager.h"

namespace facebook { namespace memcache {

class CompressionCodec;
template <class OnRequest, class RequestList>
class McServerOnRequestWrapper;
class McServerSession;
class MultiOpParent;

struct CompressionContext {
  const CompressionCodecMap* compressionCodecMap{nullptr};

  CodecIdRange codecIdRange = CodecIdRange::Empty;

  CompressionContext(
      const CompressionCodecMap* codecMap,
      CodecIdRange codecRange)
      : compressionCodecMap(codecMap),
        codecIdRange(codecRange) {}
};

/**
 * API for users of McServer to send back a reply for a request.
 *
 * Each onRequest callback is provided a context object,
 * which must eventually be surrendered back via a reply() call.
 */
class McServerRequestContext {
 public:
  using DestructorFunc = void (*)(void*);

  template <class Reply>
  static void reply(McServerRequestContext&& ctx, Reply&& reply);

  template <class Reply>
  static void reply(
      McServerRequestContext&& ctx,
      Reply&& reply,
      DestructorFunc destructor,
      void* toDestruct);

  ~McServerRequestContext();

  McServerRequestContext(McServerRequestContext&& other) noexcept;
  McServerRequestContext& operator=(McServerRequestContext&& other);

  /**
   * Get the associated McServerSession
   */
  McServerSession& session();

  double getDropProbability() const;

 private:
  McServerSession* session_;

  /* Pack these together, operation + flags takes one word */
  bool isEndContext_{false};  // Used to mark end of ASCII multi-get request
  bool noReply_;
  bool replied_{false};

  uint64_t reqid_;
  struct AsciiState {
    std::shared_ptr<MultiOpParent> parent_;
    folly::Optional<folly::IOBuf> key_;
  };
  std::unique_ptr<AsciiState> asciiState_;

  std::unique_ptr<CompressionContext> compressionContext_;

  template <class Reply>
  bool noReply(const Reply& r) const;
  bool noReply(const McLeaseGetReply& r) const;

  template <class Reply, class... Args>
  static typename std::enable_if<
      GetLike<RequestFromReplyType<Reply, RequestReplyPairs>>::value>::type
  replyImpl(McServerRequestContext&& ctx, Reply&& reply, Args&&... args);

  template <class Reply, class... Args>
  static typename std::enable_if<OtherThan<
      RequestFromReplyType<Reply, RequestReplyPairs>,
      GetLike<>>::value>::type
  replyImpl(McServerRequestContext&& ctx, Reply&& reply, Args&&... args);

  template <class Reply>
  static void replyImpl2(
      McServerRequestContext&& ctx,
      Reply&& reply,
      DestructorFunc destructor = nullptr,
      void* toDestruct = nullptr);

  folly::Optional<folly::IOBuf>& asciiKey() {
    if (!asciiState_) {
      asciiState_ = folly::make_unique<AsciiState>();
    }
    return asciiState_->key_;
  }
  bool hasParent() const {
    return asciiState_ && asciiState_->parent_;
  }
  MultiOpParent& parent() const {
    assert(hasParent());
    return *asciiState_->parent_;
  }
  bool isParentError() const;

  // Whether or not *this is used to mark the end of a multi-get request
  bool isEndContext() const {
    return isEndContext_;
  }

  /**
   * If reply is error, multi-op parent may inform this context that it will
   * assume responsibility for reporting the error. If so, this context should
   * not call McServerSession::reply. Returns true iff parent assumes
   * responsibility for reporting error. If true is returned, errorMessage is
   * moved to parent.
   */
  bool moveReplyToParent(
      mc_res_t result,
      uint32_t errorCode,
      std::string&& errorMessage) const;

  McServerRequestContext(const McServerRequestContext&) = delete;
  const McServerRequestContext& operator=(const McServerRequestContext&) =
      delete;

  /* Only McServerSession can create these */
  friend class McServerSession;
  friend class MultiOpParent;
  friend class WriteBuffer;
  McServerRequestContext(
      McServerSession& s,
      uint64_t r,
      bool nr = false,
      std::shared_ptr<MultiOpParent> parent = nullptr,
      bool isEndContext = false,
      const CompressionCodecMap* compressionCodecMap = nullptr,
      CodecIdRange range = CodecIdRange::Empty);
};

/**
 * McServerOnRequest is a polymorphic base class used as a callback
 * by AsyncMcServerWorker and McAsciiParser to hand off a request
 * to McrouterClient.
 *
 * The complexity in the implementation below is due to the fact that we
 * effectively need templated virtual member functions (which do not really
 * exist in C++).
 */
template <class RequestList>
class McServerOnRequestIf;

/**
 * OnRequest callback interface. This is an implementation detail.
 */
template <class Request>
class McServerOnRequestIf<List<Request>> {
 public:
  virtual void caretRequestReady(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& reqBody,
      McServerRequestContext&& ctx) = 0;

  virtual void requestReady(McServerRequestContext&&, Request&&) {
    LOG(ERROR) << "requestReady() not implemented for request type "
               << Request::name;
  }

  virtual ~McServerOnRequestIf() = default;
};

template <class Request, class... Requests>
class McServerOnRequestIf<List<Request, Requests...>>
    : public McServerOnRequestIf<List<Requests...>> {
 public:
  using McServerOnRequestIf<List<Requests...>>::requestReady;

  virtual void requestReady(McServerRequestContext&&, Request&&) {
    LOG(ERROR) << "requestReady() not implemented for request type "
               << Request::name;
  }

  virtual ~McServerOnRequestIf() = default;
};

class McServerOnRequest : public McServerOnRequestIf<McRequestList> {};

/**
 * Helper class to wrap user-defined callbacks in a correct virtual interface.
 * This is needed since we're mixing templates and virtual functions.
 */
template <class OnRequest, class RequestList = McRequestList>
class McServerOnRequestWrapper;

template <class OnRequest>
class McServerOnRequestWrapper<OnRequest, List<>> : public McServerOnRequest {
 public:
  using McServerOnRequest::requestReady;

  template <class... Args>
  explicit McServerOnRequestWrapper(Args&&... args)
    : onRequest_(std::forward<Args>(args)...) {}

  void caretRequestReady(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& reqBody,
      McServerRequestContext&& ctx) override final;

  void dispatchTypedRequestIfDefined(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& reqBody,
      McServerRequestContext&& ctx,
      std::true_type) {
    onRequest_.dispatchTypedRequest(headerInfo, reqBody, std::move(ctx));
  }

  void dispatchTypedRequestIfDefined(
      const UmbrellaMessageInfo&,
      const folly::IOBuf& reqBody,
      McServerRequestContext&& ctx,
      std::false_type) {
    throw std::runtime_error("dispatchTypedRequestIfDefined got bad request");
  }

  template <class Request>
  void requestReadyImpl(
      McServerRequestContext&& ctx,
      Request&& req,
      std::true_type) {
    onRequest_.onRequest(std::move(ctx), std::move(req));
  }

  template <class Request>
  void requestReadyImpl(
      McServerRequestContext&& ctx,
      Request&& req,
      std::false_type) {
    McServerRequestContext::reply(
        std::move(ctx), ReplyT<Request>(mc_res_local_error));
  }

 protected:
  OnRequest onRequest_;
};

template <class OnRequest, class Request, class... Requests>
class McServerOnRequestWrapper<OnRequest, List<Request, Requests...>>
    : public McServerOnRequestWrapper<OnRequest, List<Requests...>> {
 public:
  using McServerOnRequestWrapper<OnRequest, List<Requests...>>::requestReady;

  template <class... Args>
  explicit McServerOnRequestWrapper(Args&&... args)
    : McServerOnRequestWrapper<OnRequest, List<Requests...>>(
        std::forward<Args>(args)...) {
  }

  void requestReady(McServerRequestContext&& ctx, Request&& req)
      override final {
    this->requestReadyImpl(
        std::move(ctx),
        std::move(req),
        carbon::detail::CanHandleRequest::value<Request, OnRequest>());
  }
};

}} // facebook::memcache

#include "McServerRequestContext-inl.h"
