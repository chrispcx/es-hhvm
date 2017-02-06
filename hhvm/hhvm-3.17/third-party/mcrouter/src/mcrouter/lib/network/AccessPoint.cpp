/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AccessPoint.h"

#include <folly/Conv.h>
#include <folly/IPAddress.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

namespace {

void parseParts(folly::StringPiece s) {
  if (!s.empty()) {
    throw std::runtime_error("Invalid AccessPoint format");
  }
}

template <class... Args>
void parseParts(folly::StringPiece s, folly::StringPiece& out, Args&... args) {
  if (s.empty()) {
    return;
  }
  if (s[0] != ':') {
    throw std::runtime_error("Invalid AccessPoint format");
  }
  s.advance(1);
  auto colon = s.find(":");
  if (colon == std::string::npos) {
    out = s;
  } else {
    out = s.subpiece(0, colon);
    s.advance(colon);
    parseParts(s, args...);
  }
}

bool parseSsl(folly::StringPiece s) {
  if (s == "ssl") {
    return true;
  } else if (s == "plain") {
    return false;
  }
  throw std::runtime_error("Invalid encryption");
}

bool parseCompressed(folly::StringPiece s) {
  if (s == "compressed") {
    return true;
  } else if (s == "notcompressed") {
    return false;
  }
  throw std::runtime_error("Invalid compression config");
}

mc_protocol_t parseProtocol(folly::StringPiece str) {
  if (str == "ascii") {
    return mc_ascii_protocol;
  } else if (str == "caret") {
    return mc_caret_protocol;
  } else if (str == "umbrella") {
    return mc_umbrella_protocol;
  }
  throw std::runtime_error("Invalid protocol");
}

}  // anonymous

AccessPoint::AccessPoint(folly::StringPiece host, uint16_t port,
                         mc_protocol_t protocol, bool useSsl, bool compressed)
    : port_(port),
      protocol_(protocol),
      useSsl_(useSsl),
      compressed_(compressed) {

  try {
    folly::IPAddress ip(host);
    host_ = ip.toFullyQualified();
    isV6_ = ip.isV6();
  } catch (const folly::IPAddressFormatException& e) {
    // host is not an IP address (e.g. 'localhost')
    host_ = host.str();
    isV6_ = false;
  }
}

std::shared_ptr<AccessPoint>
AccessPoint::create(folly::StringPiece apString,
                    mc_protocol_t defaultProtocol,
                    bool defaultUseSsl,
                    uint16_t portOverride,
                    bool defaultCompressed) {
  if (apString.empty()) {
    return nullptr;
  }

  folly::StringPiece host;
  if (apString[0] == '[') {
    // IPv6
    auto closing = apString.find(']');
    if (closing == std::string::npos) {
      return nullptr;
    }
    host = apString.subpiece(1, closing - 1);
    apString.advance(closing + 1);
  } else {
    // IPv4 or hostname
    auto colon = apString.find(':');
    if (colon == std::string::npos) {
      host = apString;
      apString = "";
    } else {
      host = apString.subpiece(0, colon);
      apString.advance(colon);
    }
  }

  if (host.empty()) {
    return nullptr;
  }

  try {
    folly::StringPiece port, protocol, encr, comp;
    parseParts(apString, port, protocol, encr, comp);

    return std::make_shared<AccessPoint>(
      host,
      portOverride != 0 ? portOverride : folly::to<uint16_t>(port),
      protocol.empty() ? defaultProtocol : parseProtocol(protocol),
      encr.empty() ? defaultUseSsl : parseSsl(encr),
      comp.empty() ? defaultCompressed : parseCompressed(comp));
  } catch (const std::exception&) {
    return nullptr;
  }
}

void AccessPoint::disableCompression() {
  compressed_ = false;
}

std::string AccessPoint::toHostPortString() const {
  if (isV6_) {
    return folly::to<std::string>("[", host_, "]:", port_);
  }
  return folly::to<std::string>(host_, ":", port_);
}

std::string AccessPoint::toString() const {
  assert(protocol_ != mc_unknown_protocol);
  if (isV6_) {
    return folly::to<std::string>("[", host_, "]:", port_, ":",
                                  mc_protocol_to_string(protocol_),
                                  ":", useSsl_ ? "ssl" : "plain", ":",
                                  compressed_ ? "compressed" : "notcompressed");
  }
  return folly::to<std::string>(host_, ":", port_, ":",
                                mc_protocol_to_string(protocol_),
                                ":", useSsl_ ? "ssl" : "plain", ":",
                                compressed_ ? "compressed" : "notcompressed");
}

}}  // facebook::memcache
