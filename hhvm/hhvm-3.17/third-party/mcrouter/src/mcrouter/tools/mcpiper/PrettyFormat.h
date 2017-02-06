/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/tools/mcpiper/Color.h"

namespace facebook { namespace memcache {

/**
 * Pretty format, used to color StyledString.
 */
struct PrettyFormat {
  Color attrColor       = Color::MAGENTA;
  Color dataOpColor     = Color::DARKYELLOW;
  Color dataKeyColor    = Color::YELLOW;
  Color dataValueColor  = Color::DARKCYAN;
  Color headerColor     = Color::WHITE;
  Color matchColor      = Color::RED;
  Color msgAttrColor    = Color::GREEN;
};

}} // facebook::memcache
