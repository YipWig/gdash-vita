/*
 * reimpl/sockaddr_abi.h
 *
 * The unmodified Android/bionic .so and the PS Vita's newlib/BSD-derived
 * network stack disagree on the byte layout of struct sockaddr_in:
 *
 *   Vita (BSD-style):    u8 sin_len; u8 sin_family; u16 sin_port; ...
 *   Bionic (Linux-style): u16 sin_family;            u16 sin_port; ...
 *
 * sin_port/sin_addr happen to land at the same offsets on both, but the
 * family field does not: bionic code reading a Vita-filled sockaddr_in
 * sees (sin_len | (sin_family << 8)) as its 16-bit family value instead
 * of the real address family, so any address we hand back to the game
 * (e.g. from getaddrinfo()) looks corrupt to it and gets silently
 * rejected/retried. The reverse applies to addresses the game hands us
 * (e.g. into connect()).
 *
 * These helpers do the byte-layout translation, in place, for
 * struct sockaddr_in only (AF_INET) -- the only family this port
 * actually uses on the wire.
 *
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_SOCKADDR_ABI_H
#define SOLOADER_SOCKADDR_ABI_H

#include <sys/socket.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rewrites a single vita-native sockaddr_in (as produced by the real Vita
// getaddrinfo/accept/getsockname/... ) into bionic layout, in place, so
// the loaded Android .so can read it correctly. No-op for families other
// than AF_INET, and safely handles addr==NULL / len < sizeof(sockaddr_in).
void sockaddr_vita_to_bionic(struct sockaddr *addr, socklen_t addrlen);

// Rewrites a single bionic-layout sockaddr_in (as constructed by the
// loaded Android .so, e.g. before connect()/bind()/sendto()) into
// vita-native layout, in place, so the real Vita network calls interpret
// it correctly. No-op for families other than AF_INET (bionic AF_INET==2,
// same numeric value on both platforms).
void sockaddr_bionic_to_vita(struct sockaddr *addr, socklen_t addrlen);

// Walks an addrinfo list returned by the real Vita getaddrinfo() and
// rewrites every entry's ai_addr into bionic layout in place.
void addrinfo_list_vita_to_bionic(struct addrinfo *res);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_SOCKADDR_ABI_H
