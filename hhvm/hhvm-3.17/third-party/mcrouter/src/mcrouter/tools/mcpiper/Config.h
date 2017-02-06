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

#include <memory>
#include <string>

#include "mcrouter/tools/mcpiper/ValueFormatter.h"

namespace facebook { namespace memcache {

class CompressionCodecMap;

/**
 * Returns the default fifo root.
 */
std::string getDefaultFifoRoot();

/**
 * Creates value formatter.
 */
std::unique_ptr<ValueFormatter> createValueFormatter();

/**
 * Return current version.
 */
std::string getVersion();

/**
 * Initializes compression support.
 */
bool initCompression();

/**
 * Gets compression codec map.
 * If compression is not initialized, return nullptr.
 */
const CompressionCodecMap* getCompressionCodecMap();

}} // facebook::memcache
