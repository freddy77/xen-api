#!/usr/bin/env ocamlscript
Ocaml.sources := ["common.ml"; "lvm.ml"];
Ocaml.packs := ["xapi-storage"; "cmdliner"; "re.str"; "oUnit"];
Ocaml.ocamlflags := ["-thread"]
--
(*
 * Copyright (C) Citrix Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *)
open Common

module Command = struct
  open Storage.V.Types
  include SR.Ls

  let command common { SR.Ls.In.dbg; sr } =
    []
end

module Test = struct
  open OUnit

  let test common = ()
end

module M = Make(Command)(Test)
let _ = M.main ()
