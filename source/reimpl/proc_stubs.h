/*
 * reimpl/proc_stubs.h
 *
 * Safe no-op/failure stubs for bionic process/signal/misc APIs that have
 * no real vitasdk implementation (unlike getpid/getuid/fork/kill/... which
 * do exist in libc.a and are just registered directly). These exist purely
 * so that a call reaching them fails gracefully or no-ops instead of
 * crashing the whole app via so_util's "Unknown symbol" fatal_error().
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_PROC_STUBS_H
#define SOLOADER_PROC_STUBS_H

#include <sys/types.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

int sigprocmask_soloader(int how, const void *set, void *oldset);
unsigned int if_nametoindex_soloader(const char *ifname);
void syslog_soloader(int priority, const char *fmt, ...);
int execl_soloader(const char *path, const char *arg0, ...);
pid_t waitpid_soloader(pid_t pid, int *status, int options);
mode_t umask_soloader(mode_t mask);
int dup2_soloader(int fildes, int fildes2);
void *getpwuid_soloader(uid_t uid);
int siglongjmp_soloader(void *env, int val);
int sigsetjmp_soloader(void *env, int savemask);

int mprotect_soloader(void *addr, size_t len, int prot);
int mlock_soloader(const void *addr, size_t len);
long writev_soloader(int fd, const void *iov, int iovcnt);
unsigned int alarm_soloader(unsigned int seconds);
void __assert2_soloader(const char *file, int line, const char *function, const char *msg) __attribute__((noreturn));
unsigned long __gnu_Unwind_Find_exidx_soloader(unsigned long pc, int *pcount);
int kill_soloader(pid_t pid, int sig);
int raise_soloader(int sig);
void abort_soloader(void) __attribute__((noreturn));

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_PROC_STUBS_H
