/*
 * main.c
 *
 * Android ARMv7 Shared Libraries loader for PSVita
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021-2023 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/trace.h"
#include "utils/init.h"
#include "utils/dialog.h"
#include "utils/logger.h"
#include "utils/glutil.h"
#include "utils/settings.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <kubridge.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h> 

#include <psp2/touch.h>
#include <psp2/ctrl.h>
#include <psp2/net/netctl.h>

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "reimpl/errno.h"

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <so_util/so_util.h>
#include <psp2/kernel/processmgr.h>
#include <sched.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#include <fmod/fmod.h>

extern FMOD_RESULT F_API __real_FMOD_System_CreateSound(FMOD_SYSTEM *system, const char *name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *exinfo, FMOD_SOUND **sound);
extern FMOD_RESULT F_API __real_FMOD_System_CreateStream(FMOD_SYSTEM *system, const char *name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *exinfo, FMOD_SOUND **sound);

// FMOD gets paths straight from the game. Built-in songs/SFX arrive as
// "<APK dir>/assets/<file>" style paths that the original port rewrote
// onto ASSETS_PATH by skipping a fixed 22-char prefix; downloaded
// Newgrounds songs arrive as the Android-style writable path
// (/data/data/com.robtopx.geometryjump/songs/<id>.mp3, see
// getCocos2dxWritablePath) and were being mangled by that same skip into
// a file that doesn't exist -> level loads, no music. Translate properly.
extern int _ZN4FMOD5Sound9getLengthEPjj(void *this, unsigned *len, unsigned unit);
static FILE *fmod_tf(void);
static void fmod_ts(void);
static void collapse_double_slashes(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '/' && w > s && w[-1] == '/') continue;
        *w++ = *r;
    }
    *w = 0;
}
static void fmod_translate_path(const char *in, char *out, size_t outsz) {
    static const char android_prefix[] = "/data/data/com.robtopx.geometryjump/";
    if (!in) { out[0] = 0; return; }
    if (strncmp(in, android_prefix, sizeof(android_prefix) - 1) == 0) {
        snprintf(out, outsz, "%s%s", DATA_PATH, in + sizeof(android_prefix) - 1);
    } else if (strncmp(in, "ux0:", 4) == 0 || strncmp(in, "app0:", 5) == 0) {
        snprintf(out, outsz, "%s", in);           // already a Vita path
    } else if (strlen(in) > 22) {
        snprintf(out, outsz, "%s%s", ASSETS_PATH, &in[22]);  // legacy asset rewrite
    } else {
        snprintf(out, outsz, "%s%s", ASSETS_PATH, in);
    }
    collapse_double_slashes(out);
}

FMOD_RESULT F_API __wrap_FMOD_System_CreateSound(FMOD_SYSTEM *system, const char *name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *exinfo, FMOD_SOUND **sound) {
    char fname_real[512];
    fmod_translate_path(name_or_data, fname_real, sizeof fname_real);

    int ret = __real_FMOD_System_CreateSound(system, fname_real, mode, exinfo, sound);
    
    //logv_debug("FMOD SOUND RETURN!!::: %i", ret);
    //logv_debug("FMOD SOUND CREATION!!::: %s", fname_real);

    return ret;
}

FMOD_RESULT F_API __wrap_FMOD_System_CreateStream(FMOD_SYSTEM *system, const char *name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO *exinfo, FMOD_SOUND **sound) {
    char fname_real[512];
    fmod_translate_path(name_or_data, fname_real, sizeof fname_real);

    int ret = __real_FMOD_System_CreateStream(system, fname_real, mode, exinfo, sound);

    //logv_debug("FMOD STREAM RETURN!!::: %i", ret);
    //logv_debug("FMOD STREAM CREATION!!::: %s", fname_real);

    return ret;
}


// The game calls FMOD through the C++ API (FMOD::System::createStream /
// createSound), resolved by dynlib straight into libfmodstudio.suprx --
// the --wrap on the C API below never sees those calls. Wrap the C++
// entry points too so downloaded songs' Android-style paths get
// translated before FMOD's own file layer tries to open them.
extern int _ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(void *this, const char *fname, int mode, void *exinfo, int **sound);
extern int _ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(void *this, const char *fname, int mode, void *exinfo, int **sound);
int gdash_FMOD_System_createStream(void *this, const char *fname, int mode, void *exinfo, int **sound) {
    char real[512];
    fmod_translate_path(fname, real, sizeof real);
    const char *use = (fname && !(mode & 0x2800)) ? real : fname;
    // Downloaded songs: the game opens them with FMOD_NONBLOCKING |
    // FMOD_ACCURATETIME. fmod_trace.txt showed the stream then bouncing
    // between OPENSTATE_SETPOSITION(7)/SEEKING(6) for a long time after
    // playSound() -- ACCURATETIME makes every seek in a (VBR) MP3 a full
    // scan, which on the Vita's card is slow enough that the level's music
    // never actually starts. Built-in assets are unaffected. Open custom
    // songs synchronously and let FMOD seek by estimate instead.
    if (use && strstr(use, "/songs/")) {
        mode &= ~(0x4000 /* FMOD_ACCURATETIME */ | 0x10000 /* FMOD_NONBLOCKING */);
    }
    int r = _ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(this, use, mode, exinfo, sound);
    logv_debug("[fmod] createStream(%s -> %s) = %d", fname ? fname : "(null)", real, r);
    if (r == 0 && sound && *sound && use && strstr(use, "/songs/") && fmod_tf()) {
        unsigned len_ms = 0, len_bytes = 0;
        int r1 = _ZN4FMOD5Sound9getLengthEPjj(*sound, &len_ms, 1 /*FMOD_TIMEUNIT_MS*/);
        int r2 = _ZN4FMOD5Sound9getLengthEPjj(*sound, &len_bytes, 8 /*FMOD_TIMEUNIT_RAWBYTES*/);
        fmod_ts(); fprintf(fmod_tf(), "  song length: %u ms (r=%d), %u raw bytes (r=%d)\n", len_ms, r1, len_bytes, r2); fflush(fmod_tf());
    }
    { FILE *tf = fmod_tf(); if (tf) { fmod_ts(); fprintf(tf, "createStream(in=\"%s\" use=\"%s\" mode=0x%x) = %d sound=%p\n", fname ? fname : "(null)", (mode & 0x2800) ? "(memory)" : use, mode, r, sound ? (void*)*sound : NULL); fflush(tf); } }
    return r;
}
int gdash_FMOD_System_createSound(void *this, const char *fname, int mode, void *exinfo, int **sound) {
    char real[512];
    fmod_translate_path(fname, real, sizeof real);
    // mode bit FMOD_OPENMEMORY (0x800) / FMOD_OPENUSER (0x2000): 'fname' is data, not a path.
    const char *use = (fname && !(mode & 0x2800)) ? real : fname;
    int r = _ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(this, use, mode, exinfo, sound);
    logv_debug("[fmod] createSound(%s) = %d", (mode & 0x2800) ? "(memory)" : real, r);
    { FILE *tf = fmod_tf(); if (tf) { fmod_ts(); fprintf(tf, "createSound(in=\"%s\" use=\"%s\" mode=0x%x) = %d\n", fname ? fname : "(null)", (mode & 0x2800) ? "(memory)" : use, mode, r); fflush(tf); } }
    return r;
}


extern int _ZN4FMOD5Sound12getOpenStateEP14FMOD_OPENSTATEPjPbS4_(void *this, int *openstate, unsigned *percentbuffered, char *starving, char *diskbusy);
extern int _ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE(void *this, void *sound, void *group, char paused, void **channel);
static FILE *fmod_tf(void) { static FILE *tf; static int tried; if (!tried) { tried = 1; tf = gdash_trace_fopen("ux0:data/gdash/fmod_trace.txt", "w"); } return tf; }
static void fmod_ts(void) { FILE *tf = fmod_tf(); if (tf) fprintf(tf, "[%8u ms] ", (unsigned)(sceKernelGetProcessTimeLow() / 1000)); }
extern int _ZN4FMOD5Sound9getLengthEPjj(void *this, unsigned *len, unsigned unit);
int gdash_FMOD_getLength(void *this, unsigned *len, unsigned unit) { int r = _ZN4FMOD5Sound9getLengthEPjj(this, len, unit); fmod_ts(); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "getLength(sound=%p, unit=%u) = %d len=%u\n", this, unit, r, len ? *len : 0); fflush(fmod_tf()); } return r; }
int gdash_FMOD_Sound_getOpenState(void *this, int *openstate, unsigned *pct, char *starving, char *diskbusy) {
    int r = _ZN4FMOD5Sound12getOpenStateEP14FMOD_OPENSTATEPjPbS4_(this, openstate, pct, starving, diskbusy);
    static void *last; static int laststate = -99;
    int st = openstate ? *openstate : -1;
    if (fmod_tf() && (this != last || st != laststate)) { fprintf(fmod_tf(), "getOpenState(sound=%p) = %d openstate=%d pct=%u\n", this, r, st, pct ? *pct : 0); fflush(fmod_tf()); last = this; laststate = st; }
    return r;
}
int gdash_FMOD_System_playSound(void *this, void *sound, void *group, char paused, void **channel) {
    int r = _ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE(this, sound, group, paused, channel);
    if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "playSound(sound=%p paused=%d) = %d channel=%p\n", sound, paused, r, channel ? *channel : NULL); fflush(fmod_tf()); }
    return r;
}


extern int _ZN4FMOD14ChannelControl9setPausedEb(void *this, char paused);
extern int _ZN4FMOD7Channel11setPositionEjj(void *this, unsigned pos, unsigned unit);
extern int _ZN4FMOD14ChannelControl9setVolumeEf(void *this, float vol);
extern int _ZN4FMOD7Channel11getPositionEPjj(void *this, unsigned *pos, unsigned unit);
extern int _ZN4FMOD14ChannelControl4stopEv(void *this);
int gdash_FMOD_setPaused(void *this, char paused) { int r = _ZN4FMOD14ChannelControl9setPausedEb(this, paused); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setPaused(ch=%p, %d) = %d\n", this, paused, r); fflush(fmod_tf()); } return r; }
extern int _ZN4FMOD7Channel15getCurrentSoundEPPNS_5SoundE(void *this, void **sound);
int gdash_FMOD_setPosition(void *this, unsigned pos, unsigned unit) {
    // The game's "music offset" setting (user has -5 ms) is added to the
    // level's song start; for a level starting at 0 that yields -5 ms,
    // passed here as an unsigned 4294967291 ms -> past the end of the
    // song -> silence while the position "advances". Editor playtests
    // don't apply the setting, which is why they had music. Clamp
    // negative offsets to 0 and wrap anything past the song's end.
    unsigned req = pos;
    if ((int)pos < 0) pos = 0;
    if (unit == 1) {
        void *snd = NULL; unsigned len = 0;
        if (_ZN4FMOD7Channel15getCurrentSoundEPPNS_5SoundE(this, &snd) == 0 && snd &&
            _ZN4FMOD5Sound9getLengthEPjj(snd, &len, 1) == 0 && len > 0 && pos >= len)
            pos = 0;
    }
    int r = _ZN4FMOD7Channel11setPositionEjj(this, pos, unit);
    if (req != pos && fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setPosition: clamped %u -> %u\n", req, pos); } if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setPosition(ch=%p, %u, unit=%u) = %d\n", this, pos, unit, r); fflush(fmod_tf()); } return r; }
int gdash_FMOD_setVolume(void *this, float vol) { int r = _ZN4FMOD14ChannelControl9setVolumeEf(this, vol); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setVolume(ch=%p, %.2f) = %d\n", this, vol, r); fflush(fmod_tf()); } return r; }
int gdash_FMOD_stop(void *this) { int r = _ZN4FMOD14ChannelControl4stopEv(this); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "stop(ch=%p) = %d\n", this, r); fflush(fmod_tf()); } return r; }
int gdash_FMOD_getPosition(void *this, unsigned *pos, unsigned unit) { int r = _ZN4FMOD7Channel11getPositionEPjj(this, pos, unit); if (fmod_tf()) { fprintf(fmod_tf(), "getPosition(ch=%p) = %d pos=%u\n", this, r, pos ? *pos : 0); fflush(fmod_tf()); } return r; }


extern int _ZN4FMOD14ChannelControl11getDSPClockEPyS1_(void *this, unsigned long long *clock, unsigned long long *parent);
extern int _ZN4FMOD14ChannelControl12addFadePointEyf(void *this, unsigned long long clock, float vol);
extern int _ZN4FMOD14ChannelControl8setDelayEyyb(void *this, unsigned long long start, unsigned long long end, char stopchannels);
extern int _ZN4FMOD14ChannelControl8setPitchEf(void *this, float pitch);
extern int _ZN4FMOD6System12mixerSuspendEv(void *this);
extern int _ZN4FMOD6System11mixerResumeEv(void *this);
extern int _ZN4FMOD14ChannelControl13setVolumeRampEb(void *this, char ramp);
int gdash_FMOD_getDSPClock(void *this, unsigned long long *clock, unsigned long long *parent) {
    int r = _ZN4FMOD14ChannelControl11getDSPClockEPyS1_(this, clock, parent);
    if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "getDSPClock(ch=%p) = %d clock=%llu parent=%llu\n", this, r, clock ? *clock : 0ULL, parent ? *parent : 0ULL); fflush(fmod_tf()); }
    return r;
}
int gdash_FMOD_addFadePoint(void *this, unsigned long long clock, float vol) {
    // Level music is started with a 2 s fade-in done via two
    // fade points on the channel (vol 0 at "now", vol 1 two seconds
    // later, in parent DSP clock units). Decoding runs, the position
    // advances, but nothing is ever heard -- and the editor's playtest,
    // which doesn't use fade points, has music. Confirmed on hardware: with fade points ignored, level
    // music plays. This FMOD build's fade-point handling never brings the
    // volume up, so rely on the plain channel volume instead.
    (void)clock; (void)vol;
    int r = 0; (void)_ZN4FMOD14ChannelControl12addFadePointEyf;
    if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "addFadePoint(ch=%p, clock=%llu, vol=%.2f) = %d\n", this, clock, vol, r); fflush(fmod_tf()); }
    return r;
}
int gdash_FMOD_setDelay(void *this, unsigned long long a, unsigned long long b, char stop) {
    int r = _ZN4FMOD14ChannelControl8setDelayEyyb(this, a, b, stop);
    if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setDelay(ch=%p, start=%llu, end=%llu, stop=%d) = %d\n", this, a, b, stop, r); fflush(fmod_tf()); }
    return r;
}
int gdash_FMOD_setPitch(void *this, float pitch) {
    int r = _ZN4FMOD14ChannelControl8setPitchEf(this, pitch);
    if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setPitch(ch=%p, %.3f) = %d\n", this, pitch, r); fflush(fmod_tf()); }
    return r;
}
int gdash_FMOD_mixerSuspend(void *this) { int r = _ZN4FMOD6System12mixerSuspendEv(this); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "mixerSuspend = %d\n", r); fflush(fmod_tf()); } return r; }
int gdash_FMOD_mixerResume(void *this) { int r = _ZN4FMOD6System11mixerResumeEv(this); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "mixerResume = %d\n", r); fflush(fmod_tf()); } return r; }
int gdash_FMOD_setVolumeRamp(void *this, char ramp) { int r = _ZN4FMOD14ChannelControl13setVolumeRampEb(this, ramp); if (fmod_tf()) { fmod_ts(); fprintf(fmod_tf(), "setVolumeRamp(ch=%p, %d) = %d\n", this, ramp, r); fflush(fmod_tf()); } return r; }

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 24 * 1024 * 1024;
#endif

so_module so_mod;

// See the comment above the hook_thumb() call in main() for context.
static so_hook close_wrapper_hook;

static int GDCloseWrapper_hook(void *thisptr, int fd) {
    void *caller = __builtin_return_address(0);
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/close_wrapper_trace.txt", "w"); }
    if (f) {
        uint32_t off = (uint32_t)caller - (uint32_t)so_mod.text_base;
        fprintf(f, "CloseWrapper(this=%p, fd=%d) caller=%p (so+0x%x)\n", thisptr, fd, caller, off);
        fflush(f);
    }
    return SO_CONTINUE(int, close_wrapper_hook, thisptr, fd);
}

// EVP_get_digestbyname()/OBJ_NAME_get() never find anything that
// EVP_add_digest() just registered, confirmed multiple ways now,
// including a direct memory read of the underlying "names_lh" hash
// table global (so+0xa983a4, independently confirmed via offline ARM
// disassembly to be the SAME address OBJ_NAME_get/add/init all agree
// on -- so it isn't a duplicate-global/relocation bug): that slot reads
// 0/0 (never populated) even right after 8 straight "successful"
// EVP_add_digest() calls. Rather than keep chasing why the table never
// gets created (deep, statically-linked BoringSSL hash-table internals,
// several rounds already spent with no fix in hand), sidestep it
// entirely: we already have working, directly-resolved EVP_mdX()
// pointers (used to register them in the first place). Hook
// EVP_get_digestbyname() itself and answer the handful of names
// curl/BoringSSL actually ask for directly from that list, falling
// through to the real (broken) implementation for anything else.
static so_hook evp_get_digestbyname_hook;
static const char *g_digest_names[8];
static const void *g_digest_mds[8];
static int g_digest_count = 0;

static const void *GDGetDigestByName_hook(const char *name) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/digest_bypass_trace.txt", "w"); }
    if (name) {
        for (int i = 0; i < g_digest_count; i++) {
            if (g_digest_names[i] && strcmp(name, g_digest_names[i]) == 0) {
                if (f) { fprintf(f, "EVP_get_digestbyname(\"%s\") -> BYPASS hit, returning %p\n", name, g_digest_mds[i]); fflush(f); }
                return g_digest_mds[i];
            }
        }
    }
    const void *ret = (const void *)SO_CONTINUE(uint32_t, evp_get_digestbyname_hook, name);
    if (f) { fprintf(f, "EVP_get_digestbyname(\"%s\") -> fallthrough to real impl, returning %p\n", name ? name : "(null)", ret); fflush(f); }
    return ret;
}

// Diagnostic: TCP connect succeeds fast, SO_ERROR reads back 0,
// TCP_NODELAY/O_NONBLOCK get set fine, and NEITHER ERR_put_error() NOR
// SSL_do_handshake() is ever called before the socket is torn down via
// CloseWrapper above -- so whatever fails happens before BoringSSL's
// handshake entry point is reached. The caller of CloseWrapper
// (so+0x5eaa51, in the function starting around so+0x5ea838) does, right
// before the branch into the close-everything cleanup block: r1=0x180,
// "bl so+0x28ba16", then immediately "bl so+0x29ba1a" (return value
// passed straight through as the 2nd call's arg0), then
// "cmp r0,#0; beq.w <success, so+0x5eabf4>" -- i.e. it's a NONZERO
// return that falls through into cleanup, not a NULL one as first
// assumed. Offline disassembly of so+0x28ba16/0x29ba1a themselves
// produces garbage in both ARM and Thumb mode, and the raw bl-encoded
// immediate lands inside .rel.dyn (the relocation table), not .text --
// so those two addresses are not trustworthy as real code locations
// from static analysis alone (most likely stale/placeholder immediates
// that only resolve to something meaningful once the actual load-time
// relocation is applied, which we haven't traced through). Rather than
// keep guessing at what those two calls are, read the one thing that
// actually matters directly off real hardware: the r0 value right
// where it's tested, by hooking the "cmp r0,#0" instruction itself
// (so+0x5eaa1c) -- a normal AAPCS entry from our hook's point of view
// (r0 in, r0 forwarded unchanged to SO_CONTINUE), so logging is safe;
// if r0 is nonzero (cleanup path taken) SO_CONTINUE will run on into
// the same risky so+0x5eaa22 block and may crash afterward exactly like
// the earlier direct cleanup-entry hook did -- acceptable, since the
// one log line we need happens before that.
static so_hook cmp_r0_hook;

// so+0x5ee600: the function that actually decides whether to tear the
// connection down. It calls so+0x5eaa1c (our cmp_r0_hook target) via a
// direct "bl" -- confirmed on hardware: cmp_r0_trace.txt's caller_lr was
// exactly so+0x5ee67b. It only takes that path when a flag-test call
// (so+0x5fc4c0, effectively "HasFlag(obj, mask)") returns 0. Both hooked
// here to see the real object pointers and the flag word being tested.
static so_hook maybe_cleanup_hook;
static int GDMaybeCleanup_hook(void *r0, void *r1) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/maybe_cleanup_trace.txt", "w"); }
    void *ra = __builtin_return_address(0);
    if (f && r0) {
        uint32_t *obj = (uint32_t *)r0;
        fprintf(f, "enter this=%p r1=%p caller=%p (so-rel=0x%x)  [+0]=0x%x [+0x265]=0x%x [+0x2d8]=0x%x [+0x2dc]=0x%x [+0x40]=0x%x\n",
                r0, r1, ra, (uint32_t)((uintptr_t)ra - so_mod.text_base),
                obj[0], *(uint8_t*)((uintptr_t)r0+0x265), obj[0x2d8/4], obj[0x2dc/4], obj[0x40/4]);
        fflush(f);
    }
    return (int)SO_CONTINUE(uint32_t, maybe_cleanup_hook, r0, r1);
}

static so_hook hasflag_hook;

// The real gate turned out to be the byte at obj+0x265, not the HasFlag
// mask=3 check (that one only picks which of two equivalent cleanup
// sub-paths runs -- both converge on the same "bl so+0x5eaa1c" at
// so+0x5ee674 regardless). +0x265 is read at so+0x5ee60a; when it's
// already 1 by the time so+0x5ee600 runs (which it always was in every
// trace so far), the early "maybe nothing to do yet" bailout is skipped
// entirely. Found two direct setters via raw-byte search (capstone's
// linear sweep desyncs on this range, so a manual STRB.W [Rn,#0x265]
// encoding search was used instead): so+0x5fb744 (a small
// SetCloseState(obj, state)-shaped function) and so+0x609ad8 (guarded by
// a check on a different byte at obj+0x285, then a call to so+0x612b88).
// Hooking the entry of the small setter to see who calls it, with what
// state, and how early relative to connect().
// set265_trace.txt nailed the sequence per connection: the close flag
// (curl's conn->bits.close, byte +0x265) is cleared at so+0x5e1a34, then
// so+0x612b88 -- which is Curl_ssl_connect_nonblocking() (it even has the
// "Unrecognized parameter value passed via CURLOPT_SSLVERSION" failf path
// returning CURLE_SSL_CONNECT_ERROR=35) -- is called for the HTTPS
// connection and returns a NON-ZERO CURLcode, which makes so+0x5e1a8a
// set bits.close=1 and tear everything down. So the TLS handshake step
// fails before a single byte hits the wire. so+0x5e5798 is Curl_failf()
// (vsnprintf into data->state.buffer at +0x684) and so+0x5e571c is
// Curl_infof() (early-outs unless data->set.verbose at +0x390 is set --
// which is why we never saw any curl diagnostics). Hook both as real
// variadic C functions (AAPCS puts varargs in r0-r3+stack exactly as a
// variadic callee expects, and hook_thumb enters us with registers
// intact) and format the message ourselves, so we finally get curl's own
// words for what's failing.
static so_hook curl_failf_hook;
static so_hook curl_infof_hook;
static so_hook ssl_connect_nb_hook;
static FILE *g_curl_log = NULL;
static void curl_log_open(void) {
    static int tried = 0;
    if (!tried) { tried = 1; g_curl_log = gdash_trace_fopen("ux0:data/gdash/curl_msgs.txt", "w"); }
}
static void GDCurlFailf_hook(void *data, const char *fmt, ...) {
    curl_log_open();
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "(null fmt)", ap);
    va_end(ap);
    if (g_curl_log) { fprintf(g_curl_log, "[failf] %s\n", buf); fflush(g_curl_log); }
    // Let the real failf run too so data->state.buffer holds the message
    // for curl's own error reporting. Forward r0-r3 plus two stack words;
    // the message is already captured above regardless.
    va_start(ap, fmt);
    uint32_t a = va_arg(ap, uint32_t), b = va_arg(ap, uint32_t), c = va_arg(ap, uint32_t), d = va_arg(ap, uint32_t);
    va_end(ap);
    SO_CONTINUE(uint32_t, curl_failf_hook, data, fmt, a, b, c, d);
}
static void GDCurlInfof_hook(void *data, const char *fmt, ...) {
    curl_log_open();
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "(null fmt)", ap);
    va_end(ap);
    if (g_curl_log) { fprintf(g_curl_log, "[infof] %s", buf); if (!buf[0] || buf[strlen(buf)-1] != '\n') fputc('\n', g_curl_log); fflush(g_curl_log); }
    // Original is a no-op unless verbose is set; skip it.
    (void)data;
}
static int GDSslConnectNB_hook(void *conn, int sockindex, unsigned char *done) {
    curl_log_open();
    int ret = (int)SO_CONTINUE(uint32_t, ssl_connect_nb_hook, conn, sockindex, done);
    if (g_curl_log) {
        fprintf(g_curl_log, "[Curl_ssl_connect_nonblocking] conn=%p sockindex=%d -> CURLcode=%d done=%d\n",
                conn, sockindex, ret, done ? *done : -1);
        fflush(g_curl_log);
    }
    return ret;
}
// SSL_CTX_new() still returns NULL; curl only prints the OLDEST error on
// the queue (the harmless openssl.cnf ENOENT). Peek first AND last error
// right when SSL_CTX_new fails, before curl pops anything.
static so_hook ssl_ctx_new_hook;
static unsigned long (*ERR_peek_error_fn)(void);
static unsigned long (*ERR_peek_last_error_fn)(void);
static void (*ERR_error_string_n_fn)(unsigned long, char *, size_t);
static unsigned long (*ERR_get_error_line_data_fn)(const char **, int *, const char **, int *);
static void *GDSslCtxNew_hook(void *meth) {
    curl_log_open();
    void *ret = (void *)SO_CONTINUE(uint32_t, ssl_ctx_new_hook, meth);
    if (g_curl_log) {
        fprintf(g_curl_log, "[SSL_CTX_new] meth=%p -> %p; draining OpenSSL error queue:\n", meth, ret);
        // "malloc failure" is what every `goto err` in SSL_CTX_new reports,
        // so the file:line of each queued error is what identifies the
        // failing sub-call. Popping the queue here only costs curl its
        // (already-useless) first-error message.
        for (int i = 0; i < 16 && ERR_get_error_line_data_fn; i++) {
            const char *file = NULL, *data = NULL; int line = 0, flags = 0;
            unsigned long e = ERR_get_error_line_data_fn(&file, &line, &data, &flags);
            if (!e) break;
            char b[256] = "?";
            if (ERR_error_string_n_fn) ERR_error_string_n_fn(e, b, sizeof b);
            fprintf(g_curl_log, "   #%d %08lx %s @ %s:%d %s%s\n", i, e, b, file ? file : "?", line,
                    (flags & 2 /*ERR_TXT_STRING*/) && data ? "data=" : "", (flags & 2) && data ? data : "");
        }
        fflush(g_curl_log);
    }
    return ret;
}
// SSL_CTX_new fails at SSL_get_ex_data_X509_STORE_CTX_idx() < 0 even with
// the .so's own ex_data code now in use. Log the .so's
// CRYPTO_get_ex_new_index() calls (class, argl, argp) and results.
static so_hook exidx_hook;
static int GDGetExNewIndex_hook(int class_index, long argl, void *argp, void *newf, void *dupf, void *freef) {
    curl_log_open();
    int ret = (int)SO_CONTINUE(uint32_t, exidx_hook, class_index, argl, argp, newf, dupf, freef);
    if (g_curl_log) {
        fprintf(g_curl_log, "[CRYPTO_get_ex_new_index] class=%d argl=%ld argp=%p (%s) -> %d\n",
                class_index, argl, argp, argp ? (const char*)argp : "", ret);
        fflush(g_curl_log);
    }
    return ret;
}
static so_hook x509idx_hook;
static int GDX509StoreCtxIdx_hook(void) {
    curl_log_open();
    int ret = (int)SO_CONTINUE(uint32_t, x509idx_hook);
    if (g_curl_log) { fprintf(g_curl_log, "[SSL_get_ex_data_X509_STORE_CTX_idx] -> %d\n", ret); fflush(g_curl_log); }
    return ret;
}
/* breadcrumb instrumentation removed (was drowning the SSL init in per-call fflush to slow storage) */
static void install_breadcrumbs(void) {}
static so_hook set265_hook;
static int GDSetClose265_hook(void *r0, int r1) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/set265_trace.txt", "w"); }
    void *ra = __builtin_return_address(0);
    if (f) {
        fprintf(f, "SetClose265(obj=%p, state=%d) caller=%p (so-rel=0x%x) before[+0x265]=0x%x\n",
                r0, r1, ra, (uint32_t)((uintptr_t)ra - so_mod.text_base),
                r0 ? *(uint8_t*)((uintptr_t)r0 + 0x265) : 0xff);
        fflush(f);
    }
    return (int)SO_CONTINUE(uint32_t, set265_hook, r0, r1);
}
// EXPERIMENT: hasflag_trace.txt proved this always returns 0 for mask==3
// (the exact call so+0x5ee600 uses to decide whether to tear the
// connection down), on every single connection attempt, before the app
// has had any chance to send a request or receive a response -- the bits
// this checks only ever get set by an HTTP-response-header-parsing
// routine (so+0x5c6e40) that can't have run yet at this point. Rather
// than the flag logic itself being broken, something appears to invoke
// the "is there pending work?" check far too early on this port. As a
// direct, cheap test of that theory: force this one call site (mask==3
// only, to avoid touching whatever else in the .so may legitimately use
// this same helper for other masks) to report "yes, pending work" and
// see if the connection survives past this point.
#define HASFLAG_FORCE_MASK3 0  // disproven: see so+0x5ee680 disasm, both branches reach so+0x5ee674 cleanup anyway
static int GDHasFlag_hook(void *r0, int mask) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/hasflag_trace.txt", "w"); }
    uint32_t flags = r0 ? *(uint32_t *)((uintptr_t)r0 + 0x60) : 0xdeadbeef;
    int real_ret = (int)SO_CONTINUE(uint32_t, hasflag_hook, r0, mask);
    int ret = real_ret;
#if HASFLAG_FORCE_MASK3
    if (mask == 3) ret = 1;
#endif
    if (f) {
        fprintf(f, "HasFlag(obj=%p, mask=0x%x) flags@+0x60=0x%x -> real=%d forced=%d\n", r0, mask, flags, real_ret, ret);
        fflush(f);
    }
    return ret;
}


static int GDCmpR0_hook(int r0) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) { tried = 1; f = gdash_trace_fopen("ux0:data/gdash/cmp_r0_trace.txt", "w"); }
    // Also capture our caller's return address: whoever branched into
    // so+0x5eaa1c in the first place. Static analysis found zero direct
    // bl/blx #imm callers and zero literal pointer references anywhere
    // in the .so for this address (confirmed independently via a local
    // QEMU re-analysis pass too) -- so it's reached only through an
    // indirect (register) branch, and the only way left to find who does
    // that is to read it straight off the CPU at the moment we're hit.
    void *ra = __builtin_return_address(0);
    if (f) {
        fprintf(f, "post-alloc/init r0 = 0x%x (%s)  caller_lr=%p (so-relative=0x%x)\n",
                r0, r0 == 0 ? "ZERO -> success path" : "NONZERO -> cleanup path",
                ra, (uint32_t)((uintptr_t)ra - so_mod.text_base));
        fflush(f);
    }
    return (int)SO_CONTINUE(int, cmp_r0_hook, r0);
}
// prealloc_hook/preinit_hook (so+0x28ba16 / so+0x29ba1a) never fired:
// hook_verify_trace.txt proved hook_thumb() silently shifted their real
// patch location +2 bytes past what we asked for (both targets are
// 2-mod-4, not 4-byte aligned, and the raw bytes there decode as
// branch-shaped opcodes, not push{r4,..} prologues) -- so we were very
// likely patching over/near a linker veneer instead of a clean function
// entry, and it just never got reached/logged as expected. Rather than
// keep fighting that, hook the cleanup block itself directly: it is
// confirmed to execute on every single failed connect (close_wrapper_hook
// fires from inside it every time), so read its "this" fields (r0) right
// as we enter, before any of the fd fields get closed/cleared, to see
// which resources were already allocated and which weren't when the
// abort happened.
// cleanup_entry_hook (so+0x5eaa22) confirmed exactly ONE thing before we
// pulled it: this=0x8710b368  [+0x1a0]=0x9 (the connected socket fd)
// [+0x1a4]=-1 [+0x1a8]=-1 [+0x1ac]=-1 (all still unset) -- i.e. the abort
// happens right after the TCP connect() succeeds, before ANY TLS/BIO-type
// resource ever gets allocated on this object. It then reliably crashed
// (ABORT pc=lr=0x8710b368, exactly the "this" pointer) on every run after
// that single log line: so+0x5eaa22 is reached by fallthrough, not by
// bl/blx, and its own exit is a register-indirect tail call ("pop
// {r4,r5,r6,lr}; bx r3") that depends on r3 already holding a valid
// function pointer set up far earlier, upstream of this address, by the
// real (unhooked) control flow. Our hook is a normal AAPCS C function --
// r1-r3 are caller-saved/scratch to it, so by the time SO_CONTINUE hands
// control back to the original bytes at this same address, r3 no longer
// holds what the real code expected (it had been clobbered by our own
// hook's prologue/printf plumbing, and ended up equal to r0/this by
// coincidence) -- so the final "bx r3" jumped straight into the this
// pointer as if it were code. Self-inflicted by hooking a non-ABI
// fallthrough location, not a real game/curl bug. Removed for that
// reason -- the single log line above is the useful, trustworthy data
// point it gave us before crashing.

void fmod_init() {
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    int ret = sceNetShowNetstat();
    SceNetInitParam initparam;
    
    if (ret == SCE_NET_ERROR_ENOTINIT) {
        initparam.memory = malloc(141 * 1024);
        initparam.size = 141 * 1024;
        initparam.flags = 0;
        sceNetInit(&initparam);
    }

    // sceNet only brings up the raw BSD-socket layer. Without also
    // initializing sceNetCtl, the resolver has no access to the DNS info
    // of the connected Wi-Fi AP, so getaddrinfo()/gethostbyname() (and
    // therefore every HTTP/HTTPS request the game makes, e.g. downloading
    // custom levels or Newgrounds songs) fails even though socket(),
    // connect(), etc. are otherwise wired up correctly.
    int netctl_ret = sceNetCtlInit();
    logv_debug("[net] sceNetCtlInit(): 0x%x", netctl_ret);

    int netctl_state = SCE_NETCTL_STATE_DISCONNECTED;
    int state_ret = sceNetCtlInetGetState(&netctl_state);
    logv_debug("[net] sceNetCtlInetGetState(): 0x%x, state: %i", state_ret, netctl_state);

    if (state_ret < 0 || netctl_state != SCE_NETCTL_STATE_CONNECTED) {
        log_warn("[net] Vita is not connected to Wi-Fi, online features (custom levels, "
                 "song downloads, accounts) will not work.");
    }

    // Standalone diagnostic log, independent of DEBUG_SOLOADER (which is
    // compiled out in release builds), so we can see what's happening on
    // real hardware without a UART/debug-screen setup.
    {
        FILE *nf = gdash_trace_fopen("ux0:data/gdash/net_debug.txt", "w");
        if (nf) {
            fprintf(nf, "sceNetShowNetstat(): 0x%x\n", ret);
            fprintf(nf, "sceNetCtlInit(): 0x%x\n", netctl_ret);
            fprintf(nf, "sceNetCtlInetGetState(): 0x%x, state=%d (2=connected)\n",
                    state_ret, netctl_state);

            struct addrinfo hints, *res = NULL;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            int gai_ret = getaddrinfo("www.google.com", "80", &hints, &res);
            fprintf(nf, "getaddrinfo(\"www.google.com\", \"80\"): ret=%d errno=%d\n",
                    gai_ret, errno);
            if (gai_ret == 0 && res) {
                struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
                fprintf(nf, "resolved to: %s\n", inet_ntoa(sin->sin_addr));
                freeaddrinfo(res);

                int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                fprintf(nf, "socket(): fd=%d errno=%d\n", sock, errno);
                if (sock >= 0) {
                    int conn_ret = connect(sock, (struct sockaddr *)sin, sizeof(*sin));
                    fprintf(nf, "connect() to resolved IP:80: ret=%d errno=%d\n", conn_ret, errno);
                    close(sock);
                }
            }
            fclose(nf);
        }
    }

    sceKernelLoadStartModule("vs0:sys/external/libfios2.suprx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("vs0:sys/external/libc.suprx", 0, NULL, 0, NULL, NULL);
    sceKernelLoadStartModule("ur0:data/libfmodstudio.suprx", 0, NULL, 0, NULL, NULL);
}

// For some reason, Geometry Dash doesn't create the files necessary at boot
// to actually store your level/save data. This functions takes responsibility
// of that.
void save_files_init() {
    // Downloaded Newgrounds songs go to <DATA_PATH>songs/<id>.mp3 (see
    // getCocos2dxWritablePath in falso_jni_impl.c); make sure it exists.
    sceIoMkdir(DATA_PATH "songs", 0777);

    char* file_paths[] = {
        "ux0:data/gdash/CCGameManager.dat",
        "ux0:data/gdash/CCGameManager2.dat",
        "ux0:data/gdash/CCGameManager.dat.bak",
        "ux0:data/gdash/CCLocalLevels.dat",
        "ux0:data/gdash/CCLocalLevels.dat.bak",
        "ux0:data/gdash/CCLocalLevels2.dat",
    };

    for (int i = 0; i < 6; i++) {
        FILE* f = fopen(file_paths[i], "r");

        // if file doesn't exist, then create it
        // so that the game can open it
        if (f == NULL) {
            logv_debug("File \"%s\" doesn't exist. Creating...", file_paths[i]);

            FILE* nf = fopen(file_paths[i], "w");
            if (nf != NULL) {
                logv_debug("File \"%s\" successfully created", file_paths[i]);
                fclose(nf);
            }
        }

        fclose(f);
    }
}

float move_data[2];
int move_id;

float x_dummy;
float y_dummy;

int main() {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    soloader_init_all();

    // ssl_load_ciphers() (inside the game's statically-linked BoringSSL)
    // asserts ssl_digest_methods[SSL_MD_MD5_IDX] != NULL and calls
    // OPENSSL_die() -- a hard process abort -- if it's NULL. We've traced
    // this exhaustively: EVP_add_digest(EVP_md5()) reports success, but
    // the OBJ_NAME/LHASH lookup table used internally by
    // EVP_get_digestbyname()/OBJ_NAME_get() can't find what was just
    // inserted, even when queried with the exact same string OpenSSL's
    // own OBJ_nid2sn() reports -- a genuine bug in that table's
    // implementation on this port that we can't root-cause further
    // without source access, and can't hook around since every function
    // in this chain (ssl_load_ciphers, EVP_get_digestbyname, OBJ_NAME_get)
    // is called via direct intra-.so "bl", invisible to our import table.
    // Rather than leave this as a hard crash, NOP out the specific
    // conditional branch (so+0x760970: "beq ...") that jumps to the
    // OPENSSL_die() call when this one check fails. Instruction-patched
    // once, at load time, before any code runs, so it's not clobbered by
    // ssl_load_ciphers() re-running its own (broken) lookup later --
    // unlike a one-shot data write into the table, which would be. With
    // the branch neutralized, execution just falls through to the next
    // digest check; TLS/HTTPS should still work for anything that doesn't
    // specifically require MD5 (which no modern cipher suite does).
    {
        // Both digest-availability checks ssl_load_ciphers() does right
        // after (re)building ssl_digest_methods[] hit the same broken
        // OBJ_NAME/LHASH lookup -- confirmed by testing: patching only the
        // first (MD5, so+0x760970) just moved the crash to the second
        // (so+0x76097c, a different digest slot, same root cause) with an
        // otherwise identical signature. No further such checks follow in
        // this function (code afterwards does unrelated setup), so NOPing
        // these two covers the whole block.
        uint32_t nop = 0xe320f000; // real ARM NOP
        uint32_t *patch1 = (uint32_t *)(so_mod.text_base + 0x760970);
        uint32_t *patch2 = (uint32_t *)(so_mod.text_base + 0x76097c);
        kuKernelCpuUnrestrictedMemcpy(patch1, &nop, sizeof(nop));
        kuKernelCpuUnrestrictedMemcpy(patch2, &nop, sizeof(nop));
    }

    // Every single HTTPS connection attempt (both the game's own
    // www.google.com reachability probe and every www.boomlings.com
    // request from the profile screen) was connecting its TCP socket
    // successfully, then getting closed again immediately -- before a
    // single byte of TLS/HTTP traffic -- and retried from scratch,
    // forever, with no error/crash. We instrumented send/recv/read/write/
    // writev/sendmsg/recvmsg (all zero hits) and traced the return
    // address of the close() that was happening: it's always
    // so+0x5fb6a3, a generic Close(this,fd) helper reached ONLY via a
    // vtable/function-pointer slot -- i.e. dozens of different call sites
    // throughout libcurl's (statically linked) connect state machine can
    // all land here, and the return address alone can't tell us which
    // one fired on any given attempt. A first attempt to statically
    // pick out "the" culprit (libcurl's CURLOPT_SOCKOPTFUNCTION abort
    // path at so+0x5fa42c) and neutralize it directly changed nothing --
    // proof that wasn't the actual path being hit. So instead of
    // guessing further, we hook the shared Close(this,fd) helper itself
    // (so+0x5fb674) and log __builtin_return_address(0) from *inside*
    // the hook, which -- because the hook sits where the vtable slot
    // actually points -- gives us the real, specific call site for each
    // individual close, every time, instead of always seeing the same
    // wrapper address. SO_CONTINUE (so_util.h) restores the original
    // instructions, calls through so behavior is unchanged, then
    // re-patches -- this is a pure logging tap, not a behavior change.
#if GDASH_TRACE
    // Diagnostic taps only (logging, no behavior change). See utils/trace.h.
    close_wrapper_hook = hook_thumb(so_mod.text_base + 0x5fb674 + 1, (uintptr_t)&GDCloseWrapper_hook);
    // so+0x5eaa1c is the "cmp r0,#0" instruction itself, right after the
    // two mystery bl calls and before the branch that decides
    // cleanup-vs-success -- confirmed 4-byte aligned (0x1c % 4 == 0), so
    // hook_thumb won't silently shift the patch location this time.
    cmp_r0_hook = hook_thumb(so_mod.text_base + 0x5eaa1c + 1, (uintptr_t)&GDCmpR0_hook);
    maybe_cleanup_hook = hook_thumb(so_mod.text_base + 0x5ee600 + 1, (uintptr_t)&GDMaybeCleanup_hook);
    hasflag_hook = hook_thumb(so_mod.text_base + 0x5fc4c0 + 1, (uintptr_t)&GDHasFlag_hook);
    set265_hook = hook_thumb(so_mod.text_base + 0x5fb744 + 1, (uintptr_t)&GDSetClose265_hook);
    curl_failf_hook = hook_thumb(so_mod.text_base + 0x5e5798 + 1, (uintptr_t)&GDCurlFailf_hook);
    ERR_peek_error_fn = (void *)so_symbol(&so_mod, "ERR_peek_error");
    ERR_peek_last_error_fn = (void *)so_symbol(&so_mod, "ERR_peek_last_error");
    ERR_error_string_n_fn = (void *)so_symbol(&so_mod, "ERR_error_string_n");
    ERR_get_error_line_data_fn = (void *)so_symbol(&so_mod, "ERR_get_error_line_data");
    ssl_ctx_new_hook = hook_addr(so_symbol(&so_mod, "SSL_CTX_new"), (uintptr_t)&GDSslCtxNew_hook);
    install_breadcrumbs();
    exidx_hook = hook_addr(so_symbol(&so_mod, "CRYPTO_get_ex_new_index"), (uintptr_t)&GDGetExNewIndex_hook);
    x509idx_hook = hook_addr(so_symbol(&so_mod, "SSL_get_ex_data_X509_STORE_CTX_idx"), (uintptr_t)&GDX509StoreCtxIdx_hook);
    curl_infof_hook = hook_thumb(so_mod.text_base + 0x5e571c + 1, (uintptr_t)&GDCurlInfof_hook);
    ssl_connect_nb_hook = hook_thumb(so_mod.text_base + 0x612b88 + 1, (uintptr_t)&GDSslConnectNB_hook);
#endif
    // cleanup_entry_hook (so+0x5eaa22) was tried and pulled again -- see
    // the comment above GDCloseWrapper_hook's definition for why: it's a
    // fallthrough-only location whose exit is a register-indirect tail
    // call ("bx r3") depending on upstream register state our own hook
    // clobbers, so calling through via SO_CONTINUE crashed reliably
    // after one (valid, useful) log line.

    // NOTE: ERR_put_error/SSL_do_handshake hooks were tried here and
    // pulled again -- so_util's hook_thumb/SO_CONTINUE trampoline
    // (restore original instrs -> call through -> re-patch) isn't safe
    // against concurrent callers, and a real hardware test crashed with a
    // CPU exception with pc pointing exactly at ERR_put_error's address --
    // classic self-modifying-code race, almost certainly because curl's
    // networking runs on its own thread (lots of pthread_create/cond_wait
    // activity in the traces) and ERR_put_error got called from two
    // threads close together. They already told us what we needed before
    // that crash: across every prior clean run, neither
    // err_put_error_trace.txt nor ssl_handshake_trace.txt was ever
    // created, i.e. BoringSSL's handshake path is never reached at all --
    // the connection is torn down earlier, purely in curl's own connect
    // setup. Not re-adding these; see the connect_traced() timing
    // instrumentation in dynlib.c instead.

    fmod_init();
    save_files_init();

    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    //_ZN7cocos2d9extension13AssetsManager14setStoragePathEPKc
    int (* Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath)(JNIEnv *jni, void *unk, jstring apk_path) 
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");

    int (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit)(JNIEnv *jni, void* unk, jint w, jint h) 
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    int (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender)(void) 
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");

    void (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin)(JNIEnv * jni, jobject thiz, jint id, jfloat x, jfloat y) 
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    //jboolean (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown)(JNIEnv * jni, jobject thiz, jint keyCode) = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    void (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove)(JNIEnv * jni, jobject thiz, jint *ids, jfloat *xs, jfloat *ys)
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    void (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd)(JNIEnv * jni, jobject thiz, jint id, jfloat x, jfloat y)
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");

    bool (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown)(JNIEnv* jni, jobject thiz, jint keyCode)
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");

    int (* Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText)(JNIEnv* jni, jobject thiz, jstring text)
        = (void *)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInsertText");

    // The game's statically-linked OpenSSL/BoringSSL crashes 100%
    // deterministically (confirmed: even called single-threaded, first
    // thing, with nothing else running) the very first time anything
    // touches SSL, with a fatal OPENSSL_die() -- assertion
    // "ssl_digest_methods[SSL_MD_MD5_IDX] != NULL" inside ssl_load_ciphers()
    // (ssl/ssl_ciph.c:407). That function looks up digests by name
    // (EVP_get_digestbyname("MD5") etc.), which only succeeds if the
    // digest name table was already populated -- normally done lazily by
    // OPENSSL_init_ssl()/OPENSSL_init_crypto() the first time SSL is used.
    // Calling OPENSSL_init_ssl() ourselves first didn't help (it crashes
    // immediately by itself too), meaning ITS OWN internal digest
    // registration step isn't completing/running on this port before it
    // reaches ssl_load_ciphers(). So register the digests ssl_load_ciphers()
    // needs directly and explicitly, ourselves, via the individually
    // exported EVP_add_digest()/EVP_mdX() symbols, before calling
    // OPENSSL_init_ssl() at all -- this can't race with anything since
    // nothing else is running yet.
    {
        FILE *ossl_f = gdash_trace_fopen("ux0:data/gdash/openssl_init_trace.txt", "w");
        #define OSSL_LOG(...) do { if (ossl_f) { fprintf(ossl_f, __VA_ARGS__); fflush(ossl_f); } } while (0)

        typedef const void *(*evp_md_getter_fn)(void);
        int (* EVP_add_digest_fn)(const void *digest)
            = (void *)so_symbol(&so_mod, "EVP_add_digest");
        const char *(* EVP_MD_name_fn)(const void *md)
            = (void *)so_symbol(&so_mod, "EVP_MD_name");
        OSSL_LOG("EVP_add_digest=%p EVP_MD_name=%p\n", (void*)EVP_add_digest_fn, (void*)EVP_MD_name_fn);

        // Static analysis (offline disasm of the unmodified .so) proved
        // OBJ_NAME_get()/OBJ_NAME_add()/OBJ_NAME_init() all compute the
        // exact same PC-relative GOT slot address for the "names_lh"
        // table -- so+0xa983a4 -- ruling out a duplicate-table/broken-
        // relocation theory outright: there is only one slot, and every
        // caller agrees on where it is. What's actually AT that slot at
        // various points in the sequence is the open question now.
        #define OBJNAME_SLOT (so_mod.text_base + 0xa983a4)
        #define RD32_2(base, off) (*(volatile uint32_t *)((base) + (off)))
        OSSL_LOG("OBJ_NAME slot BEFORE any EVP_add_digest: [+0]=%x [+4]=%x (table ptr)\n",
                 RD32_2(OBJNAME_SLOT, 0), RD32_2(OBJNAME_SLOT, 4));

        const char *digest_getters[] = {
            "EVP_md4", "EVP_md5", "EVP_md5_sha1",
            "EVP_sha1", "EVP_sha224", "EVP_sha256", "EVP_sha384", "EVP_sha512",
        };
        // OpenSSL's own short names for each of the above (confirmed via
        // OBJ_nid2sn() earlier: nid 4 -> "MD5"), used as the lookup keys
        // for the EVP_get_digestbyname() bypass table below.
        const char *digest_short_names[] = {
            "MD4", "MD5", "MD5-SHA1",
            "SHA1", "SHA224", "SHA256", "SHA384", "SHA512",
        };
        if (EVP_add_digest_fn) {
            for (int i = 0; i < (int)(sizeof(digest_getters) / sizeof(digest_getters[0])); i++) {
                evp_md_getter_fn getter = (evp_md_getter_fn)so_symbol(&so_mod, digest_getters[i]);
                OSSL_LOG("%s getter=%p\n", digest_getters[i], (void*)getter);
                if (getter) {
                    const void *md = getter();
                    OSSL_LOG("  %s() = %p\n", digest_getters[i], md);
                    const char *name = (EVP_MD_name_fn && md) ? EVP_MD_name_fn(md) : NULL;
                    OSSL_LOG("  EVP_MD_name = %s\n", name ? name : "(null)");
                    int ret = EVP_add_digest_fn(md);
                    OSSL_LOG("  EVP_add_digest(%s) = %d\n", digest_getters[i], ret);
                    if (md && g_digest_count < (int)(sizeof(g_digest_mds) / sizeof(g_digest_mds[0]))) {
                        g_digest_names[g_digest_count] = digest_short_names[i];
                        g_digest_mds[g_digest_count] = md;
                        g_digest_count++;
                    }
                } else {
                    OSSL_LOG("  %s not found in .so!\n", digest_getters[i]);
                }
            }
        } else {
            OSSL_LOG("EVP_add_digest symbol not found in .so!\n");
        }

        OSSL_LOG("OBJ_NAME slot AFTER all EVP_add_digest calls: [+0]=%x [+4]=%x (table ptr)\n",
                 RD32_2(OBJNAME_SLOT, 0), RD32_2(OBJNAME_SLOT, 4));

        void *(* EVP_get_digestbyname_fn)(const char *name)
            = (void *)so_symbol(&so_mod, "EVP_get_digestbyname");
        OSSL_LOG("EVP_get_digestbyname=%p\n", (void*)EVP_get_digestbyname_fn);
        if (EVP_get_digestbyname_fn) {
            // EVP_get_digestbyname (like OBJ_NAME_get/add/init) is
            // ARM-mode code (confirmed via offline disassembly), and is
            // called via plain "bl" from many places -- a normal
            // ABI-compliant function, safe to hook via hook_arm() (NOT
            // hook_thumb() -- using the wrong one is exactly why our
            // earlier prealloc/preinit hooks on the connect path never
            // fired).
#if GDASH_TRACE
            evp_get_digestbyname_hook = hook_arm((uintptr_t)EVP_get_digestbyname_fn, (uintptr_t)&GDGetDigestByName_hook);
#endif
            void *md5 = EVP_get_digestbyname_fn("MD5");
            OSSL_LOG("EVP_get_digestbyname(\"MD5\") = %p (BEFORE OPENSSL_init_ssl, bypass hook installed)\n", md5);
        }

        // Isolate whether the problem is the STRING used to register MD5
        // vs. the OBJ_NAME table lookup itself: read EVP_md5()'s own
        // embedded nid field directly, ask OpenSSL's own OBJ_nid2sn() what
        // string that nid maps to, then query the low-level OBJ_NAME_get()
        // table directly with both "MD5" and whatever OBJ_nid2sn actually
        // returned, bypassing EVP_get_digestbyname()'s init-gate wrapper
        // entirely.
        {
            const char *(* OBJ_nid2sn_fn)(int nid) = (void *)so_symbol(&so_mod, "OBJ_nid2sn");
            const char *(* OBJ_nid2ln_fn)(int nid) = (void *)so_symbol(&so_mod, "OBJ_nid2ln");
            void *(* OBJ_NAME_get_fn)(const char *name, int type) = (void *)so_symbol(&so_mod, "OBJ_NAME_get");
            evp_md_getter_fn md5_getter = (evp_md_getter_fn)so_symbol(&so_mod, "EVP_md5");
            OSSL_LOG("OBJ_nid2sn=%p OBJ_nid2ln=%p OBJ_NAME_get=%p\n",
                     (void*)OBJ_nid2sn_fn, (void*)OBJ_nid2ln_fn, (void*)OBJ_NAME_get_fn);
            if (md5_getter && OBJ_nid2sn_fn && OBJ_nid2ln_fn && OBJ_NAME_get_fn) {
                const int32_t *md5_struct = (const int32_t *)md5_getter();
                int32_t nid = md5_struct[0];
                OSSL_LOG("EVP_md5() struct=%p nid(field0)=%d\n", (void*)md5_struct, nid);
                const char *sn = OBJ_nid2sn_fn(nid);
                const char *ln = OBJ_nid2ln_fn(nid);
                OSSL_LOG("OBJ_nid2sn(%d) = %s\n", nid, sn ? sn : "(null)");
                OSSL_LOG("OBJ_nid2ln(%d) = %s\n", nid, ln ? ln : "(null)");
                void *r1 = OBJ_NAME_get_fn("MD5", 1);
                OSSL_LOG("OBJ_NAME_get(\"MD5\", 1) = %p\n", r1);
                if (sn) {
                    void *r2 = OBJ_NAME_get_fn(sn, 1);
                    OSSL_LOG("OBJ_NAME_get(sn=\"%s\", 1) = %p\n", sn, r2);
                }
            }
        }

        // Direct memory inspection of OpenSSL's own internal init-state
        // structs, found via manual disassembly of the original .so:
        //   so_mod.text_base + 0xa982d4 = the OPENSSL_init_crypto/ossl_init
        //     "state" struct: +0x54 = base-init overall success flag,
        //     +0x58 = base-init "attempted" flag, +0x60 = sticky-error
        //     flag, +0x4c = the CRYPTO_THREAD lock pointer, +0x38 =
        //     add-all-digests-done flag.
        //   so_mod.text_base + 0xa96b84 = ssl_digest_methods[] table base;
        //     +0x60 = the MD5 slot ssl_load_ciphers() asserts non-NULL on.
        #define OSSL_STATE_BASE (so_mod.text_base + 0xa982d4)
        #define SSL_DM_BASE     (so_mod.text_base + 0xa96b84)
        #define RD32(base, off) (*(volatile uint32_t *)((base) + (off)))
        OSSL_LOG("BEFORE: state[0x54]=%x [0x58]=%x [0x60]=%x [0x4c]=%x [0x38]=%x  ssl_dm[0x60]=%x\n",
                 RD32(OSSL_STATE_BASE, 0x54), RD32(OSSL_STATE_BASE, 0x58), RD32(OSSL_STATE_BASE, 0x60),
                 RD32(OSSL_STATE_BASE, 0x4c), RD32(OSSL_STATE_BASE, 0x38), RD32(SSL_DM_BASE, 0x60));

        void (* OPENSSL_init_ssl_fn)(uint64_t opts, const void *settings)
            = (void *)so_symbol(&so_mod, "OPENSSL_init_ssl");
        OSSL_LOG("OPENSSL_init_ssl=%p\n", (void*)OPENSSL_init_ssl_fn);
        if (OPENSSL_init_ssl_fn) {
            OSSL_LOG("Calling OPENSSL_init_ssl(0, NULL)...\n");
            OPENSSL_init_ssl_fn(0, NULL);
            OSSL_LOG("OPENSSL_init_ssl() returned\n");
        }

        OSSL_LOG("AFTER: state[0x54]=%x [0x58]=%x [0x60]=%x [0x4c]=%x [0x38]=%x  ssl_dm[0x60]=%x\n",
                 RD32(OSSL_STATE_BASE, 0x54), RD32(OSSL_STATE_BASE, 0x58), RD32(OSSL_STATE_BASE, 0x60),
                 RD32(OSSL_STATE_BASE, 0x4c), RD32(OSSL_STATE_BASE, 0x38), RD32(SSL_DM_BASE, 0x60));

        if (EVP_get_digestbyname_fn) {
            void *md5 = EVP_get_digestbyname_fn("MD5");
            OSSL_LOG("EVP_get_digestbyname(\"MD5\") = %p (AFTER OPENSSL_init_ssl)\n", md5);
        }

        OSSL_LOG("AFTER2: state[0x54]=%x [0x58]=%x [0x60]=%x [0x4c]=%x [0x38]=%x  ssl_dm[0x60]=%x\n",
                 RD32(OSSL_STATE_BASE, 0x54), RD32(OSSL_STATE_BASE, 0x58), RD32(OSSL_STATE_BASE, 0x60),
                 RD32(OSSL_STATE_BASE, 0x4c), RD32(OSSL_STATE_BASE, 0x38), RD32(SSL_DM_BASE, 0x60));
        #undef RD32
        #undef RD32_2
        #undef OSSL_STATE_BASE
        #undef SSL_DM_BASE

        if (ossl_f) { fclose(ossl_f); ossl_f = NULL; }
        #undef OSSL_LOG
    }

    JNI_OnLoad(&jvm);

    Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath(&jni, NULL, APK_PATH);
    gl_init();
    Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit(&jni, NULL, 960, 544);

    int lastX[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int lastY[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};

    while (1) {
        static uint32_t oldpad;

        SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);

        SceTouchData touch;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

        if (pad.buttons & SCE_CTRL_LEFT) {
            x_dummy = 95;
            y_dummy = 480;

            int id_dummy = 16;
            
            if ((oldpad & SCE_CTRL_LEFT)) {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove(&jni, NULL, &id_dummy, &x_dummy, &y_dummy);
            } else {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(&jni, NULL, id_dummy, x_dummy, y_dummy);
            }
        } else if (oldpad & SCE_CTRL_LEFT) {
            int id_dummy = 16;
            Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(&jni, NULL, id_dummy, x_dummy, y_dummy);
        }

        if (pad.buttons & SCE_CTRL_RIGHT) {
            x_dummy = 225;
            y_dummy = 480;

            int id_dummy = 17;

            if ((oldpad & SCE_CTRL_RIGHT)) {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove(&jni, NULL, &id_dummy, &x_dummy, &y_dummy);
            } else {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(&jni, NULL, id_dummy, x_dummy, y_dummy);
            }
        } else if (oldpad & SCE_CTRL_RIGHT) {
            int id_dummy = 17;
            Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(&jni, NULL, id_dummy, x_dummy, y_dummy);
        }

        if (pad.buttons & SCE_CTRL_CROSS) {
            x_dummy = 959;
            y_dummy = 543;

            int id_dummy = 18;
            
            if ((oldpad & SCE_CTRL_CROSS)) {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove(&jni, NULL, &id_dummy, &x_dummy, &y_dummy);
            } else {
                Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(&jni, NULL, id_dummy, x_dummy, y_dummy);
            }
        } else if (oldpad & SCE_CTRL_CROSS) {
            int id_dummy = 18;
            Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(&jni, NULL, id_dummy, x_dummy, y_dummy);
        }
        
        if (((pad.buttons & SCE_CTRL_CIRCLE) && (!(oldpad & SCE_CTRL_CIRCLE))) || ((pad.buttons & SCE_CTRL_START) && (!(oldpad & SCE_CTRL_START)))) {
            Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown(&jni, NULL, 0x04);
        }

        oldpad = pad.buttons;

        for (int i = 0; i < SCE_TOUCH_MAX_REPORT; i++) {
            if (i < touch.reportNum) {
                float x = (float)touch.report[i].x * (float)960.0f / 1920.0f;
                float y = (float)touch.report[i].y * (float)544.0f / 1088.0f;
                int id = i;

                if (lastX[i] == -1 || lastY[i] == -1) {
                    Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin(&jni, NULL, i, x, y);
                } else {
                    move_data[0] = (float)x;
					move_data[1] = (float)y;
					move_id = id;

                    Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove(&jni, NULL,
                        &id, &x, &y);
                }

                lastX[i] = x;
                lastY[i] = y;

            } else {
                if (lastX[i] != -1 || lastY[i] != -1) {
                    Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd(&jni, NULL, i, lastX[i], lastY[i]);
                    lastX[i] = -1;
                    lastY[i] = -1;
                }
            }
        }

        Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender();
        gl_swap();
    }

    sceKernelExitDeleteThread(0);
}