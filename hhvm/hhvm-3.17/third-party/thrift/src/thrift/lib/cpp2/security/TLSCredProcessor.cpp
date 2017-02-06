/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <thrift/lib/cpp2/security/TLSCredProcessor.h>

#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>

using namespace folly;
using namespace wangle;

namespace {

constexpr std::chrono::milliseconds kTicketPollInterval =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(10));

void insertSeeds(const folly::dynamic& keyConfig,
                 std::vector<std::string>& seedList) {
  if (!keyConfig.isArray()) {
    return;
  }
  for (const auto& seed : keyConfig) {
    seedList.push_back(seed.asString());
  }
}
}

namespace apache {
namespace thrift {

TLSCredProcessor::TLSCredProcessor(const std::string& ticketFile,
                                   const std::string& certFile)
    : ticketFile_(ticketFile),
      certFile_(certFile),
      poller_(folly::make_unique<FilePoller>(kTicketPollInterval)) {
  if (!ticketFile_.empty()) {
    auto ticketsChangedCob = [this]() { ticketFileUpdated(); };
    poller_->addFileToTrack(ticketFile_, ticketsChangedCob);
  }
  if (!certFile_.empty()) {
    auto certChangedCob = [this]() { certFileUpdated(); };
    poller_->addFileToTrack(certFile_, certChangedCob);
  }
}

void TLSCredProcessor::stop() {
  poller_->stop();
}

TLSCredProcessor::~TLSCredProcessor() { stop(); }

void TLSCredProcessor::addTicketCallback(
    std::function<void(TLSTicketKeySeeds)> callback) {
  ticketCallbacks_.push_back(std::move(callback));
}

void TLSCredProcessor::addCertCallback(
    std::function<void()> callback) {
  certCallbacks_.push_back(std::move(callback));
}

void TLSCredProcessor::ticketFileUpdated() noexcept {
  auto seeds = processTLSTickets(ticketFile_);
  if (seeds) {
    for (auto& callback : ticketCallbacks_) {
      callback(*seeds);
    }
  }
}

void TLSCredProcessor::certFileUpdated() noexcept {
  for (const auto& callback: certCallbacks_) {
    callback();
  }
}

/* static */ Optional<TLSTicketKeySeeds> TLSCredProcessor::processTLSTickets(
    const std::string& fileName) {
  try {
    std::string jsonData;
    folly::readFile(fileName.c_str(), jsonData);
    folly::dynamic conf = folly::parseJson(jsonData);
    if (conf.type() != dynamic::Type::OBJECT) {
      LOG(ERROR) << "Error parsing " << fileName << " expected object";
      return Optional<TLSTicketKeySeeds>();
    }
    TLSTicketKeySeeds seedData;
    if (conf.count("old")) {
      insertSeeds(conf["old"], seedData.oldSeeds);
    }
    if (conf.count("current")) {
      insertSeeds(conf["current"], seedData.currentSeeds);
    }
    if (conf.count("new")) {
      insertSeeds(conf["new"], seedData.newSeeds);
    }
    return seedData;
  } catch (const std::exception& ex) {
    LOG(ERROR) << "parsing " << fileName << " " << ex.what();
    return Optional<TLSTicketKeySeeds>();
  }
}
}
}
