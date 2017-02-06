/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>
#include <folly/Range.h>

#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ShadowValidationData.h"
#include "mcrouter/config.h"
#include "mcrouter/flavor.h"
#include "mcrouter/options.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"
#include "mcrouter/standalone_options.h"

namespace facebook { namespace memcache { namespace mcrouter {

bool read_standalone_flavor(
    const std::string& flavor,
    std::unordered_map<std::string, std::string>& option_dict,
    std::unordered_map<std::string, std::string>& st_option_dict) {

  if (!readFlavor(flavor, st_option_dict, option_dict)) {
    LOG(ERROR) << "CRITICAL: Couldn't initialize from standalone flavor file "
               << flavor;
    return false;
  }
  return true;
}

std::unique_ptr<ConfigApi> createConfigApi(const McrouterOptions& opts) {
  return folly::make_unique<ConfigApi>(opts);
}

std::string performOptionSubstitution(std::string str) {
  return str;
}

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider() {
  return folly::make_unique<McExtraRouteHandleProvider>();
}

std::unique_ptr<McrouterLogger> createMcrouterLogger(McrouterInstance& router) {
  return folly::make_unique<McrouterLogger>(router);
}

void extraValidateOptions(const McrouterOptions& opts) {
  if (!opts.config.empty()) {
    // If config option is used, other options are superseded
    if (!opts.config_file.empty() || !opts.config_str.empty()) {
      VLOG(1) << "config option will supersede config-file"
        " and config-str options";
    }
    return;
  }

  size_t numSources = 0;
  if (!opts.config_file.empty()) {
    ++numSources;
  }
  if (!opts.config_str.empty()) {
    ++numSources;
  }
  if (numSources == 0) {
    throw std::logic_error("No configuration source");
  } else if (numSources > 1) {
    throw std::logic_error("More than one configuration source");
  }
}

void applyTestMode(McrouterOptions& opts) {
  opts.enable_failure_logging = false;
  opts.stats_logging_interval = 0;
}

McrouterOptions defaultTestOptions() {
  auto opts = McrouterOptions();
  applyTestMode(opts);
  return opts;
}

std::vector<std::string> defaultTestCommandLineArgs() {
  return { "--disable-failure-logging", "--stats-logging-interval=0" };
}

void logTkoEvent(Proxy& proxy, const TkoLog& tkoLog) {}

void logFailover(Proxy& proxy, const FailoverContext& failoverContext) {}

void logShadowValidationError(
    Proxy& proxy,
    const ShadowValidationData& valData) {
  VLOG_EVERY_N(1,100)
      << "Mismatch between shadow and normal reply" << std::endl
      << "Key:" << valData.fullKey << std::endl
      << "Expected Result:"
      << mc_res_to_string(valData.normalResult) << std::endl
      << "Shadow Result:"
      << mc_res_to_string(valData.shadowResult) << std::endl;
}

void initFailureLogger() { }

bool initCompression(McrouterInstanceBase&) {
  return false;
}

void scheduleSingletonCleanup() { }

std::unordered_map<std::string, folly::dynamic> additionalConfigParams() {
  return std::unordered_map<std::string, folly::dynamic>();
}

void insertCustomStartupOpts(folly::dynamic& options) {
}

std::string getBinPath(folly::StringPiece name) {
  if (name == "mcrouter") {
    return "./mcrouter/mcrouter";
  } else if (name == "mockmc") {
    return "./mcrouter/lib/network/mock_mc_server";
  }
  return "unknown";
}

}}}  // facebook::memcache::mcrouter
