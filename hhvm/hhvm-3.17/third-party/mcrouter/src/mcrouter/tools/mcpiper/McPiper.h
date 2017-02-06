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

#include <iostream>

#include "mcrouter/tools/mcpiper/Config.h"
#include "mcrouter/tools/mcpiper/MessagePrinter.h"

namespace facebook {
namespace memcache {
namespace mcpiper {

struct Settings {
  // Positional args
  std::string matchExpression;

  // Named args
  std::string fifoRoot{getDefaultFifoRoot()};
  std::string filenamePattern;
  std::string host;
  bool ignoreCase{false};
  bool invertMatch{false};
  uint32_t maxMessages{0};
  uint32_t numAfterMatch{0};
  uint16_t port{0};
  bool quiet{false};
  std::string timeFormat;
  uint32_t valueMinSize{0};
  uint32_t valueMaxSize{std::numeric_limits<uint32_t>::max()};
  int64_t minLatencyUs{0};
  size_t verboseLevel{0};
  std::string protocol;
  bool raw{false};
};

class McPiper {
 public:
  void run(
      Settings settings,
      std::ostream& targetOut = std::cout);

  void stop();

  const MessagePrinter::Stats& stats() const noexcept {
    return messagePrinter_->stats();
  };

 private:
  std::unique_ptr<MessagePrinter> messagePrinter_;
  bool running_{false};
};

} // mcpiper
} // memcache
} // facebook
