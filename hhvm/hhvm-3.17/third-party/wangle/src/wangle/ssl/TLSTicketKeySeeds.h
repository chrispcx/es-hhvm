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

#include <vector>

namespace wangle {

struct TLSTicketKeySeeds {
  std::vector<std::string> oldSeeds;
  std::vector<std::string> currentSeeds;
  std::vector<std::string> newSeeds;

  bool operator==(const TLSTicketKeySeeds& rhs) const {
    return (oldSeeds == rhs.oldSeeds &&
        currentSeeds == rhs.currentSeeds &&
        newSeeds == rhs.newSeeds);
  }
};

} // namespace wangle
