/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Proxy.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>

#include <boost/regex.hpp>

#include <folly/DynamicConverter.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/Range.h>
#include <folly/ThreadName.h>

#include "mcrouter/async.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/MessageQueue.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/McrouterInstanceBase.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShardSplitter.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

folly::fibers::FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  fmOpts.useGuardPages = opts.fibers_use_guard_pages;
  fmOpts.fibersPoolResizePeriodMs = opts.fibers_pool_resize_period_ms;
  return fmOpts;
}

} // anonymous

namespace detail {

bool processGetServiceInfoRequest(
    const McGetRequest& req,
    std::shared_ptr<ProxyRequestContextTyped<
      McrouterRouteHandleIf, McGetRequest>>& ctx) {

  return processGetServiceInfoRequestImpl(req, ctx);
}

template <class GetRequest>
bool processGetServiceInfoRequestImpl(
    const GetRequest& req,
    std::shared_ptr<
        ProxyRequestContextTyped<McrouterRouteHandleIf, GetRequest>>& ctx,
    GetLikeT<GetRequest>) {
  static const char* const kInternalGetPrefix = "__mcrouter__.";

  if (!req.key().fullKey().startsWith(kInternalGetPrefix)) {
    return false;
  }
  auto& config = ctx->proxyConfig();
  auto key = req.key().fullKey();
  key.advance(strlen(kInternalGetPrefix));
  config.serviceInfo()->handleRequest(key, ctx);
  return true;
}

} // detail

Proxy::Proxy(McrouterInstanceBase& rtr, size_t id)
    : router_(rtr),
      destinationMap_(folly::make_unique<ProxyDestinationMap>(this)),
      fiberManager_(
          fiber_local::ContextTypeTag(),
          folly::make_unique<folly::fibers::EventBaseLoopController>(),
          getFiberManagerOptions(router_.opts())),
      id_(id) {
  // Setup a full random seed sequence
  folly::Random::seed(randomGenerator_);

  messageQueue_ = folly::make_unique<MessageQueue<ProxyMessage>>(
    router_.opts().client_queue_size,
    [this] (ProxyMessage&& message) {
      this->messageReady(message.type, message.data);
    },
    router_.opts().client_queue_no_notify_rate,
    router_.opts().client_queue_wait_threshold_us,
    &nowUs,
    [this] () {
      stats_.incrementSafe(client_queue_notifications_stat);
    }
  );

  statsContainer_ = folly::make_unique<ProxyStatsContainer>(*this);
}

Proxy::Pointer Proxy::createProxy(
    McrouterInstanceBase& router,
    folly::EventBase& eventBase,
    size_t id) {
  /* This hack is needed to make sure Proxy stays alive
     until at least event base managed to run the callback below */
  auto proxy = std::shared_ptr<Proxy>(new Proxy(router, id));
  proxy->self_ = proxy;

  eventBase.runInEventBaseThread(
    [proxy, &eventBase] () {
      proxy->eventBase_ = &eventBase;
      proxy->messageQueue_->attachEventBase(eventBase);

      dynamic_cast<folly::fibers::EventBaseLoopController&>(
        proxy->fiberManager_.loopController()).attachEventBase(eventBase);

      std::chrono::milliseconds connectionResetInterval{
        proxy->router_.opts().reset_inactive_connection_interval
      };

      if (connectionResetInterval.count() > 0) {
        proxy->destinationMap_->setResetTimer(connectionResetInterval);
      }

      if (proxy->router_.opts().cpu_cycles) {
        cycles::attachEventBase(eventBase);
        proxy->fiberManager_.setObserver(&proxy->cyclesObserver_);
      }
    });

  return Pointer(proxy.get());
}

std::shared_ptr<McrouterProxyConfig> Proxy::getConfig() const {
  std::lock_guard<SFRReadLock> lg(
    const_cast<SFRLock&>(configLock_).readLock());
  return config_;
}

std::pair<std::unique_lock<SFRReadLock>, McrouterProxyConfig&>
Proxy::getConfigLocked() const {
  std::unique_lock<SFRReadLock> lock(
    const_cast<SFRLock&>(configLock_).readLock());
  /* make_pair strips the reference, so construct directly */
  return std::pair<std::unique_lock<SFRReadLock>, McrouterProxyConfig&>(
    std::move(lock), *config_);
}

std::shared_ptr<McrouterProxyConfig> Proxy::swapConfig(
    std::shared_ptr<McrouterProxyConfig> newConfig) {
  std::lock_guard<SFRWriteLock> lg(configLock_.writeLock());
  auto old = std::move(config_);
  config_ = std::move(newConfig);
  return old;
}

/** drain and delete proxy object */
Proxy::~Proxy() {
  destinationMap_.reset();

  beingDestroyed_ = true;

  if (messageQueue_) {
    messageQueue_->drain();
  }
}

void Proxy::sendMessage(ProxyMessage::Type t, void* data) noexcept {
  CHECK(messageQueue_.get());
  messageQueue_->blockingWrite(t, data);
}

void Proxy::drainMessageQueue() {
  CHECK(messageQueue_.get());
  messageQueue_->drain();
}

size_t Proxy::queueNotifyPeriod() const {
  if (messageQueue_) {
    return messageQueue_->currentNotifyPeriod();
  }
  return 0;
}

void Proxy::messageReady(ProxyMessage::Type t, void* data) {
  switch (t) {
    case ProxyMessage::Type::REQUEST:
    {
      auto preq = reinterpret_cast<ProxyRequestContext*>(data);
      preq->startProcessing();
    }
    break;

    case ProxyMessage::Type::OLD_CONFIG:
    {
      auto oldConfig = reinterpret_cast<old_config_req_t*>(data);
      delete oldConfig;
    }
    break;

    case ProxyMessage::Type::SHUTDOWN:
      /*
       * No-op. We just wanted to wake this event base up so that
       * it can exit event loop and check router->shutdown
       */
      break;
  }
}

void Proxy::routeHandlesProcessRequest(
    const McStatsRequest& req,
    std::unique_ptr<
        ProxyRequestContextTyped<McrouterRouteHandleIf, McStatsRequest>> ctx) {
  ctx->sendReply(stats_reply(this, req.key().fullKey()));
}

void Proxy::routeHandlesProcessRequest(
    const McVersionRequest&,
    std::unique_ptr<
        ProxyRequestContextTyped<McrouterRouteHandleIf, McVersionRequest>>
        ctx) {
  McVersionReply reply(mc_res_ok);
  reply.value() =
      folly::IOBuf(folly::IOBuf::COPY_BUFFER, MCROUTER_PACKAGE_STRING);
  ctx->sendReply(std::move(reply));
}

void Proxy::pump() {
  auto numPriorities = static_cast<int>(ProxyRequestPriority::kNumPriorities);
  for (int i = 0; i < numPriorities; ++i) {
    auto& queue = waitingRequests_[i];
    while (numRequestsProcessing_ < router_.opts().proxy_max_inflight_requests
           && !queue.empty()) {
      --numRequestsWaiting_;
      auto w = queue.popFront();
      stats_.decrement(proxy_reqs_waiting_stat);

      w->process(this);
    }
  }
}

uint64_t Proxy::nextRequestId() {
  return ++nextReqId_;
}

const McrouterOptions& Proxy::getRouterOptions() const {
  return router_.opts();
}

std::shared_ptr<ShadowSettings> ShadowSettings::create(
    const folly::dynamic& json,
    McrouterInstanceBase& router) {
  auto result = std::shared_ptr<ShadowSettings>(new ShadowSettings());
  try {
    checkLogic(json.isObject(), "json is not an object");
    if (auto jKeyFractionRange = json.get_ptr("key_fraction_range")) {
      checkLogic(jKeyFractionRange->isArray(),
                 "key_fraction_range is not an array");
      auto ar = folly::convertTo<std::vector<double>>(*jKeyFractionRange);
      checkLogic(ar.size() == 2, "key_fraction_range size is not 2");
      result->setKeyRange(ar[0], ar[1]);
    }
    if (auto jIndexRange = json.get_ptr("index_range")) {
      checkLogic(jIndexRange->isArray(), "index_range is not an array");
      auto ar = folly::convertTo<std::vector<size_t>>(*jIndexRange);
      checkLogic(ar.size() == 2, "index_range size is not 2");
      checkLogic(ar[0] <= ar[1], "index_range start > end");
      result->startIndex_ = ar[0];
      result->endIndex_ = ar[1];
    }
    if (auto jKeyFractionRangeRv = json.get_ptr("key_fraction_range_rv")) {
      checkLogic(jKeyFractionRangeRv->isString(),
                 "key_fraction_range_rv is not a string");
      result->keyFractionRangeRv_ = jKeyFractionRangeRv->getString();
    }
    if (auto jValidateReplies = json.get_ptr("validate_replies")) {
      checkLogic(jValidateReplies->isBool(),
                 "validate_replies is not a bool");
      result->validateReplies_ = jValidateReplies->getBool();
    }
  } catch (const std::logic_error& e) {
    MC_LOG_FAILURE(router.opts(), failure::Category::kInvalidConfig,
                   "ShadowSettings: {}", e.what());
    return nullptr;
  }

  result->registerOnUpdateCallback(router);

  return result;
}

void ShadowSettings::setKeyRange(double start, double end) {
  checkLogic(0 <= start && start <= end && end <= 1,
             "invalid key_fraction_range [{}, {}]", start, end);
  uint64_t keyStart = start * std::numeric_limits<uint32_t>::max();
  uint64_t keyEnd = end * std::numeric_limits<uint32_t>::max();
  keyRange_ = (keyStart << 32UL) | keyEnd;
}

void ShadowSettings::setValidateReplies(bool validateReplies) {
  validateReplies_ = validateReplies;
}

ShadowSettings::~ShadowSettings() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

void ShadowSettings::registerOnUpdateCallback(McrouterInstanceBase& router) {
  handle_ = router.rtVarsData().subscribeAndCall(
    [this](std::shared_ptr<const RuntimeVarsData> oldVars,
           std::shared_ptr<const RuntimeVarsData> newVars) {
      if (!newVars || keyFractionRangeRv_.empty()) {
        return;
      }
      auto val = newVars->getVariableByName(keyFractionRangeRv_);
      if (val != nullptr) {
        checkLogic(val.isArray(),
                   "runtime vars: {} is not an array", keyFractionRangeRv_);
        checkLogic(val.size() == 2,
                   "runtime vars: size of {} is not 2", keyFractionRangeRv_);
        checkLogic(val[0].isNumber(),
                   "runtime vars: {}#0 is not a number", keyFractionRangeRv_);
        checkLogic(val[1].isNumber(),
                   "runtime vars: {}#1 is not a number", keyFractionRangeRv_);
        setKeyRange(val[0].asDouble(), val[1].asDouble());
      }
    });
}

void proxy_config_swap(
    Proxy* proxy,
    std::shared_ptr<McrouterProxyConfig> config) {
  auto oldConfig = proxy->swapConfig(std::move(config));
  proxy->stats().setValue(config_last_success_stat, time(nullptr));

  if (oldConfig) {
    auto configReq = new old_config_req_t(std::move(oldConfig));
    proxy->sendMessage(ProxyMessage::Type::OLD_CONFIG, configReq);
  }
}

}}} // facebook::memcache::mcrouter
