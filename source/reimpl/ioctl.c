/*
 * reimpl/ioctl.c
 *
 * Implementation for the ioctl() function
 *
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/trace.h"
#include "reimpl/ioctl.h"

#include <stdio.h>
#include <stdarg.h>
#include "utils/logger.h"

static FILE *ioctl_trace_file(void) {
    static FILE *f = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        f = gdash_trace_fopen("ux0:data/gdash/ioctl_trace.txt", "w");
    }
    return f;
}

int ioctl_soloader(int fildes, int request, ... /* arg */) {
    va_list args;
    va_start(args, request);
    long arg = va_arg(args, long);
    va_end(args);

    FILE *f = ioctl_trace_file();
    if (f) { fprintf(f, "ioctl(fd=%d, req=0x%x, arg=0x%lx)\n", fildes, request, arg); fflush(f); }
    return 0;
}
