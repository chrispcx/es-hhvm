(**
 * Copyright (c) 2015, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the "hack" directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

val with_expr_hook:
  (Nast.expr -> Typing_defs.locl Typing_defs.ty -> unit) -> (unit -> 'a) -> 'a

val debug_print_last_pos:
  'a -> unit

val fun_def:
  TypecheckerOptions.t -> Nast.fun_ -> unit
val class_def:
  TypecheckerOptions.t -> Nast.class_ -> unit
val typedef_def:
  TypecheckerOptions.t -> Nast.typedef -> unit
val gconst_def:
  Nast.gconst -> TypecheckerOptions.t -> unit

val expr:
  Typing_env.env -> Nast.expr ->
  Typing_env.env * Typing_defs.locl Typing_defs.ty

val get_self_from_c:
  Nast.class_ ->
  Typing_defs.decl Typing_defs.ty
