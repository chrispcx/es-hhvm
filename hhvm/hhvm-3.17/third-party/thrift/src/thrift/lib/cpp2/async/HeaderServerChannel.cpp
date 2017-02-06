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

#include <thrift/lib/cpp2/async/HeaderServerChannel.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp/transport/TTransportException.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <folly/io/Cursor.h>
#include <folly/String.h>

#include <utility>
#include <exception>

using std::unique_ptr;
using std::pair;
using folly::IOBuf;
using folly::IOBufQueue;
using folly::make_unique;
using namespace apache::thrift::transport;
using namespace apache::thrift;
using folly::EventBase;
using apache::thrift::async::TAsyncSocket;
using apache::thrift::async::TAsyncTransport;
using apache::thrift::TApplicationException;
using apache::thrift::server::TServerObserver;
using apache::thrift::protocol::PROTOCOL_TYPES;

namespace apache { namespace thrift {

std::atomic<uint32_t> HeaderServerChannel::sample_(0);

HeaderServerChannel::HeaderServerChannel(
  const std::shared_ptr<TAsyncTransport>& transport)
    : HeaderServerChannel(
        std::shared_ptr<Cpp2Channel>(
            Cpp2Channel::newChannel(transport,
                make_unique<ServerFramingHandler>(*this),
                make_unique<ServerSaslNegotiationHandler>(*this))))
{}

HeaderServerChannel::HeaderServerChannel(
    const std::shared_ptr<Cpp2Channel>& cpp2Channel)
    : callback_(nullptr)
    , arrivalSeqId_(1)
    , lastWrittenSeqId_(0)
    , sampleRate_(0)
    , timeoutSASL_(5000)
    , saslServerCallback_(*this)
    , cpp2Channel_(cpp2Channel) {}

void HeaderServerChannel::destroy() {
  DestructorGuard dg(this);

  saslServerCallback_.cancelTimeout();
  if (saslServer_) {
    saslServer_->detachEventBase();
  }

  if (callback_) {
    auto error = folly::make_exception_wrapper<TTransportException>(
        "Channel destroyed");
    callback_->channelClosed(std::move(error));
  }

  cpp2Channel_->closeNow();

  folly::DelayedDestruction::destroy();
}

// Header framing
unique_ptr<IOBuf>
HeaderServerChannel::ServerFramingHandler::addFrame(unique_ptr<IOBuf> buf,
                                                    THeader* header) {
  channel_.updateClientType(header->getClientType());

  // Note: This THeader function may throw.  However, we don't want to catch
  // it here, because this would send an empty message out on the wire.
  // Instead we have to catch it at sendMessage
  return header->addHeader(
    std::move(buf),
    channel_.getPersistentWriteHeaders(),
    false /* Data already transformed in AsyncProcessor.h */);
}

std::tuple<unique_ptr<IOBuf>, size_t, unique_ptr<THeader>>
HeaderServerChannel::ServerFramingHandler::removeFrame(IOBufQueue* q) {
  std::unique_ptr<THeader> header(new THeader(THeader::ALLOW_BIG_FRAMES));
  // removeHeader will set seqid in header.
  // For older clients with seqid in the protocol, header
  // will dig in to the protocol to get the seqid correctly.
  if (!q || !q->front() || q->front()->empty()) {
    return make_tuple(std::unique_ptr<IOBuf>(), 0, nullptr);
  }

  std::unique_ptr<folly::IOBuf> buf;
  size_t remaining = 0;
  try {
    buf = header->removeHeader(q, remaining,
                               channel_.getPersistentReadHeaders());
  } catch (const std::exception& e) {
    LOG(ERROR) << "Received invalid request from client: "
               << folly::exceptionStr(e) << " "
               << getTransportDebugString(channel_.getTransport());
    throw;
  }
  if (!buf) {
    return make_tuple(std::unique_ptr<IOBuf>(), remaining, nullptr);
  }

  CLIENT_TYPE ct = header->getClientType();
  if (!channel_.isSupportedClient(ct) &&
      ct != THRIFT_HEADER_SASL_CLIENT_TYPE) {
    LOG(ERROR) << "Server rejecting unsupported client type " << ct;
    channel_.checkSupportedClient(ct);
  }

  // Check if protocol used in the buffer is consistent with the protocol
  // id in the header.

  folly::io::Cursor c(buf.get());
  auto byte = c.read<uint8_t>();
  // Initialize it to a value never used on the wire
  PROTOCOL_TYPES protInBuf = PROTOCOL_TYPES::T_DEBUG_PROTOCOL;
  if (byte == 0x82) {
    protInBuf = PROTOCOL_TYPES::T_COMPACT_PROTOCOL;
  } else if (byte == 0x80) {
    protInBuf = PROTOCOL_TYPES::T_BINARY_PROTOCOL;
  } else if (ct != THRIFT_HTTP_SERVER_TYPE) {
    LOG(ERROR) << "Received corrupted request from client: "
               << getTransportDebugString(channel_.getTransport()) << ". "
               << "Corrupted payload in header message. In message header, "
               << "protoId: " << header->getProtocolId() << ", "
               << "clientType: " << folly::to<std::string>(ct) << ". "
               << "First few bytes of payload: "
               << getTHeaderPayloadString(buf.get());

  }

  if (protInBuf != PROTOCOL_TYPES::T_DEBUG_PROTOCOL &&
      header->getProtocolId() != protInBuf) {
    LOG(ERROR) << "Received corrupted request from client: "
               << getTransportDebugString(channel_.getTransport()) << ". "
               << "Protocol mismatch, in message header, protocolId: "
               << folly::to<std::string>(header->getProtocolId()) << ", "
               << "clientType: " << folly::to<std::string>(ct) << ", "
               << "in payload, protocolId: "
               << folly::to<std::string>(protInBuf)
               << ". First few bytes of payload: "
               << getTHeaderPayloadString(buf.get());
  }

  // In order to allow negotiation to happen when the client requests
  // sasl but it's not supported, we don't throw an exception in the
  // sasl case.  Instead, we let the message bubble up, and check if
  // the client is supported in handleSecurityMessage called from the
  // ServerSaslNegotiationHandler.

  header->setMinCompressBytes(channel_.getMinCompressBytes());
  // Only set default transforms if client has not set any
  if (header->getWriteTransforms().empty()) {
    header->setTransforms(channel_.getDefaultWriteTransforms());
  }
  return make_tuple(std::move(buf), 0, std::move(header));
}

bool
HeaderServerChannel::ServerSaslNegotiationHandler::handleSecurityMessage(
    std::unique_ptr<folly::IOBuf>&& buf,
    std::unique_ptr<apache::thrift::transport::THeader>&& header) {
  auto ct = header->getClientType();
  auto protectionState = channel_.getProtectionState();
  bool fallThrough = false;
  if (ct == THRIFT_HEADER_SASL_CLIENT_TYPE) {
    if (!channel_.isSupportedClient(ct) ||
        (!channel_.getSaslServer() &&
         !protectionHandler_->getSaslEndpoint())) {
      if (protectionState == ProtectionState::UNKNOWN) {
        // The client tried to use SASL, but it's not supported by
        // policy.  Tell the client to fall back.
        try {
          auto trans = header->getWriteTransforms();
          channel_.sendMessage(nullptr,
                               THeader::transform(
                                   IOBuf::create(0),
                                   trans,
                                   channel_.getMinCompressBytes()),
                               header.get());
        } catch (const std::exception& e) {
          LOG(ERROR) << "Failed to send message: " << e.what();
        }
      } else {
        // The supported client set changed halfway through or
        // something.  Bail out.
        channel_.setProtectionState(ProtectionState::INVALID);
        LOG(WARNING) << "Inconsistent SASL support";
        auto ex = folly::make_exception_wrapper<TTransportException>(
            "Inconsistent SASL support");
        channel_.messageReceiveErrorWrapped(std::move(ex));
      }
    } else if (protectionState == ProtectionState::UNKNOWN ||
               protectionState == ProtectionState::INPROGRESS ||
               protectionState == ProtectionState::WAITING) {
      // Technically we shouldn't get any new messages while in the INPROGRESS
      // state, but we'll allow it to fall through here and let the saslServer_
      // state machine throw an error.
      channel_.setProtectionState(ProtectionState::INPROGRESS);
      channel_.getSaslServer()->setProtocolId(header->getProtocolId());
      channel_.getSaslServerCallback()->setHeader(std::move(header));
      channel_.getSaslServer()->consumeFromClient(
          channel_.getSaslServerCallback(), std::move(buf));
    } else {
      // else, fall through to application message processing
      fallThrough = true;
    }
  } else if ((protectionState == ProtectionState::VALID ||
              protectionState == ProtectionState::INPROGRESS ||
              protectionState == ProtectionState::WAITING) &&
              !channel_.isSupportedClient(ct)) {
    // Either negotiation has completed or negotiation is incomplete,
    // non-sasl was received, but is not permitted.
    // We should fail hard in this case.
    channel_.setProtectionState(ProtectionState::INVALID);
    LOG(WARNING) << "non-SASL message received on SASL channel";
    auto ex = folly::make_exception_wrapper<TTransportException>(
        "non-SASL message received on SASL channel");
    channel_.messageReceiveErrorWrapped(std::move(ex));
  } else if (protectionState == ProtectionState::UNKNOWN) {
    // This is the path non-SASL-aware (or SASL-disabled) clients will
    // take.
    VLOG(5) << "non-SASL client connection received";
    channel_.setProtectionState(ProtectionState::NONE);
    fallThrough = true;
  } else if ((protectionState == ProtectionState::VALID ||
              protectionState == ProtectionState::INPROGRESS ||
              protectionState == ProtectionState::WAITING) &&
              channel_.isSupportedClient(ct)) {
    // If a client  permits a non-secure connection, we allow falling back to
    // one even if a SASL handshake is in progress, or SASL handshake has been
    // completed. The reason for latter is that we should allow a fallback if
    // client timed out in the last leg of the handshake.
    VLOG(5) << "Client initiated a fallback during a SASL handshake";
    // Cancel any SASL-related state, and log
    channel_.setProtectionState(ProtectionState::NONE);
    fallThrough = true;
    channel_.getSaslServerCallback()->cancelTimeout();
    if (channel_.getSaslServer()) {
      // Should be set here, but just in case check that saslServer_
      // exists
      channel_.getSaslServer()->detachEventBase();
    }
    const auto& observer = std::dynamic_pointer_cast<TServerObserver>(
      channel_.getEventBase()->getObserver());
    if (observer) {
      observer->saslFallBack();
    }
  }

  return fallThrough;
}

std::string
HeaderServerChannel::getTHeaderPayloadString(IOBuf* buf) {
  auto len = std::min<size_t>(buf->length(), 20);
  return folly::cEscape<std::string>(
      folly::StringPiece((const char*)buf->data(), len));
}

std::string
HeaderServerChannel::getTransportDebugString(TAsyncTransport* transport) {
  if (!transport) {
    return std::string();
  }

  auto ret = folly::to<std::string>("(transport ",
                                    folly::demangle(typeid(*transport)));

  try {
    folly::SocketAddress addr;
    transport->getPeerAddress(&addr);
    folly::toAppend(", address ", addr.getAddressStr(),
                    ", port ", addr.getPort(),
                    &ret);
  } catch (const std::exception& e) {
  }

  ret += ')';
  return ret;
}

// Client Interface

HeaderServerChannel::HeaderRequest::HeaderRequest(
      HeaderServerChannel* channel,
      unique_ptr<IOBuf>&& buf,
      unique_ptr<THeader>&& header,
      unique_ptr<sample> sample)
  : channel_(channel)
  , header_(std::move(header))
  , active_(true) {

  this->buf_ = std::move(buf);
  if (sample) {
    timestamps_.readBegin = sample->readBegin;
    timestamps_.readEnd = sample->readEnd;
  }
}

/**
 * send a reply to the client.
 *
 * Note that to be backwards compatible with thrift1, the generated
 * code calls sendReply(nullptr) for oneway calls where seqid !=
 * ONEWAY_SEQ_ID.  This is so that the sendCatchupRequests code runs
 * correctly for in-order responses to older clients.  sendCatchupRequests
 * does not actually send null buffers, it just ignores them.
 *
 */
void HeaderServerChannel::HeaderRequest::sendReply(
    unique_ptr<IOBuf>&& buf,
    MessageChannel::SendCallback* cb) {
  // This method is only called and active_ is only touched in evb, so
  // it is safe to use this flag from both timeout and normal responses.
  auto& header = active_ ? header_ : timeoutHeader_;
  if (!channel_->outOfOrder_.value()) {
    // In order processing, make sure the ordering is correct.
    if (InOrderRecvSeqId_ != channel_->lastWrittenSeqId_ + 1) {
      // Save it until we can send it in order.
      channel_->inOrderRequests_[InOrderRecvSeqId_] =
        std::make_tuple(cb, std::move(buf), std::move(header));
    } else {
      // Send it now, and send any subsequent requests in order.
      channel_->sendCatchupRequests(std::move(buf), cb, header.get());
    }
  } else {
    if (!buf) {
      // oneway calls are OK do this, but is a bug for twoway.
      DCHECK(isOneway());
      return;
    }
    try {
      // out of order, send as soon as it is done.
      channel_->sendMessage(cb, std::move(buf), header.get());
    } catch (const std::exception& e) {
      LOG(ERROR) << "Failed to send message: " << e.what();
    }
  }
}

void HeaderServerChannel::HeaderRequest::serializeAndSendError(
  apache::thrift::transport::THeader& header,
  TApplicationException& tae,
  const std::string& methodName,
  int32_t protoSeqId,
  MessageChannel::SendCallback* cb
) {
  std::unique_ptr<folly::IOBuf> exbuf;
  uint16_t proto = header.getProtocolId();
  try {
    exbuf = serializeError(proto, tae, methodName, protoSeqId);
  } catch (const TProtocolException& pe) {
    LOG(ERROR) << "serializeError failed. type=" << pe.getType()
               << " what()=" << pe.what();
    channel_->closeNow();
    return;
  }
  exbuf = THeader::transform(std::move(exbuf),
                             header.getWriteTransforms(),
                             header.getMinCompressBytes());
  sendReply(std::move(exbuf), cb);
}

/**
 * Send a serialized error back to the client.
 * For a header server, this means serializing the exception, and setting
 * an error flag in the header.
 */
void HeaderServerChannel::HeaderRequest::sendErrorWrapped(
    folly::exception_wrapper ew,
    std::string exCode,
    MessageChannel::SendCallback* cb) {

  // Other types are unimplemented.
  DCHECK(ew.is_compatible_with<TApplicationException>());

  header_->setHeader("ex", exCode);
  ew.with_exception([&](TApplicationException& tae) {
      std::unique_ptr<folly::IOBuf> exbuf;
      uint16_t proto = header_->getProtocolId();
      auto transforms = header_->getWriteTransforms();
      try {
        exbuf = serializeError(proto, tae, getBuf());
      } catch (const TProtocolException& pe) {
        LOG(ERROR) << "serializeError failed. type=" << pe.getType()
            << " what()=" << pe.what();
        channel_->closeNow();
        return;
      }
      exbuf = THeader::transform(std::move(exbuf),
                                 transforms,
                                 header_->getMinCompressBytes());
      sendReply(std::move(exbuf), cb);
    });
}

void HeaderServerChannel::HeaderRequest::sendErrorWrapped(
  folly::exception_wrapper ew,
  std::string exCode,
  const std::string& methodName,
  int32_t protoSeqId,
  MessageChannel::SendCallback* cb) {
  // Other types are unimplemented.
  DCHECK(ew.is_compatible_with<TApplicationException>());

  header_->setHeader("ex", exCode);
  ew.with_exception([&](TApplicationException& tae) {
      serializeAndSendError(*header_, tae, methodName, protoSeqId, cb);
    });
}

void HeaderServerChannel::HeaderRequest::sendTimeoutResponse(
    const std::string& methodName,
    int32_t protoSeqId,
    MessageChannel::SendCallback* cb,
    const std::map<std::string, std::string>& headers,
    TimeoutResponseType responseType) {
  // Sending timeout response always happens on eb thread, while normal
  // request handling might still be work-in-progress on tm thread and
  // touches the per-request THeader at any time. This builds a new THeader
  // and only reads certain fields from header_. To avoid race condition,
  // DO NOT read any header from the per-request THeader.
  timeoutHeader_ = header_->clone();
  auto errorCode = responseType == TimeoutResponseType::QUEUE ?
    kServerQueueTimeoutErrorCode : kTaskExpiredErrorCode;
  timeoutHeader_->setHeader("ex", errorCode);
  auto errorMsg = responseType == TimeoutResponseType::QUEUE ?
    "Queue Timeout" : "Task expired";
  for (const auto& it : headers) {
    timeoutHeader_->setHeader(it.first, it.second);
  }

  TApplicationException tae(
      TApplicationException::TApplicationExceptionType::TIMEOUT,
      errorMsg);
  serializeAndSendError(*timeoutHeader_, tae, methodName, protoSeqId, cb);
}

void HeaderServerChannel::sendCatchupRequests(
    std::unique_ptr<folly::IOBuf> next_req,
    MessageChannel::SendCallback* cb,
    THeader* header) {

  DestructorGuard dg(this);

  std::unique_ptr<THeader> header_ptr;
  while (true) {
    if (next_req) {
      try {
        sendMessage(cb, std::move(next_req), header);
      } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to send message: " << e.what();
      }
    } else if (nullptr != cb) {
      // There is no message (like a oneway req), but there is a callback
      cb->messageSent();
    }
    lastWrittenSeqId_++;

    // Check for the next req
    auto next = inOrderRequests_.find(lastWrittenSeqId_ + 1);
    if (next != inOrderRequests_.end()) {
      next_req = std::move(std::get<1>(next->second));
      cb = std::get<0>(next->second);
      header_ptr = std::move(std::get<2>(next->second));
      header = header_ptr.get();
      inOrderRequests_.erase(next);
    } else {
      break;
    }
  }
}

// Interface from MessageChannel::RecvCallback
bool HeaderServerChannel::shouldSample() {
  return (sampleRate_ > 0) &&
    ((sample_++ % sampleRate_) == 0);
}

void HeaderServerChannel::messageReceived(unique_ptr<IOBuf>&& buf,
                                          unique_ptr<THeader>&& header,
                                          unique_ptr<sample> sample) {
  DestructorGuard dg(this);

  uint32_t recvSeqId = header->getSequenceNumber();
  bool outOfOrder = (header->getFlags() & HEADER_FLAG_SUPPORT_OUT_OF_ORDER);
  if (!outOfOrder_.hasValue()) {
    outOfOrder_ = outOfOrder;
  } else if (outOfOrder_.value() != outOfOrder) {
    LOG(ERROR) << "Channel " << (outOfOrder_.value() ? "" : "doesn't ")
               << "support out-of-order, but received a message with the "
               << "out-of-order bit " << (outOfOrder ? "set" : "unset");
    messageReceiveErrorWrapped(
        folly::make_exception_wrapper<TTransportException>(
            "Bad out-of-order flag"));
    return;
  }

  if (!outOfOrder) {
    // Create a new seqid for in-order messages because they might not
    // be sequential.  This seqid is only used internally in HeaderServerChannel
    recvSeqId = arrivalSeqId_++;
  }

  if (callback_) {
    unique_ptr<HeaderRequest> request(
        new HeaderRequest(this,
                          std::move(buf),
                          std::move(header),
                          std::move(sample)));

    if (!outOfOrder) {
      if (inOrderRequests_.size() > MAX_REQUEST_SIZE) {
        // There is probably nothing useful we can do here.
        LOG(WARNING) << "Hit in order request buffer limit";
        auto ex = folly::make_exception_wrapper<TTransportException>(
            "Hit in order request buffer limit");
        messageReceiveErrorWrapped(std::move(ex));
        return;
      }
      request->setInOrderRecvSequenceId(recvSeqId);
    }

    auto ew = folly::try_and_catch<std::exception>([&]() {
        callback_->requestReceived(std::move(request));
      });
    if (ew) {
      LOG(WARNING) << "Could not parse request: " << ew.what();
      messageReceiveErrorWrapped(std::move(ew));
      return;
    }

  }
}

void HeaderServerChannel::messageChannelEOF() {
  DestructorGuard dg(this);

  auto ew = folly::make_exception_wrapper<TTransportException>(
      "Channel Closed");
  if (callback_) {
    callback_->channelClosed(std::move(ew));
  }
}

void HeaderServerChannel::messageReceiveErrorWrapped(
    folly::exception_wrapper&& ex) {
  DestructorGuard dg(this);

  VLOG(1) << "Receive error: " << ex.what();

  if (callback_) {
    callback_->channelClosed(std::move(ex));
  }
}

void HeaderServerChannel::SaslServerCallback::saslSendClient(
    std::unique_ptr<folly::IOBuf>&& response) {
  if (channel_.timeoutSASL_ > 0) {
    channel_.getEventBase()->timer().scheduleTimeout(this,
        std::chrono::milliseconds(channel_.timeoutSASL_));
  }
  try {
    auto trans = header_->getWriteTransforms();
    channel_.setProtectionState(ProtectionState::WAITING);
    channel_.sendMessage(nullptr,
                         THeader::transform(
                           std::move(response),
                           trans,
                           channel_.getMinCompressBytes()),
                         header_.get());
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to send message: " << e.what();
  }
}

void HeaderServerChannel::SaslServerCallback::saslError(
    folly::exception_wrapper&& ex) {
  folly::HHWheelTimer::Callback::cancelTimeout();
  const auto& observer = std::dynamic_pointer_cast<TServerObserver>(
    channel_.getEventBase()->getObserver());

  try {
    // Fall back to insecure.  This will throw an exception if the
    // insecure client type is not supported.
    channel_.setClientType(THRIFT_HEADER_CLIENT_TYPE);
  } catch (const std::exception& e) {
    if (observer) {
      observer->saslError();
    }
    channel_.setProtectionState(ProtectionState::INVALID);
    LOG(ERROR) << "SASL required by server but failed: " << ex.what();
    channel_.messageReceiveErrorWrapped(std::move(ex));
    return;
  }

  if (observer) {
    observer->saslFallBack();
  }

  VLOG(1) << "SASL server falling back to insecure: " << ex.what();

  // Send the client a null message so the client will try again.
  // TODO mhorowitz: generate a real message here.
  header_->setClientType(THRIFT_HEADER_SASL_CLIENT_TYPE);
  try {
    auto trans = header_->getWriteTransforms();
    channel_.sendMessage(nullptr,
                         THeader::transform(
                           IOBuf::create(0),
                           trans,
                           channel_.getMinCompressBytes()),
                         header_.get());
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to send message: " << e.what();
  }
  channel_.setProtectionState(ProtectionState::NONE);
  // We need to tell saslServer that the security channel is no longer
  // available, so that it does not attempt to send messages to the server.
  // Since the server-side SASL code is virtually non-blocking, it should be
  // rare that this is actually necessary.
  channel_.saslServer_->detachEventBase();
}

void HeaderServerChannel::SaslServerCallback::saslComplete() {
  // setProtectionState could eventually destroy the channel
  DestructorGuard dg(&channel_);

  const auto& observer = std::dynamic_pointer_cast<TServerObserver>(
    channel_.getEventBase()->getObserver());

  if (observer) {
    observer->saslComplete();
  }

  folly::HHWheelTimer::Callback::cancelTimeout();
  auto& saslServer = channel_.saslServer_;
  VLOG(5) << "SASL server negotiation complete: "
             << saslServer->getServerIdentity() << " <= "
             << saslServer->getClientIdentity();
  channel_.setProtectionState(ProtectionState::VALID);
  channel_.setClientType(THRIFT_HEADER_SASL_CLIENT_TYPE);
}

}} // apache::thrift
