(*
 * Copyright (C) 2024 Citrix Systems Inc.
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

type pidwaiter

external safe_exec : string list -> string array -> (string * Unix.file_descr * int) list -> int -> string option -> pidwaiter * int = "caml_safe_exec"

external pidwaiter_dontwait : pidwaiter -> unit = "caml_pidwaiter_dontwait"

(* timeout <= 0 wait infinite *)
external pidwaiter_waitpid : ?timeout:float -> pidwaiter -> int * Unix.process_status = "caml_pidwaiter_waitpid"

external pidwaiter_waitpid_nohang : pidwaiter -> int * Unix.process_status = "caml_pidwaiter_waitpid_nohang"
