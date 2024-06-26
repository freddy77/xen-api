(*
 * Copyright (C) 2006-2011 Citrix Systems Inc.
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
(** Module that defines API functions for PGPU objects
 * @group Graphics
*)

val update_gpus : __context:Context.t -> unit
(** Synchronise the PGPU objects in the database with the actual devices
 *  in the local host. *)

val add_enabled_VGPU_types :
  __context:Context.t -> self:API.ref_PGPU -> value:API.ref_VGPU_type -> unit
(** Enable one of the VGPU types supported by the PGPU. *)

val remove_enabled_VGPU_types :
  __context:Context.t -> self:API.ref_PGPU -> value:API.ref_VGPU_type -> unit
(** Disable one of the VGPU types supported by the PGPU. *)

val set_enabled_VGPU_types :
     __context:Context.t
  -> self:API.ref_PGPU
  -> value:API.ref_VGPU_type list
  -> unit
(** Enable a set of VGPU types supported by the PGPU. *)

val set_GPU_group :
  __context:Context.t -> self:API.ref_PGPU -> value:API.ref_GPU_group -> unit
(** Move the PGPU to a new GPU group. *)

(* Return the number of VGPUs of the specified type for which capacity
 * remains on the PGPU. *)
val get_remaining_capacity :
     __context:Context.t
  -> self:API.ref_PGPU
  -> vgpu_type:API.ref_VGPU_type
  -> int64

val assert_can_run_VGPU :
  __context:Context.t -> self:API.ref_PGPU -> vgpu:API.ref_VGPU -> unit
(** Check whether a VGPU can run on a particular PGPU. *)

val enable_dom0_access :
     __context:Context.t
  -> self:API.ref_PGPU
  -> [`disable_on_reboot | `disabled | `enable_on_reboot | `enabled]

val disable_dom0_access :
     __context:Context.t
  -> self:API.ref_PGPU
  -> [`disable_on_reboot | `disabled | `enable_on_reboot | `enabled]

(* For AMD MxGPU. Acts on the local host only.
 * Ensures that the "gim" kernel module is loaded on localhost,
 * that PCI DB entries exist for the VF PCI devices reported by the module,
 * and that those entries have the "physical_function" field set correctly. *)
val mxgpu_vf_setup : __context:Context.t -> unit

(* For nvidia vgpu. Acts on the local host only.  Ensures that the
 * nvidia VFs are created on localhost, that PCI DB entries exist for
 * the VF PCI devices, and that those entries have the
 * "physical_function" field set correctly.  If [pf] is [Ref.null], all
 * VFs will be created. *)
val nvidia_vf_setup :
  __context:Context.t -> pf:API.ref_PCI -> enable:bool -> unit
