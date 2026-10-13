/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#include "cli.h"
#include "monitor.h"

int main(int argc, char **argv)
{
    cli_options_t options;

    if (cli_parse(argc, argv, &options) != 0)
        return 2;
    if (options.command == CMD_HELP)
        return 0;
    return monitor_run(options.watch_seconds);
}
