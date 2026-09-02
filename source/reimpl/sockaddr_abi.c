/*
 * reimpl/sockaddr_abi.c
 *
 * See sockaddr_abi.h for the full explanation of the bug this works
 * around: Vita's BSD-style "sin_len + 1-byte sin_family" sockaddr_in
 * vs. bionic's plain 2-byte sin_family, with sin_port/sin_addr landing
 * at the same offsets on both. We do the translation on raw bytes so
 * this doesn't depend on which struct definition happens to be in
 * scope in the caller's translation unit.
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/sockaddr_abi.h"

#include <string.h>
#include <stdint.h>

#define SOCKADDR_IN_SIZE 16
#define AF_INET_VAL 2

void sockaddr_vita_to_bionic(struct sockaddr *addr, socklen_t addrlen) {
    if (!addr || addrlen < SOCKADDR_IN_SIZE) {
        return;
    }
    uint8_t *b = (uint8_t *)addr;
    // Vita layout: b[0]=sin_len, b[1]=sin_family
    if (b[1] != AF_INET_VAL) {
        return;
    }
    uint8_t family = b[1];
    // b[2..7] (port + addr) stay as-is -- same offsets on both ABIs.
    b[0] = family; // bionic: low byte of 16-bit LE sin_family
    b[1] = 0;      // bionic: high byte of sin_family
    memset(b + 8, 0, SOCKADDR_IN_SIZE - 8); // bionic sin_zero[8]
}

void sockaddr_bionic_to_vita(struct sockaddr *addr, socklen_t addrlen) {
    if (!addr || addrlen < SOCKADDR_IN_SIZE) {
        return;
    }
    uint8_t *b = (uint8_t *)addr;
    // Bionic layout: 16-bit LE sin_family at b[0..1].
    if (b[0] != AF_INET_VAL || b[1] != 0) {
        return;
    }
    // b[2..7] (port + addr) stay as-is.
    b[0] = SOCKADDR_IN_SIZE; // vita sin_len
    b[1] = AF_INET_VAL;      // vita sin_family
    memset(b + 8, 0, SOCKADDR_IN_SIZE - 8); // vita sin_vport + sin_zero[6]
}

void addrinfo_list_vita_to_bionic(struct addrinfo *res) {
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        if (ai->ai_addr && ai->ai_family == AF_INET_VAL) {
            sockaddr_vita_to_bionic(ai->ai_addr, (socklen_t)ai->ai_addrlen);
        }
    }
}
