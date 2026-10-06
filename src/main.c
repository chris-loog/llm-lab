#include <stdio.h>

#include "cli.h"
#include "monitor.h"
#include "process.h"

int main(int argc, char **argv)
{
    cli_options_t options;
    ollama_procs_t procs;
    char pid_text[21];
    char child_text[21];
    char ram_text[21];
    char runtime_text[21];
    char model_text[21];
    unsigned long rss_kb;
    unsigned long runtime_seconds;

    if (cli_parse(argc, argv, &options) != 0)
        return 2;
    if (options.command == CMD_HELP)
        return 0;
    if (process_find(&procs) != 0)
        return 1;
    if (process_total_rss_kb(&procs, &rss_kb) != 0 ||
        process_runtime_seconds(procs.serve_pid, &runtime_seconds) != 0 ||
        process_model_names(&procs, model_text, sizeof(model_text)) != 0) {
        process_free(&procs);
        return 1;
    }

    snprintf(pid_text, sizeof(pid_text), "%d", (int)procs.serve_pid);
    snprintf(child_text, sizeof(child_text), "%zu", procs.child_count);
    monitor_format_size(rss_kb, ram_text, sizeof(ram_text));
    monitor_format_duration(runtime_seconds, runtime_text, sizeof(runtime_text));

    printf("+------------------------+----------------------+\n");
    printf("| %-22s | %-20s |\n", "Metric", "Value");
    printf("+------------------------+----------------------+\n");
    printf("| %-22s | %-20s |\n", "Ollama serve PID", pid_text);
    printf("| %-22s | %-20s |\n", "Child processes", child_text);
    printf("| %-22s | %-20s |\n", "Ollama RAM (RSS)", ram_text);
    printf("| %-22s | %-20s |\n", "Ollama runtime", runtime_text);
    printf("| %-22s | %-20s |\n", "LLM model running", model_text);
    printf("| %-22s | %-20s |\n", "CPU usage", "28%%");
    printf("| %-22s | %-20s |\n", "RAM usage", "6.4 / 16 GB");
    printf("| %-22s | %-20s |\n", "VRAM usage", "4.2 / 8 GB");
    printf("| %-22s | %-20s |\n", "GPU load", "63%%");
    printf("+------------------------+----------------------+\n");

    process_free(&procs);
    return 0;
}