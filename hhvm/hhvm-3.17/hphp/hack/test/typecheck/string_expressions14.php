<?hh // strict
/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the "hack" directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

class A {
  public function __construct(public A $x) {}
  public function __toString(): string {
    return "A";
  }
}

function test(A $x): void {
  $x = "$x->x->x";
}
