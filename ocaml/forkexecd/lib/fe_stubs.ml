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
external pidwaiter_waitpid0 : ?timeout:float -> int -> bool = "caml_pidwaiter_waitpid"

external pidwaiter_waitpid_nohang0 : pidwaiter -> int * Unix.process_status = "caml_pidwaiter_waitpid_nohang"

let pidwaiter_waitpid ?timeout _waiter pid = (* -> int * Unix.process_status = *)
   match timeout with
   | Some timeout ->
       let timed_out = pidwaiter_waitpid0 ~timeout:timeout pid in
       (* TODO timeout *)
       let (pid, status) = Unix.waitpid [] pid in
       if not timed_out then
          (pid, status)
       else (
         match status with
         | Unix.WSIGNALED n ->
           if n = Sys.sigkill then
             raise (Unix.Unix_error(Unix.ETIMEDOUT, "waitpid", ""))
           else
             (pid, status)
         | _ ->
           (pid, status)
       )
   | None ->
       (* TODO set reap *)
       Unix.waitpid [] pid

let pidwaiter_waitpid_nohang _waiter pid =
   (* TODO set reap *)
   Unix.waitpid [WNOHANG] pid

