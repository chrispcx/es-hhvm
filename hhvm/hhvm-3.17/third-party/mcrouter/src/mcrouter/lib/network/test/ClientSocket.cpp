/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ClientSocket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <thread>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

ClientSocket::ClientSocket(uint16_t port) {
  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  auto portStr = folly::to<std::string>(port);
  auto ret = ::getaddrinfo("localhost", portStr.data(), &hints, &res);
  checkRuntime(!ret, "Failed to find a local IP: {}", ::gai_strerror(ret));
  SCOPE_EXIT {
    ::freeaddrinfo(res);
  };
  socketFd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (socketFd_ < 0) {
    throwRuntime("Failed to create a socket for port {}: {}",
                 port, folly::errnoStr(errno));
  }

  if (::connect(socketFd_, res->ai_addr, res->ai_addrlen) != 0) {
    auto errStr = folly::errnoStr(errno);
    ::close(socketFd_);
    throwRuntime("Failed to connect to port {}: {}", port, errStr);
  }
}

ClientSocket::ClientSocket(ClientSocket&& other) noexcept
    : socketFd_(other.socketFd_) {
  other.socketFd_ = -1;
}

ClientSocket& ClientSocket::operator=(ClientSocket&& other) noexcept {
  if (this != &other) {
    if (socketFd_ >= 0) {
      ::close(socketFd_);
    }
    socketFd_ = other.socketFd_;
    other.socketFd_ = -1;
  }
  return *this;
}

ClientSocket::~ClientSocket() {
  if (socketFd_ >= 0) {
    ::close(socketFd_);
  }
}

void ClientSocket::write(folly::StringPiece data,
                         std::chrono::milliseconds timeout) {
  auto tmo = to<timeval_t>(timeout);
  ::setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<char*>(&tmo), sizeof(timeval_t));

  ssize_t n = folly::writeFull(socketFd_, data.data(), data.size());
  if (n == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      throwRuntime("timeout writing to socket");
    }
    throwRuntime("failed to write to socket: {}", folly::errnoStr(errno));
  }

  checkRuntime(n == data.size(),
               "failed to write to socket. Written {}, expected {}",
               n, data.size());
}

std::string ClientSocket::sendRequest(folly::StringPiece request,
                                      size_t replySize,
                                      std::chrono::milliseconds timeout) {
  write(request, timeout);

  auto tmo = to<timeval_t>(timeout);
  ::setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&tmo), sizeof(timeval_t));

  std::vector<char> replyBuf(replySize + 1);
  ssize_t n = folly::readFull(socketFd_, replyBuf.data(), replySize);
  if (n == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      throwRuntime("timeout reading from socket");
    }
    throwRuntime("failed to read from socket: {}", folly::errnoStr(errno));
  } else if (n == 0) {
    throwRuntime("peer closed the socket");
  }
  checkRuntime(n == replySize,
               "failed to read from socket. Read {}, expected {}",
               n, replySize);
  return std::string(replyBuf.data(), n);
}

std::string ClientSocket::sendRequest(folly::StringPiece request,
                                      std::chrono::milliseconds timeout) {
  write(request, timeout);

  auto tmo = to<timeval_t>(timeout);
  ::setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&tmo), sizeof(timeval_t));

  const size_t maxReplySize = 1000000;
  std::vector<char> replyBuf(maxReplySize + 1);
  ssize_t n = folly::readFull(socketFd_, replyBuf.data(), maxReplySize);
  if (n == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      throwRuntime("timeout reading from socket");
    }
    throwRuntime("failed to read from socket: {}", folly::errnoStr(errno));
  }
  checkRuntime(n < maxReplySize,
               "the reply buffer may be too small because we used it up");
  return std::string(replyBuf.data(), n);
}

}}  // facebook::memcache
