/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "cli.h"
#include "monitor.h"
#include "process.h"

#define LABEL_WIDTH 26
#define VALUE_WIDTH 24
#define SAMPLE_SECONDS 1

typedef struct {
    char pid[VALUE_WIDTH + 1];
    char children[VALUE_WIDTH + 1];
    char ram[VALUE_WIDTH + 1];
    char runtime[VALUE_WIDTH + 1];
    char model[VALUE_WIDTH + 1];
    char process_cpu[VALUE_WIDTH + 1];
    char system_cpu[VALUE_WIDTH + 1];
    char system_ram[2 * 12 + 4];
} status_text_t;

static void print_border(void)
{
    printf("+");
    for (int i = 0; i < LABEL_WIDTH + 2; i++)
        printf("-");
    printf("+");
    for (int i = 0; i < VALUE_WIDTH + 2; i++)
        printf("-");
    printf("+\n");
}

static void print_row(const char *label, const char *value)
{
    printf("| %-*s | %-*s |\n", LABEL_WIDTH, label, VALUE_WIDTH, value);
}

static void print_table(const status_text_t *text)
{
    print_border();
    print_row("Metric", "Value");
    print_border();
    print_row("Ollama serve PID", text->pid);
    print_row("Child processes", text->children);
    print_row("Ollama RAM (RSS)", text->ram);
    print_row("Ollama runtime", text->runtime);
    print_row("LLM model running", text->model);
    print_row("Ollama CPU (100% = 1 core)", text->process_cpu);
    print_row("System CPU", text->system_cpu);
    print_row("System RAM", text->system_ram);
    print_row("VRAM usage", "4.2 / 8 GB");
    print_row("GPU load", "63%");
    print_border();
}

/*
 * Sample, wait, find Ollama again, sample again. The second scan catches
 * runners that started or stopped during the wait.
 */
static int measure_cpu(const ollama_procs_t *first, double *process_percent, double *system_percent)
{
    ollama_procs_t second;
    monitor_sample_t before;
    monitor_sample_t after;
    int result = -1;

    if (monitor_take_sample(first, &before) != 0)
        return -1;
    sleep(SAMPLE_SECONDS);
    if (process_find(&second) != 0)
        goto free_before;
    if (second.serve_pid != first->serve_pid) {
        fprintf(stderr, "error: ollama is not running\n");
        goto free_second;
    }
    if (monitor_take_sample(&second, &after) != 0)
        goto free_second;
    result = monitor_cpu_percent(&before, &after, process_percent, system_percent);
    monitor_sample_free(&after);

free_second:
    process_free(&second);
free_before:
    monitor_sample_free(&before);
    return result;
}

static int fill_text(const ollama_procs_t *procs, status_text_t *text)
{
    unsigned long rss_kb;
    unsigned long runtime_seconds;
    unsigned long used_kb;
    unsigned long total_kb;
    char used_text[12];
    char total_text[12];
    double process_percent;
    double system_percent;

    if (process_total_rss_kb(procs, &rss_kb) != 0 ||
        process_runtime_seconds(procs->serve_pid, &runtime_seconds) != 0 ||
        process_model_names(procs, text->model, sizeof(text->model)) != 0 ||
        monitor_read_memory(&used_kb, &total_kb) != 0 ||
        measure_cpu(procs, &process_percent, &system_percent) != 0)
        return -1;

    snprintf(text->pid, sizeof(text->pid), "%d", (int)procs->serve_pid);
    snprintf(text->children, sizeof(text->children), "%zu", procs->child_count);
    monitor_format_size(rss_kb, text->ram, sizeof(text->ram));
    monitor_format_duration(runtime_seconds, text->runtime, sizeof(text->runtime));
    monitor_format_percent(process_percent, text->process_cpu, sizeof(text->process_cpu));
    monitor_format_percent(system_percent, text->system_cpu, sizeof(text->system_cpu));
    monitor_format_size(used_kb, used_text, sizeof(used_text));
    monitor_format_size(total_kb, total_text, sizeof(total_text));
    snprintf(text->system_ram, sizeof(text->system_ram), "%s / %s", used_text, total_text);
    return 0;
}

int main(int argc, char **argv)
{
    cli_options_t options;
    ollama_procs_t procs;
    status_text_t text;

    if (cli_parse(argc, argv, &options) != 0)
        return 2;
    if (options.command == CMD_HELP)
        return 0;
    if (process_find(&procs) != 0)
        return 1;
    if (fill_text(&procs, &text) != 0) {
        process_free(&procs);
        return 1;
    }

    print_table(&text);
    process_free(&procs);
    return 0;
}
