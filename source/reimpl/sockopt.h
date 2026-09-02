/*
 * reimpl/sockopt.h
 *
 * Translates Android/bionic setsockopt()/getsockopt() level & option
 * constants to their PS Vita (SCE_NET_*) equivalents before forwarding
 * to the real sceNet-backed implementations.
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_SOCKOPT_H
#define SOLOADER_SOCKOPT_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

int setsockopt_soloader(int sockfd, int level, int optname,
                         const void *optval, socklen_t optlen);
int getsockopt_soloader(int sockfd, int level, int optname,
                         void *optval, socklen_t *optlen);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_SOCKOPT_H
