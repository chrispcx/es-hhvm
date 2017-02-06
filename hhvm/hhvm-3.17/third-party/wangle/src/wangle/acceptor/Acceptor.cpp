/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/Acceptor.h>

#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/ssl/SSLContextManager.h>
#include <wangle/acceptor/AcceptorHandshakeManager.h>
#include <wangle/acceptor/SecurityProtocolContextManager.h>
#include <fcntl.h>
#include <folly/ScopeGuard.h>
#include <folly/io/async/EventBase.h>
#include <fstream>
#include <sys/types.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>
#include <gflags/gflags.h>

using folly::AsyncSocket;
using folly::AsyncSSLSocket;
using folly::AsyncSocketException;
using folly::AsyncServerSocket;
using folly::AsyncTransportWrapper;
using folly::EventBase;
using folly::SocketAddress;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::filebuf;
using std::ifstream;
using std::ios;
using std::shared_ptr;
using std::string;

namespace wangle {

static const std::string empty_string;
std::atomic<uint64_t> Acceptor::totalNumPendingSSLConns_{0};

Acceptor::Acceptor(const ServerSocketConfig& accConfig) :
  accConfig_(accConfig),
  socketOptions_(accConfig.getSocketOptions()) {
}

void
Acceptor::init(AsyncServerSocket* serverSocket,
               EventBase* eventBase,
               SSLStats* stats) {
  CHECK(nullptr == this->base_ || eventBase == this->base_);

  if (accConfig_.isSSL()) {
    if (accConfig_.allowInsecureConnectionsOnSecureServer) {
      securityProtocolCtxManager_.addPeeker(&tlsPlaintextPeekingCallback_);
    }
    securityProtocolCtxManager_.addPeeker(&defaultPeekingCallback_);

    if (!sslCtxManager_) {
      sslCtxManager_ = folly::make_unique<SSLContextManager>(
        eventBase,
        "vip_" + getName(),
        accConfig_.strictSSL, stats);
    }
    try {
      for (const auto& sslCtxConfig : accConfig_.sslContextConfigs) {
        sslCtxManager_->addSSLContextConfig(
          sslCtxConfig,
          accConfig_.sslCacheOptions,
          &accConfig_.initialTicketSeeds,
          accConfig_.bindAddress,
          cacheProvider_);
        parseClientHello_ |= sslCtxConfig.clientHelloParsingEnabled;
      }

      CHECK(sslCtxManager_->getDefaultSSLCtx());
    } catch (const std::runtime_error& ex) {
      sslCtxManager_->clear();
      // This is not a Not a fatal error, but useful to know.
      LOG(INFO) << "Failed to configure TLS. This is not a fatal error. "
                << ex.what();
    }
  }
  base_ = eventBase;
  state_ = State::kRunning;
  downstreamConnectionManager_ = ConnectionManager::makeUnique(
    eventBase, accConfig_.connectionIdleTimeout, this);

  if (serverSocket) {
    serverSocket->addAcceptCallback(this, eventBase);

    for (auto& fd : serverSocket->getSockets()) {
      if (fd < 0) {
        continue;
      }
      for (const auto& opt: socketOptions_) {
        opt.first.apply(fd, opt.second);
      }
    }
  }
}

void Acceptor::resetSSLContextConfigs() {
  try {
    sslCtxManager_->resetSSLContextConfigs(accConfig_.sslContextConfigs,
                                           accConfig_.sslCacheOptions,
                                           nullptr,
                                           accConfig_.bindAddress,
                                           cacheProvider_);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << "Failed to re-configure TLS: "
               << ex.what()
               << "will keep old config";
  }

}

Acceptor::~Acceptor(void) {
}

void Acceptor::addSSLContextConfig(const SSLContextConfig& sslCtxConfig) {
  sslCtxManager_->addSSLContextConfig(sslCtxConfig,
                                      accConfig_.sslCacheOptions,
                                      &accConfig_.initialTicketSeeds,
                                      accConfig_.bindAddress,
                                      cacheProvider_);
}

void
Acceptor::drainAllConnections() {
  if (downstreamConnectionManager_) {
    downstreamConnectionManager_->initiateGracefulShutdown(
      gracefulShutdownTimeout_);
  }
}

void Acceptor::setLoadShedConfig(const LoadShedConfiguration& from,
                       IConnectionCounter* counter) {
  loadShedConfig_ = from;
  connectionCounter_ = counter;
}

bool Acceptor::canAccept(const SocketAddress& address) {
  if (!connectionCounter_) {
    return true;
  }

  uint64_t maxConnections = connectionCounter_->getMaxConnections();
  if (maxConnections == 0) {
    return true;
  }

  uint64_t currentConnections = connectionCounter_->getNumConnections();
  if (currentConnections < maxConnections) {
    return true;
  }

  if (loadShedConfig_.isWhitelisted(address)) {
    return true;
  }

  // Take care of the connection counts across all acceptors.
  // Expensive since a lock must be taken to get the counter.
  const auto activeConnLimit = loadShedConfig_.getMaxActiveConnections();
  const auto totalConnLimit = loadShedConfig_.getMaxConnections();
  const auto activeConnCount = getActiveConnectionCountForLoadShedding();
  const auto totalConnCount = getConnectionCountForLoadShedding();

  bool activeConnExceeded =
      (activeConnLimit > 0) && (activeConnCount >= activeConnLimit);
  bool totalConnExceeded =
      (totalConnLimit > 0) && (totalConnCount >= totalConnLimit);
  if (!activeConnExceeded && !totalConnExceeded) {
    return true;
  }

  VLOG(4) << address.describe() << " not whitelisted";
  return false;
}

void
Acceptor::connectionAccepted(
    int fd, const SocketAddress& clientAddr) noexcept {
  if (!canAccept(clientAddr)) {
    // Send a RST to free kernel memory faster
    struct linger optLinger = {1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    close(fd);
    return;
  }
  auto acceptTime = std::chrono::steady_clock::now();
  for (const auto& opt: socketOptions_) {
    opt.first.apply(fd, opt.second);
  }

  onDoneAcceptingConnection(fd, clientAddr, acceptTime);
}

void Acceptor::onDoneAcceptingConnection(
    int fd,
    const SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime) noexcept {
  TransportInfo tinfo;
  processEstablishedConnection(fd, clientAddr, acceptTime, tinfo);
}

void
Acceptor::processEstablishedConnection(
    int fd,
    const SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo) noexcept {
  bool shouldDoSSL = false;
  if (accConfig_.isSSL()) {
    CHECK(sslCtxManager_);
    shouldDoSSL = sslCtxManager_->getDefaultSSLCtx() != nullptr;
  }
  if (shouldDoSSL) {
    AsyncSSLSocket::UniquePtr sslSock(
      makeNewAsyncSSLSocket(
        sslCtxManager_->getDefaultSSLCtx(), base_, fd));
    ++numPendingSSLConns_;
    ++totalNumPendingSSLConns_;
    if (numPendingSSLConns_ > accConfig_.maxConcurrentSSLHandshakes) {
      VLOG(2) << "dropped SSL handshake on " << accConfig_.name <<
        " too many handshakes in progress";
      auto error = SSLErrorEnum::DROPPED;
      auto latency = std::chrono::milliseconds(0);
      updateSSLStats(sslSock.get(), latency, error);
      auto ex = folly::make_exception_wrapper<SSLException>(
          error, latency, sslSock->getRawBytesReceived());
      sslConnectionError(ex);
      return;
    }

    tinfo.tfoSucceded = sslSock->getTFOSucceded();
    startHandshakeManager(
        std::move(sslSock),
        this,
        clientAddr,
        acceptTime,
        tinfo);
  } else {
    tinfo.secure = false;
    tinfo.acceptTime = acceptTime;
    AsyncSocket::UniquePtr sock(makeNewAsyncSocket(base_, fd));
    tinfo.tfoSucceded = sock->getTFOSucceded();
    plaintextConnectionReady(
        std::move(sock),
        clientAddr,
        empty_string,
        SecureTransportType::NONE,
        tinfo);
  }
}

void Acceptor::startHandshakeManager(
    AsyncSSLSocket::UniquePtr sslSock,
    Acceptor* acceptor,
    const SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo) noexcept {
  auto manager = securityProtocolCtxManager_.getHandshakeManager(
      this, clientAddr, acceptTime, tinfo);
  manager->start(std::move(sslSock));
}

void
Acceptor::connectionReady(
    AsyncTransportWrapper::UniquePtr sock,
    const SocketAddress& clientAddr,
    const string& nextProtocolName,
    SecureTransportType secureTransportType,
    TransportInfo& tinfo) {
  // Limit the number of reads from the socket per poll loop iteration,
  // both to keep memory usage under control and to prevent one fast-
  // writing client from starving other connections.
  auto asyncSocket = sock->getUnderlyingTransport<AsyncSocket>();
  asyncSocket->setMaxReadsPerEvent(16);
  tinfo.initWithSocket(asyncSocket);
  if (state_ < State::kDraining) {
    onNewConnection(
      std::move(sock),
      &clientAddr,
      nextProtocolName,
      secureTransportType,
      tinfo);
  }
}

void Acceptor::plaintextConnectionReady(
    AsyncTransportWrapper::UniquePtr sock,
    const SocketAddress& clientAddr,
    const string& nextProtocolName,
    SecureTransportType secureTransportType,
    TransportInfo& tinfo) {
  connectionReady(
      std::move(sock),
      clientAddr,
      nextProtocolName,
      secureTransportType,
      tinfo);
}

void
Acceptor::sslConnectionReady(AsyncTransportWrapper::UniquePtr sock,
                             const SocketAddress& clientAddr,
                             const string& nextProtocol,
                             SecureTransportType secureTransportType,
                             TransportInfo& tinfo) {
  CHECK(numPendingSSLConns_ > 0);
  --numPendingSSLConns_;
  --totalNumPendingSSLConns_;
  connectionReady(
      std::move(sock),
      clientAddr,
      nextProtocol,
      secureTransportType,
      tinfo);
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void Acceptor::sslConnectionError(const folly::exception_wrapper& ex) {
  CHECK(numPendingSSLConns_ > 0);
  --numPendingSSLConns_;
  --totalNumPendingSSLConns_;
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void
Acceptor::acceptError(const std::exception& ex) noexcept {
  // An error occurred.
  // The most likely error is out of FDs.  AsyncServerSocket will back off
  // briefly if we are out of FDs, then continue accepting later.
  // Just log a message here.
  LOG(ERROR) << "error accepting on acceptor socket: " << ex.what();
}

void
Acceptor::acceptStopped() noexcept {
  VLOG(3) << "Acceptor " << this << " acceptStopped()";
  // Drain the open client connections
  drainAllConnections();

  // If we haven't yet finished draining, begin doing so by marking ourselves
  // as in the draining state. We must be sure to hit checkDrained() here, as
  // if we're completely idle, we can should consider ourself drained
  // immediately (as there is no outstanding work to complete to cause us to
  // re-evaluate this).
  if (state_ != State::kDone) {
    state_ = State::kDraining;
    checkDrained();
  }
}

void
Acceptor::onEmpty(const ConnectionManager& cm) {
  VLOG(3) << "Acceptor=" << this << " onEmpty()";
  if (state_ == State::kDraining) {
    checkDrained();
  }
}

void
Acceptor::checkDrained() {
  CHECK(state_ == State::kDraining);
  if (forceShutdownInProgress_ ||
      (downstreamConnectionManager_->getNumConnections() != 0) ||
      (numPendingSSLConns_ != 0)) {
    return;
  }

  VLOG(2) << "All connections drained from Acceptor=" << this << " in thread "
          << base_;

  downstreamConnectionManager_.reset();

  state_ = State::kDone;

  onConnectionsDrained();
}

void
Acceptor::drainConnections(double pctToDrain) {
  if (downstreamConnectionManager_) {
    LOG(INFO) << "Draining " << pctToDrain * 100 << "% of "
              << getNumConnections() << " connections from Acceptor=" << this
              << " in thread " << base_;
    assert(base_->isInEventBaseThread());
    downstreamConnectionManager_->
      drainConnections(pctToDrain, gracefulShutdownTimeout_);
  }
}

milliseconds
Acceptor::getConnTimeout() const {
  return accConfig_.connectionIdleTimeout;
}

void Acceptor::addConnection(ManagedConnection* conn) {
  // Add the socket to the timeout manager so that it can be cleaned
  // up after being left idle for a long time.
  downstreamConnectionManager_->addConnection(conn, true);
}

void
Acceptor::forceStop() {
  base_->runInEventBaseThread([&] { dropAllConnections(); });
}

void
Acceptor::dropAllConnections() {
  if (downstreamConnectionManager_) {
    LOG(INFO) << "Dropping all connections from Acceptor=" << this
              << " in thread " << base_;
    assert(base_->isInEventBaseThread());
    forceShutdownInProgress_ = true;
    downstreamConnectionManager_->dropAllConnections();
    CHECK(downstreamConnectionManager_->getNumConnections() == 0);
    downstreamConnectionManager_.reset();
  }
  CHECK(numPendingSSLConns_ == 0);

  state_ = State::kDone;
  onConnectionsDrained();
}

void
Acceptor::dropConnections(double pctToDrop) {
  base_->runInEventBaseThread([&, pctToDrop] {
    if (downstreamConnectionManager_) {
      LOG(INFO) << "Dropping " << pctToDrop * 100 << "% of "
                << getNumConnections() << " connections from Acceptor=" << this
                << " in thread " << base_;
      assert(base_->isInEventBaseThread());
      forceShutdownInProgress_ = true;
      downstreamConnectionManager_->dropConnections(pctToDrop);
    }
  });
}

} // namespace wangle
