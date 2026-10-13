/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#ifndef MONITOR_H
#define MONITOR_H

#include <stddef.h>
#include <time.h>

#include "process.h"

/* Write kB as "34.4 MiB" below 1 GiB, "5.2 GiB" from 1 GiB up. */
void monitor_format_size(unsigned long kb, char *buf, size_t size);

/* Two largest units, truncated: "45s", "16m 26s", "3h 12m", "2d 5h". */
void monitor_format_duration(unsigned long seconds, char *buf, size_t size);

typedef struct {
    cpu_sample_t procs;
    unsigned long long system_busy; /* /proc/stat ticks, all CPUs */
    unsigned long long system_total;
    struct timespec taken;
} monitor_sample_t;

/* Read process and system CPU counters. 0 on success, -1 after printing an error. */
int monitor_take_sample(const ollama_procs_t *procs, monitor_sample_t *out);

void monitor_sample_free(monitor_sample_t *sample);

/*
 * Process CPU is top-style (100 % = one core, can exceed 100 %); system CPU
 * is 0-100 % of the whole machine. Returns 0 on success, -1 if the samples
 * are not in time order.
 */
int monitor_cpu_percent(const monitor_sample_t *before, const monitor_sample_t *after,
                        double *process_percent, double *system_percent);

/* RAM used (MemTotal - MemAvailable) and total, in kB. 0 on success, -1 after printing an error. */
int monitor_read_memory(unsigned long *used_kb, unsigned long *total_kb);

/* One decimal and a percent sign: "212.4 %". */
void monitor_format_percent(double percent, char *buf, size_t size);

/*
 * Print the status table. watch_seconds is 0 for one table, or the redraw
 * interval; watch mode runs until Ctrl+C. Returns the exit code: 0, or 1 after
 * printing an error to stderr.
 */
int monitor_run(int watch_seconds);

#endif
