/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#define _POSIX_C_SOURCE 200809L

#include "monitor.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "gpu.h"

#define KB_PER_MIB 1024UL
#define KB_PER_GIB (1024UL * 1024UL)

/* Integer math in tenths avoids "1024.0 MiB" that "%.1f" prints for 1048575 kB. */
void monitor_format_size(unsigned long kb, char *buf, size_t size)
{
    unsigned long mib_tenths = (kb * 10 + KB_PER_MIB / 2) / KB_PER_MIB;

    if (mib_tenths < 10240) {
        snprintf(buf, size, "%lu.%lu MiB", mib_tenths / 10, mib_tenths % 10);
        return;
    }

    unsigned long gib_tenths = (kb * 10 + KB_PER_GIB / 2) / KB_PER_GIB;

    snprintf(buf, size, "%lu.%lu GiB", gib_tenths / 10, gib_tenths % 10);
}

void monitor_format_duration(unsigned long seconds, char *buf, size_t size)
{
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    if (days > 0)
        snprintf(buf, size, "%lud %luh", days, hours % 24);
    else if (hours > 0)
        snprintf(buf, size, "%luh %lum", hours, minutes % 60);
    else if (minutes > 0)
        snprintf(buf, size, "%lum %lus", minutes, seconds % 60);
    else
        snprintf(buf, size, "%lus", seconds);
}

/* guest time is already inside user time, so it is not added again. */
static int read_system_cpu(unsigned long long *busy, unsigned long long *total)
{
    unsigned long long v[8];
    FILE *file = fopen("/proc/stat", "r");
    int count;

    if (file == NULL) {
        fprintf(stderr, "error: cannot open /proc/stat: %s\n", strerror(errno));
        return -1;
    }
    count = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3],
                   &v[4], &v[5], &v[6], &v[7]);
    fclose(file);
    if (count != 8) {
        fprintf(stderr, "error: cannot parse /proc/stat\n");
        return -1;
    }

    *total = 0;
    for (int i = 0; i < 8; i++)
        *total += v[i];
    *busy = *total - (v[3] + v[4]);
    return 0;
}

int monitor_take_sample(const ollama_procs_t *procs, monitor_sample_t *out)
{
    if (process_cpu_sample(procs, &out->procs) != 0)
        return -1;
    if (read_system_cpu(&out->system_busy, &out->system_total) != 0) {
        process_cpu_sample_free(&out->procs);
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &out->taken) != 0) {
        fprintf(stderr, "error: cannot read the clock: %s\n", strerror(errno));
        process_cpu_sample_free(&out->procs);
        return -1;
    }
    return 0;
}

void monitor_sample_free(monitor_sample_t *sample)
{
    process_cpu_sample_free(&sample->procs);
}

int monitor_cpu_percent(const monitor_sample_t *before, const monitor_sample_t *after,
                        double *process_percent, double *system_percent)
{
    long ticks_per_second = sysconf(_SC_CLK_TCK);
    double seconds = (double)(after->taken.tv_sec - before->taken.tv_sec) +
                     (double)(after->taken.tv_nsec - before->taken.tv_nsec) / 1e9;
    unsigned long long total = after->system_total - before->system_total;

    if (ticks_per_second <= 0 || seconds <= 0 || after->system_total < before->system_total) {
        fprintf(stderr, "error: cannot compute CPU usage\n");
        return -1;
    }

    *process_percent = (double)process_cpu_ticks_delta(&before->procs, &after->procs) /
                       (seconds * (double)ticks_per_second) * 100.0;
    *system_percent =
        total == 0 ? 0.0
                   : (double)(after->system_busy - before->system_busy) / (double)total * 100.0;
    return 0;
}

int monitor_read_memory(unsigned long *used_kb, unsigned long *total_kb)
{
    char line[256];
    unsigned long available = 0;
    int have_total = 0;
    int have_available = 0;
    FILE *file = fopen("/proc/meminfo", "r");

    if (file == NULL) {
        fprintf(stderr, "error: cannot open /proc/meminfo: %s\n", strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "MemTotal: %lu kB", total_kb) == 1)
            have_total = 1;
        else if (sscanf(line, "MemAvailable: %lu kB", &available) == 1)
            have_available = 1;
    }
    fclose(file);

    if (!have_total || !have_available || available > *total_kb) {
        fprintf(stderr, "error: cannot parse /proc/meminfo\n");
        return -1;
    }
    *used_kb = *total_kb - available;
    return 0;
}

void monitor_format_percent(double percent, char *buf, size_t size)
{
    snprintf(buf, size, "%.1f %%", percent);
}

#define LABEL_WIDTH 26
#define VALUE_WIDTH 24
#define FIRST_SAMPLE_SECONDS 1
#define CLEAR_SCREEN "\033[H\033[2J"

typedef struct {
    char pid[VALUE_WIDTH + 1];
    char children[VALUE_WIDTH + 1];
    char ram[VALUE_WIDTH + 1];
    char runtime[VALUE_WIDTH + 1];
    char model[VALUE_WIDTH + 1];
    char process_cpu[VALUE_WIDTH + 1];
    char system_cpu[VALUE_WIDTH + 1];
    char system_ram[2 * 12 + 4];
    char process_vram[VALUE_WIDTH + 1];
    char system_vram[2 * 12 + 4];
    char gpu_load[VALUE_WIDTH + 1];
} status_text_t;

static volatile sig_atomic_t stop_requested;

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
    print_row("Ollama VRAM", text->process_vram);
    print_row("System VRAM", text->system_vram);
    print_row("GPU load", text->gpu_load);
    print_border();
}

/* VRAM and GPU load rows. A read error on a found card is an error; the rest is "n/a". */
static int fill_gpu_text(const ollama_procs_t *procs, const gpu_t *gpu, status_text_t *text)
{
    gpu_stats_t stats;
    unsigned long vram_kb;
    char used_text[12];
    char total_text[12];
    int status;

    if (!gpu->found) {
        const char *none = "n/a (no AMD GPU)";

        snprintf(text->process_vram, sizeof(text->process_vram), "%s", none);
        snprintf(text->system_vram, sizeof(text->system_vram), "%s", none);
        snprintf(text->gpu_load, sizeof(text->gpu_load), "%s", none);
        return 0;
    }

    if (gpu_read_stats(gpu, &stats) != 0)
        return -1;
    status = gpu_process_vram_kb(procs, &vram_kb);
    if (status < 0)
        return -1;

    if (status == GPU_DENIED)
        snprintf(text->process_vram, sizeof(text->process_vram), "n/a (run with sudo)");
    else
        monitor_format_size(vram_kb, text->process_vram, sizeof(text->process_vram));
    monitor_format_size(stats.vram_used / 1024, used_text, sizeof(used_text));
    monitor_format_size(stats.vram_total / 1024, total_text, sizeof(total_text));
    snprintf(text->system_vram, sizeof(text->system_vram), "%s / %s", used_text, total_text);
    monitor_format_percent((double)stats.busy_percent, text->gpu_load, sizeof(text->gpu_load));
    return 0;
}

static int fill_text(const ollama_procs_t *procs, const gpu_t *gpu, const monitor_sample_t *before,
                     const monitor_sample_t *after, status_text_t *text)
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
        monitor_cpu_percent(before, after, &process_percent, &system_percent) != 0 ||
        fill_gpu_text(procs, gpu, text) != 0)
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

static void handle_sigint(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/* SIGINT stays blocked except inside pselect, so it cannot slip in just before the sleep. */
static int install_sigint(sigset_t *old_mask)
{
    struct sigaction action;
    sigset_t block;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigprocmask(SIG_BLOCK, &block, old_mask) != 0) {
        fprintf(stderr, "error: cannot set up Ctrl+C handling: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Returns 0 after the full time, 1 if Ctrl+C woke it up, -1 on error. */
static int wait_seconds(int seconds, const sigset_t *mask)
{
    struct timespec timeout = {.tv_sec = seconds, .tv_nsec = 0};

    if (stop_requested)
        return 1;
    if (pselect(0, NULL, NULL, NULL, &timeout, mask) == 0)
        return 0;
    if (errno == EINTR)
        return stop_requested ? 1 : 0;
    fprintf(stderr, "error: cannot wait: %s\n", strerror(errno));
    return -1;
}

/* Draws one table. In watch mode on a terminal it clears first; piped output just appends. */
static void draw(const status_text_t *text, int watch)
{
    int clear = watch && isatty(STDOUT_FILENO);

    if (clear)
        printf(CLEAR_SCREEN);
    else if (watch)
        printf("\n");
    print_table(text);
    fflush(stdout);
}

/*
 * One round: wait, find Ollama again, take the next sample and draw. The new
 * sample replaces *before. Returns 0, 1 if stopped by Ctrl+C, -1 on error.
 */
static int run_round(int seconds, const sigset_t *mask, pid_t serve_pid, const gpu_t *gpu,
                     monitor_sample_t *before, int watch)
{
    ollama_procs_t procs;
    monitor_sample_t after;
    status_text_t text;
    int result = wait_seconds(seconds, mask);

    if (result != 0 || process_find(&procs) != 0)
        return result != 0 ? result : -1;
    if (procs.serve_pid != serve_pid) {
        fprintf(stderr, "error: ollama is not running\n");
        result = -1;
    } else if (monitor_take_sample(&procs, &after) != 0) {
        result = -1;
    } else {
        result = fill_text(&procs, gpu, before, &after, &text);
        if (result == 0) {
            draw(&text, watch);
            monitor_sample_free(before);
            *before = after;
        } else {
            monitor_sample_free(&after);
        }
    }
    process_free(&procs);
    return result;
}

int monitor_run(int watch_seconds)
{
    ollama_procs_t procs;
    monitor_sample_t before;
    sigset_t mask;
    gpu_t gpu;
    pid_t serve_pid;
    int seconds = FIRST_SAMPLE_SECONDS;
    int result;

    sigemptyset(&mask);
    if (watch_seconds > 0 && install_sigint(&mask) != 0)
        return 1;
    if (process_find(&procs) != 0)
        return 1;
    serve_pid = procs.serve_pid;
    if (gpu_find(&gpu) != 0 || monitor_take_sample(&procs, &before) != 0) {
        process_free(&procs);
        return 1;
    }
    process_free(&procs);

    do {
        result = run_round(seconds, &mask, serve_pid, &gpu, &before, watch_seconds > 0);
        seconds = watch_seconds;
    } while (result == 0 && watch_seconds > 0);

    monitor_sample_free(&before);
    return result < 0 ? 1 : 0;
}
