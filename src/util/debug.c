// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _GNU_SOURCE
#include <dlfcn.h>

#include <kywc/log.h>

#include "util/debug.h"

#if HAVE_LIBUNWIND

#define UNW_LOCAL_ONLY
#include <libunwind.h>

void debug_backtrace(void)
{
    unw_context_t context;
    int ret = unw_getcontext(&context);
    if (ret) {
        kywc_log(KYWC_ERROR, "unw_getcontext failed: %s [%d]", unw_strerror(ret), ret);
        return;
    }

    unw_cursor_t cursor;
    ret = unw_init_local(&cursor, &context);
    if (ret) {
        kywc_log(KYWC_ERROR, "unw_init_local failed: %s [%d]", unw_strerror(ret), ret);
        return;
    }

    kywc_log(KYWC_SILENT, "Backtrace:");

    unw_proc_info_t pip = { .unwind_info = NULL };
    unw_word_t off, ip;
    Dl_info dlinfo;
    char procname[256];
    const char *filename;

    int i = 0;
    ret = unw_step(&cursor);
    while (ret > 0) {
        ret = unw_get_proc_info(&cursor, &pip);
        if (ret) {
            kywc_log(KYWC_ERROR, "unw_get_proc_info failed: %s [%d]", unw_strerror(ret), ret);
            break;
        }

        off = 0;
        ret = unw_get_proc_name(&cursor, procname, 256, &off);
        if (ret && ret != -UNW_ENOMEM) {
            if (ret != -UNW_EUNSPEC) {
                kywc_log(KYWC_ERROR, "unw_get_proc_name failed: %s [%d]", unw_strerror(ret), ret);
            }
            procname[0] = '?';
            procname[1] = 0;
        }

        if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0) {
            ip = pip.start_ip + off;
        }
        if (dladdr((void *)(uintptr_t)(ip), &dlinfo) && dlinfo.dli_fname && *dlinfo.dli_fname) {
            filename = dlinfo.dli_fname;
        } else {
            filename = "?";
        }

        if (unw_is_signal_frame(&cursor)) {
            kywc_log(KYWC_SILENT, "  %u: <signal handler called>", i++);
        } else {
            kywc_log(KYWC_SILENT, "  %u: %s (%s%s+0x%x) [%p]", i++, filename, procname,
                     ret == -UNW_ENOMEM ? "..." : "", (int)off, (void *)(uintptr_t)(ip));
        }

        ret = unw_step(&cursor);
        if (ret < 0) {
            kywc_log(KYWC_ERROR, "unw_step failed: %s [%d]", unw_strerror(ret), ret);
        }
    }
}

#else /* HAVE_LIBUNWIND */

#include <execinfo.h>

#define BACKTRACE_SIZE (256)

void debug_backtrace(void)
{
    void *array[BACKTRACE_SIZE];
    int size = backtrace(array, BACKTRACE_SIZE);

    kywc_log(KYWC_SILENT, "Backtrace:");

    Dl_info info;
    const char *mod;

    for (int i = 0; i < size; i++) {
        if (dladdr(array[i], &info) == 0) {
            kywc_log(KYWC_SILENT, "  %u: ?? [%p]", i, array[i]);
            continue;
        }
        mod = (info.dli_fname && *info.dli_fname) ? info.dli_fname : "(vdso)";
        if (info.dli_saddr) {
            kywc_log(KYWC_SILENT, "  %u: %s (%s+0x%x) [%p]", i, mod, info.dli_sname,
                     (unsigned int)((char *)array[i] - (char *)info.dli_saddr), array[i]);
        } else {
            kywc_log(KYWC_SILENT, "  %u: %s (%p+0x%x) [%p]", i, mod, info.dli_fbase,
                     (unsigned int)((char *)array[i] - (char *)info.dli_fbase), array[i]);
        }
    }
}

#endif
