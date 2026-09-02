/*
 * dynlib.h
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

#ifndef SOLOADER_DYNLIB_H
#define SOLOADER_DYNLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_util/so_util.h>

void resolve_imports(so_module *mod);

// Lightweight tracked-socket-fd set, used so read_traced/write_traced (which
// have to sit on the same "read"/"write" import slots used for ALL file and
// socket I/O) can log only the socket traffic without drowning the trace
// file in ordinary asset-file reads. Called from socket_traced() on
// creation and from close_soloader() on close.
void gdash_net_track_fd(int fd, int is_socket);
int gdash_net_is_tracked_fd(int fd);

#include <sys/types.h>
#include <sys/socket.h>
ssize_t read_traced(int fd, void *buf, size_t len);
ssize_t write_traced(int fd, const void *buf, size_t len);
ssize_t sendmsg_traced(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg_traced(int sockfd, struct msghdr *msg, int flags);


#ifdef __cplusplus
};
#endif

#endif // SOLOADER_DYNLIB_H
