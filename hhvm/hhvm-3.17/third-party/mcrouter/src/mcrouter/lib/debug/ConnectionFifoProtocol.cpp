/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ConnectionFifoProtocol.h"

#include <folly/Format.h>

namespace facebook {
namespace memcache {

bool MessageHeader::isUnixDomainSocket() const {
  return folly::StringPiece(peerAddress(), kAddressMaxSize)
      .startsWith(kUnixSocketPrefix);
}

folly::SocketAddress MessageHeader::getLocalAddress() {
  folly::SocketAddress address;

  if (version() < 2) {
    return address;
  }

  try {
    if (isUnixDomainSocket()) {
      address = getPeerAddress();
    } else {
      address.setFromLocalPort(localPort());
    }
  } catch (const std::exception& ex) {
    VLOG(2) << "Error parsing address: " << ex.what();
  }

  return address;
}

folly::SocketAddress MessageHeader::getPeerAddress() {
  folly::SocketAddress address;

  if (peerAddress()[0] == '\0') {
    return address;
  }

  try {
    if (isUnixDomainSocket()) {
      auto sp = folly::StringPiece(peerAddress(), kAddressMaxSize);
      sp.removePrefix(kUnixSocketPrefix);
      if (!sp.empty()) {
        address.setFromPath(sp);
      }
    } else {
      address.setFromIpPort(peerAddress(), peerPort());
    }
  } catch (const std::exception& ex) {
    VLOG(2) << "Error parsing address: " << ex.what();
  }

  return address;
}

/* static */ size_t MessageHeader::size(uint8_t v) {
  switch (v) {
    case 1:
      return sizeof(MessageHeader) - sizeof(localPort_) - sizeof(direction_) -
          sizeof(typeId_) - sizeof(timeUs_);
    case 2:
      return sizeof(MessageHeader) - sizeof(typeId_) - sizeof(timeUs_);
    case 3:
      return sizeof(MessageHeader);
    default:
      throw std::logic_error(folly::sformat("Invalid version {}", v));
  }
}

} // memcache
} // facebook
