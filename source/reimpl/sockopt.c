/*
 * reimpl/sockopt.c
 *
 * Android/bionic ships numerically different values for the level and
 * option-name arguments of setsockopt()/getsockopt() than the PS Vita's
 * sceNet stack expects (e.g. bionic SOL_SOCKET == 1, Vita SOL_SOCKET ==
 * 0xFFFF; bionic SO_KEEPALIVE == 9, Vita SO_KEEPALIVE == 0x8).
 *
 * The loaded libcocos2dcpp.so is unmodified Android ARM code and always
 * passes bionic-numbered constants, so every call reaching this file is
 * translated from bionic values to the real vitasdk (SCE_NET_*) values
 * before being forwarded to the actual sceNet-backed setsockopt/
 * getsockopt.
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/trace.h"
#include "reimpl/sockopt.h"

#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include "reimpl/errno.h"

#define BIONIC_SOL_SOCKET      1

#define BIONIC_SO_DEBUG        1
#define BIONIC_SO_REUSEADDR    2
#define BIONIC_SO_TYPE         3
#define BIONIC_SO_ERROR        4
#define BIONIC_SO_DONTROUTE    5
#define BIONIC_SO_BROADCAST    6
#define BIONIC_SO_SNDBUF       7
#define BIONIC_SO_RCVBUF       8
#define BIONIC_SO_KEEPALIVE    9
#define BIONIC_SO_OOBINLINE    10
#define BIONIC_SO_NO_CHECK     11
#define BIONIC_SO_PRIORITY     12
#define BIONIC_SO_LINGER       13
#define BIONIC_SO_BSDCOMPAT    14
#define BIONIC_SO_REUSEPORT    15
#define BIONIC_SO_PASSCRED     16
#define BIONIC_SO_PEERCRED     17
#define BIONIC_SO_RCVLOWAT     18
#define BIONIC_SO_SNDLOWAT     19
#define BIONIC_SO_RCVTIMEO     20
#define BIONIC_SO_SNDTIMEO     21

static FILE *sockopt_trace_file(void) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/sockopt_trace.txt", "w");
    }
    return f;
}

static int translate_sol_socket_optname(int bionic_optname, int *out) {
    switch (bionic_optname) {
        case BIONIC_SO_REUSEADDR: *out = SO_REUSEADDR; return 1;
        case BIONIC_SO_TYPE:      *out = SO_TYPE;      return 1;
        case BIONIC_SO_ERROR:     *out = SO_ERROR;      return 1;
        case BIONIC_SO_BROADCAST: *out = SO_BROADCAST;  return 1;
        case BIONIC_SO_SNDBUF:    *out = SO_SNDBUF;      return 1;
        case BIONIC_SO_RCVBUF:    *out = SO_RCVBUF;      return 1;
        case BIONIC_SO_KEEPALIVE: *out = SO_KEEPALIVE;   return 1;
        case BIONIC_SO_LINGER:    *out = SO_LINGER;      return 1;
        case BIONIC_SO_REUSEPORT: *out = SO_REUSEPORT;   return 1;
        case BIONIC_SO_RCVLOWAT:  *out = SO_RCVLOWAT;    return 1;
        case BIONIC_SO_SNDLOWAT:  *out = SO_SNDLOWAT;    return 1;
        case BIONIC_SO_RCVTIMEO:  *out = SO_RCVTIMEO;    return 1;
        case BIONIC_SO_SNDTIMEO:  *out = SO_SNDTIMEO;    return 1;
        default:
            return 0;
    }
}

int setsockopt_soloader(int sockfd, int level, int optname,
                         const void *optval, socklen_t optlen) {
    FILE *f = sockopt_trace_file();

    if (level == BIONIC_SOL_SOCKET) {
        int real_optname;
        if (!translate_sol_socket_optname(optname, &real_optname)) {
            if (f) { fprintf(f, "setsockopt(fd=%d, SOL_SOCKET, optname=%d) unmapped, ok\n", sockfd, optname); fflush(f); }
            return 0;
        }
        int ret = setsockopt(sockfd, SOL_SOCKET, real_optname, optval, optlen);
        int e = errno;
        if (f) {
            fprintf(f, "setsockopt(fd=%d, SOL_SOCKET, bionic_optname=%d -> vita 0x%x) = %d (errno=%d)\n",
                    sockfd, optname, real_optname, ret, e);
            fflush(f);
        }
        errno = e;
        return ret;
    }

    int ret = setsockopt(sockfd, level, optname, optval, optlen);
    int e = errno;
    if (f) {
        fprintf(f, "setsockopt(fd=%d, level=%d, optname=%d) = %d (errno=%d)\n",
                sockfd, level, optname, ret, e);
        fflush(f);
    }
    errno = e;
    return ret;
}

int getsockopt_soloader(int sockfd, int level, int optname,
                         void *optval, socklen_t *optlen) {
    FILE *f = sockopt_trace_file();

    if (level == BIONIC_SOL_SOCKET) {
        int real_optname;
        if (!translate_sol_socket_optname(optname, &real_optname)) {
            if (f) { fprintf(f, "getsockopt(fd=%d, SOL_SOCKET, optname=%d) unmapped, 0\n", sockfd, optname); fflush(f); }
            if (optval && optlen && *optlen >= sizeof(int)) {
                *(int *)optval = 0;
                *optlen = sizeof(int);
            }
            return 0;
        }
        int ret = getsockopt(sockfd, SOL_SOCKET, real_optname, optval, optlen);
        int e = errno;
        if (f) {
            int has_val = (optval && optlen && *optlen >= sizeof(int));
            fprintf(f, "getsockopt(fd=%d, SOL_SOCKET, bionic_optname=%d -> vita 0x%x) = %d (errno=%d) *optval=%d optlen=%u\n",
                    sockfd, optname, real_optname, ret, e,
                    has_val ? *(int *)optval : -12345,
                    optlen ? (unsigned)*optlen : 0);
            fflush(f);
        }
        errno = e;
        return ret;
    }

    return getsockopt(sockfd, level, optname, optval, optlen);
}
