/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyDestination.h"

#include <limits>
#include <random>

#include <folly/fibers/Fiber.h>
#include <folly/Memory.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/ThreadLocalSSLContextProvider.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/OptionsUtil.h"
#include "mcrouter/routes/DestinationRoute.h"
#include "mcrouter/stats.h"
#include "mcrouter/TkoTracker.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

constexpr double kProbeExponentialFactor = 1.5;
constexpr double kProbeJitterMin = 0.05;
constexpr double kProbeJitterMax = 0.5;
constexpr double kProbeJitterDelta = kProbeJitterMax - kProbeJitterMin;
// Jitters for closing rxmiting connections will be between 1 and
// kReconnectionHoldoffFactor.
constexpr uint32_t kReconnectionHoldoffFactor = 25;

static_assert(kProbeJitterMax >= kProbeJitterMin,
              "ProbeJitterMax should be greater or equal tham ProbeJitterMin");

stat_name_t getStatName(ProxyDestination::State st) {
  switch (st) {
    case ProxyDestination::State::kNew:
      return num_servers_new_stat;
    case ProxyDestination::State::kUp:
      return num_servers_up_stat;
    case ProxyDestination::State::kClosed:
      return num_servers_closed_stat;
    case ProxyDestination::State::kDown:
      return num_servers_down_stat;
    case ProxyDestination::State::kNumStates:
      CHECK(false);
  }
  return num_stats; // shouldn't reach here
}

}  // anonymous namespace

void ProxyDestination::schedule_next_probe() {
  assert(!proxy->router().opts().disable_tko_tracking);

  int delay_ms = probe_delay_next_ms;
  if (probe_delay_next_ms < 2) {
    // int(1 * 1.5) == 1, so advance it to 2 first
    probe_delay_next_ms = 2;
  } else {
    probe_delay_next_ms *= kProbeExponentialFactor;
  }
  if (probe_delay_next_ms > proxy->router().opts().probe_delay_max_ms) {
    probe_delay_next_ms = proxy->router().opts().probe_delay_max_ms;
  }

  // Calculate random jitter
  double r = (double)rand() / (double)RAND_MAX;
  double tmo_jitter_pct = r * kProbeJitterDelta + kProbeJitterMin;
  delay_ms = (double)delay_ms * (1.0 + tmo_jitter_pct);
  assert(delay_ms > 0);

  if (!probeTimer_.scheduleTimeout(delay_ms)) {
    MC_LOG_FAILURE(proxy->router().opts(),
                   memcache::failure::Category::kSystemError,
                   "failed to schedule probe timer for ProxyDestination");
  }
}

void ProxyDestination::timerCallback() {
  // Note that the previous probe might still be in flight
  if (!probe_req) {
    probe_req = folly::make_unique<McVersionRequest>();
    ++stats_.probesSent;
    auto selfPtr = selfPtr_;
    proxy->fiberManager().addTask([selfPtr]() mutable {
      auto pdstn = selfPtr.lock();
      if (pdstn == nullptr) {
        return;
      }
      pdstn->proxy->destinationMap()->markAsActive(*pdstn);
      // will reconnect if connection was closed
      auto reply = pdstn->getAsyncMcClient().sendSync(
        *pdstn->probe_req,
        pdstn->shortestTimeout_);
      pdstn->handle_tko(reply.result(), true);
      pdstn->probe_req.reset();
    });
  }
  schedule_next_probe();
}

void ProxyDestination::start_sending_probes() {
  probe_delay_next_ms = proxy->router().opts().probe_delay_initial_ms;
  probeTimer_.attachEventBase(std::addressof(proxy->eventBase()));
  schedule_next_probe();
}

void ProxyDestination::stop_sending_probes() {
  stats_.probesSent = 0;

  // Cancel timeout before calling detachEventBase to prevent an assert failure.
  probeTimer_.cancelTimeout();

  // Need to detach event base here. Otherwise attachEventBase on next call to
  // start_sending_probes will assert(false).
  probeTimer_.detachEventBase();
}

void ProxyDestination::handle_tko(const mc_res_t result, bool is_probe_req) {
  if (proxy->router().opts().disable_tko_tracking) {
    return;
  }

  if (isErrorResult(result)) {
    if (isHardTkoErrorResult(result)) {
      if (tracker->recordHardFailure(this)) {
        onTkoEvent(TkoLogEvent::MarkHardTko, result);
        start_sending_probes();
      }
    } else if (isSoftTkoErrorResult(result)) {
      if (tracker->recordSoftFailure(this)) {
        onTkoEvent(TkoLogEvent::MarkSoftTko, result);
        start_sending_probes();
      }
    }
    return;
  }

  if (tracker->isTko()) {
    if (is_probe_req && tracker->recordSuccess(this)) {
      onTkoEvent(TkoLogEvent::UnMarkTko, result);
      stop_sending_probes();
    }
    return;
  }

  tracker->recordSuccess(this);
}

void ProxyDestination::handleRxmittingConnection() {
  if (!client_) {
    return;
  }
  const auto retransCycles =
      proxy->router().opts().collect_rxmit_stats_every_hz;
  if (retransCycles > 0) {
    const auto curCycles = cycles::getCpuCycles();
    if (curCycles > lastRetransCycles_ + retransCycles) {
      lastRetransCycles_ = curCycles;
      const auto currRetransPerKByte = client_->getRetransmissionInfo();
      if (currRetransPerKByte >= 0.0) {
        stats_.retransPerKByte = currRetransPerKByte;
        proxy->stats().setValue(
            retrans_per_kbyte_max_stat,
            std::max(
                proxy->stats().getValue(retrans_per_kbyte_max_stat),
                static_cast<uint64_t>(currRetransPerKByte)));
        proxy->stats().increment(
            retrans_per_kbyte_sum_stat,
            static_cast<int64_t>(currRetransPerKByte));
        proxy->stats().increment(retrans_num_total_stat);
      }

      if (proxy->router().isRxmitReconnectionDisabled()) {
        return;
      }

      if (rxmitsToCloseConnection_ > 0 &&
          currRetransPerKByte >= rxmitsToCloseConnection_) {
        std::uniform_int_distribution<uint64_t> dist(
            1, kReconnectionHoldoffFactor);
        const uint64_t reconnectionJitters =
            retransCycles * dist(proxy->randomGenerator());
        if (lastConnCloseCycles_ + reconnectionJitters > curCycles) {
          return;
        }
        client_->closeNow();
        proxy->stats().increment(retrans_closed_connections_stat);
        lastConnCloseCycles_ = curCycles;

        const auto maxThreshold =
            proxy->router().opts().max_rxmit_reconnect_threshold;
        const uint64_t maxRxmitReconnThreshold = maxThreshold == 0
            ? std::numeric_limits<uint64_t>::max()
            : maxThreshold;
        rxmitsToCloseConnection_ =
            std::min(maxRxmitReconnThreshold, 2 * rxmitsToCloseConnection_);
      } else if (3 * currRetransPerKByte < rxmitsToCloseConnection_) {
        const auto minThreshold =
            proxy->router().opts().min_rxmit_reconnect_threshold;
        rxmitsToCloseConnection_ =
            std::max(minThreshold, rxmitsToCloseConnection_ / 2);
      }
    }
  }
}

void ProxyDestination::onReply(
    const mc_res_t result,
    DestinationRequestCtx& destreqCtx) {
  handle_tko(result, false);

  if (!stats_.results) {
    stats_.results = folly::make_unique<std::array<uint64_t, mc_nres>>();
  }
  ++(*stats_.results)[result];
  destreqCtx.endTime = nowUs();

  int64_t latency = destreqCtx.endTime - destreqCtx.startTime;
  stats_.avgLatency.insertSample(latency);

  handleRxmittingConnection();
}

size_t ProxyDestination::getPendingRequestCount() const {
  folly::SpinLockGuard g(clientLock_);
  return client_ ? client_->getPendingRequestCount() : 0;
}

size_t ProxyDestination::getInflightRequestCount() const {
  folly::SpinLockGuard g(clientLock_);
  return client_ ? client_->getInflightRequestCount() : 0;
}

std::shared_ptr<ProxyDestination> ProxyDestination::create(
    Proxy& proxy,
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath) {
  std::shared_ptr<ProxyDestination> ptr(new ProxyDestination(
    proxy,
    std::move(ap),
    timeout,
    qosClass,
    qosPath));
  ptr->selfPtr_ = ptr;
  return ptr;
}

ProxyDestination::~ProxyDestination() {
  if (tracker->removeDestination(this)) {
    onTkoEvent(TkoLogEvent::RemoveFromConfig, mc_res_ok);
    stop_sending_probes();
  }

  if (proxy->destinationMap()) {
    // Only remove if we are not shutting down Proxy.
    proxy->destinationMap()->removeDestination(*this);
  }

  if (client_) {
    client_->setStatusCallbacks(nullptr, nullptr);
    client_->closeNow();
  }

  proxy->stats().decrement(getStatName(stats_.state));
  proxy->stats().decrement(num_servers_stat);
  magic_ = kDeadBeef;
}

ProxyDestination::ProxyDestination(
    Proxy& proxy_,
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath)
    : proxy(&proxy_),
      accessPoint_(std::move(ap)),
      shortestTimeout_(timeout),
      qosClass_(qosClass),
      qosPath_(qosPath),
      rxmitsToCloseConnection_(
          proxy->router().opts().min_rxmit_reconnect_threshold),
      probeTimer_(*this) {
  static uint64_t next_magic = 0x12345678900000LL;
  magic_ = __sync_fetch_and_add(&next_magic, 1);
  proxy->stats().increment(num_servers_new_stat);
  proxy->stats().increment(num_servers_stat);
}

bool ProxyDestination::may_send() const {
  return !tracker->isTko();
}

void ProxyDestination::resetInactive() {
  // No need to reset non-existing client.
  if (client_) {
    std::unique_ptr<AsyncMcClient> client;
    {
      folly::SpinLockGuard g(clientLock_);
      client = std::move(client_);
    }
    client->closeNow();
  }
}

void ProxyDestination::initializeAsyncMcClient() {
  assert(!client_);

  ConnectionOptions options(accessPoint_);
  auto& opts = proxy->router().opts();
  options.noNetwork = opts.no_network;
  options.tcpKeepAliveCount = opts.keepalive_cnt;
  options.tcpKeepAliveIdle = opts.keepalive_idle_s;
  options.tcpKeepAliveInterval = opts.keepalive_interval_s;
  options.writeTimeout = shortestTimeout_;
  options.sessionCachingEnabled = opts.ssl_connection_cache;
  if (!opts.debug_fifo_root.empty()) {
    options.debugFifoPath = getClientDebugFifoFullPath(opts);
  }
  if (opts.enable_qos) {
    options.enableQoS = true;
    options.qosClass = qosClass_;
    options.qosPath = qosPath_;
  }
  options.useJemallocNodumpAllocator = opts.jemalloc_nodump_buffers;
  if (accessPoint_->compressed()) {
    if (auto codecManager = proxy->router().getCodecManager()) {
      options.compressionCodecMap = codecManager->getCodecMap();
    }
  }

  if (accessPoint_->useSsl()) {
    checkLogic(!opts.pem_cert_path.empty() &&
               !opts.pem_key_path.empty() &&
               !opts.pem_ca_path.empty(),
               "Some of ssl key paths are not set!");
    options.sslContextProvider = [&opts] {
      return getSSLContext(opts.pem_cert_path, opts.pem_key_path,
                           opts.pem_ca_path);
    };
  }

  auto client = folly::make_unique<AsyncMcClient>(proxy->eventBase(),
                                                  std::move(options));
  {
    folly::SpinLockGuard g(clientLock_);
    client_ = std::move(client);
  }

  client_->setRequestStatusCallbacks(
    [this](int pending, int inflight) {
      if (pending != 0) {
        proxy->stats().increment(destination_pending_reqs_stat, pending);
        proxy->stats().setValue(
            destination_max_pending_reqs_stat,
            std::max(
                proxy->stats().getValue(destination_max_pending_reqs_stat),
                proxy->stats().getValue(destination_pending_reqs_stat)));
      }
      if (inflight != 0) {
        proxy->stats().increment(destination_inflight_reqs_stat, inflight);
        proxy->stats().setValue(
            destination_max_inflight_reqs_stat,
            std::max(
                proxy->stats().getValue(destination_max_inflight_reqs_stat),
                proxy->stats().getValue(destination_inflight_reqs_stat)));
      }
    },
    [this](size_t numToSend) {
      proxy->stats().increment(destination_batches_sum_stat);
      proxy->stats().increment(destination_requests_sum_stat, numToSend);
    });

  client_->setStatusCallbacks(
    [this] () mutable {
      setState(State::kUp);
    },
    [this] (bool aborting) mutable {
      if (aborting) {
        setState(State::kClosed);
      } else {
        setState(State::kDown);
        handle_tko(mc_res_connect_error, /* is_probe_req= */ false);
      }
    });

  client_->setReplyStatsCallback([proxy = proxy, accessPoint = accessPoint_](
      ReplyStatsContext replyStatsContext) {
    if (accessPoint->compressed()) {
      if (replyStatsContext.usedCodecId > 0) {
        proxy->stats().increment(replies_compressed_stat);
      } else {
        proxy->stats().increment(replies_not_compressed_stat);
      }
      proxy->stats().increment(
          reply_traffic_before_compression_stat,
          replyStatsContext.replySizeBeforeCompression);
      proxy->stats().increment(
          reply_traffic_after_compression_stat,
          replyStatsContext.replySizeAfterCompression);
    }
    // For Scuba logging
    fiber_local::setReplyStatsContext(replyStatsContext);
  });

  if (opts.target_max_inflight_requests > 0) {
    client_->setThrottle(opts.target_max_inflight_requests,
                         opts.target_max_pending_requests);
  }
}

AsyncMcClient& ProxyDestination::getAsyncMcClient() {
  if (!client_) {
    initializeAsyncMcClient();
  }
  return *client_;
}

void ProxyDestination::onTkoEvent(TkoLogEvent event, mc_res_t result) const {
  auto logUtil = [this, result](folly::StringPiece eventStr) {
    VLOG(1) << accessPoint_->toHostPortString() << " (" << poolName_ << ") "
            << eventStr << ". Total hard TKOs: "
            << tracker->globalTkos().hardTkos << "; soft TKOs: "
            << tracker->globalTkos().softTkos << ". Reply: "
            << mc_res_to_string(result);
  };

  switch (event) {
    case TkoLogEvent::MarkHardTko:
      logUtil("marked hard TKO");
      break;
    case TkoLogEvent::MarkSoftTko:
      logUtil("marked soft TKO");
      break;
    case TkoLogEvent::UnMarkTko:
      logUtil("unmarked TKO");
      break;
    case TkoLogEvent::RemoveFromConfig:
      logUtil("was TKO, removed from config");
      break;
  }

  TkoLog tkoLog(*accessPoint_, tracker->globalTkos());
  tkoLog.event = event;
  tkoLog.isHardTko = tracker->isHardTko();
  tkoLog.isSoftTko = tracker->isSoftTko();
  tkoLog.avgLatency = stats_.avgLatency.value();
  tkoLog.probesSent = stats_.probesSent;
  tkoLog.poolName = poolName_;
  tkoLog.result = result;

  logTkoEvent(*proxy, tkoLog);
}

void ProxyDestination::setState(State new_st) {
  if (stats_.state == new_st) {
    return;
  }

  auto logUtil = [this](const char* s) {
    VLOG(1) << "server " << pdstnKey_ << " " << s << " ("
            << proxy->stats().getValue(num_servers_up_stat) << " of "
            << proxy->stats().getValue(num_servers_stat) << ")";
  };

  auto old_name = getStatName(stats_.state);
  auto new_name = getStatName(new_st);
  proxy->stats().decrement(old_name);
  proxy->stats().increment(new_name);
  stats_.state = new_st;

  switch (stats_.state) {
    case State::kUp:
      logUtil("up");
      break;
    case State::kClosed:
      logUtil("closed");
      break;
    case State::kDown:
      logUtil("down");
      break;
    case State::kNew:
    case State::kNumStates:
      assert(false);
      break;
  }
}

void ProxyDestination::updateShortestTimeout(
    std::chrono::milliseconds timeout) {
  if (!timeout.count()) {
    return;
  }
  if (shortestTimeout_.count() == 0 || shortestTimeout_ > timeout) {
    shortestTimeout_ = timeout;
    if (client_) {
      client_->updateWriteTimeout(shortestTimeout_);
    }
  }
}

}}}  // facebook::memcache::mcrouter
