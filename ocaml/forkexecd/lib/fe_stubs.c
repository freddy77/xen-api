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
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include <caml/alloc.h>
#include <caml/mlvalues.h>
#include <caml/fail.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/custom.h>
#include <caml/unixsupport.h>
#include <caml/signals.h>

#undef DEBUG

#define FOREACH_LIST(name, list) \
    for(value name = (list); name != Val_emptylist; name = Field(name, 1))

// XXX debug
#define DEBUG 1

static int create_thread_minstack(pthread_t *th, void *(*proc)(void *), void *arg);

typedef struct {
    pid_t pid;
    bool reaped;
} pidwaiter;

#define PIDWAITER(name, value) \
    pidwaiter *const name = ((pidwaiter*) Data_custom_val(value))

static void *proc_reap(void *arg)
{
    pid_t pid = (pid_t) (intptr_t) arg;

    int status;
    waitpid(pid, &status, 0);

    return NULL;
}

static void pidwaiter_finalize(value v)
{
    PIDWAITER(waiter, v);

    // reap the pid to avoid zombies
    if (!waiter->reaped) {
        pthread_t th;
        create_thread_minstack(&th, proc_reap, (void *) (intptr_t) waiter->pid);
        pthread_detach(th);
    }
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

static value alloc_my_pidwaiter(pid_t pid)
{
    value v = caml_alloc_custom(&custom_ops, sizeof(pidwaiter), 0, 1);
    PIDWAITER(waiter, v);
    waiter->pid = pid;
    waiter->reaped = false;
    return v;
}

typedef struct {
    const char *uuid;
    int current_fd;
    int wanted_fd;
} mapping;

typedef struct {
    char **args;
    char **environment;
    mapping *mappings;
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

static inline void sort_mapping(mapping *const mappings, unsigned num_mappings,
                                int (*compare)(const mapping *a, const mapping *b))
{
    qsort(mappings, num_mappings, sizeof(*mappings),
          (int (*)(const void *a, const void *b)) compare);
}

static void adjust_args(char **args, mapping *const mappings, unsigned num_mappings);
static bool fd_is_used(const mapping *const mappings, unsigned num_mappings, int fd);
static void close_unwanted_fds(mapping *const mappings, unsigned num_mappings);
static int compare_current(const mapping *a, const mapping *b);
static int compare_wanted(const mapping *a, const mapping *b);

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
    CAMLlocal1(waiter);

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
        sizeof(mapping) * num_mappings +
        strings_size;
    exec_info *info = (exec_info *) calloc(total_size, 1);
    if (!info)
        unix_error(ENOMEM, "safe_exec", Nothing);

    caml_enter_blocking_section();

    char **ptrs = (char**)(info + 1);
    mapping *mappings = (mapping *) (ptrs + num_args + 1 + num_environment + 1);
    char *strings = (char*) (mappings + num_mappings);
#ifdef DEBUG
    mapping *const initial_mappings = mappings;
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

    // copy mappings
    info->mappings = mappings;
    FOREACH_LIST(map, id_mapping) {
        mapping *m = mappings++;
        value item = Field(map, 0);
        m->uuid = String_val(Field(item, 0));
        m->current_fd = Int_val(Field(item, 1));
        m->wanted_fd = Int_val(Field(item, 2));
    }

#ifdef DEBUG
    if (strings - (char*)info != total_size
        || initial_mappings != (mapping*) ptrs
        || initial_strings != (char*) mappings) {
        caml_leave_blocking_section();
        unix_error(EACCES, "safe_exec", Nothing);
    }
#endif

//    unix_error(ENOSYS, "safe_exec", Nothing);

    // rename all command line
    // fork
    pid_t pid = vfork();
    if (pid < 0) {
        free(info);
        unix_error(errno, "safe_exec", Nothing);
    }

    if (pid == 0) {
        // child

#if 0
        for (unsigned i = 0; i < num_mappings; ++i) {
            const mapping *m = &info->mappings[i];
            printf("mapping %s %d %d\n", m->uuid, m->current_fd, m->wanted_fd);
        }
        printf("\n");
#endif

        // close earlier to have space for the redirections in case we are using a lot of file
        //descriptors
        close_unwanted_fds(info->mappings, num_mappings);

        if (setsid() < 0)
            _exit(1);

        // redirect file descriptors
        sort_mapping(info->mappings, num_mappings, compare_wanted);
        int last_free = 3;
        for (unsigned i = 0; i < num_mappings; ++i) {
            mapping *m = &info->mappings[i];
            // find fd to use
            while (fd_is_used(info->mappings, num_mappings, last_free))
                ++last_free;
            // duplicate on new one
            int old_fd = m->current_fd;
            dup2(old_fd, last_free);
            m->current_fd = -1;
            if (!fd_is_used(info->mappings, num_mappings, old_fd))
                close(old_fd);
            m->current_fd = last_free;
            if (m->wanted_fd < 0)
               m->wanted_fd = last_free;
        }

#if 0
        FILE *f = fopen("out", "a");
        for (unsigned i = 0; i < num_mappings; ++i) {
            const mapping *m = &info->mappings[i];
            fprintf(f, "mapping %s %d %d\n", m->uuid, m->current_fd, m->wanted_fd);
        }
        fprintf(f, "\n");
        fclose(f);
#endif

        // TODO always needed??
        for (int i = 0; i < 3; ++i)
            close(i);
        open("/dev/null", O_WRONLY);
        dup2(0, 1);
        dup2(0, 2);

        for (unsigned i = 0; i < num_mappings; ++i) {
            mapping *m = &info->mappings[i];
            if (m->wanted_fd <= 2) {
                dup2(m->current_fd, m->wanted_fd);
                close(m->current_fd);
                m->current_fd = m->wanted_fd;
            }
        }

        adjust_args(info->args, info->mappings, num_mappings);

        execve(info->args[0], info->args, info->environment);
        _exit(errno == ENOENT ? 127 : 126);
    }
    free(info);

    caml_leave_blocking_section();

    waiter = alloc_my_pidwaiter(pid);

    res = caml_alloc_small(2, 0);
    Field(res, 0) = waiter;
    Field(res, 1) = Val_int(pid);

    CAMLreturn(res);
}

static void adjust_args(char **args, mapping *const mappings, unsigned num_mappings)
{
    for (size_t n = 0; args[n]; ++n) {
        char *arg = args[n];
        size_t len = strlen(arg);
        if (len < 36)
            continue;

        // replace uuid with file descriptor
        char *uuid = arg + len - 36;
        for (unsigned i = 0; i < num_mappings; ++i) {
            const mapping *m = &mappings[i];
            if (strcmp(m->uuid, uuid) != 0)
                continue;
            //sprintf(uuid, "%d", m->wanted_fd >= 0 ? m->wanted_fd : m->current_fd);
            sprintf(uuid, "%d", m->current_fd);
        }
    }
}

static bool fd_is_used(const mapping *const mappings, unsigned num_mappings, int fd)
{
    for (unsigned i = 0; i < num_mappings; ++i) {
        const mapping *m = &mappings[i];
        if (m->current_fd == fd)
            return true;
    }
    return false;
}

static int compare_current(const mapping *a, const mapping *b)
{
    return a->current_fd - b->current_fd;
}

static int compare_wanted(const mapping *a, const mapping *b)
{
    return a->wanted_fd - b->wanted_fd;
}

static bool close_fds_from(int fd);

static void close_unwanted_fds(mapping *const mappings, unsigned num_mappings)
{
    sort_mapping(mappings, num_mappings, compare_current);

    int prev = -1;
    for (unsigned i = 0; i < num_mappings; ++i) {
        const mapping *m = &mappings[i];
        for (int fd = prev + 1; fd < m->current_fd; ++fd)
            close(fd);
        prev = m->current_fd;
    }
    close_fds_from(prev + 1);
}

static bool close_fds_from(int fd_from)
{
    // first method, use close_range
#if (defined(__linux__) && defined(SYS_close_range)) \
    || (defined(__FreeBSD__) && defined(CLOSE_RANGE_CLOEXEC))
    static bool close_range_supported = true;
    if (close_range_supported) {
#if defined(__linux__)
        if (syscall(SYS_close_range, fd_from, ~0U, 0) == 0)
#else
        if (close_range(fd_from, ~0U, 0) == 0)
#endif
            return true;

        if (errno == ENOSYS)
            close_range_supported = false;
    }
#endif

    // second method, read fds list from /proc
    DIR *dir = opendir("/proc/self/fd");
    if (dir) {
        const int dir_fd = dirfd(dir);
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            char *end = NULL;
            unsigned long fd = strtoul(ent->d_name, &end, 10);
            if (end == NULL || *end)
                continue;
            if (fd >= fd_from && fd != dir_fd)
                close(fd);
        }
        closedir(dir);
        return true;
    }

    // third method, use just a loop
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
        return false;
    for (int fd = fd_from; fd < limit.rlim_cur; ++ fd)
        close(fd);

    return true;
}

CAMLprim value
caml_pidwaiter_dontwait(value waiter_val)
{
    CAMLparam1(waiter_val);
    PIDWAITER(waiter, waiter_val);

    if (!waiter->reaped) {
        // TODO we need to wait the pid
        unix_error(ENOSYS, "xxx", Nothing);
    }

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
    return NULL;
}

static int
create_thread_minstack(pthread_t *th, void *(*proc)(void *), void *arg)
{
    int res;

    // disable any possible signal handler so we can safely use a small stack
    // for the thread
    sigset_t sigset, old_sigset;
    sigfillset(&sigset);
    sigprocmask(SIG_SETMASK, &sigset, &old_sigset);

    pthread_attr_t th_attr;
    res = pthread_attr_init(&th_attr);
    if (res)
        return res;
    pthread_attr_setstacksize(&th_attr, PTHREAD_STACK_MIN);

    // Create timeout thread
    res = pthread_create(th, &th_attr, proc, arg);

    pthread_attr_destroy(&th_attr);
    sigprocmask(SIG_SETMASK, &old_sigset, NULL);

    return res;
}

/*
 * Wait a process with a given timeout.
 * At the end of timeout (if trigger) kill the process.
 * To avoid race we need to wait a specific process, but this is blocking
 * and we use a timeout to implement the wait. Timer functions are per
 * process, not per thread.
 */
static bool wait_process_timeout(pid_t pid, double timeout)
{
    // TODO handle errors

    // compute deadline
    timeout_kill tm = { pid, false };
    clock_gettime(CLOCK_MONOTONIC, &tm.ts);

    double f = floor(timeout);
    tm.ts.tv_sec += f;
    tm.ts.tv_nsec += (timeout - f) * 1000000000.;

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    pthread_cond_init(&tm.cond, &attr);
    pthread_condattr_destroy(&attr);

    pthread_mutex_init(&tm.mtx, NULL);

    // Create timeout thread
    pthread_t th;
    create_thread_minstack(&th, proc_timeout_kill, &tm);

    // Wait the process, we avoid to reap the other process to avoid
    // race conditions. Consider:
    // - process exit;
    // - we reap the thread;
    // - OS reuse the pid;
    // - timeout thread terminate the pid, now reused.
    // Avoiding reaping the process will create a zombie process so
    // the KILL would be directed to that.
    siginfo_t info;
    waitid(P_PID, pid, &info, WEXITED|WNOWAIT);

    // Close the timeout thread
    pthread_mutex_lock(&tm.mtx);
    pthread_cond_broadcast(&tm.cond);
    pthread_mutex_unlock(&tm.mtx);
    pthread_join(th, NULL);

    // Cleanup
    pthread_cond_destroy(&tm.cond);
    pthread_mutex_destroy(&tm.mtx);

    return tm.timed_out;
}

CAMLprim value
caml_pidwaiter_waitpid(value timeout_value, value waiter_value)
{
    CAMLparam2(timeout_value, waiter_value);
    PIDWAITER(waiter, waiter_value);
    double timeout = timeout_value == Val_none ? 0 : Double_val(Some_val(timeout_value));
    pid_t const pid = waiter->pid;

    caml_enter_blocking_section();

    bool timed_out = false;
    if (timeout > 0) {
        timed_out = wait_process_timeout(pid, timeout);
    } else {
        siginfo_t info;
        waitid(P_PID, pid, &info, WEXITED|WNOWAIT);
    }

    caml_leave_blocking_section();

    CAMLreturn(timed_out ? Val_true : Val_false);
}

CAMLprim value
caml_pidwaiter_pid(value waiter_value)
{
    CAMLparam1(waiter_value);
    PIDWAITER(waiter, waiter_value);

    CAMLreturn(Val_int(waiter->pid));
}

CAMLprim value
caml_pidwaiter_set_reap(value waiter_value)
{
    CAMLparam1(waiter_value);
    PIDWAITER(waiter, waiter_value);

    waiter->reaped = true;

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

// vim: expandtab ts=4 sw=4 sts=4:
