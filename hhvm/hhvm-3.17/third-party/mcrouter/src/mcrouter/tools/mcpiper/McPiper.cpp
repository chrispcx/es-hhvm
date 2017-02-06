/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McPiper.h"

#include <unordered_set>

#include "mcrouter/lib/network/CarbonMessageList.h"

#include "mcrouter/tools/mcpiper/FifoReader.h"
#include "mcrouter/tools/mcpiper/MessagePrinter.h"

using namespace facebook::memcache::mcpiper;

namespace facebook {
namespace memcache {
namespace mcpiper {

namespace {

const std::unordered_set<size_t> kNotSupporttedTypes = {McStatsReply::typeId};

MessagePrinter::Options getOptions(const Settings& settings, McPiper* mcpiper) {
  MessagePrinter::Options options;

  options.numAfterMatch = settings.numAfterMatch;
  options.quiet = settings.quiet;
  options.raw = settings.raw;
  options.maxMessages = settings.maxMessages;
  options.disableColor = settings.raw || !isatty(fileno(stdout));

  // Time Function
  static struct timeval prevTs = {0, 0};
  if (!settings.timeFormat.empty()) {
    if (settings.timeFormat == "absolute") {
      options.printTimeFn = printTimeAbsolute;
    } else if (settings.timeFormat == "diff") {
      options.printTimeFn = [](const struct timeval& ts) {
        return printTimeDiff(ts, prevTs);
      };
      gettimeofday(&prevTs, nullptr);
    } else if (settings.timeFormat == "offset") {
      options.printTimeFn = [](const struct timeval& ts) {
        return printTimeOffset(ts, prevTs);
      };
    } else {
      LOG(ERROR) << "Invalid time format. absolute|diff|offset expected, got "
                 << settings.timeFormat << ". Timestamps will not be shown.";
    }
  }

  // Exit function
  options.stopRunningFn = [mcpiper]() {
    mcpiper->stop();
  };

  return options;
}

MessagePrinter::Filter getFilter(const Settings& settings) {
  MessagePrinter::Filter filter;

  filter.valueMinSize = settings.valueMinSize;
  filter.valueMaxSize = settings.valueMaxSize;
  filter.minLatencyUs = settings.minLatencyUs;
  filter.invertMatch = settings.invertMatch;

  // Host
  if (!settings.host.empty()) {
    try {
      filter.host = folly::SocketAddress(settings.host,
                      1 /* port */, true /* allowNameLookup */).getIPAddress();
      std::cerr << "Host: " << filter.host.toFullyQualified() << std::endl;
    } catch (...) {
      LOG(ERROR) << "Invalid IP address provided: " << filter.host;
      exit(1);
    }
  }

  // Port
  if (settings.port != 0) {
    filter.port = settings.port;
    std::cerr << "Port: " << filter.port << std::endl;
  }

  // Protocol
  if (!settings.protocol.empty()) {
    auto protocol = mc_string_to_protocol(settings.protocol.c_str());
    if (protocol == mc_ascii_protocol ||
        protocol == mc_caret_protocol ||
        protocol == mc_umbrella_protocol) {
      filter.protocol.emplace(protocol);
    } else {
      LOG(ERROR) << "Invalid protocol. ascii|caret|umbrella expected, got "
                 << settings.protocol
                 << ". Protocol filter will not be applied.";
    }
  }

  // Builds data pattern
  filter.pattern = buildRegex(settings.matchExpression, settings.ignoreCase);
  if (filter.pattern) {
    if (settings.invertMatch) {
      std::cerr << "Don't match: ";
    } else {
      std::cerr << "Match: ";
    }
    std::cerr << *filter.pattern << std::endl;
  }

  return filter;
}

} // anonymous

void McPiper::stop() {
  running_ = false;
}

void McPiper::run(Settings settings, std::ostream& targetOut) {
  running_ = true;
  // Builds filename pattern
  auto filenamePattern = buildRegex(settings.filenamePattern,
                                    settings.ignoreCase);
  if (filenamePattern) {
    std::cerr << "Filename pattern: " << *filenamePattern << std::endl;
  }

  messagePrinter_ = folly::make_unique<MessagePrinter>(
      getOptions(settings, this),
      getFilter(settings),
      createValueFormatter(),
      targetOut);

  std::unordered_map<uint64_t, SnifferParser<MessagePrinter>> parserMap;

  // Callback from fifoManager. Read the data and feed the correct parser.
  auto fifoReaderCallback = [&parserMap, this](
      uint64_t connectionId,
      uint64_t packetId,
      folly::SocketAddress from,
      folly::SocketAddress to,
      uint32_t typeId,
      uint64_t msgStartTime,
      folly::ByteRange data) {
    if (!running_) {
      return;
    }
    if (kNotSupporttedTypes.find(typeId) != kNotSupporttedTypes.end()) {
      return;
    }
    auto it = parserMap.find(connectionId);
    if (it == parserMap.end()) {
      it = parserMap
               .emplace(
                   std::piecewise_construct,
                   std::forward_as_tuple(connectionId),
                   std::forward_as_tuple(*messagePrinter_))
               .first;
    }
    auto& snifferParser = it->second;

    if (packetId == 0) {
      snifferParser.parser().reset();
    }

    snifferParser.setAddresses(std::move(from), std::move(to));
    snifferParser.setCurrentMsgStartTime(msgStartTime);
    snifferParser.parser().parse(
        data, typeId, packetId == 0 /* isFirstPacket */);
  };

  initCompression();

  folly::EventBase eventBase;
  FifoReaderManager fifoManager(eventBase, fifoReaderCallback,
                                settings.fifoRoot, std::move(filenamePattern));

  eventBase.setMaxReadAtOnce(1);
  while (running_) {
    eventBase.loopOnce();
  }
}

} // mcpiper namespace
} // memcache namespace
} // facebook namespace
