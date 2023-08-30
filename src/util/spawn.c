// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#include <kywc/log.h>

#include "util/spawn.h"

bool spawn_invoke(const char *command)
{
    wordexp_t p;
    char **argv;

    if (wordexp(command, &p, 0)) {
        kywc_log(KYWC_ERROR, "command illegal: %s", command);
        wordfree(&p);
        return false;
    }
    argv = p.we_wordv;

    /*
     * Avoid zombie processes by using a double-fork, whereby the
     * grandchild becomes orphaned & the responsibility of the OS.
     */
    pid_t child = 0, grandchild = 0;

    child = fork();
    switch (child) {
    case -1:
        kywc_log(KYWC_ERROR, "unable to fork()");
        goto out;
    case 0:
        setsid();
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, NULL);
        grandchild = fork();
        if (grandchild == 0) {
            execvp(argv[0], argv);
            kywc_log_errno(KYWC_ERROR, "execvp failed");
            _exit(1);
        } else if (grandchild < 0) {
            kywc_log(KYWC_ERROR, "unable to fork()");
        }
        _exit(0);
    default:
        break;
    }
    waitpid(child, NULL, 0);

out:
    wordfree(&p);
    return true;
}
