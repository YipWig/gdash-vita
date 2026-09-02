/*
 * reimpl/io.c
 *
 * Wrappers and implementations for some of the IO functions.
 *
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/trace.h"
#include "reimpl/io.h"
#include "../dynlib.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include "reimpl/errno.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/rng.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_newlib_to_oflags_musl(int flags);
// dirent64_bionic * dirent_newlib_to_dirent_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_stat_bionic(struct stat * src, stat64_bionic * dst);
#include "_struct_converters.c"

// The Android build of the game (statically-linked OpenSSL) seeds its
// PRNG by reading /dev/urandom, which doesn't exist on the Vita's
// filesystem -- causing that entropy source to silently fail, which in
// turn can make RAND_bytes()/SSL_connect() give up before ever writing a
// TLS ClientHello (a TCP connect() can succeed while zero bytes are ever
// sent -- exactly what was observed). We redirect /dev/urandom and
// /dev/random to a small file seeded once from the Vita's real hardware
// RNG (sceKernelGetRandomNumber).
#define URANDOM_SEED_PATH "ux0:data/gdash/urandom_seed.bin"
#define URANDOM_SEED_SIZE 4096

// Regenerated on every /dev/urandom open (not just once), so: (a) repeat
// TLS handshakes don't all reuse the exact same "random" bytes, and
// (b) a 4KB pool gives a lot of headroom against RAND_poll()/friends
// reading it more than once per open before we can detect EOF.
static void ensure_urandom_seed_file(void) {
    unsigned char buf[URANDOM_SEED_SIZE];
    for (size_t off = 0; off < sizeof(buf); off += 64) {
        size_t chunk = sizeof(buf) - off;
        if (chunk > 64) chunk = 64;
        sceKernelGetRandomNumber(buf + off, chunk);
    }

    FILE *f = fopen(URANDOM_SEED_PATH, "wb");
    if (f) {
        fwrite(buf, 1, sizeof(buf), f);
        fclose(f);
    }
}

static int is_urandom_path(const char *fname) {
    return strcmp(fname, "/dev/urandom") == 0 || strcmp(fname, "/dev/random") == 0;
}

static void trace_urandom(const char *fname) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/urandom_trace.txt", "w");
    }
    if (f) {
        fprintf(f, "redirected: %s\n", fname);
        fflush(f);
    }
}


static FILE *io_trace_file(void) {
    static FILE *tf = NULL; static int tried = 0;
    if (!tried) { tried = 1; tf = gdash_trace_fopen("ux0:data/gdash/fopen_trace.txt", "w"); }
    return tf;
}


// Translate the Android app-private data prefix to DATA_PATH for EVERY
// path-taking file API, not just fopen(). Needed because cocos2d-x's
// Android CCFileUtils treats any path that doesn't start with '/' as
// relative to the APK assets: a native "ux0:..." writable path made
// isFileExist()/stat() look for downloaded songs inside the .apk (never
// found), while fopen() happily wrote them to the memory card. So the
// writable path handed to the game is the Android-style
// /data/data/com.robtopx.geometryjump/songs/ and this translates it.
#define ANDROID_DATA_PREFIX "/data/data/com.robtopx.geometryjump/"
static const char *translate_path(const char *in, char *out, size_t outsz) {
    if (!in) return in;
    if (strncmp(in, ANDROID_DATA_PREFIX, sizeof(ANDROID_DATA_PREFIX) - 1) == 0) {
        snprintf(out, outsz, "%s%s", DATA_PATH, in + sizeof(ANDROID_DATA_PREFIX) - 1);
        // collapse "//" (the game appends "/<id>.mp3" to the writable path)
        for (char *r = out, *w = out; ; r++) { if (*r == '/' && w > out && w[-1] == '/') continue; *w++ = *r; if (!*r) break; }
        return out;
    }
    return in;
}

int access_soloader(const char *path, int mode) {
    char b[512]; const char *p = translate_path(path, b, sizeof b);
    int r = access(p, mode);
    logv_debug("[io] access(%s): %i", p, r);
    return r;
}
int remove_soloader(const char *path) { char b[512]; return remove(translate_path(path, b, sizeof b)); }
int unlink_soloader(const char *path) { char b[512]; return unlink(translate_path(path, b, sizeof b)); }
int rmdir_soloader(const char *path) { char b[512]; return rmdir(translate_path(path, b, sizeof b)); }
int mkdir_soloader(const char *path, int mode) { char b[512]; return mkdir(translate_path(path, b, sizeof b), mode); }
int rename_soloader(const char *a, const char *bb) {
    char b1[512], b2[512];
    return rename(translate_path(a, b1, sizeof b1), translate_path(bb, b2, sizeof b2));
}

FILE *fopen_soloader(char *fname, char *mode) {
    if (is_urandom_path(fname)) {
        trace_urandom(fname);
        ensure_urandom_seed_file();
        return fopen_soloader(URANDOM_SEED_PATH, mode);
    } else if (strcmp(fname, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(fname, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char * fname_real = strdup(fname);
    str_replace(fname_real, "/data/data/com.robtopx.geometryjump/", DATA_PATH);
    for (char *r = fname_real, *w = fname_real; ; r++) { if (*r == '/' && w > fname_real && w[-1] == '/') continue; *w++ = *r; if (!*r) break; }

    #ifdef USE_SCELIBC_IO
        FILE* ret = sceLibcBridge_fopen(fname_real, mode);
    #else
        FILE* ret = fopen(fname_real, mode);
    #endif

    logv_debug("[io] fopen(%s, %s): 0x%x", fname_real, mode, ret);
    {
        FILE *tf = io_trace_file();
        if (tf) { fprintf(tf, "fopen(\"%s\" -> \"%s\", %s) = %p errno=%d\n", fname, fname_real, mode, (void*)ret, errno); fflush(tf); }
    }

    free(fname_real);

    // sceLibcBridge_fopen reports failures through SceLibc's errno, not
    // newlib's, so the .so (which reads errno via our __errno bridge over
    // newlib's) saw errno == 0 after a failed fopen. OpenSSL's
    // OPENSSL_init_crypto(LOAD_CONFIG) fopen()s openssl.cnf, and only
    // tolerates the file being absent if errno == ENOENT; with errno == 0
    // it recorded "error:02001000:system library:fopen" as a hard failure,
    // OPENSSL_init_ssl() failed, SSL_CTX_new() returned NULL and curl gave
    // "SSL: couldn't create a context" (CURLE_OUT_OF_MEMORY) on every
    // HTTPS connection. Report a sane errno for a failed open.
    if (ret == NULL && errno == 0)
        errno = ENOENT;

    return ret;
}

int open_soloader(char *_fname, int flags) {
    if (is_urandom_path(_fname)) {
        trace_urandom(_fname);
        ensure_urandom_seed_file();
        return open_soloader(URANDOM_SEED_PATH, flags);
    } else if (strcmp(_fname, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", flags);
    } else if (strcmp(_fname, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", flags);
    }

    flags = oflags_newlib_to_oflags_musl(flags);
    char pb[512];
    _fname = (char *)translate_path(_fname, pb, sizeof pb);
    int ret = open(_fname, flags);
    logv_debug("[io] open(%s, %x): %i", _fname, flags, ret);
    return ret;
}

int fstat_soloader(int fd, void *statbuf) {
    struct stat st;
    int res = fstat(fd, &st);
    if (res == 0)
        stat_newlib_to_stat_bionic(&st, statbuf);

    logv_debug("[io] fstat(fd#%i): %i", fd, res);
    return res;
}

int stat_soloader(char *_pathname, stat64_bionic *statbuf) {
    struct stat st;
    char pb[512];
    _pathname = (char *)translate_path(_pathname, pb, sizeof pb);
    int res = stat(_pathname, &st);

    if (res == 0)
        stat_newlib_to_stat_bionic(&st, statbuf);

    logv_debug("[io] stat(%s): %i", _pathname, res);
    { FILE *tf = io_trace_file(); if (tf) { fprintf(tf, "stat(\"%s\") = %d errno=%d size=%ld\n", _pathname, res, errno, res == 0 ? (long)st.st_size : -1L); fflush(tf); } }
    return res;
}

int fclose_soloader(FILE * f) {
    #ifdef USE_SCELIBC_IO
        int ret = sceLibcBridge_fclose(f);
    #else
        int ret = fclose(f);
    #endif

    logv_debug("[io] fclose(0x%x): %i", f, ret);
    { FILE *tf = io_trace_file(); if (tf) { fprintf(tf, "fclose(%p) = %d\n", (void*)f, ret); fflush(tf); } }
    return ret;
}

extern so_module so_mod;

int close_soloader(int fd) {
    static FILE *tf = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; tf = gdash_trace_fopen("ux0:data/gdash/close_trace.txt", "w"); }
    // Check tracked-ness (and log a resolvable so+offset for the caller)
    // BEFORE untracking -- the profile-loading hang shows every socket
    // getting connect()ed, TCP_NODELAY/O_NONBLOCK set, then closed again
    // with ZERO send/recv/read/write/writev/sendmsg/recvmsg in between, for
    // both the game's own www.boomlings.com requests AND its
    // www.google.com connectivity probe. Something bails out right after
    // setup, before ever touching the socket for I/O -- we need to know
    // *what*, and the caller of this close() is the best lead we have.
    int was_socket = gdash_net_is_tracked_fd(fd);
    void *caller = __builtin_return_address(0);
    gdash_net_track_fd(fd, 0);
    int ret = close(fd);
    if (tf) {
        if (was_socket) {
            uint32_t off = (uint32_t)caller - (uint32_t)so_mod.text_base;
            fprintf(tf, "close(fd#%d) = %d (errno=%d) [SOCKET] caller=%p (so+0x%x)\n",
                    fd, ret, errno, caller, off);
        } else {
            fprintf(tf, "close(fd#%d) = %d (errno=%d)\n", fd, ret, errno);
        }
        fflush(tf);
    }
    logv_debug("[io] close(fd#%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char pb[512];
    _pathname = (char *)translate_path(_pathname, pb, sizeof pb);
    DIR* ret = opendir(_pathname);
    logv_debug("[io] opendir(\"%s\"): 0x%x", _pathname, ret);
    return ret;
}

struct dirent * readdir_soloader(DIR * dir) {
    struct dirent* ret = readdir(dir);
    log_debug("[io] readdir()");
    return ret;
}

int readdir_r_soloader(DIR *dirp, dirent64_bionic *entry, dirent64_bionic **result) {
    struct dirent dirent_tmp;
    struct dirent* pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_dirent_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    log_debug("[io] readdir_r()");
    return ret;
}

int closedir_soloader(DIR* dir) {
    int ret = closedir(dir);
    logv_debug("[io] closedir(0x%x): %i", dir, ret);
    return ret;
}

// The .so was compiled against Android/bionic headers, where O_NONBLOCK is
// 0x800 (Linux asm-generic value). VitaSDK's newlib/BSD-derived fcntl.h uses
// 0x4000 for O_NONBLOCK instead. F_GETFL/F_SETFL happen to have the same
// numeric values (3/4) on both, so only the flag *value* needs translating.
// This used to be a total no-op stub that always returned 0 without touching
// the real socket -- meaning curl's "set socket non-blocking before connect"
// dance silently did nothing, and every connect() ran fully blocking with no
// curl-side timeout enforcement (curl's timeout logic relies on polling a
// non-blocking socket). That's the most likely explanation for online
// features (HTTPS to www.boomlings.com) appearing to just hang/fail.
#define ANDROID_O_NONBLOCK 0x800

static FILE *fcntl_trace_file(void) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/fcntl_trace.txt", "w");
    }
    return f;
}

// The .so was compiled against Android/bionic headers, where O_NONBLOCK is
// 0x800. VitaSDK's fcntl() operates on regular files via sceIo -- it does
// NOT actually apply to sceNet socket descriptors, which have their own,
// separate non-blocking control: setsockopt(fd, SOL_SOCKET, SO_NONBLOCK, ...).
// So the previous "translate flags and call real fcntl()" fix was still a
// no-op for sockets specifically (curl's use case). This tries the
// socket-specific path first, and only falls back to real fcntl() for
// non-socket fds.
int fcntl_soloader(int fd, int cmd, ...) {
    va_list args;
    va_start(args, cmd);
    int arg = va_arg(args, int);
    va_end(args);

    FILE *tf = fcntl_trace_file();
    if (tf) { fprintf(tf, "fcntl(fd#%i, cmd#%i, arg=0x%x)\n", fd, cmd, arg); fflush(tf); }

    if (cmd == F_GETFL) {
        int val = 0;
        unsigned int len = sizeof(val);
        int ret = getsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &val, &len);
        if (tf) { fprintf(tf, "  getsockopt(SO_NONBLOCK) = %i val=%i\n", ret, val); fflush(tf); }
        if (ret == 0) {
            return val ? ANDROID_O_NONBLOCK : 0;
        }
        int real_flags = fcntl(fd, F_GETFL, 0);
        if (real_flags < 0) return 0;
        int android_flags = 0;
        if (real_flags & O_NONBLOCK) android_flags |= ANDROID_O_NONBLOCK;
        return android_flags;
    } else if (cmd == F_SETFL) {
        int nonblock = (arg & ANDROID_O_NONBLOCK) ? 1 : 0;
        int ret = setsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock));
        if (tf) { fprintf(tf, "  setsockopt(SO_NONBLOCK, %i) = %i (errno=%i)\n", nonblock, ret, errno); fflush(tf); }
        if (ret == 0) return 0;
        int real_flags = 0;
        if (arg & ANDROID_O_NONBLOCK) real_flags |= O_NONBLOCK;
        return fcntl(fd, F_SETFL, real_flags);
    }

    // Unhandled fcntl command -- preserve old fake-success behavior rather
    // than risk passing through a command/arg encoding we haven't verified.
    return 0;
}
