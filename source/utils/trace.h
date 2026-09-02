#ifndef GDASH_TRACE_H
#define GDASH_TRACE_H
#include <stdio.h>
#include <string.h>

// Diagnostic tracing switch. All the *_trace.txt / curl_msgs.txt /
// tls_hello.hex writers go through gdash_trace_fopen(); with GDASH_TRACE
// off they get NULL and every writer already checks for that, so the
// tracing costs nothing and writes nothing on the memory card.
#ifndef GDASH_TRACE
#define GDASH_TRACE 0
#endif

static inline FILE *gdash_trace_fopen(const char *path, const char *mode) {
#if GDASH_TRACE
    return fopen(path, mode);
#else
    (void)path; (void)mode;
    return NULL;
#endif
}
#endif
