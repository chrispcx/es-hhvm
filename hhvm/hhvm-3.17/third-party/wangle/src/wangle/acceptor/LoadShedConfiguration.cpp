/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/acceptor/LoadShedConfiguration.h>

#include <folly/Conv.h>
#include <openssl/ssl.h>

using std::string;
using folly::SocketAddress;

namespace wangle {

void LoadShedConfiguration::addWhitelistAddr(folly::StringPiece input) {
  auto addr = input.str();
  size_t separator = addr.find_first_of('/');
  if (separator == string::npos) {
    whitelistAddrs_.insert(SocketAddress(addr, 0));
  } else {
    unsigned prefixLen = folly::to<unsigned>(addr.substr(separator + 1));
    addr.erase(separator);
    whitelistNetworks_.insert(NetworkAddress(SocketAddress(addr, 0), prefixLen));
  }
}

bool LoadShedConfiguration::isWhitelisted(const SocketAddress& address) const {
  if (whitelistAddrs_.find(address) != whitelistAddrs_.end()) {
    return true;
  }
  for (auto& network : whitelistNetworks_) {
    if (network.contains(address)) {
      return true;
    }
  }
  return false;
}

} // namespace wangle
