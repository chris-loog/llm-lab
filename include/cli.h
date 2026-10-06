#ifndef CLI_H
#define CLI_H

typedef enum {
    CMD_HELP,
    CMD_STATUS,
} command_t;

typedef struct {
    command_t command;
    int watch_seconds; /* 0 = print once and exit */
} cli_options_t;

/*
 * Parse argv into out. Returns 0 on success, -1 on bad input after printing
 * the error to stderr. The caller exits with code 2 on -1.
 * For CMD_HELP the usage text is already printed to stdout.
 */
int cli_parse(int argc, char **argv, cli_options_t *out);

#endif
