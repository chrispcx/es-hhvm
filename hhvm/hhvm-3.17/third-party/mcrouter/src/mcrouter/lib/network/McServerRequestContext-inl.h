/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/network/McServerSession.h"
#include "mcrouter/lib/network/WriteBuffer.h"

namespace facebook { namespace memcache {

template <class Reply>
void McServerRequestContext::reply(
    McServerRequestContext&& ctx,
    Reply&& reply) {
  replyImpl(std::move(ctx), std::move(reply));
}

template <class Reply>
void McServerRequestContext::reply(
    McServerRequestContext&& ctx,
    Reply&& reply,
    DestructorFunc destructor,
    void* toDestruct) {
  replyImpl(std::move(ctx), std::move(reply), destructor, toDestruct);
}

template <class Reply, class... Args>
typename std::enable_if<
    GetLike<RequestFromReplyType<Reply, RequestReplyPairs>>::value>::type
McServerRequestContext::replyImpl(
    McServerRequestContext&& ctx,
    Reply&& reply,
    Args&&... args) {
  // On error, multi-get parent may assume responsiblity of replying
  if (ctx.moveReplyToParent(
          reply.result(),
          reply.appSpecificErrorCode(),
          std::move(reply.message()))) {
    replyImpl2(std::move(ctx), Reply(), std::forward<Args>(args)...);
  } else {
    replyImpl2(std::move(ctx), std::move(reply), std::forward<Args>(args)...);
  }
}

template <class Reply, class... Args>
typename std::enable_if<OtherThan<
    RequestFromReplyType<Reply, RequestReplyPairs>,
    GetLike<>>::value>::type
McServerRequestContext::replyImpl(
    McServerRequestContext&& ctx,
    Reply&& reply,
    Args&&... args) {
  replyImpl2(std::move(ctx), std::move(reply), std::forward<Args>(args)...);
}

template <class Reply>
void McServerRequestContext::replyImpl2(
    McServerRequestContext&& ctx,
    Reply&& reply,
    DestructorFunc destructor,
    void* toDestruct) {
  ctx.replied_ = true;
  auto session = ctx.session_;
  if (toDestruct != nullptr) {
    assert(destructor != nullptr);
  }
  // Call destructor(toDestruct) on error, or pass ownership to write buffer
  std::unique_ptr<void, void (*)(void*)> destructorContainer(
      toDestruct, destructor);

  if (ctx.noReply(reply)) {
    session->reply(nullptr, ctx.reqid_);
    return;
  }

  session->ensureWriteBufs();

  uint64_t reqid = ctx.reqid_;
  auto wb = session->writeBufs_->get();
  if (!wb->prepareTyped(
          std::move(ctx), std::move(reply), std::move(destructorContainer))) {
    session->transport_->close();
    return;
  }
  session->reply(std::move(wb), reqid);
}

/**
 * No reply if either:
 *  1) We saw an error (the error will be printed out by the end context),
 *  2) This is a miss, except for lease-get (lease-get misses still have
 *     'LVALUE' replies with the token).
 * Lease-gets are handled in a separate overload below.
 */
template <class Reply>
bool McServerRequestContext::noReply(const Reply& r) const {
  if (noReply_) {
    return true;
  }
  if (!hasParent()) {
    return false;
  }
  return isParentError() || r.result() != mc_res_found;
}

inline bool McServerRequestContext::noReply(const McLeaseGetReply&) const {
  if (noReply_) {
    return true;
  }
  if (!hasParent()) {
    return false;
  }
  return isParentError();
}

template <class T, class Enable = void>
struct HasDispatchTypedRequest {
  static constexpr std::false_type value{};
};

template <class T>
struct HasDispatchTypedRequest<
  T,
  typename std::enable_if<
    std::is_same<
      decltype(std::declval<T>().dispatchTypedRequest(
                 std::declval<UmbrellaMessageInfo>(),
                 std::declval<folly::IOBuf>(),
                 std::declval<McServerRequestContext>())),
      bool>::value>::type> {
  static constexpr std::true_type value{};
};

template <class OnRequest>
void McServerOnRequestWrapper<OnRequest, List<>>::caretRequestReady(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& reqBuf,
    McServerRequestContext&& ctx) {
  dispatchTypedRequestIfDefined(
    headerInfo, reqBuf, std::move(ctx),
    HasDispatchTypedRequest<OnRequest>::value);
}

}}  // facebook::memcache
