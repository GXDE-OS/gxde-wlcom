// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "nls.h"
#include "server.h"
#include "util/logger.h"
#include "util/spawn.h"

static struct server server = {
    .options.enable_xwayland = true,
    .options.log_to_file = true,
};
static int exit_value = 0;

static const struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },    { "debug", no_argument, NULL, 'd' },
    { "version", no_argument, NULL, 'v' }, { "session", required_argument, NULL, 's' },
    { "verbose", no_argument, NULL, 'V' }, { 0, 0, 0, 0 },
};

static const char usage[] =
    "Usage: kylin-wlcom [options] [command]\n"
    "\n"
    "  -h, --help               Show help message and quit.\n"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -s, --session <process>  Run session on startup\n"
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
    } else {
        printf("Unknown debug flag: %s", flag);
    }
}

static void terminate(int exit_code)
{
    if (!server.display) {
        exit(exit_code);
    } else {
        exit_value = exit_code;
        wl_display_terminate(server.display);
    }
}

static void sig_handler(int signal)
{
    terminate(EXIT_SUCCESS);
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
    char *session_process = NULL;

    int c;
    while (1) {
        int option_index = 0;
        c = getopt_long(argc, argv, "hdD:s:vV", long_options, &option_index);
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
            session_process = optarg;
            break;
        case 'v': // version
            printf("kylin-wlcom version " KYWC_VERSION "\n");
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
    logger_init(level, server.options.log_to_file);

    /* ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
    /* handle SIGTERM signals */
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    if (!server_init(&server)) {
        terminate(EXIT_FAILURE);
        goto shutdown;
    }

    if (!server_start(&server)) {
        terminate(EXIT_FAILURE);
        goto shutdown;
    }

    if (session_process) {
        spawn_invoke(session_process);
    }

    server_run(&server);

shutdown:
    server_finish(&server);
    logger_finish();

    return exit_value;
}
