/*
 * reimpl/proc_stubs.c
 *
 * See proc_stubs.h.
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/trace.h"
#include "reimpl/proc_stubs.h"
#include "../dynlib.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include "reimpl/errno.h"
#include <so_util/so_util.h>
#include <setjmp.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>

extern so_module so_mod;

int sigprocmask_soloader(int how, const void *set, void *oldset) {
    (void)how; (void)set; (void)oldset;
    // No real POSIX signal delivery to worry about on the Vita for the
    // signals this game cares about (SIGPIPE and friends) -- pretend the
    // mask change succeeded rather than crashing on an unresolved import.
    return 0;
}

unsigned int if_nametoindex_soloader(const char *ifname) {
    (void)ifname;
    return 0; // "no such interface", per POSIX -- a valid, safe failure.
}

void syslog_soloader(int priority, const char *fmt, ...) {
    (void)priority; (void)fmt;
    // no-op: nowhere sensible to send this on the Vita.
}

int execl_soloader(const char *path, const char *arg0, ...) {
    (void)path; (void)arg0;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid_soloader(pid_t pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    errno = ECHILD; // "no child processes" -- always true here.
    return -1;
}

mode_t umask_soloader(mode_t mask) {
    (void)mask;
    return 0; // previous mask was "no restrictions"
}

int dup2_soloader(int fildes, int fildes2) {
    (void)fildes; (void)fildes2;
    errno = ENOSYS;
    return -1;
}

void *getpwuid_soloader(uid_t uid) {
    (void)uid;
    return NULL; // "no such user", standard/expected failure mode.
}

// mprotect() has no real vitasdk implementation. It's most plausibly hit
// by statically-linked OpenSSL's secure-heap / guard-page setup, which
// only runs once the game actually attempts a real TLS handshake -- if
// this were left unresolved, calling it would hit so_util's
// fatal_error(), which itself calls vitaGL/dialog functions that are
// unsafe to call from a non-main thread (GD does its networking on a
// background thread), turning a controlled "unknown symbol" message into
// a hard crash. Pretending success is safe: we don't enforce W^X page
// protection the way this call expects to change it anyway.
int mprotect_soloader(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;
}

int mlock_soloader(const void *addr, size_t len) {
    (void)addr; (void)len;
    return 0; // no real memory paging/swapping to lock against here.
}

long writev_soloader(int fd, const void *iov_raw, int iovcnt) {
    struct iovec_soloader { void *iov_base; size_t iov_len; };
    const struct iovec_soloader *iov = (const struct iovec_soloader *)iov_raw;
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        // Route through write_traced (not raw write()) -- writev() is a
        // very likely path for BoringSSL/curl's scatter-gather socket
        // writes (TLS record header + payload as separate iovecs), and
        // send_traced/recv_traced/read_traced/write_traced all showed zero
        // hits during the profile-loading hang, meaning whatever *does*
        // move bytes on that socket was invisible to us before this.
        ssize_t n = write_traced(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            return (total > 0) ? total : -1;
        }
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

unsigned int alarm_soloader(unsigned int seconds) {
    (void)seconds;
    return 0; // no previous alarm was pending.
}

// bionic's assert() macro calls __assert2(file, line, func, expr) on
// failure. It's unresolved on the Vita, and since this game's network
// code runs on a background thread, letting it hit so_util's
// fatal_error() (which calls vitaGL/dialog functions unsafe off the
// main thread) turns a diagnosable assertion failure into an opaque
// hard crash/coredump. Log the exact assertion before terminating, so
// we can actually see what tripped it.
void __assert2_soloader(const char *file, int line, const char *function, const char *msg) {
    FILE *f = gdash_trace_fopen("ux0:data/gdash/assert_trace.txt", "w");
    if (f) {
        fprintf(f, "assertion failed: %s\n  at %s:%d in %s()\n",
                msg ? msg : "(null)",
                file ? file : "(null)", line,
                function ? function : "(null)");
        fflush(f);
        fclose(f);
    }
    abort_soloader();
}

// bionic's dynamic linker exposes __gnu_Unwind_Find_exidx() so libgcc's
// unwinder can locate the .ARM.exidx table of whichever loaded object a
// given PC belongs to, for C++ exception unwinding / stack-based cleanup.
// Vita's own toolchain has no equivalent (static linking doesn't need
// one), so it was completely missing here -- meaning ANY C++ exception
// thrown inside the loaded (unmodified bionic) libcocos2dcpp.so, e.g.
// from its networking/socket error-handling code, would hit an
// unresolved import the instant unwinding started, which -- like
// mprotect/__assert2 before -- turns into an opaque hard crash when it
// happens on a background thread. We implement it for real: the loaded
// .so's own ELF program headers (already relocated to their live
// addresses by so_util, PT_ARM_EXIDX aside -- that one is left as the
// original link-time-relative offset and needs so_mod.text_base added)
// tell us exactly where its exception-unwind table lives.
unsigned long __gnu_Unwind_Find_exidx_soloader(unsigned long pc, int *pcount) {
    (void)pc;
    for (int i = 0; i < so_mod.ehdr->e_phnum; i++) {
        if (so_mod.phdr[i].p_type == PT_ARM_EXIDX) {
            unsigned long start = so_mod.phdr[i].p_vaddr + so_mod.text_base;
            if (pcount) {
                *pcount = (int)(so_mod.phdr[i].p_memsz / 8); // 8 bytes/entry
            }
            return start;
        }
    }
    if (pcount) {
        *pcount = 0;
    }
    return 0;
}

// bionic's sigsetjmp()/siglongjmp() are unresolved on the Vita (no
// dynamic-linker equivalent needed for statically-linked homebrew), but
// the loaded .so imports them as real out-of-line functions rather than
// inlining them, so we can implement them the same way glibc/cygwin do:
// a genuine wrapper function that itself contains the setjmp(), and
// which longjmp() later resumes into (execution then just falls through
// to our own "return" back to whoever originally called us). bionic's
// sigjmp_buf is allocated by the CALLER (the game's own .so) and is far
// larger than newlib's jmp_buf, so writing our own (smaller) jmp_buf at
// the front of that buffer is safe. Missing this, like mprotect/__assert2
// before it, would hit so_util's fatal_error() -- unsafe off the main
// thread, where GD's networking runs -- turning a rare control-flow
// path (used by OpenSSL/libc error handling) into a silent hang or
// crash instead of just working.
// vitasdk/newlib only defines a real sigjmp_buf type (and sigsetjmp as a
// macro around it) on Cygwin/RTEMS -- it doesn't exist for our target at
// all. Since sigprocmask_soloader() above is already a no-op (there is no
// real POSIX signal delivery to preserve/restore on the Vita for anything
// this game cares about), the "save/restore signal mask" half of
// sig{set,long}jmp is meaningless here anyway -- so we implement them as
// plain setjmp/longjmp over an ordinary jmp_buf. bionic's sigjmp_buf
// (allocated by the caller, the game's own .so) is far larger than
// newlib's jmp_buf, so writing our own jmp_buf at the front of it is safe.
int sigsetjmp_soloader(void *env, int savemask) {
    (void)savemask;
    return setjmp(*(jmp_buf *)env);
}

int siglongjmp_soloader(void *env, int val) {
    longjmp(*(jmp_buf *)env, val ? val : 1);
    return 0; // unreachable -- longjmp() never returns.
}

// vitasdk's real kill() (via newlib's reentrant _kill_r()) only actually
// implements SIGINT(2) and SIGTERM(15) -- any other signal number falls
// through to a hardcoded "not implemented" UDF trap deep inside newlib,
// which shows up as a bare CPU exception with zero context about what
// actually asked for it. bionic's abort() (and libstdc++'s
// std::terminate(), reached from an uncaught C++ exception -- very
// plausible from GD's OpenSSL-based networking error handling, which we
// just made reachable by implementing __gnu_Unwind_Find_exidx and
// sig{set,long}jmp) raises SIGABRT(6) via raise()->kill(getpid(), 6),
// which is exactly this unsupported case. Worse, when it happens on a
// background thread, the resulting fault (caught by our kubridge
// exception handler, which can't safely unwind/skip it either) leaves
// that thread stuck re-faulting rather than dying, which is indistinguishable
// from the outside from an infinite loading screen -- the main thread
// just never gets an answer back. Intercept it here: let the two signals
// the real implementation supports through, and turn anything else into
// a clean abort() -- routed through our own kuKernelRegisterAbortHandler
// (see utils/init.c), which logs the real pc/lr of whoever actually
// called kill()/raise() before terminating, instead of a silent fault loop.
int kill_soloader(pid_t pid, int sig) {
    if (sig == 2 || sig == 15) {
        return kill(pid, sig);
    }

    // NOTE: calling our own abort() here was tried first and does NOT
    // avoid the problem -- newlib's abort() itself calls raise(SIGABRT),
    // which calls this exact same real kill()/_kill_r() path internally,
    // hitting the identical UDF trap just one call deeper. So instead of
    // ever reaching vitasdk's real kill() for an unsupported signal
    // (almost certainly SIGABRT from an uncaught C++ exception or a
    // libc-internal check somewhere in the game's networking code), log
    // the caller's OWN return address -- __builtin_return_address(0) is
    // the address *inside libcocos2dcpp.so* that actually called
    // kill()/raise()/abort(), far more useful than anything we'd see
    // once execution is inside newlib's own internals -- then terminate
    // the whole Vita process cleanly ourselves.
    FILE *f = gdash_trace_fopen("ux0:data/gdash/kill_trace.txt", "w");
    if (f) {
        void *caller = __builtin_return_address(0);
        fprintf(f, "kill(pid=%d, sig=%d) called from %p", (int)pid, sig, caller);
        if ((uintptr_t)caller >= so_mod.text_base) {
            fprintf(f, " (so+0x%x)", (unsigned)((uintptr_t)caller - so_mod.text_base));
        }
        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }

    sceKernelExitProcess(0);
    for (;;) {}
    return 0; // unreachable
}

// bionic's raise()/abort() were being registered as direct passthroughs
// to vitasdk's real raise()/abort() -- which is the ACTUAL bug behind
// the "infinite loading" symptom, not a leftover from before: those call
// vitasdk's internal _raise_r()/_kill_r() reentrant syscall stubs
// directly (a tail call, bypassing the public kill() symbol entirely),
// so hooking "kill" alone was never enough. Route both through the same
// kill_soloader() logic so any non-SIGINT/SIGTERM signal -- almost
// always SIGABRT here -- gets the same clean, logged process exit
// instead of hitting newlib's unsupported-signal UDF trap.
// kill_soloader()'s own __builtin_return_address() only ever sees us
// (raise_soloader/abort_soloader), not the game .so's real call site --
// that's only useful for the direct "kill" hook path. Log our OWN
// caller here instead, which for raise()/abort() (as actually called by
// the game, per the last test round) IS the real .so call site.
static void log_abort_caller(const char *which, void *caller) {
    FILE *f = fopen("ux0:data/gdash/abort_trace.txt", "w");
    if (f) {
        fprintf(f, "%s() called from %p", which, caller);
        if ((uintptr_t)caller >= so_mod.text_base) {
            fprintf(f, " (so+0x%x)", (unsigned)((uintptr_t)caller - so_mod.text_base));
        }
        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }
}

int raise_soloader(int sig) {
    log_abort_caller("raise", __builtin_return_address(0));
    return kill_soloader(getpid(), sig);
}

// abort() is ALWAYS reached (at least for the crash we're chasing) via
// BoringSSL's internal OPENSSL_die(), which is a LOCAL function inside
// libcocos2dcpp.so, not an import we can hook directly -- so
// __builtin_return_address(0) here only ever shows OPENSSL_die's own
// fixed "bl abort" return address, identical no matter which of its ~80
// internal call sites actually triggered it. To find the real culprit,
// read OPENSSL_die's OWN saved lr straight off the stack: its prologue
// is `push {r3, lr}` with nothing popped before the call to abort(), so
// at abort()'s true entry (before OUR OWN prologue disturbs anything),
// [sp+4] holds OPENSSL_die's caller -- the actual assert/check that
// fired. Has to be naked + raw asm: by the time any ordinary C function
// body runs, the compiler's own prologue has already changed sp by an
// amount we can't rely on.
__attribute__((noinline, used)) static void abort_stack_walk_helper(uint32_t die_return_addr, uint32_t die_caller_addr) {
    FILE *f = fopen("ux0:data/gdash/abort_trace.txt", "w");
    if (f) {
        fprintf(f, "abort() called from OPENSSL_die (return 0x%08x)", die_return_addr);
        if (die_return_addr >= so_mod.text_base) {
            fprintf(f, " (so+0x%x)", die_return_addr - (uint32_t)so_mod.text_base);
        }
        fprintf(f, "\n  OPENSSL_die() itself called from 0x%08x", die_caller_addr);
        if (die_caller_addr >= so_mod.text_base) {
            fprintf(f, " (so+0x%x)", die_caller_addr - (uint32_t)so_mod.text_base);
        }
        fprintf(f, "\n");

        // Direct peek at OpenSSL's own internal init-state structs (found
        // via manual disasm of the original .so), captured at the exact
        // moment of the crash -- so_mod.text_base + 0xa982d4 is the
        // OPENSSL_init_crypto "state" struct (+0x54 base-init success
        // flag, +0x58 attempted flag, +0x60 sticky-error flag, +0x4c the
        // CRYPTO_THREAD lock ptr, +0x38 add-all-digests-done flag), and
        // so_mod.text_base + 0xa96b84 is the ssl_digest_methods[] table
        // (+0x60 = the MD5 slot ssl_load_ciphers() is asserting on).
        {
            uint32_t ossl_state = (uint32_t)so_mod.text_base + 0xa982d4;
            uint32_t ssl_dm = (uint32_t)so_mod.text_base + 0xa96b84;
            fprintf(f, "  state[0x54]=%08x [0x58]=%08x [0x60]=%08x [0x4c]=%08x [0x38]=%08x\n",
                    *(volatile uint32_t *)(ossl_state + 0x54),
                    *(volatile uint32_t *)(ossl_state + 0x58),
                    *(volatile uint32_t *)(ossl_state + 0x60),
                    *(volatile uint32_t *)(ossl_state + 0x4c),
                    *(volatile uint32_t *)(ossl_state + 0x38));
            fprintf(f, "  ssl_dm[0x5c]=%08x [0x60]=%08x [0x64]=%08x [0x68]=%08x\n",
                    *(volatile uint32_t *)(ssl_dm + 0x5c),
                    *(volatile uint32_t *)(ssl_dm + 0x60),
                    *(volatile uint32_t *)(ssl_dm + 0x64),
                    *(volatile uint32_t *)(ssl_dm + 0x68));
        }

        fflush(f);
        fclose(f);
    }
    kill_soloader(getpid(), 6 /* SIGABRT */);
    for (;;) {} // unreachable
}

__attribute__((naked)) void abort_soloader(void) {
    __asm__ volatile(
        "push {r4, lr}\n"
        "mov r0, lr\n"
        "ldr r1, [sp, #12]\n"
        "bl abort_stack_walk_helper\n"
    );
}

// See CMakeLists.txt (search "vita-elf-create") for why this exists:
// vita-elf-create needs several KB of free space between the end of the
// R/E segment and the start of the R/W segment to inject Sony's SCE
// metadata, and that space is just whatever page-alignment slack happens
// to fall out of the linker's rounding -- which can very easily be too
// small (it was, repeatedly, while adding the diagnostics in this file).
// Padding .rodata explicitly makes this deterministic instead of being
// at the mercy of exactly how many bytes the last edit happened to add.
const unsigned char linker_segment_padding[12288] = { 0xAA };

