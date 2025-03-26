// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#include <kywc/log.h>

#include "util/limit.h"
#include "util/spawn.h"

bool spawn_invoke(const char *command)
{
    wordexp_t p = { 0 };
    if (wordexp(command, &p, 0) || !p.we_wordv[0]) {
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
        /* do not give our signal mask to the new process */
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        limit_unset_nofile();

        pid_t grandchild = fork();
        if (grandchild == 0) {
            /* close stdout/stderr */
            int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0660);
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
    wordexp_t p = { 0 };
    if (wordexp(session, &p, 0) || !p.we_wordv[0]) {
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
        setsid();
        /* do not give our signal mask to the new process */
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        limit_unset_nofile();

        /* close stdout/stderr */
        int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0660);
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

void spawn_wait(pid_t pid)
{
    waitpid(pid, NULL, WNOHANG);
}

static bool set_cloexec(int fd, bool cloexec)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        kywc_log_errno(KYWC_ERROR, "fcntl failed");
        return false;
    }
    if (cloexec) {
        flags = flags | FD_CLOEXEC;
    } else {
        flags = flags & ~FD_CLOEXEC;
    }
    if (fcntl(fd, F_SETFD, flags) == -1) {
        kywc_log_errno(KYWC_ERROR, "fcntl failed");
        return false;
    }
    return true;
}

struct wl_client *spawn_client(struct wl_display *display, const char *command)
{
    wordexp_t p = { 0 };
    if (wordexp(command, &p, 0) || !p.we_wordv[0]) {
        kywc_log(KYWC_ERROR, "command illegal: %s", command);
        wordfree(&p);
        return NULL;
    }

    int fds[2] = { -1, -1 };
    /* create a socket pair with cloexec */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        kywc_log_errno(KYWC_ERROR, "socketpair failed");
        goto error;
    }
    if (!set_cloexec(fds[0], true) || !set_cloexec(fds[1], true)) {
        kywc_log(KYWC_ERROR, "Failed to set O_CLOEXEC on socket");
        goto error;
    }

    struct wl_client *client = wl_client_create(display, fds[0]);
    if (!client) {
        kywc_log_errno(KYWC_ERROR, "wl_client_create failed");
        goto error;
    }
    fds[0] = -1; /* not ours anymore */

    pid_t child = fork();
    switch (child) {
    case -1:
        kywc_log(KYWC_ERROR, "unable to fork()");
        goto error;
    case 0:
        setsid();
        /* do not give our signal mask to the new process */
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        limit_unset_nofile();

        pid_t grandchild = fork();
        if (grandchild == 0) {
            if (!set_cloexec(fds[1], false)) {
                kywc_log(KYWC_ERROR, "Failed to unset CLOEXEC on FD");
                _exit(1);
            }

            char wayland_socket_str[16];
            snprintf(wayland_socket_str, sizeof(wayland_socket_str), "%d", fds[1]);
            setenv("WAYLAND_SOCKET", wayland_socket_str, true);
            kywc_log(KYWC_ERROR, "run %s in %s", command, wayland_socket_str);

            /* close stdout/stderr */
            int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0660);
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
    close(fds[1]);
    return client;

error:
    if (fds[0] >= 0) {
        close(fds[0]);
    }
    if (fds[1] >= 0) {
        close(fds[1]);
    }
    wordfree(&p);
    return NULL;
}
