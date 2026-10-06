#include <stdio.h>

#include "cli.h"

int main(int argc, char **argv) {
    cli_options_t options;

    if (cli_parse(argc, argv, &options) != 0)
        return 2;
    if (options.command == CMD_HELP)
        return 0;

    printf("+------------------------+----------------------+\n");
    printf("| %-22s | %-20s |\n", "Metric", "Value");
    printf("+------------------------+----------------------+\n");
    printf("| %-22s | %-20s |\n", "LLM model running", "Llama 3.2 3B");
    printf("| %-22s | %-20s |\n", "CPU usage", "28%%");
    printf("| %-22s | %-20s |\n", "RAM usage", "6.4 / 16 GB");
    printf("| %-22s | %-20s |\n", "VRAM usage", "4.2 / 8 GB");
    printf("| %-22s | %-20s |\n", "GPU load", "63%%");
    printf("+------------------------+----------------------+\n");

    return 0;
}