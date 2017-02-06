/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#ifndef THRIFT_ASYNC_CPP2CHANNEL_H_
#define THRIFT_ASYNC_CPP2CHANNEL_H_ 1

#include <thrift/lib/cpp2/async/SaslEndpoint.h>
#include <thrift/lib/cpp2/async/MessageChannel.h>
#include <thrift/lib/cpp2/async/TAsyncTransportHandler.h>
#include <folly/io/async/DelayedDestruction.h>
#include <thrift/lib/cpp/async/TAsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp/transport/THeader.h>
#include <folly/io/IOBufQueue.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/StaticPipeline.h>
#include <wangle/channel/OutputBufferingHandler.h>
#include <thrift/lib/cpp2/async/FramingHandler.h>
#include <thrift/lib/cpp2/async/ProtectionHandler.h>
#include <thrift/lib/cpp2/async/SaslNegotiationHandler.h>
#include <thrift/lib/cpp2/async/PcapLoggingHandler.h>
#include <memory>

#include <deque>
#include <vector>

namespace apache { namespace thrift {

using apache::thrift::transport::THeader;

class Cpp2Channel
  : public MessageChannel
  , public wangle::Handler<
        std::pair<std::unique_ptr<folly::IOBuf>, std::unique_ptr<THeader>>,
        int, // last inbound handler so this doesn't matter
        // Does nothing when writing
        std::pair<std::unique_ptr<folly::IOBuf>, THeader*>,
        std::pair<std::unique_ptr<folly::IOBuf>, THeader*>>
 {
 public:
  explicit Cpp2Channel(
    const std::shared_ptr<apache::thrift::async::TAsyncTransport>& transport,
    std::unique_ptr<FramingHandler> framingHandler,
    std::unique_ptr<ProtectionHandler> protectionHandler = nullptr,
    std::unique_ptr<SaslNegotiationHandler> saslNegotiationHandler = nullptr);

  // TODO(jsedgwick) This should be protected, but wangle::StaticPipeline
  // will encase this in a folly::Optional, which requires a public destructor.
  // Need to add a static_assert to Optional to make that prereq clearer
  ~Cpp2Channel() override {}

  static std::unique_ptr<Cpp2Channel,
                         folly::DelayedDestruction::Destructor>
  newChannel(
      const std::shared_ptr<apache::thrift::async::TAsyncTransport>& transport,
      std::unique_ptr<FramingHandler> framingHandler,
      std::unique_ptr<SaslNegotiationHandler> saslHandler = nullptr) {
    return std::unique_ptr<Cpp2Channel,
      folly::DelayedDestruction::Destructor>(
      new Cpp2Channel(transport,
                      std::move(framingHandler),
                      nullptr,
                      std::move(saslHandler)));
  }
  void closeNow();

  void setTransport(
      const std::shared_ptr<async::TAsyncTransport>& transport) {
    transport_ = transport;
    transportHandler_->setTransport(transport);
  }
  async::TAsyncTransport* getTransport() {
    return transport_.get();
  }

  // DelayedDestruction methods
  void destroy() override;

  // BytesToBytesHandler methods
  void read(Context* ctx,
            std::pair<std::unique_ptr<folly::IOBuf>,
                      std::unique_ptr<THeader>> bufAndHeader) override;
  void readEOF(Context* ctx) override;
  void readException(Context* ctx, folly::exception_wrapper e) override;
  folly::Future<folly::Unit> close(Context* ctx) override;

  folly::Future<folly::Unit> write(
      Context* ctx,
      std::pair<std::unique_ptr<folly::IOBuf>, THeader*> bufAndHeader) override
  {
    return ctx->fireWrite(std::move(bufAndHeader));
  }

  void writeSuccess() noexcept;
  void writeError(size_t bytesWritten,
                  const apache::thrift::transport::TTransportException& ex)
    noexcept;

  void processReadEOF() noexcept;

  // Interface from MessageChannel
  void sendMessage(SendCallback* callback,
                   std::unique_ptr<folly::IOBuf>&& buf,
                   apache::thrift::transport::THeader* header) override;
  void setReceiveCallback(RecvCallback* callback) override;

  // event base methods
  virtual void attachEventBase(folly::EventBase*);
  virtual void detachEventBase();
  folly::EventBase* getEventBase();

  // Queued sends feature - optimizes by minimizing syscalls in high-QPS
  // loads for greater throughput, but at the expense of some
  // minor latency increase.
  void setQueueSends(bool queueSends) {
    if (pipeline_) {
      pipeline_->getHandler<wangle::OutputBufferingHandler>(1)->queueSends_ = queueSends;
    }
  }

  ProtectionHandler* getProtectionHandler() const {
    return protectionHandler_.get();
  }

  void setReadBufferSize(uint32_t readBufferSize) {
    framingHandler_->setReadBufferSize(readBufferSize);
  }

private:
  std::shared_ptr<apache::thrift::async::TAsyncTransport> transport_;
  std::unique_ptr<folly::IOBufQueue> queue_;
  std::deque<SendCallback*> sendCallbacks_;

  RecvCallback* recvCallback_;
  bool eofInvoked_;

  std::unique_ptr<RecvCallback::sample> sample_;

  std::shared_ptr<ProtectionHandler> protectionHandler_;
  std::shared_ptr<FramingHandler> framingHandler_;
  std::shared_ptr<SaslNegotiationHandler> saslNegotiationHandler_;

  typedef wangle::StaticPipeline<
    folly::IOBufQueue&,
    std::pair<std::unique_ptr<folly::IOBuf>,
              apache::thrift::transport::THeader*>,
    TAsyncTransportHandler,
    wangle::OutputBufferingHandler,
    ProtectionHandler,
    PcapLoggingHandler,
    FramingHandler,
    SaslNegotiationHandler,
    Cpp2Channel>
  Pipeline;
  std::shared_ptr<Pipeline> pipeline_;
  TAsyncTransportHandler* transportHandler_;
};

}} // apache::thrift

#endif // THRIFT_ASYNC_CPP2CHANNEL_H_
