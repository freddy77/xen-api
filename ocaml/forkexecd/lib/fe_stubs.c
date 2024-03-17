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
#include <caml/unixsupport.h>

#undef DEBUG

#define FOREACH_LIST(name, list) \
    for(value name = (list); name != Val_emptylist; name = Field(name, 1))

// XXX debug
#define DEBUG 1

static void pidwaiter_finalize(value v)
{
  // TODO
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

typedef struct {
    char **args;
    char **environment;
    int syslog_flags;
    const char *syslog_key;
} exec_info;

static char *append_string(char **p_dest, const char *s)
{
    char *const dest = *p_dest;
    size_t const size = strlen(s) + 1;
    memcpy(dest, s, size);
    *p_dest = dest + size;
    return dest;
}

CAMLprim value
caml_safe_exec(value args, value environment, value id_mapping, value syslog_flags, value syslog_key)
// val safe_exec : string list -> string array -> (string * int * int) list -> int -> string option -> int * pidwaiter
{
    value res;
    unsigned num_args = 0, num_mappings = 0;
    const mlsize_t num_environment = caml_array_length(environment);
    size_t strings_size = 0;
    const int flags = Int_val(syslog_flags);
    const char *const key = (syslog_key == Val_none) ? NULL : String_val(Some_val(syslog_key));
    CAMLparam5(args, environment, id_mapping, syslog_flags, syslog_key);

    FOREACH_LIST(arg, args) {
        strings_size += strlen(String_val(Field(arg, 0))) + 1;
        ++num_args;
    }
    if (num_args < 1)
        unix_error(EINVAL, "safe_exec", Nothing);

    for (mlsize_t n = 0; n < num_environment; ++n)
        strings_size += strlen(String_val(Field(environment, n))) + 1;

    FOREACH_LIST(map, id_mapping) {
        ++num_mappings;
        value item = Field(map, 0);
        const char *const uuid = String_val(Field(item, 0));
        const int current_fd = Int_val(Field(item, 1));
        const int wanted_fd = Int_val(Field(item, 2));
        // Check values.
        // The check for UUID length makes sure we'll have space to replace last part of
        // the command line arguments with file descriptor.
        if (strlen(uuid) != 36 || current_fd < 0 || wanted_fd < -1 || wanted_fd > 2)
            unix_error(EINVAL, "safe_exec", Nothing);
    }

    if (syslog_key == Val_none && key)
        strings_size += strlen(key) + 1;

    size_t total_size =
        sizeof(exec_info) +
        sizeof(char*) * (num_args + 1 + num_environment + 1) +
        strings_size;
    exec_info *info = (exec_info *) calloc(total_size, 1);
    if (!info)
        unix_error(ENOMEM, "safe_exec", Nothing);
    char **ptrs = (char**)(info + 1);
    char *strings = (char*)(ptrs + num_args + 1 + num_environment + 1);
#ifdef DEBUG
    char *const initial_strings = strings;
#endif

    info->syslog_flags = flags;
    info->syslog_key = key;
    if (key)
        info->syslog_key = append_string(&strings, key);

    // copy arguments into list
    info->args = ptrs;
    FOREACH_LIST(arg, args) {
        const char *const s = String_val(Field(arg, 0));
        *ptrs++ = append_string(&strings, s);
    }
    *ptrs++ = NULL;

    // copy environments into list
    info->environment = ptrs;
    for (mlsize_t n = 0; n < num_environment; ++n) {
        const char *const s = String_val(Field(environment, n));
        *ptrs++ = append_string(&strings, s);
    }
    *ptrs++ = NULL;

    free(info);

#ifdef DEBUG
    if (strings - (char*)info != total_size || initial_strings != (char*) ptrs)
        unix_error(EACCES, "safe_exec", Nothing);
#endif

    res = caml_alloc_small(2, 0);
    Field(res, 0) = Val_int(123); // TODO pid
    Field(res, 1) = alloc_my_pidwaiter(); // TODO pidwaiter

    CAMLreturn(res);
}
