/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "squangle/logger/DBEventCounter.h"
#include <glog/logging.h>

namespace facebook {
namespace db {

ExponentialMovingAverage::ExponentialMovingAverage(double smoothingFactor)
    : smoothingFactor_(smoothingFactor) {}

void ExponentialMovingAverage::addSample(double sample) {
  if (hasRegisteredFirstSample_) {
    currentValue_ =
        smoothingFactor_ * sample + (1 - smoothingFactor_) * currentValue_;
  } else {
    currentValue_ = sample;
    hasRegisteredFirstSample_ = true;
  }
}

void SimpleDbCounter::printStats() {
  LOG(INFO) << "Client Stats\n"
            << "Opened Connections " << numOpenedConnections() << "\n"
            << "Closed Connections " << numClosedConnections() << "\n"
            << "Failed Queries " << numFailedQueries() << "\n"
            << "Succeeded Queries " << numSucceededQueries() << "\n"
            << "SSL Connections " << numSSLConnections() << "\n"
            << "Reused SSL Sessions " << numReusedSSLSessions() << "\n";
}
}
} // facebook::db
