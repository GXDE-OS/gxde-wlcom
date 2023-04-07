#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "server.h"
#include "util/logger.h"

static const struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },
    { "debug", no_argument, NULL, 'd' },
    { "version", no_argument, NULL, 'v' },
    { "verbose", no_argument, NULL, 'V' },
    { 0, 0, 0, 0 },
};

static const char usage[] =
    "Usage: kylin-wlcom [options] [command]\n"
    "\n"
    "  -h, --help       Show help message and quit.\n"
    "  -d, --debug      Enables full logging, including debug information.\n"
    "  -v, --version    Show the version number and quit.\n"
    "  -V, --verbose    Enables more verbose logging.\n"
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

int main(int argc, char *argv[])
{
    struct server server = { 0 };
    bool enable_debug = false;
    bool enable_verbose = false;

    int c;
    while (1) {
        int option_index = 0;
        c = getopt_long(argc, argv, "hdD:vV", long_options, &option_index);
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
            // enable_debug_flag(optarg);
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
    logger_init(level, false);

    if (!server_init(&server)) {
        exit(EXIT_FAILURE);
    }

    server_start(&server);

    server_finish(&server);
    logger_finish();
    return 0;
}
