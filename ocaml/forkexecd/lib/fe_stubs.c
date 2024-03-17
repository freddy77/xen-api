/*
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
 */

#include <errno.h>
#include <string.h>

#include <caml/alloc.h>
#include <caml/mlvalues.h>
#include <caml/fail.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/custom.h>

static void pidwaiter_finalize(value v)
{
    printf("Finalize\n");
}

static struct custom_operations custom_ops = {
  "com.xenserver.pidwaiter",
  pidwaiter_finalize,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default,
  custom_compare_ext_default,
  custom_fixed_length_default
};

static value alloc_my_pidwaiter(void)
{
  value v = caml_alloc_custom(&custom_ops, sizeof(void*), 0, 1);
  return v;
}

static void print_list(value str_list)
{
    printf("list is [");
    while (str_list != Val_emptylist) {
        printf("\"%s\", ", Field(str_list, 0));
        str_list = Field(str_list, 1);
    }
    printf("]\n");
}

CAMLprim        value
caml_safe_exec(value args, value environment, value id_mapping, value syslog_flags, value syslog_key)
// val safe_exec : string list -> string list -> (string * int * int) list -> int -> string option -> int * pidwaiter
{
    value res;
    CAMLparam5(args, environment, id_mapping, syslog_flags, syslog_key);

    print_list(args);
    print_list(environment);

    // TODO mapping

    printf("flags %d\n", Int_val(syslog_flags));

    printf("syslog_keyion is %s \"%s\"\n",
           syslog_key == Val_none ? "None": "Some",
           syslog_key == Val_none ? "" : String_val(Some_val(syslog_key)));

    res = caml_alloc_small(2, 0);
    Field(res, 0) = Val_int(123); // TODO pid
    Field(res, 1) = alloc_my_pidwaiter(); // TODO pidwaiter

    CAMLreturn(res);
}
