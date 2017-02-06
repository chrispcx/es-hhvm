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

#include <folly/ThreadLocal.h>
#include <folly/futures/SharedPromise.h>
#include <folly/io/async/DelayedDestruction.h>
#include <wangle/bootstrap/BaseClientBootstrap.h>
#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/broadcast/BroadcastHandler.h>

namespace wangle {

template <typename R, typename P = DefaultPipeline>
class ServerPool {
 public:
  virtual ~ServerPool() {}

  /**
   * Kick off an upstream connect request given the BaseClientBootstrap
   * when a broadcast is not available locally.
   */
  virtual folly::Future<P*> connect(
      BaseClientBootstrap<P>* client,
      const R& routingData) noexcept = 0;
};

/**
 * A pool of upstream broadcast pipelines. There is atmost one broadcast
 * for any unique routing data. Creates and maintains upstream connections
 * and broadcast pipeliens as necessary.
 *
 * Meant to be used as a thread-local instance.
 */
template <typename T, typename R, typename P = DefaultPipeline>
class BroadcastPool {
 public:
  class BroadcastManager : public PipelineManager,
                           public folly::DelayedDestruction {
   public:
    using UniquePtr = std::unique_ptr<
      BroadcastManager, folly::DelayedDestruction::Destructor>;

    BroadcastManager(
        BroadcastPool<T, R, P>* broadcastPool,
        const R& routingData)
        : broadcastPool_(broadcastPool),
          routingData_(routingData),
          client_(broadcastPool_->clientBootstrapFactory_->newClient()) {
      client_->pipelineFactory(broadcastPool_->broadcastPipelineFactory_);
    }

    virtual ~BroadcastManager() {
      if (client_->getPipeline()) {
        client_->getPipeline()->setPipelineManager(nullptr);
      }
    }

    folly::Future<BroadcastHandler<T, R>*> getHandler();

    // PipelineManager implementation
    void deletePipeline(PipelineBase* pipeline) override;

   private:
    void handleConnectError(const std::exception& ex) noexcept;

    BroadcastPool<T, R, P>* broadcastPool_{nullptr};
    R routingData_;

    std::unique_ptr<BaseClientBootstrap<P>> client_;

    bool connectStarted_{false};
    bool deletingBroadcast_{false};
    folly::SharedPromise<BroadcastHandler<T, R>*> sharedPromise_;
  };

  BroadcastPool(
      std::shared_ptr<ServerPool<R, P>> serverPool,
      std::shared_ptr<BroadcastPipelineFactory<T, R>> pipelineFactory,
      std::shared_ptr<BaseClientBootstrapFactory<>> clientFactory =
          std::make_shared<ClientBootstrapFactory>())
      : serverPool_(serverPool),
        broadcastPipelineFactory_(pipelineFactory),
        clientBootstrapFactory_(clientFactory) {}

  virtual ~BroadcastPool() {}

  // Non-copyable
  BroadcastPool(const BroadcastPool&) = delete;
  BroadcastPool& operator=(const BroadcastPool&) = delete;

  // Movable
  BroadcastPool(BroadcastPool&&) = default;
  BroadcastPool& operator=(BroadcastPool&&) = default;

  /**
   * Gets the BroadcastHandler, or creates one if it doesn't exist already,
   * for the given routingData.
   *
   * If a broadcast is already available for the given routingData,
   * returns the BroadcastHandler from the pipeline. If not, an upstream
   * connection is created and stored along with a new broadcast pipeline
   * for this routingData, and its BroadcastHandler is returned.
   *
   * Caller should immediately subscribe to the returned BroadcastHandler
   * to prevent it from being garbage collected.
   */
  virtual folly::Future<BroadcastHandler<T, R>*> getHandler(
      const R& routingData);

  /**
   * Checks if a broadcast is available locally for the given routingData.
   */
  bool isBroadcasting(const R& routingData) {
    return (broadcasts_.find(routingData) != broadcasts_.end());
  }

  virtual void deleteBroadcast(const R& routingData) {
    broadcasts_.erase(routingData);
  }

 private:
  std::shared_ptr<ServerPool<R, P>> serverPool_;
  std::shared_ptr<BroadcastPipelineFactory<T, R>> broadcastPipelineFactory_;
  std::shared_ptr<BaseClientBootstrapFactory<>> clientBootstrapFactory_;
  std::map<R, typename BroadcastManager::UniquePtr> broadcasts_;
};

} // namespace wangle

#include <wangle/channel/broadcast/BroadcastPool-inl.h>
