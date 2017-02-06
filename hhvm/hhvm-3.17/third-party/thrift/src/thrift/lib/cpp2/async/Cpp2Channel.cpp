/*
 * Copyright 2014 Facebook, Inc.
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

#include <thrift/lib/cpp2/async/Cpp2Channel.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp/concurrency/Util.h>
#include <thrift/lib/cpp2/async/PcapLoggingHandler.h>

#include <folly/io/IOBufQueue.h>
#include <folly/io/Cursor.h>
#include <folly/String.h>

#include <glog/logging.h>

using std::unique_ptr;
using std::pair;
using folly::IOBuf;
using folly::IOBufQueue;
using namespace folly::io;
using namespace apache::thrift::transport;
using folly::EventBase;
using namespace apache::thrift::concurrency;
using apache::thrift::async::TAsyncTransport;

namespace apache { namespace thrift {

Cpp2Channel::Cpp2Channel(
  const std::shared_ptr<TAsyncTransport>& transport,
  std::unique_ptr<FramingHandler> framingHandler,
  std::unique_ptr<ProtectionHandler> protectionHandler,
  std::unique_ptr<SaslNegotiationHandler> saslNegotiationHandler)
    : transport_(transport)
    , queue_(new IOBufQueue(IOBufQueue::cacheChainLength()))
    , recvCallback_(nullptr)
    , eofInvoked_(false)
    , protectionHandler_(std::move(protectionHandler))
    , framingHandler_(std::move(framingHandler))
    , saslNegotiationHandler_(std::move(saslNegotiationHandler)) {
  if (!protectionHandler_) {
    protectionHandler_.reset(new ProtectionHandler);
  }
  framingHandler_->setProtectionHandler(protectionHandler_.get());

  if (!saslNegotiationHandler_) {
    saslNegotiationHandler_ = folly::make_unique<DummySaslNegotiationHandler>();
  }
  saslNegotiationHandler_->setProtectionHandler(protectionHandler_.get());
  auto pcapLoggingHandler = std::make_shared<PcapLoggingHandler>([this]{
      return protectionHandler_->getProtectionState() ==
          ProtectionHandler::ProtectionState::VALID;
  });
  pipeline_ = Pipeline::create(
      TAsyncTransportHandler(transport),
      wangle::OutputBufferingHandler(),
      protectionHandler_,
      pcapLoggingHandler,
      framingHandler_,
      saslNegotiationHandler_,
      this);
  // Let the pipeline know that this handler owns the pipeline itself.
  // The pipeline will then avoid destruction order issues.
  // CHECK that this operation is successful.
  CHECK(pipeline_->setOwner(this));
  pipeline_->transportActive();
  // TODO getHandler() with no index should return first valid handler?
  transportHandler_ = pipeline_->getHandler<TAsyncTransportHandler>(0);
}

folly::Future<folly::Unit> Cpp2Channel::close(Context* ctx) {
  DestructorGuard dg(this);
  if (transport_) {
    // If transport is taken over, no need to call processReadEOF. This can
    // happen when processing HTTP GET request, where the ownership of the
    // socket is transferred to GetHandler.
    processReadEOF();
  }
  return ctx->fireClose();
}

void Cpp2Channel::closeNow() {
  // closeNow can invoke callbacks
  DestructorGuard dg(this);

  if (pipeline_) {
    pipeline_->close();
  }

  // Note that close() above might kill the pipeline_, so let's check again.
  if (pipeline_) {
    pipeline_.reset();
  }
}

void Cpp2Channel::destroy() {
  closeNow();
  MessageChannel::destroy();
}

void Cpp2Channel::attachEventBase(
  EventBase* eventBase) {
  transportHandler_->attachEventBase(eventBase);
}

void Cpp2Channel::detachEventBase() {
  transportHandler_->detachEventBase();
}

EventBase* Cpp2Channel::getEventBase() {
  return transport_->getEventBase();
}

void Cpp2Channel::read(
    Context* ctx,
    std::pair<std::unique_ptr<folly::IOBuf>,
              std::unique_ptr<THeader>> bufAndHeader) {
  DestructorGuard dg(this);

  if (recvCallback_ && recvCallback_->shouldSample() && !sample_) {
    sample_.reset(new RecvCallback::sample);
    sample_->readBegin = Util::currentTimeUsec();
  }

  if (!recvCallback_) {
    VLOG(5) << "Received a message, but no recvCallback_ installed!";
    return;
  }

  if (sample_) {
    sample_->readEnd = Util::currentTimeUsec();
  }

  recvCallback_->messageReceived(std::move(bufAndHeader.first),
                                 std::move(bufAndHeader.second),
                                 std::move(sample_));
}

void Cpp2Channel::readEOF(Context* ctx) {
  processReadEOF();
}

void Cpp2Channel::readException(Context* ctx, folly::exception_wrapper e)  {
  DestructorGuard dg(this);
  VLOG(5) << "Got a read error: " << folly::exceptionStr(e);
  if (recvCallback_) {
    recvCallback_->messageReceiveErrorWrapped(std::move(e));
  }
  processReadEOF();
}

void Cpp2Channel::writeSuccess() noexcept {
  assert(sendCallbacks_.size() > 0);

  DestructorGuard dg(this);
  auto* cb = sendCallbacks_.front();
  if (cb) {
    cb->messageSent();
  }
  sendCallbacks_.pop_front();
}

void Cpp2Channel::writeError(size_t bytesWritten,
                               const TTransportException& ex) noexcept {
  assert(sendCallbacks_.size() > 0);

  // Pop last write request, call error callback

  DestructorGuard dg(this);
  VLOG(5) << "Got a write error: " << folly::exceptionStr(ex);
  auto* cb = sendCallbacks_.front();
  if (cb) {
    cb->messageSendError(
        folly::make_exception_wrapper<TTransportException>(ex));
  }
  sendCallbacks_.pop_front();
}

void Cpp2Channel::processReadEOF() noexcept {
  transport_->setReadCallback(nullptr);

  VLOG(5) << "Got an EOF on channel";
  if (recvCallback_ && !eofInvoked_) {
    eofInvoked_ = true;
    recvCallback_->messageChannelEOF();
  }
}

// Low level interface
void Cpp2Channel::sendMessage(SendCallback* callback,
                              std::unique_ptr<folly::IOBuf>&& buf,
                              apache::thrift::transport::THeader* header) {
  // Callback may be null.
  assert(buf);

  if (!transport_->good()) {
    VLOG(5) << "Channel is !good() in sendMessage";
    // Callback must be last thing in sendMessage, or use guard
    if (callback) {
      callback->messageSendError(
          folly::make_exception_wrapper<TTransportException>(
            "Channel is !good()"));
    }
    return;
  }

  if (callback) {
    callback->sendQueued();
  }
  sendCallbacks_.push_back(callback);

  DestructorGuard dg(this);

  auto future = pipeline_->write(std::make_pair(std::move(buf), header));
  future.then([this,dg](folly::Try<folly::Unit>&& t) {
    if (t.withException<TTransportException>(
          [&](const TTransportException& ex) {
            writeError(0, ex);
          }) ||
        t.withException<std::exception>(
          [&](const std::exception& ex) {
            writeError(0, TTransportException(ex.what()));
          })) {
      return;
    } else {
      writeSuccess();
    }
  });
}

void Cpp2Channel::setReceiveCallback(RecvCallback* callback) {
  if (recvCallback_ == callback) {
    return;
  }

  // Still want to set recvCallback_ for outstanding EOFs
  recvCallback_ = callback;

  if (!transport_->good()) {
    transport_->setReadCallback(nullptr);
    return;
  }

  if (callback) {
    transportHandler_->attachReadCallback();
  } else {
    transportHandler_->detachReadCallback();
  }
}

}} // apache::thrift
