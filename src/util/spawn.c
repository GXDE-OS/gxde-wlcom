// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#include <kywc/log.h>

#include "util/limit.h"
#include "util/spawn.h"

bool spawn_invoke(const char *command)
{
    wordexp_t p;
    if (wordexp(command, &p, 0)) {
        kywc_log(KYWC_ERROR, "command illegal: %s", command);
        wordfree(&p);
        return false;
    }

    pid_t child = fork();
    switch (child) {
    case -1:
        kywc_log(KYWC_ERROR, "unable to fork()");
        wordfree(&p);
        return false;
    case 0:
        setsid();
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, NULL);
        limit_unset_nofile();

        pid_t grandchild = fork();
        if (grandchild == 0) {
            /* close stdout/stderr */
            int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
            if (devnull < 0) {
                kywc_log_errno(KYWC_ERROR, "failed to open /dev/null");
                _exit(1);
            }
            if (kywc_log_get_level() < KYWC_DEBUG) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
            }

            char **argv = p.we_wordv;
            execvp(argv[0], argv);

            kywc_log_errno(KYWC_ERROR, "execvp failed");
            close(devnull);
            _exit(1);
        } else if (grandchild < 0) {
            kywc_log(KYWC_ERROR, "unable to fork()");
        }
        _exit(0);
    default:
        break;
    }

    waitpid(child, NULL, 0);
    wordfree(&p);
    return true;
}

pid_t spawn_session(const char *session)
{
    wordexp_t p;
    if (wordexp(session, &p, 0)) {
        kywc_log(KYWC_ERROR, "session illegal: %s", session);
        wordfree(&p);
        return -1;
    }

    pid_t child = fork();
    switch (child) {
    case -1:
        kywc_log(KYWC_ERROR, "unable to fork()");
        wordfree(&p);
        return -1;
    case 0:
        /* child */
        limit_unset_nofile();
        /* close stdout/stderr */
        int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
        if (devnull < 0) {
            kywc_log_errno(KYWC_ERROR, "failed to open /dev/null");
            _exit(1);
        }
        if (kywc_log_get_level() < KYWC_DEBUG) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }

        char **argv = p.we_wordv;
        execvp(argv[0], argv);

        kywc_log_errno(KYWC_ERROR, "execvp failed");
        close(devnull);
        _exit(1);
    default:
        wordfree(&p);
        return child;
    }
}
