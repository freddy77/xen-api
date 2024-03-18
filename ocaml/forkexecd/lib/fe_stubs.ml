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
external _pidwaiter_waitpid_timeout : ?timeout:float -> pidwaiter -> bool = "caml_pidwaiter_waitpid"

external _pidwaiter_set_reap: pidwaiter -> unit = "caml_pidwaiter_set_reap"

(* get the pid of the waiter *)
external _pidwaiter_pid: pidwaiter -> int = "caml_pidwaiter_pid"

let _pidwaiter_waitpid flags waiter =
   let waiter_pid = _pidwaiter_pid waiter in
   let (pid, status) = Unix.waitpid flags waiter_pid in
   (* we can't reap the process twice, otherwise we could reap another process *)
   if pid = waiter_pid then _pidwaiter_set_reap waiter ;
   (pid, status)

let pidwaiter_waitpid ?timeout waiter =
   let timed_out =
      match timeout with
      | Some timeout ->
          _pidwaiter_waitpid_timeout ~timeout:timeout waiter
      | None ->
          false
   in
   let (pid, status) = _pidwaiter_waitpid [] waiter in
   match timed_out, status with
   | true, Unix.WSIGNALED n when n = Sys.sigkill ->
       raise (Unix.Unix_error(Unix.ETIMEDOUT, "waitpid", ""))
   | _ ->
      (pid, status)

let pidwaiter_waitpid_nohang waiter =
   _pidwaiter_waitpid [WNOHANG] waiter
