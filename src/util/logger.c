// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "util/logger.h"

static FILE *log_fp = NULL;
static enum kywc_log_level log_level = KYWC_WARN;
static bool log_in_realtime = true;

static const char *level_colors[] = {
    [KYWC_SILENT] = "",         [KYWC_FATAL] = "\x1B[7;31m", [KYWC_ERROR] = "\x1B[1;31m",
    [KYWC_WARN] = "\x1B[1;35m", [KYWC_INFO] = "\x1B[1;34m",  [KYWC_DEBUG] = "\x1B[1;90m",
};

static const char *level_headers[] = {
    [KYWC_SILENT] = "",     [KYWC_FATAL] = "[FATAL]", [KYWC_ERROR] = "[ERROR]",
    [KYWC_WARN] = "[WARN]", [KYWC_INFO] = "[INFO]",   [KYWC_DEBUG] = "[DEBUG]",
};

static void log_file(enum kywc_log_level level, const char *fmt, va_list args)
{
    if (level > log_level) {
        return;
    }

    // FIXME: thread-safe
    if (log_in_realtime) {
        time_t time_log = time(NULL);
        struct tm *tm_log = localtime(&time_log);
        fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] %s: ", tm_log->tm_year + 1900,
                tm_log->tm_mon + 1, tm_log->tm_mday, tm_log->tm_hour, tm_log->tm_min,
                tm_log->tm_sec, log_fp == stdout ? level_colors[level] : level_headers[level]);
    } else {
        struct timespec ts = { 0 };
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(log_fp, "[%02d:%02d:%02d.%03ld] %s: ", (int)(ts.tv_sec / 60 / 60),
                (int)(ts.tv_sec / 60 % 60), (int)(ts.tv_sec % 60), ts.tv_nsec / 1000000,
                log_fp == stdout ? level_colors[level] : level_headers[level]);
    }

    vfprintf(log_fp, fmt, args);
    if (log_fp == stdout) {
        fprintf(log_fp, "\x1B[0m");
    }
    fprintf(log_fp, "\n");

    fflush(log_fp);
}

void kywc_log(enum kywc_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_file(level, format, args);
    va_end(args);
}

void kywc_vlog(enum kywc_log_level level, const char *format, va_list args)
{
    log_file(level, format, args);
}

static enum kywc_log_level detect_log_level(enum kywc_log_level level)
{
    /* set logger log_level from env */
    const char *level_str = getenv("KYWC_LOG_LEVEL");
    if (!level_str) {
        return level;
    }

    if (strcmp(level_str, "DEBUG") == 0) {
        return KYWC_DEBUG;
    } else if (strcmp(level_str, "INFO") == 0) {
        return KYWC_INFO;
    } else if (strcmp(level_str, "WARN") == 0) {
        return KYWC_WARN;
    } else if (strcmp(level_str, "ERROR") == 0) {
        return KYWC_ERROR;
    } else if (strcmp(level_str, "FATAL") == 0) {
        return KYWC_FATAL;
    } else if (strcmp(level_str, "SILENT") == 0) {
        return KYWC_SILENT;
    } else {
        fprintf(stderr, "logger: env error, support (DEBUG|INFO|WARN|ERROR|FATAL|SILENT)\n");
        return level;
    }
}

void logger_init(enum kywc_log_level level, bool log_to_file, bool realtime)
{
    log_in_realtime = realtime;
    log_level = detect_log_level(level);
    fprintf(stdout, "logger: current log level is %s\n", level_headers[log_level]);

    /* don't log to file */
    if (!log_to_file) {
        log_fp = stdout;
        return;
    }

    /* get home dir */
    const char *home = getenv("HOME");

    size_t size_path = 1 + strlen(home) + strlen("/.log");
    char *log_home = calloc(size_path, sizeof(char));
    if (!log_home) {
        fprintf(stderr, "alloc log path failed\n");
        return;
    }

    snprintf(log_home, size_path, "%s/.log", home);
    const char *filename = "kylin-wlcom.log";
    size_t floder = strlen(log_home);
    size_t size = 2 + floder + strlen(filename);
    char *log_path = calloc(size, sizeof(char));
    snprintf(log_path, size, "%s/%s", log_home, filename);
    free(log_home);
    if (!log_path) {
        fprintf(stderr, "alloc log file path failed\n");
        return;
    }

    log_path[floder] = '\0';
    int ret = access(log_path, F_OK);
    if (ret) {
        fprintf(stdout, "logger: %s not exist, create it\n", log_path);
        ret = mkdir(log_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ret) {
            fprintf(stderr, "create log dir failed: %s\n", strerror(errno));
            return;
        }
    }

    log_path[floder] = '/';
    fprintf(stdout, "logger: path is %s\n", log_path);

    log_fp = fopen(log_path, "a"); // appending mode
    free(log_path);
    if (!log_fp) {
        fprintf(stderr, "logger: create log file failed: %s\n", strerror(errno));
        return;
    }
}

enum kywc_log_level kywc_log_get_level(void)
{
    return log_level;
}

void logger_set_level(enum kywc_log_level level)
{
    if (level == log_level) {
        return;
    }

    log_level = level;
    kywc_log(KYWC_SILENT, "logger: current log level is %s", level_headers[level]);
}

void logger_finish(void)
{
    if (!log_fp) {
        return;
    }

    fflush(log_fp);
    fclose(log_fp);
}
