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

#include <string>

#include <folly/Range.h>

#include "mcrouter/tools/mcpiper/Color.h"

namespace facebook { namespace memcache {

/**
 * A string that has additional style data.
 * Currently only foreground colors are supported.
 */
class StyledString {
 public:
  StyledString();
  explicit StyledString(std::string s, Color color = Color::DEFAULT);

  /**
   * Raw uncolored text of this StyledString.
   */
  folly::StringPiece text() const;

  StyledString operator+(const StyledString& b) const;

  /**
   * For convenience, we maintain a stack of colors to use with append.
   * Initially the stack contains Color::DEFAULT.
   *
   * A typical usage would be
   *   s.pushAppendColor(myColor);
   *   s.append(a);
   *   s.append(b);
   *   s.popAppendColor();
   */
  void pushAppendColor(Color color);
  void popAppendColor();

  /**
   * Append the string using the current append color.
   */
  void append(const std::string& s);

  /**
   * Append the string, explicitly setting the color
   */
  void append(const std::string& s, Color color);

  /**
   * Append the string with all the color info.
   */
  void append(const StyledString& s);

  /**
   * For convenience, calls the relevant append()
   */
  template <class T>
  StyledString& operator+=(T&& s) {
    append(std::forward<T>(s));
    return *this;
  }

  /**
   * Append the char using the current append color.
   */
  void pushBack(char c);

  /**
   * Append the char with this color.
   */
  void pushBack(char c, Color color);

  /**
   * Change the color of the range [begin, begin + size)
   */
  void setFg(size_t begin, size_t size, Color color);

  /**
   * @return The color of the character at i
   */
  Color fgColorAt(size_t i) const;

  /**
   * @return  The size of the string.
   */
  size_t size() const;

 private:
  std::string text_;
  std::vector<Color> fg_;
  std::vector<Color> stack_;
};

}} // facebook::memcache
