// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <libintl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "server.h"
#include "util/debug.h"
#include "util/limit.h"
#include "util/logger.h"
#include "util/spawn.h"

static struct server server = {
    .options = {
        .enable_xwayland = true,
        .log_to_file = true,
        .log_in_realtime = true,
        .binding_session = true,
    },
    .session_pid = -1,
};
static int exit_value = 0;

static const struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },    { "debug", no_argument, NULL, 'd' },
    { "version", no_argument, NULL, 'v' }, { "session", required_argument, NULL, 's' },
    { "verbose", no_argument, NULL, 'V' }, { 0, 0, 0, 0 },
};

static const char usage[] =
    "Usage: gxde-wlcom [options] [command]\n"
    "\n"
    "  -h, --help               Show help message and quit.\n"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -s, --session <process>  Run session on startup.\n"
    "  -v, --version            Show the version number and quit.\n"
    "  -V, --verbose            Enables more verbose logging.\n"
    "\n";

static bool detect_suid(void)
{
    if (geteuid() != 0 && getegid() != 0) {
        return false;
    }

    if (getuid() == geteuid() && getgid() == getegid()) {
        return false;
    }

    printf("SUID operation is no longer supported, refusing to start.\n");
    return true;
}

static void enable_debug_flag(struct server *server, const char *flag)
{
    if (strcmp(flag, "noxwayland") == 0) {
        server->options.enable_xwayland = false;
    } else if (strcmp(flag, "logtostdout") == 0) {
        server->options.log_to_file = false;
    } else if (strcmp(flag, "loginmtime") == 0) {
        server->options.log_in_realtime = false;
    } else if (strcmp(flag, "nosessionbinding") == 0) {
        server->options.binding_session = false;
    } else {
        printf("Unknown debug flag: %s\n", flag);
    }
}

static void terminate(int exit_code)
{
    if (!server.display) {
        exit(exit_code);
    } else if (!server.terminate) {
        exit_value = exit_code;
        wl_display_terminate(server.display);
    }
}

static void set_signal(int sig, void *handler)
{
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    if (handler != SIG_IGN) {
        sigaddset(&act.sa_mask, sig);
    }
    act.sa_sigaction = handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(sig, &act, NULL);
}

static void sig_handler(int signo, siginfo_t *sip, void *unused)
{
    if (signo == SIGINT && server.terminate) {
        return;
    }

    if (sip->si_code == SI_USER) {
        kywc_log(KYWC_SILENT, "Received signal %s(%u) sent by process %u, uid %u", strsignal(signo),
                 signo, sip->si_pid, sip->si_uid);
    } else {
        switch (signo) {
        case SIGINT:
            kywc_log(KYWC_SILENT, "Received signal %s(%u) by Ctrl+C", strsignal(signo), signo);
            break;
        case SIGSEGV:
        case SIGBUS:
        case SIGILL:
        case SIGFPE:
        case SIGABRT:
            kywc_log(KYWC_SILENT, "Caught %s(%u) at address %p", strsignal(signo), signo,
                     sip->si_addr);
            debug_backtrace();
            /* abort() raises SIGABRT */
            set_signal(SIGABRT, SIG_DFL);
            abort();
        }
    }

    terminate(EXIT_SUCCESS);
}

static void child_handler(int signo, siginfo_t *sip, void *unused)
{
    if (sip->si_pid != server.session_pid) {
        return;
    }

    switch (sip->si_code) {
    case CLD_EXITED:
        kywc_log(KYWC_ERROR, "session %d exited with %d", sip->si_pid, sip->si_status);
        break;
    case CLD_KILLED:
    case CLD_DUMPED:;
        const char *signame = strsignal(sip->si_status);
        kywc_log(KYWC_ERROR, "session %d terminated with signal %d (%s)", sip->si_pid,
                 sip->si_status, signame ? signame : "unknown");
        break;
    default:
        kywc_log(KYWC_ERROR, "session %d terminated unexpectedly: %d", sip->si_pid, sip->si_code);
        break;
    }

    spawn_wait(server.session_pid);
    server.session_pid = -1;

    if (server.options.binding_session) {
        kywc_log(KYWC_FATAL, "gxde-wlcom abort...");
        terminate(EXIT_SUCCESS);
    }
}

static void start_session(void *data)
{
    server.session_pid = spawn_session(server.session_process);
    if (server.session_pid < 0) {
        kywc_log(KYWC_ERROR, "session %s start failed%s", server.session_process,
                 server.options.binding_session ? ", abort" : "");
        if (server.options.binding_session) {
            terminate(EXIT_FAILURE);
        }
    } else {
        kywc_log(KYWC_INFO, "session %s(%d) started", server.session_process, server.session_pid);
    }
}

int main(int argc, char *argv[])
{
#if HAVE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);
#endif

    bool enable_debug = false;
    bool enable_verbose = false;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "hdD:s:vV", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'h':
            printf("%s", usage);
            exit(EXIT_SUCCESS);
            break;
        case 'd':
            enable_debug = true;
            break;
        case 'D': // extended debug options
            enable_debug_flag(&server, optarg);
            break;
        case 's':
            server.session_process = optarg;
            break;
        case 'v': // version
            printf("gxde-wlcom version " KYWC_VERSION "\n");
            exit(EXIT_SUCCESS);
            break;
        case 'V': // verbose
            enable_verbose = true;
            break;
        default:
            fprintf(stderr, "%s", usage);
            exit(EXIT_FAILURE);
        }
    }

    /* SUID operation is deprecated, so block it for now */
    if (detect_suid()) {
        exit(EXIT_FAILURE);
    }

    enum kywc_log_level level = KYWC_WARN;
    if (enable_debug) {
        level = KYWC_DEBUG;
    } else if (enable_verbose) {
        level = KYWC_INFO;
    }
    logger_init(level, server.options.log_to_file, server.options.log_in_realtime);
    kywc_log(KYWC_SILENT, "gxde-wlcom %s starting...", KYWC_VERSION);

    /* set Number of open files to max */
    limit_set_nofile();

    /* ignore SIGPIPE */
    set_signal(SIGPIPE, SIG_IGN);
    /* ctrl+c signal */
    set_signal(SIGINT, sig_handler);
    /* crash signals */
    set_signal(SIGSEGV, sig_handler);
    set_signal(SIGBUS, sig_handler);
    set_signal(SIGILL, sig_handler);
    set_signal(SIGFPE, sig_handler);
    set_signal(SIGABRT, sig_handler);

    if (!server_init(&server)) {
        terminate(EXIT_FAILURE);
        goto shutdown;
    }

    if (!server_start(&server)) {
        terminate(EXIT_FAILURE);
        goto shutdown;
    }

    /* child terminated or stopped */
    set_signal(SIGCHLD, child_handler);

    if (server.session_process) {
        wl_event_loop_add_idle(server.event_loop, start_session, NULL);
    }

    server_run(&server);

    set_signal(SIGCHLD, SIG_DFL);
    /* kill the session process */
    if (server.session_pid > 0) {
        if (kill(server.session_pid, SIGTERM) < 0) {
            kill(server.session_pid, SIGKILL);
        }
    }

shutdown:
    server_finish(&server);
    logger_finish();

    return exit_value;
}
