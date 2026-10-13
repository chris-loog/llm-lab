/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#include "cli.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WATCH_MIN 1
#define WATCH_MAX 3600

static void print_usage(FILE *stream)
{
    fprintf(stream, "Usage:\n"
                    "  llm-lab status             show Ollama and system usage once\n"
                    "  llm-lab status --watch N   redraw every N seconds (1-3600), Ctrl+C exits\n"
                    "  llm-lab --help             show this text\n");
}

static int fail(const char *fmt, ...)
{
    va_list args;

    fputs("error: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputs("\nTry 'llm-lab --help'.\n", stderr);
    return -1;
}

static int has_help_flag(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            return 1;
    }
    return 0;
}

static int parse_interval(const char *text, int *seconds)
{
    char *end;
    long value;

    /* strtol does not clear errno on success, so a stale ERANGE would count. */
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value < WATCH_MIN || value > WATCH_MAX)
        return fail("invalid interval '%s' (use %d-%d)", text, WATCH_MIN, WATCH_MAX);

    *seconds = (int)value;
    return 0;
}

int cli_parse(int argc, char **argv, cli_options_t *out)
{
    if (has_help_flag(argc, argv)) {
        print_usage(stdout);
        out->command = CMD_HELP;
        out->watch_seconds = 0;
        return 0;
    }

    if (argc < 2) {
        print_usage(stderr);
        return fail("missing command");
    }
    if (strcmp(argv[1], "status") != 0)
        return fail("unknown argument '%s'", argv[1]);

    out->command = CMD_STATUS;
    out->watch_seconds = 0;
    if (argc == 2)
        return 0;

    if (strcmp(argv[2], "--watch") != 0)
        return fail("unknown argument '%s'", argv[2]);
    if (argc == 3)
        return fail("--watch needs a number");
    if (parse_interval(argv[3], &out->watch_seconds) != 0)
        return -1;
    if (argc > 4)
        return fail("unknown argument '%s'", argv[4]);

    return 0;
}
