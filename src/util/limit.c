// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <sys/resource.h>

#include <kywc/log.h>

#include "util/limit.h"

static struct rlimit original_nofile_rlimit = { 0 };

void limit_set_nofile(void)
{
    if (getrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to get max open files limit");
        return;
    }

    struct rlimit new_rlimit = original_nofile_rlimit;
    new_rlimit.rlim_cur = new_rlimit.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &new_rlimit) != 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to set max open files limit");
        kywc_log(KYWC_INFO, "Running with %ld max open files", original_nofile_rlimit.rlim_cur);
        return;
    }

    kywc_log(KYWC_INFO, "Running with %ld max open files", new_rlimit.rlim_cur);
}

void limit_unset_nofile(void)
{
    if (original_nofile_rlimit.rlim_cur == 0) {
        return;
    }

    if (setrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to restore max open files limit");
    }
}
