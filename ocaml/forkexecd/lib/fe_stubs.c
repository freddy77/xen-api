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
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include <sys/wait.h>

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

typedef struct {
    pid_t pid;
} pidwaiter;

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
  value v = caml_alloc_custom(&custom_ops, sizeof(pidwaiter), 0, 1);
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

#ifdef DEBUG
    if (strings - (char*)info != total_size || initial_strings != (char*) ptrs)
        unix_error(EACCES, "safe_exec", Nothing);
#endif

//    unix_error(ENOSYS, "safe_exec", Nothing);

    // rename all command line
    // fork
    pid_t pid = fork();
    if (pid < 0) {
        free(info);
        unix_error(errno, "safe_exec", Nothing);
    }

    if (pid == 0) {
        // child
        // TODO adjust file descriptors
        execve(info->args[0], info->args, info->environment);
        exit(errno == ENOENT ? 127 : 126);
    }
    free(info);

    res = caml_alloc_small(2, 0);
    Field(res, 0) = alloc_my_pidwaiter(); // TODO pidwaiter
    Field(res, 1) = Val_int(pid);

    CAMLreturn(res);
}

CAMLprim value
caml_pidwaiter_dontwait(value waiter)
{
    CAMLparam1(waiter);

    unix_error(ENOSYS, "xxx", Nothing);

    CAMLreturn(Val_unit);
}

typedef struct {
    pid_t pid;
    bool timed_out;
    struct timespec ts;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
} timeout_kill;

static void *proc_timeout_kill(void *arg)
{
    timeout_kill *tm = (timeout_kill *) arg;
    pthread_mutex_lock(&tm->mtx);
    int res  = pthread_cond_timedwait(&tm->cond, &tm->mtx, &tm->ts);
    pthread_mutex_unlock(&tm->mtx);

    if (res == ETIMEDOUT) {
        kill(tm->pid, SIGKILL);
        tm->timed_out = true;
    }
}

CAMLprim value
caml_pidwaiter_waitpid(value timeout_, value pid_)
{
    CAMLparam2(timeout_, pid_);
    double timeout = timeout_ == Val_none ? 0 : Double_val(Some_val(timeout_));
    pid_t const pid = Int_val(pid_);

    // TODO handle errors
    timeout_kill tm = { pid, false };
    clock_gettime(CLOCK_MONOTONIC, &tm.ts);

    if (timeout > 0) {
        // TODO if no timeout just use a wait !!
        double f = floor(timeout);
        tm.ts.tv_sec += f;
        tm.ts.tv_nsec += (timeout - f) * 1000000000.;
    }

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    pthread_cond_init(&tm.cond, &attr);
    pthread_condattr_destroy(&attr);

    pthread_mutex_init(&tm.mtx, NULL);

    pthread_t th;
    pthread_create(&th, NULL, proc_timeout_kill, &tm);
    siginfo_t info;
    waitid(P_PID, pid, &info, WEXITED|WNOWAIT);
    pthread_mutex_lock(&tm.mtx);
    pthread_cond_broadcast(&tm.cond);
    pthread_mutex_unlock(&tm.mtx);
    pthread_join(th, NULL);
    pthread_cond_destroy(&tm.cond);
    pthread_mutex_destroy(&tm.mtx);

    CAMLreturn(tm.timed_out ? Val_true : Val_false);
}

CAMLprim value
caml_pidwaiter_waitpid_nohang(value waiter)
{
    CAMLparam1(waiter);

    unix_error(ENOSYS, "xxx", Nothing);

    // TODO type
    CAMLreturn(Val_unit);
}

/*
 * waiter : proc thread with small thread
 *    pthread_cond_timedwait (cond, time + XXX)
 *    if (timeout) kill(pid, SIGKILL)
 * reaper : proc thread with small thread, detached
 *    waitpid (pid)
 *
 */
