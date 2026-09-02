/*
 * dynlib.c
 *
 * Resolving dynamic imports of the .so.
 *
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

// Disable IDE complaints about _identifiers and global interfaces
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "cppcoreguidelines-interfaces-global-init"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

// Suppress `mktemp` deprecation warning
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "utils/trace.h"
#include "dynlib.h"
extern int gdash_FMOD_System_createStream(void *this, const char *fname, int mode, void *exinfo, int **sound);
extern int gdash_FMOD_Sound_getOpenState(void *this, int *openstate, unsigned *pct, char *starving, char *diskbusy);
extern int gdash_FMOD_System_playSound(void *this, void *sound, void *group, char paused, void **channel);
extern int gdash_FMOD_setPaused(void *, char);
extern int gdash_FMOD_getDSPClock(void *, unsigned long long *, unsigned long long *);
extern int gdash_FMOD_addFadePoint(void *, unsigned long long, float);
extern int gdash_FMOD_setDelay(void *, unsigned long long, unsigned long long, char);
extern int gdash_FMOD_setPitch(void *, float);
extern int gdash_FMOD_mixerSuspend(void *);
extern int gdash_FMOD_mixerResume(void *);
extern int gdash_FMOD_setVolumeRamp(void *, char);
extern int gdash_FMOD_getLength(void *, unsigned *, unsigned);
extern int gdash_FMOD_setPosition(void *, unsigned, unsigned);
extern int gdash_FMOD_setVolume(void *, float);
extern int gdash_FMOD_stop(void *);
extern int gdash_FMOD_getPosition(void *, unsigned *, unsigned);
extern int gdash_FMOD_System_createSound(void *this, const char *fname, int mode, void *exinfo, int **sound);

#include <psp2/kernel/clib.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <netdb.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <zlib.h>
#include <dirent.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/param.h>
#include <signal.h>
#include <grp.h>

#include <so_util/so_util.h>

#include "utils/glutil.h"
#include "utils/utils.h"
#include "utils/logger.h"

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "reimpl/env.h"
#include <errno.h>
#include "reimpl/errno.h"
#include "reimpl/io.h"
#include "reimpl/ioctl.h"
#include "reimpl/log.h"
#include "reimpl/mem.h"
#include "reimpl/pthr.h"
#include "reimpl/sockopt.h"
#include "reimpl/sockaddr_abi.h"
#include "reimpl/proc_stubs.h"
#include "reimpl/sys.h"

#include "fmod_symbols.h"

extern void * _ZNSt9exceptionD2Ev;
extern void * _ZSt17__throw_bad_allocv;
extern void * _ZSt9terminatev;
extern void * _ZdaPv;
extern void * _ZdlPv;
extern void * _Znaj;
extern void * __cxa_allocate_exception;
extern void * __cxa_begin_catch;
extern void * __cxa_end_catch;
extern void * __cxa_free_exception;
extern void * __cxa_rethrow;
extern void * __cxa_throw;
extern void * __gxx_personality_v0;
extern void *_ZNSt8bad_castD1Ev;
extern void *_ZTISt8bad_cast;
extern void *_ZTISt9exception;
extern void *_ZTVN10__cxxabiv117__class_type_infoE;
extern void *_ZTVN10__cxxabiv120__si_class_type_infoE;
extern void *_ZTVN10__cxxabiv121__vmi_class_type_infoE;
extern void *_Znwj;
extern void *__aeabi_atexit;
extern void *__aeabi_d2lz;
extern void *__aeabi_d2ulz;
extern void *__aeabi_dadd;
extern void *__aeabi_dcmpgt;
extern void *__aeabi_dcmplt;
extern void *__aeabi_ddiv;
extern void *__aeabi_dmul;
extern void *__aeabi_f2lz;
extern void *__aeabi_f2ulz;
extern void *__aeabi_i2d;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_l2d;
extern void *__aeabi_l2f;
extern void *__aeabi_ldivmod;
extern void *__aeabi_memclr;
extern void *__aeabi_memcpy;
extern void *__aeabi_memmove;
extern void *__aeabi_memset4;
extern void *__aeabi_memset8;
extern void *__aeabi_memset;
extern void *__aeabi_ui2d;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_ul2d;
extern void *__aeabi_ul2f;
extern void *__aeabi_uldivmod;
extern void *__aeabi_unwind_cpp_pr0;
extern void *__aeabi_unwind_cpp_pr1;
extern void *__cxa_atexit;
extern void *__cxa_finalize;
extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;
extern void *__cxa_pure_virtual;
extern void *__gnu_ldivmod_helper;
extern void *__gnu_unwind_frame;
extern void *__srget;
extern void *__stack_chk_fail;
extern void *__stack_chk_guard;
extern void *__swbuf;

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

static FILE __sF_fake[3];

char* load_file(char const* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open file %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* source = (char*)malloc(length + 1);
    if (!source) {
        fprintf(stderr, "Memory allocation error\n");
        fclose(file);
        return NULL;
    }

    fread(source, 1, length, file);
    fclose(file);

    source[length] = '\0'; // Null-terminate the string
    return source;
}

char fixed_shader[] = {
    "if (_lensCircleStrength > 0.0) {\n \
    float dist = distance(v_texCoord * _textureScaleInv, _lensCircleOrigin);\n \
    float k;\n \
    if (_lensCircleStart == _lensCircleEnd) {\n \
        if (dist >= _lensCircleEnd) {\n \
            k = _lensCircleStrength;\n \
        } else {\n \
            k = 0.0f;\n \
        }\n \
    } else {\n \
        k = _lensCircleStrength * (1.0 - smoothstep(_lensCircleEnd, _lensCircleStart, dist));\n \
    }\n \
    if (_lensCircleAdditive) gl_FragColor.rgb = gl_FragColor.rgb + (_lensCircleTint * k);\n \
    else gl_FragColor.rgb = gl_FragColor.rgb * (1.0 - k) + (_lensCircleTint * k);\n \
}\n \
}"
};
// this is a really stupid way to fix it but fuck it lol
void glShaderSource_soloader(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {
    if(strstr(string[1], "// SHOCKWAVE") != NULL) {
        char *tmp = malloc(strlen(string[1]) + 8 * 1024);
        strcpy(tmp, string[1]);

        char *s2 = strstr(tmp, "if (_lensCircleStrength > 0.0) {");
        memcpy(s2, fixed_shader, strlen(fixed_shader) + 1);
        
        glShaderSource(shader, 1, &tmp, NULL);
        free(tmp);
    }
    else {
        glShaderSource(shader, count, string, length);
    }
}

void __stack_chk_fail_fake() {
	// Some versions of libyoyo.so apparently stack smash on Startup, with this workaround we prevent the app from crashing
	logv_debug("stack smashing detected!! on: %p\n", __builtin_return_address(0));
}

// --- Network call tracing (persistent, non-debug-gated) -------------------
// The game's own networking (curl statically linked inside libcocos2dcpp.so)
// calls these directly through the PLT hooks below. Logging what it actually
// does tells us whether it even tries, and where/how it fails.
// Serialize our own socket/connect/getaddrinfo import hooks. The loaded
// .so's statically-linked OpenSSL/BoringSSL performs lazy, NON-thread-safe
// one-time cipher/digest table initialization (ssl_load_ciphers(), called
// from an internal SSL_library_init-equivalent routine) the very first
// time any networking code path is exercised. If two threads both enter
// that path at nearly the same time (e.g. a background thread opening a
// connection while the main thread does too), the table can be observed
// half-populated by one thread while the other clears/rebuilds it,
// producing a NULL ssl_digest_methods[SSL_MD_MD5_IDX] and a fatal
// OPENSSL_die() abort deep inside code we cannot hook directly (it's all
// intra-.so, not imported). We can't patch that code, but we DO own every
// external entry point that can trigger it -- so force full serialization
// here as a mitigation for the race.
static pthread_mutex_t g_net_hook_mutex = PTHREAD_MUTEX_INITIALIZER;

static FILE *net_trace_file(void) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/net_trace.txt", "w");
    }
    return f;
}

int socket_traced(int domain, int type, int protocol) {
    pthread_mutex_lock(&g_net_hook_mutex);
    int ret = socket(domain, type, protocol);
    int sv_errno = errno;
    FILE *f = net_trace_file();
    if (f) { fprintf(f, "socket(domain=%d, type=%d, proto=%d) = %d (errno=%d)\n",
                      domain, type, protocol, ret, sv_errno); fflush(f); }
    if (ret >= 0) gdash_net_track_fd(ret, 1);
    pthread_mutex_unlock(&g_net_hook_mutex);
    errno = sv_errno;
    return ret;
}

int connect_traced(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    pthread_mutex_lock(&g_net_hook_mutex);
    FILE *f = net_trace_file();

    // The loaded .so is unmodified bionic ARM code -- any sockaddr_in it
    // builds itself (rather than one we handed back via getaddrinfo) uses
    // bionic's 2-byte sin_family layout, not the Vita's BSD-style
    // sin_len+sin_family layout. Translate a local copy before touching
    // sin_family/sin_addr ourselves or handing it to the real connect().
    char addr_buf[128];
    const struct sockaddr *real_addr = addr;
    if (addr && addrlen > 0 && addrlen <= sizeof(addr_buf)) {
        memcpy(addr_buf, addr, addrlen);
        sockaddr_bionic_to_vita((struct sockaddr *)addr_buf, addrlen);
        real_addr = (struct sockaddr *)addr_buf;
    }

    char ipstr[64] = "?";
    int port = -1;
    if (real_addr && real_addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)real_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof(ipstr));
        port = ntohs(sin->sin_port);
    }
    if (f) { fprintf(f, "connect(fd=%d, %s:%d) starting...\n", sockfd, ipstr, port); fflush(f); }

    // DIAGNOSTIC: how long does our own synchronous connect+poll actually
    // take? If it eats up several seconds of wall-clock time, curl's own
    // (independent, already-ticking since it queued this transfer)
    // per-transfer timeout could expire the instant we return "success",
    // causing it to abort immediately after connect() without ever
    // attempting the TLS handshake -- which is exactly what we're seeing
    // (SSL_do_handshake() confirmed never called). Time the whole function
    // and the poll() call separately.
    struct timeval t_start, t_before_poll, t_after_poll, t_end;
    gettimeofday(&t_start, NULL);

    // The game's own non-blocking-connect setup relies on fcntl(), which
    // (even fixed to use the Vita-correct SO_NONBLOCK sockopt) never even
    // gets called before connect() -- so connect() runs fully blocking, with
    // no timeout enforcement of its own, and can hang for a very long time
    // (BSD default connect timeout) if the remote doesn't respond as
    // expected. Force our own bounded non-blocking connect + poll here so we
    // never hang forever regardless of what the caller did/didn't set up.
    int nonblock = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock));

    int ret = connect(sockfd, real_addr, addrlen);
    int conn_errno = errno;

    if (ret < 0 && conn_errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        gettimeofday(&t_before_poll, NULL);
        int poll_ret = poll(&pfd, 1, 8000); // 8s bound, instead of forever
        gettimeofday(&t_after_poll, NULL);
        long poll_ms = (t_after_poll.tv_sec - t_before_poll.tv_sec) * 1000L
                     + (t_after_poll.tv_usec - t_before_poll.tv_usec) / 1000L;
        if (f) { fprintf(f, "  connect in progress, poll() = %d revents=0x%x took=%ldms\n", poll_ret, pfd.revents, poll_ms); fflush(f); }

        if (poll_ret > 0 && (pfd.revents & POLLOUT)) {
            int so_error = 0;
            socklen_t elen = sizeof(so_error);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &elen);
            if (so_error == 0) {
                ret = 0;
            } else {
                ret = -1;
                conn_errno = so_error;
            }
        } else {
            ret = -1;
            conn_errno = ETIMEDOUT;
        }
    }

    // Restore blocking mode -- the caller (curl) still thinks it never
    // changed, and later send()/recv() calls expect the mode it originally
    // set (or didn't set, i.e. blocking).
    nonblock = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock));

    gettimeofday(&t_end, NULL);
    long total_ms = (t_end.tv_sec - t_start.tv_sec) * 1000L
                   + (t_end.tv_usec - t_start.tv_usec) / 1000L;
    if (f) {
        fprintf(f, "connect(fd=%d, %s:%d) = %d (errno=%d) total_took=%ldms\n", sockfd, ipstr, port, ret, conn_errno, total_ms);
        fflush(f);
    }
    errno = conn_errno;
    pthread_mutex_unlock(&g_net_hook_mutex);
    return ret;
}

int getaddrinfo_traced(const char *node, const char *service,
                        const struct addrinfo *hints, struct addrinfo **res) {
    pthread_mutex_lock(&g_net_hook_mutex);
    int ret = getaddrinfo(node, service, hints, res);
    // The Vita's getaddrinfo() fills sockaddr_in with BSD-style
    // sin_len+sin_family; the loaded bionic .so expects plain 2-byte
    // sin_family. Without this translation every resolved address looks
    // like garbage to the game and gets silently discarded/retried.
    if (ret == 0 && res && *res) {
        addrinfo_list_vita_to_bionic(*res);
    }
    int sv_errno = errno;
    FILE *f = net_trace_file();
    if (f) { fprintf(f, "getaddrinfo(node=%s, service=%s) = %d (errno=%d)\n",
                      node ? node : "(null)", service ? service : "(null)", ret, sv_errno); fflush(f); }
    pthread_mutex_unlock(&g_net_hook_mutex);
    errno = sv_errno;
    return ret;
}

struct hostent *gethostbyname_traced(const char *name) {
    struct hostent *ret = gethostbyname(name);
    FILE *f = net_trace_file();
    if (f) { fprintf(f, "gethostbyname(name=%s) = %p (errno=%d)\n",
                      name ? name : "(null)", (void*)ret, errno); fflush(f); }
    return ret;
}

// Bionic MSG_* flag values differ from the Vita's (newlib) ones, and
// libcurl's plain-HTTP send path always passes MSG_NOSIGNAL (bionic
// 0x4000), which sceNet does not know at all -> send() failed with
// errno 106 and curl reported "Send failure" on every Newgrounds song
// download (audio.ngfiles.com is plain HTTP). Translate the flags we can
// and drop the ones that have no Vita equivalent.
static int msg_flags_bionic_to_vita(int b) {
    int v = 0;
    if (b & 0x01)  v |= MSG_OOB;
    if (b & 0x02)  v |= MSG_PEEK;
    if (b & 0x40)  v |= MSG_DONTWAIT;   // bionic MSG_DONTWAIT
    if (b & 0x100) v |= MSG_WAITALL;    // bionic MSG_WAITALL
    // 0x4000 MSG_NOSIGNAL, 0x8000 MSG_MORE, 0x20 MSG_TRUNC, ...: dropped
    return v;
}

ssize_t send_traced(int sockfd, const void *buf, size_t len, int flags) {
    ssize_t ret = send(sockfd, buf, len, msg_flags_bionic_to_vita(flags));
    FILE *f = net_trace_file();
    if (f) { fprintf(f, "send(fd=%d, len=%u, flags=0x%x) = %d (errno=%d)\n", sockfd, (unsigned)len, flags, (int)ret, errno); fflush(f); }
    return ret;
}

ssize_t recv_traced(int sockfd, void *buf, size_t len, int flags) {
    ssize_t ret = recv(sockfd, buf, len, msg_flags_bionic_to_vita(flags));
    FILE *f = net_trace_file();
    if (f) { fprintf(f, "recv(fd=%d, len=%u, flags=0x%x) = %d (errno=%d)\n", sockfd, (unsigned)len, flags, (int)ret, errno); fflush(f); }
    return ret;
}
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tracked-socket-fd set (see dynlib.h). The game's TLS/HTTP layer never
// showed up calling our traced send()/recv() at all (net_trace.txt has zero
// send/recv lines across the whole repeated-connect-cycle hang on the
// profile screen) -- so it must be using read()/write() on the socket fd
// instead. Those import slots are shared with ALL file I/O, so we only log
// when the fd is a known socket, tracked here.
#define GDASH_MAX_TRACKED_FDS 32
static int g_tracked_fds[GDASH_MAX_TRACKED_FDS];
static pthread_mutex_t g_tracked_fds_mutex = PTHREAD_MUTEX_INITIALIZER;

void gdash_net_track_fd(int fd, int is_socket) {
    pthread_mutex_lock(&g_tracked_fds_mutex);
    if (is_socket) {
        int slot = -1;
        for (int i = 0; i < GDASH_MAX_TRACKED_FDS; i++) {
            if (g_tracked_fds[i] == fd) { slot = -2; break; } // already tracked
            if (slot < 0 && g_tracked_fds[i] == 0) slot = i;
        }
        if (slot >= 0) g_tracked_fds[slot] = fd + 1; // +1 so fd=0 isn't confused with "empty"
    } else {
        for (int i = 0; i < GDASH_MAX_TRACKED_FDS; i++) {
            if (g_tracked_fds[i] == fd + 1) { g_tracked_fds[i] = 0; break; }
        }
    }
    pthread_mutex_unlock(&g_tracked_fds_mutex);
}

int gdash_net_is_tracked_fd(int fd) {
    int found = 0;
    pthread_mutex_lock(&g_tracked_fds_mutex);
    for (int i = 0; i < GDASH_MAX_TRACKED_FDS; i++) {
        if (g_tracked_fds[i] == fd + 1) { found = 1; break; }
    }
    pthread_mutex_unlock(&g_tracked_fds_mutex);
    return found;
}


// inet_pton with bionic address-family constants. Bionic AF_INET6 is 10,
// Vita/newlib AF_INET6 is 28, so the .so's inet_pton(AF_INET6=10, host)
// reached the Vita implementation with an unknown family and returned -1
// (error) instead of 0 ("not an address"). curl only adds the TLS SNI
// extension when BOTH Curl_inet_pton(AF_INET, host) and
// Curl_inet_pton(AF_INET6, host) return 0; the stray -1 made it treat
// "www.boomlings.com" as an IP literal and skip SNI -- the decoded
// ClientHello (tls_hello.hex) had every extension except server_name,
// which Cloudflare answers with alert 40 handshake_failure.
#include <arpa/inet.h>
int inet_pton_soloader(int af, const char *src, void *dst) {
    if (!src || !dst) { errno = EINVAL; return -1; }
    if (af == 2 /* AF_INET, same on both */) {
        int r = inet_pton(AF_INET, src, dst);
        return r > 0 ? 1 : 0;
    }
    if (af == 10 /* bionic AF_INET6 */) {
        if (!strchr(src, ':'))
            return 0;                       // can't be an IPv6 literal
        int r = inet_pton(AF_INET6, src, dst);
        return r > 0 ? 1 : 0;
    }
    errno = EAFNOSUPPORT;
    return -1;
}

ssize_t read_traced(int fd, void *buf, size_t len) {
    ssize_t ret = read(fd, buf, len);
    int sv_errno = errno;
    if (gdash_net_is_tracked_fd(fd)) {
        FILE *f = net_trace_file();
        if (f) {
            fprintf(f, "read(fd=%d, len=%u) = %d (errno=%d)", fd, (unsigned)len, (int)ret, sv_errno);
            if (ret > 0) {
                fprintf(f, " bytes[0..%d)=\"", (int)(ret < 32 ? ret : 32));
                for (ssize_t i = 0; i < ret && i < 32; i++) {
                    unsigned char c = ((unsigned char *)buf)[i];
                    fprintf(f, (c >= 32 && c < 127) ? "%c" : "\\x%02x", c);
                }
                fprintf(f, "\"");
            }
            fprintf(f, "\n");
            fflush(f);
        }
    }
    errno = sv_errno;
    return ret;
}

ssize_t write_traced(int fd, const void *buf, size_t len) {
    ssize_t ret = write(fd, buf, len);
    int sv_errno = errno;
    if (gdash_net_is_tracked_fd(fd)) {
        FILE *f = net_trace_file();
        if (f) {
            fprintf(f, "write(fd=%d, len=%u) = %d (errno=%d)", fd, (unsigned)len, (int)ret, sv_errno);
            // Full hex dump of TLS handshake records (content type 0x16) so the
            // ClientHello's cipher suites / extensions (SNI, groups...) can be
            // decoded offline: Cloudflare answers it with alert 40
            // (handshake_failure) on every attempt.
            if (len > 5 && ((const unsigned char *)buf)[0] == 0x16) {
                FILE *h = gdash_trace_fopen("ux0:data/gdash/tls_hello.hex", "a");
                if (h) {
                    fprintf(h, "fd=%d len=%u\n", fd, (unsigned)len);
                    for (size_t i = 0; i < len && i < 2048; i++) fprintf(h, "%02x", ((const unsigned char *)buf)[i]);
                    fprintf(h, "\n");
                    fclose(h);
                }
            }
            if (len > 0) {
                size_t show = len < 32 ? len : 32;
                fprintf(f, " bytes[0..%u)=\"", (unsigned)show);
                for (size_t i = 0; i < show; i++) {
                    unsigned char c = ((const unsigned char *)buf)[i];
                    fprintf(f, (c >= 32 && c < 127) ? "%c" : "\\x%02x", c);
                }
                fprintf(f, "\"");
            }
            fprintf(f, "\n");
            fflush(f);
        }
    }
    errno = sv_errno;
    return ret;
}

// ---------------------------------------------------------------------------
// Static analysis of the connect()-then-immediately-close() pattern (see
// close_trace.txt: caller is always so+0x5fb6a3, a generic
// Close(this,fd)-style helper reached only via a vtable/function-pointer
// slot -- never a direct "bl", so we can't trace its own caller the same
// way) turned up a very suggestive detail: a sibling error path in the
// same source-level function (around so+0x5fa832, right before it grabs
// that same close vtable slot) sets a fixed value 0x2a (42) into a
// register that's very plausibly a libcurl CURLcode -- and 42 is exactly
// CURLE_ABORTED_BY_CALLBACK. That's the code curl returns when its own
// progress/timeout callback (Curl_now()-based, i.e. driven by
// clock_gettime(CLOCK_MONOTONIC)/gettimeofday()) decides a transfer should
// be aborted -- e.g. because it *thinks* a timeout has already elapsed.
// If the Vita's clock backing these calls ever jumps or misbehaves right
// after the (real, ~100-300ms) blocking TCP connect our connect_traced
// performs, curl could see an apparent multi-second/negative jump and
// immediately abort the request before ever touching the socket for I/O --
// which matches every single symptom we've seen. Log only suspicious
// deltas (huge jump or time going backwards) per clock id, so this stays
// cheap despite clock_gettime/gettimeofday being called every frame.
static FILE *clock_trace_file(void) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/clock_trace.txt", "w");
    }
    return f;
}

static void clock_trace_check(const char *who, clockid_t clk_id, long sec, long nsec) {
    static long last_sec[8] = {0};
    static long last_nsec[8] = {0};
    static int have_last[8] = {0};
    static int logged = 0;
    int idx = (clk_id >= 0 && clk_id < 8) ? (int)clk_id : 7;
    long delta_ms = 0;
    int suspicious = 0;
    if (have_last[idx]) {
        long dsec = sec - last_sec[idx];
        long dnsec = nsec - last_nsec[idx];
        delta_ms = dsec * 1000 + dnsec / 1000000;
        if (delta_ms < 0 || delta_ms > 2000) suspicious = 1;
    } else {
        have_last[idx] = 1;
    }
    last_sec[idx] = sec;
    last_nsec[idx] = nsec;
    if (suspicious || logged < 8) {
        logged++;
        FILE *f = clock_trace_file();
        if (f) {
            fprintf(f, "%s(clk_id=%d) = %ld.%09ld  delta_ms=%ld%s\n",
                    who, (int)clk_id, sec, nsec, delta_ms, suspicious ? "  <== SUSPICIOUS" : "");
            fflush(f);
        }
    }
}

int clock_gettime_traced(clockid_t clk_id, struct timespec *tp) {
    int ret = clock_gettime(clk_id, tp);
    if (ret == 0 && tp) clock_trace_check("clock_gettime", clk_id, tp->tv_sec, tp->tv_nsec);
    return ret;
}

int gettimeofday_traced(struct timeval *tv, void *tz) {
    int ret = gettimeofday(tv, tz);
    if (ret == 0 && tv) clock_trace_check("gettimeofday", (clockid_t)6 /* own bucket */, tv->tv_sec, (long)tv->tv_usec * 1000);
    return ret;
}
// ---------------------------------------------------------------------------

ssize_t sendmsg_traced(int sockfd, const struct msghdr *msg, int flags) {
    ssize_t ret = sendmsg(sockfd, msg, msg_flags_bionic_to_vita(flags));
    int sv_errno = errno;
    FILE *f = net_trace_file();
    if (f) {
        size_t total_len = 0;
        if (msg) for (size_t i = 0; i < msg->msg_iovlen; i++) total_len += msg->msg_iov[i].iov_len;
        fprintf(f, "sendmsg(fd=%d, iovlen=%d, total=%zu) = %zd (errno=%d)\n",
                sockfd, msg ? (int)msg->msg_iovlen : -1, total_len, ret, sv_errno);
        fflush(f);
    }
    errno = sv_errno;
    return ret;
}

ssize_t recvmsg_traced(int sockfd, struct msghdr *msg, int flags) {
    ssize_t ret = recvmsg(sockfd, msg, msg_flags_bionic_to_vita(flags));
    int sv_errno = errno;
    FILE *f = net_trace_file();
    if (f) {
        size_t total_len = 0;
        if (msg) for (size_t i = 0; i < msg->msg_iovlen; i++) total_len += msg->msg_iov[i].iov_len;
        fprintf(f, "recvmsg(fd=%d, iovlen=%d, total=%zu) = %zd (errno=%d)\n",
                sockfd, msg ? (int)msg->msg_iovlen : -1, total_len, ret, sv_errno);
        fflush(f);
    }
    errno = sv_errno;
    return ret;
}
// ---------------------------------------------------------------------------

int bind_traced(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    char addr_buf[128];
    const struct sockaddr *real_addr = addr;
    if (addr && addrlen > 0 && addrlen <= sizeof(addr_buf)) {
        memcpy(addr_buf, addr, addrlen);
        sockaddr_bionic_to_vita((struct sockaddr *)addr_buf, addrlen);
        real_addr = (struct sockaddr *)addr_buf;
    }
    return bind(sockfd, real_addr, addrlen);
}

ssize_t sendto_traced(int sockfd, const void *buf, size_t len, int flags,
                       const struct sockaddr *dest_addr, socklen_t addrlen) {
    char addr_buf[128];
    const struct sockaddr *real_addr = dest_addr;
    if (dest_addr && addrlen > 0 && addrlen <= sizeof(addr_buf)) {
        memcpy(addr_buf, dest_addr, addrlen);
        sockaddr_bionic_to_vita((struct sockaddr *)addr_buf, addrlen);
        real_addr = (struct sockaddr *)addr_buf;
    }
    return sendto(sockfd, buf, len, msg_flags_bionic_to_vita(flags), real_addr, addrlen);
}

int accept_traced(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int ret = accept(sockfd, addr, addrlen);
    if (ret >= 0 && addr && addrlen) {
        sockaddr_vita_to_bionic(addr, *addrlen);
    }
    return ret;
}

ssize_t recvfrom_traced(int sockfd, void *buf, size_t len, int flags,
                         struct sockaddr *src_addr, socklen_t *addrlen) {
    ssize_t ret = recvfrom(sockfd, buf, len, msg_flags_bionic_to_vita(flags), src_addr, addrlen);
    if (ret >= 0 && src_addr && addrlen) {
        sockaddr_vita_to_bionic(src_addr, *addrlen);
    }
    return ret;
}

int getsockname_traced(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int ret = getsockname(sockfd, addr, addrlen);
    if (ret == 0 && addr && addrlen) {
        sockaddr_vita_to_bionic(addr, *addrlen);
    }
    return ret;
}

int getpeername_traced(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int ret = getpeername(sockfd, addr, addrlen);
    if (ret == 0 && addr && addrlen) {
        sockaddr_vita_to_bionic(addr, *addrlen);
    }
    return ret;
}
// ---------------------------------------------------------------------------

int __atomic_dec(volatile int *ptr) {
    return __sync_fetch_and_sub (ptr, 1);
}

int __atomic_inc(volatile int *ptr) {
    return __sync_fetch_and_add (ptr, 1);
}

int __system_property_get(const char* name, char* value) {
    logv_error("__system_property_get(name: \"%s\")", name);
    return 0;
}

int sigaction(int signal, const struct sigaction* bionic_new_action, struct sigaction* bionic_old_action) {
    logv_error("sigaction: %i", signal);
    return 0;
}

void *dlsym_fake(void *restrict handle, const char *restrict symbol) {
    // Usage example:
    // if (strcmp("AMotionEvent_getAxisValue", symbol) == 0)
    //    return &AMotionEvent_getAxisValue;

    // Persistent (non-debug-gated) log, since dlopen() is faked to "succeed"
    // (see the "dlopen" hook returning ret1 below) -- if the game relies on
    // dlopen+dlsym for something (e.g. a system libcurl.so), it'll think it
    // worked and then silently get NULL function pointers from here.
    {
        static FILE *dlsym_log = NULL;
        static int dlsym_log_tried = 0;
        if (!dlsym_log_tried) {
            dlsym_log_tried = 1;
            dlsym_log = gdash_trace_fopen("ux0:data/gdash/dlsym_requests.txt", "w");
        }
        if (dlsym_log) {
            fprintf(dlsym_log, "%s\n", symbol);
            fflush(dlsym_log);
        }
    }

    logv_error("Symbol %s not found", symbol);
    return NULL;
}
so_default_dynlib default_dynlib[] = {
        // Common C/C++ internals
        { "_ZNSt8bad_castD1Ev", (uintptr_t)&_ZNSt8bad_castD1Ev },
        { "_ZNSt9exceptionD2Ev", (uintptr_t)&_ZNSt9exceptionD2Ev },
        { "_ZSt17__throw_bad_allocv", (uintptr_t)&_ZSt17__throw_bad_allocv },
        { "_ZSt9terminatev", (uintptr_t)&_ZSt9terminatev },
        { "_ZTISt8bad_cast", (uintptr_t)&_ZTISt8bad_cast },
        { "_ZTISt9exception", (uintptr_t)&_ZTISt9exception },
        { "_ZTVN10__cxxabiv117__class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv117__class_type_infoE },
        { "_ZTVN10__cxxabiv120__si_class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv120__si_class_type_infoE },
        { "_ZTVN10__cxxabiv121__vmi_class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv121__vmi_class_type_infoE },
        { "_ZdaPv", (uintptr_t)&_ZdaPv },
        { "_ZdlPv", (uintptr_t)&_ZdlPv },
        { "_Znaj", (uintptr_t)&_Znaj },
        { "_Znwj", (uintptr_t)&_Znwj },
        { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
        { "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz },
        { "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
        { "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
        { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
        { "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
        { "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
        { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
        { "__aeabi_f2lz", (uintptr_t)&__aeabi_f2lz },
        { "__aeabi_f2ulz", (uintptr_t)&__aeabi_f2ulz },
        { "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
        { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
        { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
        { "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
        { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
        { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
        { "__aeabi_memclr", (uintptr_t)&__aeabi_memclr },
        { "__aeabi_memclr4", (uintptr_t)&__aeabi_memclr },
        { "__aeabi_memclr8", (uintptr_t)&__aeabi_memclr },
        { "__aeabi_memcpy", (uintptr_t)&__aeabi_memcpy },
        { "__aeabi_memcpy4", (uintptr_t)&__aeabi_memcpy },
        { "__aeabi_memcpy8", (uintptr_t)&__aeabi_memcpy },
        { "__aeabi_memmove", (uintptr_t)&__aeabi_memmove },
        { "__aeabi_memmove4", (uintptr_t)&__aeabi_memmove },
        { "__aeabi_memmove8", (uintptr_t)&__aeabi_memmove },
        { "__aeabi_memset", (uintptr_t)&__aeabi_memset },
        { "__aeabi_memset4",  (uintptr_t)&__aeabi_memset4 },
        { "__aeabi_memset8", (uintptr_t)&__aeabi_memset8 },
        { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
        { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
        { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
        { "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
        { "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
        { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
        { "__aeabi_unwind_cpp_pr0", (uintptr_t)&__aeabi_unwind_cpp_pr0 },
        { "__aeabi_unwind_cpp_pr1", (uintptr_t)&__aeabi_unwind_cpp_pr1 },
        { "__atomic_dec", (uintptr_t)&__atomic_dec },
        { "__atomic_inc", (uintptr_t)&__atomic_inc },
        { "__cxa_allocate_exception", (uintptr_t)&__cxa_allocate_exception },
        { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
        { "__cxa_begin_catch", (uintptr_t)&__cxa_begin_catch },
        { "__cxa_end_catch", (uintptr_t)&__cxa_end_catch },
        { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
        { "__cxa_free_exception", (uintptr_t)&__cxa_free_exception },
        { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
        { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
        { "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
        { "__cxa_rethrow", (uintptr_t)&__cxa_rethrow },
        { "__cxa_throw", (uintptr_t)&__cxa_throw },
        { "__gnu_ldivmod_helper", (uintptr_t)&__gnu_ldivmod_helper },
        { "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
        { "__google_potentially_blocking_region_begin", (uintptr_t)&ret0 },
        { "__google_potentially_blocking_region_end", (uintptr_t)&ret0 },
        { "__gxx_personality_v0", (uintptr_t)&__gxx_personality_v0 },
        { "__sF", (uintptr_t)&__sF_fake },
        { "__srget", (uintptr_t)&__srget },
        { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_fake },
        { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },
        { "__swbuf", (uintptr_t)&__swbuf },
        { "__system_property_get", (uintptr_t)&__system_property_get },


        // ctype
        { "_ctype_", (uintptr_t)&BIONIC_ctype_ },
        { "_tolower_tab_", (uintptr_t)&BIONIC_tolower_tab_ },
        { "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_ },
        { "isalnum", (uintptr_t)&isalnum },
        { "isalpha", (uintptr_t)&isalpha },
        { "isblank", (uintptr_t)&isblank },
        { "iscntrl", (uintptr_t)&iscntrl },
        { "isgraph", (uintptr_t)&isgraph },
        { "islower", (uintptr_t)&islower },
        { "isprint", (uintptr_t)&isprint },
        { "ispunct", (uintptr_t)&ispunct },
        { "isspace", (uintptr_t)&isspace },
        { "isupper", (uintptr_t)&isupper },
        { "isxdigit", (uintptr_t)&isxdigit },
        { "tolower", (uintptr_t)&tolower },
        { "toupper", (uintptr_t)&toupper },


        // Android SDK standard logging
        { "__android_log_print", (uintptr_t)&__android_log_print },
        { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
        { "__android_log_write", (uintptr_t)&__android_log_write },


        // Math
        { "acos", (uintptr_t)&acos },
        { "acosf", (uintptr_t)&acosf },
        { "asin", (uintptr_t)&asin },
        { "asinh", (uintptr_t)&asinh },
        { "asinf", (uintptr_t)&asinf },
        { "atan", (uintptr_t)&atan },
        { "atan2", (uintptr_t)&atan2 },
        { "atan2f", (uintptr_t)&atan2f },
        { "atanf", (uintptr_t)&atanf },
        { "ceil", (uintptr_t)&ceil },
        { "ceilf", (uintptr_t)&ceilf },
        { "cos", (uintptr_t)&cos },
        { "cosf", (uintptr_t)&cosf },
        { "exp", (uintptr_t)&exp },
        { "exp2", (uintptr_t)&exp2 },
        { "exp2f", (uintptr_t)&exp2f },
        { "expf", (uintptr_t)&expf },
        { "floor", (uintptr_t)&floor },
        { "floorf", (uintptr_t)&floorf },
        { "fmod", (uintptr_t)&fmod },
        { "fmodf", (uintptr_t)&fmodf },
        { "fmax", (uintptr_t)&fmax },
        { "fmaxf", (uintptr_t)&fmaxf },
        { "fmaxl", (uintptr_t)&fmaxl },
        { "fmin", (uintptr_t)&fmin },
        { "fminf", (uintptr_t)&fminf },
        { "fminl", (uintptr_t)&fminl },
        { "frexp", (uintptr_t)&frexp },
        { "ldexp", (uintptr_t)&ldexp },
        { "ldexpf", (uintptr_t)&ldexpf },
        { "log", (uintptr_t)&log },
        { "log10", (uintptr_t)&log10 },
        { "log10f", (uintptr_t)&log10f },
        { "logf", (uintptr_t)&logf },
        { "lrint", (uintptr_t)&lrint },
        { "lrintf", (uintptr_t)&lrintf },
        { "lround", (uintptr_t)&lround },
        { "lroundf", (uintptr_t)&lroundf },
        { "modf", (uintptr_t)&modf },
        { "pow", (uintptr_t)&pow },
        { "powf", (uintptr_t)&powf },
        { "rint", (uintptr_t)&rint },
        { "rintf", (uintptr_t)&rintf },
        { "round", (uintptr_t)&round },
        { "roundf", (uintptr_t)&roundf },
        { "scalbn", (uintptr_t)&scalbn },
        { "scalbnf", (uintptr_t)&scalbnf },
        { "sin", (uintptr_t)&sin },
        { "sincos", (uintptr_t)&sincos },
        { "sincosf", (uintptr_t)&sincosf },
        { "sinf", (uintptr_t)&sinf },
        { "sinh", (uintptr_t)&sinh },
        { "sqrt", (uintptr_t)&sqrt },
        { "sqrtf", (uintptr_t)&sqrtf },
        { "tan", (uintptr_t)&tan },
        { "tanf", (uintptr_t)&tanf },
        { "tanh", (uintptr_t)&tanh },
        { "trunc", (uintptr_t)&trunc },
        { "truncf", (uintptr_t)&truncf },


        // Process / signal / misc (needed by OpenSSL entropy seeding
        // and SIGPIPE handling around send()/recv())
        { "getpid", (uintptr_t)&getpid },
        { "getuid", (uintptr_t)&getuid },
        { "geteuid", (uintptr_t)&geteuid },
        { "getgid", (uintptr_t)&getgid },
        { "getegid", (uintptr_t)&getegid },
        { "socketpair", (uintptr_t)&socketpair },
        { "fork", (uintptr_t)&fork },
        { "kill", (uintptr_t)&kill_soloader },
        { "setuid", (uintptr_t)&setuid },
        { "setgid", (uintptr_t)&setgid },
        { "initgroups", (uintptr_t)&initgroups },
        { "getnameinfo", (uintptr_t)&getnameinfo },
        { "gai_strerror", (uintptr_t)&gai_strerror },
        { "basename", (uintptr_t)&basename },
        { "sigprocmask", (uintptr_t)&sigprocmask_soloader },
        { "sigsetjmp", (uintptr_t)&sigsetjmp_soloader },
        { "siglongjmp", (uintptr_t)&siglongjmp_soloader },
        { "__isnanf", (uintptr_t)&__isnanf },
        { "__fpclassifyd", (uintptr_t)&__fpclassifyd },
        { "if_nametoindex", (uintptr_t)&if_nametoindex_soloader },
        { "syslog", (uintptr_t)&syslog_soloader },
        { "execl", (uintptr_t)&execl_soloader },
        { "waitpid", (uintptr_t)&waitpid_soloader },
        { "umask", (uintptr_t)&umask_soloader },
        { "dup2", (uintptr_t)&dup2_soloader },
        { "getpwuid", (uintptr_t)&getpwuid_soloader },
        { "mprotect", (uintptr_t)&mprotect_soloader },
        { "mlock", (uintptr_t)&mlock_soloader },
        { "writev", (uintptr_t)&writev_soloader },
        { "alarm", (uintptr_t)&alarm_soloader },
        { "bsearch", (uintptr_t)&bsearch },
        { "isdigit", (uintptr_t)&isdigit },
        { "setbuf", (uintptr_t)&setbuf },
        { "__assert2", (uintptr_t)&__assert2_soloader },
        { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx_soloader },

        // Sockets
        { "accept", (uintptr_t)&accept_traced },
        { "bind", (uintptr_t)&bind_traced },
        { "connect", (uintptr_t)&connect_traced },
        { "freeaddrinfo", (uintptr_t)&freeaddrinfo },
        { "getaddrinfo", (uintptr_t)&getaddrinfo_traced },
        { "gethostbyaddr", (uintptr_t)&gethostbyaddr },
        { "gethostbyname", (uintptr_t)&gethostbyname_traced },
        { "gethostname", (uintptr_t)&gethostname },
        { "getpeername", (uintptr_t)&getpeername_traced },
        { "getservbyname", (uintptr_t)&getservbyname },
        { "getsockname", (uintptr_t)&getsockname_traced },
        { "getsockopt", (uintptr_t)&getsockopt_soloader },
        { "inet_aton", (uintptr_t)&inet_aton },
        { "inet_ntoa", (uintptr_t)&inet_ntoa },
        { "inet_ntop", (uintptr_t)&inet_ntop },
        { "listen", (uintptr_t)&listen },
        { "poll", (uintptr_t)&poll },
        { "recv", (uintptr_t)&recv_traced },
        { "recvfrom", (uintptr_t)&recvfrom_traced },
        { "recvmsg", (uintptr_t)&recvmsg_traced },
        { "select", (uintptr_t)&select },
        { "send", (uintptr_t)&send_traced },
        { "sendmsg", (uintptr_t)&sendmsg_traced },
        { "sendto", (uintptr_t)&sendto_traced },
        { "setsockopt", (uintptr_t)&setsockopt_soloader },
        { "shutdown", (uintptr_t)&shutdown },
        { "socket", (uintptr_t)&socket_traced },
        

        // Memory
        { "calloc", (uintptr_t)&calloc },
        { "free", (uintptr_t)&free },
        { "malloc", (uintptr_t)&malloc },
        { "memalign", (uintptr_t)&memalign },
        { "memcmp", (uintptr_t)&memcmp },
        { "memcpy", (uintptr_t)&memcpy },
        { "memmem", (uintptr_t)&memmem },
        { "memmove", (uintptr_t)&memmove },
        { "memset", (uintptr_t)&memset },
        { "mmap", (uintptr_t)&mmap },
        { "munmap", (uintptr_t)&munmap },
        { "realloc", (uintptr_t)&realloc },
        { "valloc", (uintptr_t)&valloc },
        

        // IO
        { "close", (uintptr_t)&close_soloader },
        { "closedir", (uintptr_t)&closedir_soloader },
        { "fclose", (uintptr_t)&fclose_soloader },
        { "fcntl", (uintptr_t)&fcntl_soloader },
        { "fopen", (uintptr_t)&fopen_soloader },
        { "fstat", (uintptr_t)&fstat_soloader },
        { "ioctl", (uintptr_t)&ioctl_soloader },
        { "open", (uintptr_t)&open_soloader },
        { "opendir", (uintptr_t)&opendir_soloader },
        { "readdir", (uintptr_t)&readdir_soloader },
        { "readdir_r", (uintptr_t)&readdir_r_soloader },
        { "stat", (uintptr_t)&stat_soloader },

        #ifdef USE_SCELIBC_IO
            { "fdopen", (uintptr_t)&sceLibcBridge_fdopen },
            { "feof", (uintptr_t)&sceLibcBridge_feof },
            { "ferror", (uintptr_t)&sceLibcBridge_ferror },
            { "fflush", (uintptr_t)&sceLibcBridge_fflush },
            { "fgetc", (uintptr_t)&sceLibcBridge_fgetc },
            { "fgetpos", (uintptr_t)&sceLibcBridge_fgetpos },
            { "fgets", (uintptr_t)&sceLibcBridge_fgets },
            { "fputc", (uintptr_t)&sceLibcBridge_fputc },
            { "fputs", (uintptr_t)&sceLibcBridge_fputs },
            { "fread", (uintptr_t)&sceLibcBridge_fread },
            { "freopen", (uintptr_t)&sceLibcBridge_freopen },
            { "fseek", (uintptr_t)&sceLibcBridge_fseek },
            { "fsetpos", (uintptr_t)&sceLibcBridge_fsetpos },
            { "ftell", (uintptr_t)&sceLibcBridge_ftell },
            { "fwrite", (uintptr_t)&sceLibcBridge_fwrite },
            { "getc", (uintptr_t)&sceLibcBridge_getc },
            { "getwc", (uintptr_t)&sceLibcBridge_getwc },
            { "putc", (uintptr_t)&sceLibcBridge_putc },
            { "putchar", (uintptr_t)&sceLibcBridge_putchar },
            { "puts", (uintptr_t)&sceLibcBridge_puts },
            { "putwc", (uintptr_t)&sceLibcBridge_putwc },
            { "setvbuf", (uintptr_t)&sceLibcBridge_setvbuf },
            { "ungetc", (uintptr_t)&sceLibcBridge_ungetc },
            { "ungetwc", (uintptr_t)&sceLibcBridge_ungetwc },
        #else
            { "fdopen", (uintptr_t)&fdopen },
            { "feof", (uintptr_t)&feof },
            { "ferror", (uintptr_t)&ferror },
            { "fflush", (uintptr_t)&fflush },
            { "fgetc", (uintptr_t)&fgetc },
            { "fgetpos", (uintptr_t)&fgetpos },
            { "fgets", (uintptr_t)&fgets },
            { "fputc", (uintptr_t)&fputc },
            { "fputs", (uintptr_t)&fputs },
            { "fread", (uintptr_t)&fread },
            { "freopen", (uintptr_t)&freopen },
            { "fseek", (uintptr_t)&fseek },
            { "fsetpos", (uintptr_t)&fsetpos },
            { "ftell", (uintptr_t)&ftell },
            { "fwrite", (uintptr_t)&fwrite },
            { "getc", (uintptr_t)&getc },
            { "getwc", (uintptr_t)&getwc },
            { "putc", (uintptr_t)&putc },
            { "putchar", (uintptr_t)&putchar },
            { "puts", (uintptr_t)&puts },
            { "putwc", (uintptr_t)&putwc },
            { "setvbuf", (uintptr_t)&setvbuf },
            { "ungetc", (uintptr_t)&ungetc },
            { "ungetwc", (uintptr_t)&ungetwc },
        #endif

        { "access", (uintptr_t)&access_soloader },
        { "chdir", (uintptr_t)&chdir },
        { "chmod", (uintptr_t)&chmod },
        { "dup", (uintptr_t)&dup },
        { "fileno", (uintptr_t)&fileno },
        { "fseeko", (uintptr_t)&fseeko }, // TODO: wrap normal fseek for SceLibc version?
        { "ftello", (uintptr_t)&ftello },
        { "ftruncate", (uintptr_t)&ftruncate },
        { "getcwd", (uintptr_t)&getcwd },
        { "lseek", (uintptr_t)&lseek },
        { "lstat", (uintptr_t)&lstat },
        { "mkdir", (uintptr_t)&mkdir_soloader },
        { "pipe", (uintptr_t)&pipe },
        { "read", (uintptr_t)&read_traced },
        { "realpath", (uintptr_t)&realpath },
        { "remove", (uintptr_t)&remove_soloader },
        { "rename", (uintptr_t)&rename_soloader },
        { "rewind", (uintptr_t)&rewind },
        { "rmdir", (uintptr_t)&rmdir_soloader },
        { "truncate", (uintptr_t)&truncate },
        { "unlink", (uintptr_t)&unlink_soloader },
        { "write", (uintptr_t)&write_traced },

        // *printf, *scanf
        { "snprintf", (uintptr_t)&snprintf },
        { "sprintf", (uintptr_t)&sprintf },
        { "vasprintf", (uintptr_t)&vasprintf },
        { "vprintf", (uintptr_t)&vprintf },
        { "vsnprintf", (uintptr_t)&vsnprintf },
        { "vsprintf", (uintptr_t)&vsprintf },
        { "vsscanf", (uintptr_t)&vsscanf },
        { "vswprintf", (uintptr_t)&vswprintf },
        { "printf", (uintptr_t)&sceClibPrintf },
        { "swprintf", (uintptr_t)&swprintf },

        #ifdef USE_SCELIBC_IO
            { "fprintf", (uintptr_t)&sceLibcBridge_fprintf },
            { "fscanf", (uintptr_t)&sceLibcBridge_fscanf },
            { "sscanf", (uintptr_t)&sceLibcBridge_sscanf },
            { "vfprintf", (uintptr_t)&sceLibcBridge_vfprintf },
        #else
            { "fprintf", (uintptr_t)&fprintf },
            { "fscanf", (uintptr_t)&fscanf },
            { "sscanf", (uintptr_t)&sscanf },
            { "vfprintf", (uintptr_t)&vfprintf },
        #endif


        // EGL
        { "eglBindAPI", (uintptr_t)&eglBindAPI },
        { "eglChooseConfig", (uintptr_t)&eglChooseConfig },
        { "eglCreateContext", (uintptr_t)&eglCreateContext },
        { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurface },
        { "eglDestroyContext", (uintptr_t)&eglDestroyContext },
        { "eglDestroySurface", (uintptr_t)&eglDestroySurface },
        { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
        { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
        { "eglGetError", (uintptr_t)&eglGetError },
        { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
        { "eglInitialize", (uintptr_t)&eglInitialize },
        { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
        { "eglQuerySurface", (uintptr_t)&eglQuerySurface },
        { "eglSwapBuffers", (uintptr_t)&eglSwapBuffers },
        { "eglTerminate", (uintptr_t)&eglTerminate },


        // OpenGL
        { "glActiveTexture", (uintptr_t)&glActiveTexture },
        { "glAlphaFunc", (uintptr_t) &glAlphaFunc },
        { "glAlphaFuncx", (uintptr_t)&glAlphaFuncx },
        { "glAttachShader", (uintptr_t)&glAttachShader },
        { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
        { "glBindBuffer", (uintptr_t)&glBindBuffer },
        { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
        { "glBindFramebufferOES", (uintptr_t)&glBindFramebuffer },
        { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
        { "glBindFramebufferOES", (uintptr_t)&glBindFramebuffer },
        { "glBindTexture", (uintptr_t)&glBindTexture },
        { "glBlendEquation", (uintptr_t)&glBlendEquation },
        { "glBlendEquationOES", (uintptr_t)&glBlendEquation },
        { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate },
        { "glBlendFunc", (uintptr_t)&glBlendFunc },
        { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
        { "glBufferData", (uintptr_t)&glBufferData },
        { "glBufferSubData", (uintptr_t)&glBufferSubData },
        { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
        { "glClear", (uintptr_t)&glClear },
        { "glClearColor", (uintptr_t)&glClearColor },
        { "glClearColorx", (uintptr_t)&glClearColorx },
        { "glClearDepthf", (uintptr_t)&glClearDepthf },
        { "glClearDepthx", (uintptr_t)&glClearDepthx },
        { "glClearStencil", (uintptr_t)&glClearStencil },
        { "glClientActiveTexture", (uintptr_t)&glClientActiveTexture },
        { "glColor4f", (uintptr_t)&glColor4f },
        { "glColor4x", (uintptr_t)&glColor4x },
        { "glColorMask", (uintptr_t)&glColorMask },
        { "glColorPointer", (uintptr_t)&glColorPointer },
#ifdef USE_CG_SHADERS
        { "glCompileShader", (uintptr_t)&glCompileShaderHook },
#else
        { "glCompileShader", (uintptr_t)&glCompileShader },
#endif
        { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
        { "glCompressedTexSubImage2D", (uintptr_t)&ret0 },
        { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D },
        { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D },
        { "glCreateProgram", (uintptr_t)&glCreateProgram },
        { "glCreateShader", (uintptr_t)&glCreateShader },
        { "glCullFace", (uintptr_t)&glCullFace },
        { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
        { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteFramebuffersOES", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
        { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
        { "glDeleteRenderbuffersOES", (uintptr_t)&glDeleteRenderbuffers },
        { "glDeleteShader", (uintptr_t)&glDeleteShader },
        { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
        { "glDepthFunc", (uintptr_t)&glDepthFunc },
        { "glDepthMask", (uintptr_t)&glDepthMask },
        { "glDepthRangef", (uintptr_t) &glDepthRangef },
        { "glDetachShader", (uintptr_t)&ret0 },
        { "glDisable", (uintptr_t)&glDisable },
        { "glDisableClientState", (uintptr_t)&glDisableClientState },
        { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
        { "glDrawArrays", (uintptr_t)&glDrawArrays },
        { "glDrawElements", (uintptr_t)&glDrawElements },
        { "glEnable", (uintptr_t)&glEnable },
        { "glEnableClientState", (uintptr_t)&glEnableClientState },
        { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
        { "glFlush", (uintptr_t)&glFlush },
        { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferRenderbufferOES", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
        { "glFramebufferTexture2DOES", (uintptr_t) &glFramebufferTexture2D },
        { "glFrontFace", (uintptr_t)&glFrontFace },
        { "glGenBuffers", (uintptr_t)&glGenBuffers },
        { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
        { "glGenFramebuffersOES", (uintptr_t)&glGenFramebuffers },
        { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
        { "glGenRenderbuffersOES", (uintptr_t)&glGenRenderbuffers },
        { "glGenTextures", (uintptr_t)&glGenTextures },
        { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
        { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib },
        { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform },
        { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
        { "glGetError", (uintptr_t)&glGetError },
        { "glGetFloatv", (uintptr_t)&glGetFloatv },
        { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
        { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
        { "glIsEnabled", (uintptr_t)&glIsEnabled },
        { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
        { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
        { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
        { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
        { "glGetShaderSource", (uintptr_t)&glGetShaderSource },
        { "glGetString", (uintptr_t)&glGetString },
        { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
        { "glHint", (uintptr_t)&glHint },
        { "glLightModelxv", (uintptr_t)&glLightModelxv },
        { "glLightx", (uintptr_t)&ret0 },
        { "glLightxv", (uintptr_t)&glLightxv },
        { "glLineWidth", (uintptr_t)&glLineWidth },
        { "glLinkProgram", (uintptr_t)&glLinkProgram },
        { "glLoadIdentity", (uintptr_t)&glLoadIdentity },
        { "glLoadMatrixf", (uintptr_t)&glLoadMatrixf },
        { "glLoadMatrixx", (uintptr_t)&glLoadMatrixx },
        { "glMaterialf", (uintptr_t)&glMaterialf },
        { "glMaterialfv", (uintptr_t)&glMaterialfv },
        { "glMaterialx", (uintptr_t)&ret0 },
        { "glMaterialxv", (uintptr_t)&glMaterialxv },
        { "glMatrixMode", (uintptr_t)&glMatrixMode },
        { "glMultMatrixf", (uintptr_t)&glMultMatrixf },
        { "glNormalPointer", (uintptr_t)&glNormalPointer },
        { "glPixelStorei", (uintptr_t)&ret0 },
        { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
        { "glPopMatrix", (uintptr_t)&glPopMatrix },
        { "glPushMatrix", (uintptr_t)&glPushMatrix },
        { "glReadPixels", (uintptr_t)&glReadPixels },
        { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
        { "glRenderbufferStorageOES", (uintptr_t)&glRenderbufferStorage },
        { "glRotatef", (uintptr_t)&glRotatef },
        { "glScalef", (uintptr_t)&glScalef },
        { "glScissor", (uintptr_t)&glScissor },
        { "glShadeModel", (uintptr_t)&glShadeModel },
#ifdef USE_CG_SHADERS
        { "glShaderSource", (uintptr_t)&glShaderSourceHook },
#else
        { "glShaderSource", (uintptr_t)&glShaderSource_soloader },
#endif
        { "glStencilFunc", (uintptr_t)&glStencilFunc },
        { "glStencilFuncSeparate", (uintptr_t)&glStencilFuncSeparate },
        { "glStencilMask", (uintptr_t)&glStencilMask },
        { "glStencilOp", (uintptr_t)&glStencilOp },
        { "glStencilOpSeparate", (uintptr_t)&glStencilOpSeparate },
        { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
        { "glTexEnvfv", (uintptr_t)&glTexEnvfv },
        { "glTexEnvi", (uintptr_t)&glTexEnvi },
        { "glTexEnvx", (uintptr_t)&glTexEnvx },
        { "glTexEnvxv", (uintptr_t)&glTexEnvxv },
        { "glTexImage2D", (uintptr_t)&glTexImage2D },
        { "glTexParameterf", (uintptr_t)&glTexParameterf },
        { "glTexParameteri", (uintptr_t)&glTexParameteri },
        { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
        { "glTranslatef", (uintptr_t)&glTranslatef },
        { "glUniform1f", (uintptr_t)&glUniform1f },
        { "glUniform1fv", (uintptr_t)&glUniform1fv },
        { "glUniform1i", (uintptr_t)&glUniform1i },
        { "glUniform1iv", (uintptr_t)&glUniform1iv },
        { "glUniform2f", (uintptr_t)&glUniform2f },
        { "glUniform2fv", (uintptr_t)&glUniform2fv },
        { "glUniform2iv", (uintptr_t)&glUniform2iv },
        { "glUniform2i", (uintptr_t)&glUniform2i },
        { "glUniform3f", (uintptr_t)&glUniform3f },
        { "glUniform3fv", (uintptr_t)&glUniform3fv },
        { "glUniform3iv", (uintptr_t)&glUniform3iv },
        { "glUniform3i", (uintptr_t)&glUniform3i },
        { "glUniform4f", (uintptr_t)&glUniform4f },
        { "glUniform4fv", (uintptr_t)&glUniform4fv },
        { "glUniform4iv", (uintptr_t)&glUniform4iv },
        { "glUniform4i", (uintptr_t)&glUniform4i },
        { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv },
        { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
        { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
        { "glUseProgram", (uintptr_t)&glUseProgram },
        { "glValidateProgram", (uintptr_t)&ret0 },
        { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f },
        { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
        { "glVertexPointer", (uintptr_t)&glVertexPointer },
        { "glViewport", (uintptr_t)&glViewport },


        // Pthread
        { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
        { "pthread_attr_init", (uintptr_t) &pthread_attr_init_soloader },
        { "pthread_attr_setdetachstate", (uintptr_t) &pthread_attr_setdetachstate_soloader },
        { "pthread_attr_setstacksize", (uintptr_t) &pthread_attr_setstacksize_soloader },
        { "pthread_cond_broadcast", (uintptr_t) &pthread_cond_broadcast_soloader },
        { "pthread_cond_destroy", (uintptr_t) &pthread_cond_destroy_soloader },
        { "pthread_cond_init", (uintptr_t) &pthread_cond_init_soloader },
        { "pthread_cond_signal", (uintptr_t) &pthread_cond_signal_soloader },
        { "pthread_cond_timedwait", (uintptr_t) &pthread_cond_timedwait_soloader },
        { "pthread_cond_wait", (uintptr_t) &pthread_cond_wait_soloader },
        { "pthread_create", (uintptr_t) &pthread_create_soloader },
        { "pthread_detach", (uintptr_t) &pthread_detach_soloader },
        { "pthread_equal", (uintptr_t) &pthread_equal_soloader },
        { "pthread_exit", (uintptr_t)&pthread_exit },
        { "pthread_getschedparam", (uintptr_t) &pthread_getschedparam_soloader },
        { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
        { "pthread_join", (uintptr_t) &pthread_join_soloader },
        { "pthread_key_create", (uintptr_t)&pthread_key_create },
        { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
        { "pthread_kill", (uintptr_t)&pthread_kill_soloader },
        { "pthread_mutex_destroy", (uintptr_t) &pthread_mutex_destroy_soloader },
        { "pthread_mutex_init", (uintptr_t) &pthread_mutex_init_soloader },
        { "pthread_mutex_lock", (uintptr_t) &pthread_mutex_lock_soloader },
        { "pthread_mutex_trylock", (uintptr_t) &pthread_mutex_trylock_soloader },
        { "pthread_mutex_unlock", (uintptr_t) &pthread_mutex_unlock_soloader },
        { "pthread_mutexattr_destroy", (uintptr_t) &pthread_mutexattr_destroy_soloader },
        { "pthread_mutexattr_init", (uintptr_t) &pthread_mutexattr_init_soloader },
        { "pthread_mutexattr_settype", (uintptr_t) &pthread_mutexattr_settype_soloader },
        { "pthread_once", (uintptr_t)&pthread_once_soloader },
        { "pthread_self", (uintptr_t) &pthread_self_soloader },
        { "pthread_setname_np", (uintptr_t) &pthread_setname_np_soloader },
        { "pthread_setschedparam", (uintptr_t) &pthread_setschedparam_soloader },
        { "pthread_setspecific", (uintptr_t)&pthread_setspecific },
        { "pthread_sigmask", (uintptr_t)&ret0 },

        { "sem_destroy", (uintptr_t) &sem_destroy_soloader },
        { "sem_getvalue", (uintptr_t) &sem_getvalue_soloader },
        { "sem_init", (uintptr_t) &sem_init_soloader },
        { "sem_post", (uintptr_t) &sem_post_soloader },
        { "sem_timedwait", (uintptr_t) &sem_timedwait_soloader },
        { "sem_trywait", (uintptr_t) &sem_trywait_soloader },
        { "sem_wait", (uintptr_t) &sem_wait_soloader },

        { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max },
        { "sched_get_priority_min", (uintptr_t)&sched_get_priority_min },
        { "sched_yield", (uintptr_t)&sched_yield },

        // wchar, wctype
        { "btowc", (uintptr_t)&btowc },
        { "iswalpha", (uintptr_t)&iswalpha },
        { "iswcntrl", (uintptr_t)&iswcntrl },
        { "iswctype", (uintptr_t)&iswctype },
        { "iswdigit", (uintptr_t)&iswdigit },
        { "iswdigit", (uintptr_t)&iswdigit },
        { "iswlower", (uintptr_t)&iswlower },
        { "iswprint", (uintptr_t)&iswprint },
        { "iswpunct", (uintptr_t)&iswpunct },
        { "iswspace", (uintptr_t)&iswspace },
        { "iswupper", (uintptr_t)&iswupper },
        { "iswxdigit", (uintptr_t)&iswxdigit },
        { "mbrlen", (uintptr_t)&mbrlen },
        { "mbrtowc", (uintptr_t)&mbrtowc },
        { "towlower", (uintptr_t)&towlower },
        { "towupper", (uintptr_t)&towupper },
        { "wcrtomb", (uintptr_t)&wcrtomb },
        { "wcscasecmp", (uintptr_t)&wcscasecmp },
        { "wcscmp", (uintptr_t)&wcscmp },
        { "wcscoll", (uintptr_t)&wcscoll },
        { "wcscpy", (uintptr_t)&wcscpy },
        { "wcsftime", (uintptr_t)&wcsftime },
        { "wcslcat", (uintptr_t)&wcslcat },
        { "wcslcpy", (uintptr_t)&wcslcpy },
        { "wcslen", (uintptr_t)&wcslen },
        { "wcsncasecmp", (uintptr_t)&wcsncasecmp },
        { "wcsncpy", (uintptr_t)&wcsncpy },
        { "wcstombs", (uintptr_t)&wcstombs },
        { "wcsxfrm", (uintptr_t)&wcsxfrm },
        { "wctob", (uintptr_t)&wctob },
        { "wctype", (uintptr_t)&wctype },
        { "wmemchr", (uintptr_t)&wmemchr },
        { "wmemcmp", (uintptr_t)&wmemcmp },
        { "wmemcpy", (uintptr_t)&wmemcpy },
        { "wmemmove", (uintptr_t)&wmemmove },
        { "wmemset", (uintptr_t)&wmemset },


        // libdl
        { "dlclose", (uintptr_t)&ret0 },
        { "dlerror", (uintptr_t)&ret0 },
        { "dlopen", (uintptr_t)&ret1 },
        { "dlsym", (uintptr_t)&dlsym_fake },


        // Errno
        { "__errno", (uintptr_t)&__errno_soloader },
        { "strerror", (uintptr_t)&strerror_soloader },
        { "strerror_r", (uintptr_t)&strerror_r_soloader },
        

        // Strings
        { "memchr", (uintptr_t)&memchr },
        { "memrchr", (uintptr_t)&memrchr },
        { "strcasecmp", (uintptr_t)&strcasecmp },
        { "strcat", (uintptr_t)&strcat },
        { "strchr", (uintptr_t)&strchr },
        { "strcmp", (uintptr_t)&strcmp },
        { "strcoll", (uintptr_t)&strcoll },
        { "strcpy", (uintptr_t)&strcpy },
        { "strcspn", (uintptr_t)&strcspn },
        { "strdup", (uintptr_t)&strdup },
        { "strlcat", (uintptr_t)&strlcat },
        { "strlcpy", (uintptr_t)&strlcpy },
        { "strlen", (uintptr_t)&strlen },
        { "strncasecmp", (uintptr_t)&strncasecmp },
        { "strncat", (uintptr_t)&strncat },
        { "strncmp", (uintptr_t)&strncmp },
        { "strncpy", (uintptr_t)&strncpy },
        { "strnlen", (uintptr_t)&strnlen },
        { "strpbrk", (uintptr_t)&strpbrk },
        { "strrchr", (uintptr_t)&strrchr },
        { "strspn", (uintptr_t)&strspn },
        { "strstr", (uintptr_t)&strstr },
        { "strtok", (uintptr_t)&strtok },
        { "strtok_r", (uintptr_t)&strtok_r },
        { "strxfrm", (uintptr_t)&strxfrm },
        

        // Syscalls
        { "syscall", (uintptr_t)&syscall },
        { "sysconf", (uintptr_t)&ret0 },
        { "system", (uintptr_t)&system },


        // Time
        { "clock", (uintptr_t)&clock },
        { "clock_getres", (uintptr_t)&clock_getres },
        { "clock_gettime", (uintptr_t)&clock_gettime_traced },
        { "difftime", (uintptr_t)&difftime },
        { "gettimeofday", (uintptr_t)&gettimeofday_traced },
        { "gmtime", (uintptr_t)&gmtime },
        { "gmtime_r", (uintptr_t)&gmtime_r },
        { "localtime", (uintptr_t)&localtime },
        { "localtime_r", (uintptr_t)&localtime_r },
        { "mktime", (uintptr_t)&mktime },
        { "nanosleep", (uintptr_t)&nanosleep },
        { "strftime", (uintptr_t)&strftime },
        { "time", (uintptr_t)&time },
        { "tzset", (uintptr_t)&tzset },


        // Temp
        { "mkstemp", (uintptr_t)&mkstemp },
        { "mktemp", (uintptr_t)&mktemp },
        { "tmpfile", (uintptr_t)&tmpfile },
        { "tmpnam", (uintptr_t)&tmpnam },


        // stdlib
        { "abort", (uintptr_t)&abort_soloader },
        { "atof", (uintptr_t)&atof },
        { "atoi", (uintptr_t)&atoi },
        { "atol", (uintptr_t)&atol },
        { "atoll", (uintptr_t)&atoll },
        { "exit", (uintptr_t)&exit },
        { "lrand48", (uintptr_t)&lrand48 },
        { "prctl", (uintptr_t)&ret0 },
        { "sleep", (uintptr_t)&sleep },
        { "srand48", (uintptr_t)&srand48 },
        { "strtod", (uintptr_t)&strtod },
        { "strtof", (uintptr_t)&strtof },
        { "strtoimax", (uintptr_t)&strtoimax },
        { "strtol", (uintptr_t)&strtol },
        { "strtold", (uintptr_t)&strtold },
        { "strtoll", (uintptr_t)&strtoll },
        { "strtoul", (uintptr_t)&strtoul },
        { "strtoull", (uintptr_t)&strtoull },
        { "strtoumax", (uintptr_t)&strtoumax },
        { "usleep", (uintptr_t)&usleep },

        #ifdef USE_SCELIBC_IO
            { "qsort", (uintptr_t)&sceLibcBridge_qsort },
            { "rand", (uintptr_t)&sceLibcBridge_rand },
            { "srand", (uintptr_t)&sceLibcBridge_srand },
        #else
            { "qsort", (uintptr_t)&qsort },
            { "rand", (uintptr_t)&rand },
            { "srand", (uintptr_t)&srand },
        #endif


        // Env
        { "getenv", (uintptr_t)&getenv_soloader },
        { "setenv", (uintptr_t)&setenv_soloader },


        // Jmp
        { "setjmp", (uintptr_t)&setjmp }, // TODO: May have different struct size?
        { "longjmp", (uintptr_t)&longjmp }, // TODO: May have different struct size?


        // Signals
        { "bsd_signal", (uintptr_t)&signal },
        { "raise", (uintptr_t)&raise_soloader },
        { "sigaction", (uintptr_t)&sigaction },
        
        
        // Locale
        { "setlocale", (uintptr_t)&setlocale },


        // zlib
        { "adler32", (uintptr_t)&adler32 },
        { "compress", (uintptr_t)&compress },
        { "compressBound", (uintptr_t)&compressBound },
        { "crc32", (uintptr_t)&crc32 },
        { "deflate", (uintptr_t)&deflate },
        { "deflateEnd", (uintptr_t)&deflateEnd },
        { "deflateInit2_", (uintptr_t)&deflateInit2_ },
        { "deflateInit_", (uintptr_t)&deflateInit_ },
        { "deflateReset", (uintptr_t)&deflateReset },
        { "gzclose", (uintptr_t)&gzclose },
        { "gzgets", (uintptr_t)&gzgets },
        { "gzopen", (uintptr_t)&gzopen },
        { "inflate", (uintptr_t)&inflate },
        { "inflateEnd", (uintptr_t)&inflateEnd },
        { "inflateInit2_", (uintptr_t)&inflateInit2_ },
        { "inflateInit_", (uintptr_t)&inflateInit_ },
        { "inflateReset", (uintptr_t)&inflateReset },
        { "uncompress", (uintptr_t)&uncompress },

        // pthread
		{ "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
		{ "pthread_attr_init", (uintptr_t) &pthread_attr_init_soloader },
		{ "pthread_attr_setdetachstate", (uintptr_t) &pthread_attr_setdetachstate_soloader },
		{ "pthread_attr_setstacksize", (uintptr_t) &pthread_attr_setstacksize_soloader },
		{ "pthread_cond_broadcast", (uintptr_t) &pthread_cond_broadcast_soloader },
		{ "pthread_cond_destroy", (uintptr_t) &pthread_cond_destroy_soloader },
		{ "pthread_cond_init", (uintptr_t) &pthread_cond_init_soloader },
		{ "pthread_cond_signal", (uintptr_t) &pthread_cond_signal_soloader },
		{ "pthread_cond_timedwait", (uintptr_t) &pthread_cond_timedwait_soloader },
		{ "pthread_cond_wait", (uintptr_t) &pthread_cond_wait_soloader },
		{ "pthread_create", (uintptr_t) &pthread_create_soloader },
		{ "pthread_detach", (uintptr_t) &pthread_detach_soloader },
		{ "pthread_equal", (uintptr_t) &pthread_equal_soloader },
		{ "pthread_exit", (uintptr_t)&pthread_exit },
		{ "pthread_getschedparam", (uintptr_t) &pthread_getschedparam_soloader },
		{ "pthread_getspecific", (uintptr_t)&pthread_getspecific },
		{ "pthread_join", (uintptr_t) &pthread_join_soloader },
		{ "pthread_key_create", (uintptr_t)&pthread_key_create },
		{ "pthread_key_delete", (uintptr_t)&pthread_key_delete },
		{ "pthread_kill", (uintptr_t)&pthread_kill_soloader },
		{ "pthread_mutex_destroy", (uintptr_t) &pthread_mutex_destroy_soloader },
		{ "pthread_mutex_init", (uintptr_t) &pthread_mutex_init_soloader },
		{ "pthread_mutex_lock", (uintptr_t) &pthread_mutex_lock_soloader },
		{ "pthread_mutex_trylock", (uintptr_t) &pthread_mutex_trylock_soloader },
		{ "pthread_mutex_unlock", (uintptr_t) &pthread_mutex_unlock_soloader },
		{ "pthread_mutexattr_destroy", (uintptr_t) &pthread_mutexattr_destroy_soloader },
		{ "pthread_mutexattr_init", (uintptr_t) &pthread_mutexattr_init_soloader },
		{ "pthread_mutexattr_settype", (uintptr_t) &pthread_mutexattr_settype_soloader },
		{ "pthread_once", (uintptr_t)&pthread_once_soloader },
		{ "pthread_self", (uintptr_t) &pthread_self_soloader },
		{ "pthread_setname_np", (uintptr_t) &pthread_setname_np_soloader },
		{ "pthread_setschedparam", (uintptr_t) &pthread_setschedparam_soloader },
		{ "pthread_setspecific", (uintptr_t)&pthread_setspecific },
		{ "pthread_sigmask", (uintptr_t)&ret0 },
        { "pthread_rwlock_init", (uintptr_t)&pthread_rwlock_init_soloader},
        { "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock_soloader},
        { "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock_soloader},
        { "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock_soloader},
        { "pthread_rwlock_destroy", (uintptr_t)&pthread_rwlock_destroy_soloader},

		{ "sem_destroy", (uintptr_t) &sem_destroy_soloader },
		{ "sem_getvalue", (uintptr_t) &sem_getvalue_soloader },
		{ "sem_init", (uintptr_t) &sem_init_soloader },
		{ "sem_post", (uintptr_t) &sem_post_soloader },
		{ "sem_timedwait", (uintptr_t) &sem_timedwait_soloader },
		{ "sem_trywait", (uintptr_t) &sem_trywait_soloader },
		{ "sem_wait", (uintptr_t) &sem_wait_soloader },

		{ "sched_get_priority_max", (uintptr_t)&sched_get_priority_max },
		{ "sched_get_priority_min", (uintptr_t)&sched_get_priority_min },
		{ "sched_yield", (uintptr_t)&sched_yield },

        //inet
        { "inet_pton", (uintptr_t)&inet_pton_soloader },

        // FMOD
        {"_ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_", (uintptr_t)&_ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_},
        {"_ZN4FMOD14ChannelControl11getUserDataEPPv", (uintptr_t)&_ZN4FMOD14ChannelControl11getUserDataEPPv},
        {"_ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE", (uintptr_t)&_ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE},
        {"_ZN4FMOD3DSP17setParameterFloatEif", (uintptr_t)&_ZN4FMOD3DSP17setParameterFloatEif},
        {"FMOD_System_Create", (uintptr_t)&FMOD_System_Create},
        {"_ZN4FMOD6System10getVersionEPj", (uintptr_t)&_ZN4FMOD6System10getVersionEPj},
        {"FMOD_Debug_Initialize", (uintptr_t)&FMOD_Debug_Initialize},
        {"_ZN4FMOD6System19getStreamBufferSizeEPjS1_", (uintptr_t)&_ZN4FMOD6System19getStreamBufferSizeEPjS1_},
        {"_ZN4FMOD6System16setDSPBufferSizeEji", (uintptr_t)&_ZN4FMOD6System16setDSPBufferSizeEji},
        {"_ZN4FMOD6System16getDSPBufferSizeEPjPi", (uintptr_t)&_ZN4FMOD6System16getDSPBufferSizeEPjPi},
        {"_ZN4FMOD6System17getSoftwareFormatEPiP16FMOD_SPEAKERMODES1_", (uintptr_t)&_ZN4FMOD6System17getSoftwareFormatEPiP16FMOD_SPEAKERMODES1_},
        {"_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi", (uintptr_t)&_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi},
        {"_ZN4FMOD6System4initEijPv", (uintptr_t)&_ZN4FMOD6System4initEijPv},
        {"_ZN4FMOD6System18createChannelGroupEPKcPPNS_12ChannelGroupE", (uintptr_t)&_ZN4FMOD6System18createChannelGroupEPKcPPNS_12ChannelGroupE},
        {"_ZN4FMOD14ChannelControl13setVolumeRampEb", (uintptr_t)&gdash_FMOD_setVolumeRamp},
        {"_ZN4FMOD3DSP18setMeteringEnabledEbb", (uintptr_t)&_ZN4FMOD3DSP18setMeteringEnabledEbb},
        {"_ZN4FMOD12ChannelGroup8addGroupEPS0_bPPNS_13DSPConnectionE", (uintptr_t)&_ZN4FMOD12ChannelGroup8addGroupEPS0_bPPNS_13DSPConnectionE},
        {"_ZN4FMOD6System15createDSPByTypeE13FMOD_DSP_TYPEPPNS_3DSPE", (uintptr_t)&_ZN4FMOD6System15createDSPByTypeE13FMOD_DSP_TYPEPPNS_3DSPE},
        {"_ZN4FMOD3DSP16setParameterBoolEib", (uintptr_t)&_ZN4FMOD3DSP16setParameterBoolEib},
        {"_ZN4FMOD14ChannelControl6addDSPEiPNS_3DSPE", (uintptr_t)&_ZN4FMOD14ChannelControl6addDSPEiPNS_3DSPE},
        {"_ZN4FMOD6System11mixerResumeEv", (uintptr_t)&gdash_FMOD_mixerResume},
        {"_ZN4FMOD6System6updateEv", (uintptr_t)&_ZN4FMOD6System6updateEv},
        {"_ZN4FMOD6System12mixerSuspendEv", (uintptr_t)&gdash_FMOD_mixerSuspend},
        {"_ZN4FMOD14ChannelControl9setPausedEb", (uintptr_t)&gdash_FMOD_setPaused},
        {"FMOD_Channel_GetFadePoints", (uintptr_t)&FMOD_Channel_GetFadePoints},
        {"FMOD_Channel_GetDSPClock", (uintptr_t)&FMOD_Channel_GetDSPClock},
        {"_ZN4FMOD14ChannelControl11getDSPClockEPyS1_", (uintptr_t)&gdash_FMOD_getDSPClock},
        {"FMOD_Channel_RemoveFadePoints", (uintptr_t)&FMOD_Channel_RemoveFadePoints},
        {"_ZN4FMOD14ChannelControl9setVolumeEf", (uintptr_t)&gdash_FMOD_setVolume},
        {"_ZN4FMOD5Sound12getOpenStateEP14FMOD_OPENSTATEPjPbS4_", (uintptr_t)&gdash_FMOD_Sound_getOpenState},
        {"_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (uintptr_t)&gdash_FMOD_System_createStream},
        {"_ZN4FMOD5Sound12setLoopCountEi", (uintptr_t)&_ZN4FMOD5Sound12setLoopCountEi},
        {"_ZN4FMOD5Sound7releaseEv", (uintptr_t)&_ZN4FMOD5Sound7releaseEv},
        {"FMOD_Memory_GetStats", (uintptr_t)&FMOD_Memory_GetStats},
        {"_ZN4FMOD6System11getCPUUsageEP14FMOD_CPU_USAGE", (uintptr_t)&ret0},
        {"_ZN4FMOD14ChannelControl4stopEv", (uintptr_t)&gdash_FMOD_stop},
        {"_ZN4FMOD6System5closeEv", (uintptr_t)&_ZN4FMOD6System5closeEv},
        {"_ZN4FMOD6System7releaseEv", (uintptr_t)&_ZN4FMOD6System7releaseEv},
        {"_ZN4FMOD12ChannelGroup7releaseEv", (uintptr_t)&_ZN4FMOD12ChannelGroup7releaseEv},
        {"FMOD_System_LockDSP", (uintptr_t)&FMOD_System_LockDSP},
        {"_ZN4FMOD7Channel13getLoopPointsEPjjS1_j", (uintptr_t)&_ZN4FMOD7Channel13getLoopPointsEPjjS1_j},
        {"_ZN4FMOD7Channel11getPositionEPjj", (uintptr_t)&gdash_FMOD_getPosition},
        {"FMOD_Channel_GetDelay", (uintptr_t)&FMOD_Channel_GetDelay},
        {"FMOD_System_UnlockDSP", (uintptr_t)&FMOD_System_UnlockDSP},
        {"_ZN4FMOD14ChannelControl11setUserDataEPv", (uintptr_t)&_ZN4FMOD14ChannelControl11setUserDataEPv},
        {"_ZN4FMOD14ChannelControl9getVolumeEPf", (uintptr_t)&_ZN4FMOD14ChannelControl9getVolumeEPf},
        {"_ZN4FMOD14ChannelControl8getPitchEPf", (uintptr_t)&_ZN4FMOD14ChannelControl8getPitchEPf},
        {"_ZN4FMOD7Channel11setPositionEjj", (uintptr_t)&gdash_FMOD_setPosition},
        {"_ZN4FMOD7Channel15getCurrentSoundEPPNS_5SoundE", (uintptr_t)&_ZN4FMOD7Channel15getCurrentSoundEPPNS_5SoundE},
        {"_ZN4FMOD5Sound9getLengthEPjj", (uintptr_t)&gdash_FMOD_getLength},
        {"_ZN4FMOD14ChannelControl8setPitchEf", (uintptr_t)&gdash_FMOD_setPitch},
        {"_ZN4FMOD14ChannelControl9getPausedEPb", (uintptr_t)&_ZN4FMOD14ChannelControl9getPausedEPb},
        {"_ZN4FMOD14ChannelControl12addFadePointEyf", (uintptr_t)&gdash_FMOD_addFadePoint},
        {"_ZN4FMOD7Channel12setLoopCountEi", (uintptr_t)&_ZN4FMOD7Channel12setLoopCountEi},
        {"_ZN4FMOD14ChannelControl8setDelayEyyb", (uintptr_t)&gdash_FMOD_setDelay},
        {"_ZN4FMOD7Channel13setLoopPointsEjjjj", (uintptr_t)&_ZN4FMOD7Channel13setLoopPointsEjjjj},
        {"_ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE", (uintptr_t)&gdash_FMOD_System_playSound},
        {"_ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E", (uintptr_t)&_ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E},
        {"_ZN4FMOD12ChannelGroup14getNumChannelsEPi", (uintptr_t)&_ZN4FMOD12ChannelGroup14getNumChannelsEPi},
        {"_ZN4FMOD12ChannelGroup10getChannelEiPPNS_7ChannelE", (uintptr_t)&_ZN4FMOD12ChannelGroup10getChannelEiPPNS_7ChannelE},
        {"_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (uintptr_t)&gdash_FMOD_System_createSound},
        {"_ZN4FMOD6System7lockDSPEv", (uintptr_t)&_ZN4FMOD6System7lockDSPEv},
        {"_ZN4FMOD6System9unlockDSPEv", (uintptr_t)&_ZN4FMOD6System9unlockDSPEv},
        {"_ZN4FMOD14ChannelControl10getNumDSPsEPi", (uintptr_t)&_ZN4FMOD14ChannelControl10getNumDSPsEPi},
        {"_ZN4FMOD14ChannelControl9removeDSPEPNS_3DSPE", (uintptr_t)&_ZN4FMOD14ChannelControl9removeDSPEPNS_3DSPE},
        {"_ZN4FMOD3DSP7releaseEv", (uintptr_t)&_ZN4FMOD3DSP7releaseEv},
        //
};

void resolve_imports(so_module* mod) {
    __sF_fake[0] = *stdin;
    __sF_fake[1] = *stdout;
    __sF_fake[2] = *stderr;

    {
        FILE *f = gdash_trace_fopen("ux0:data/gdash/hook_addrs.txt", "w");
        if (f) {
            fprintf(f, "connect_traced=%p\n", (void*)&connect_traced);
            fprintf(f, "socket_traced=%p\n", (void*)&socket_traced);
            fprintf(f, "getaddrinfo_traced=%p\n", (void*)&getaddrinfo_traced);
            fprintf(f, "fcntl_soloader=%p\n", (void*)&fcntl_soloader);
            fprintf(f, "real connect()=%p\n", (void*)&connect);
            fprintf(f, "real socket()=%p\n", (void*)&socket);
            fprintf(f, "setsockopt_soloader=%p\n", (void*)&setsockopt_soloader);
            fprintf(f, "getsockopt_soloader=%p\n", (void*)&getsockopt_soloader);
            fprintf(f, "ioctl_soloader=%p\n", (void*)&ioctl_soloader);
            fclose(f);
        }
    }

    so_resolve(mod, default_dynlib, sizeof(default_dynlib), 0);
}