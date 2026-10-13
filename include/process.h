/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    pid_t serve_pid;
    pid_t *children; /* heap array, freed by process_free */
    size_t child_count;
} ollama_procs_t;

/*
 * Find "ollama serve" and every descendant of it. Returns 0 on success, -1
 * after printing the error to stderr (not running, several instances, or a
 * /proc read error). The caller exits with code 1 on -1.
 */
int process_find(ollama_procs_t *out);

void process_free(ollama_procs_t *procs);

/*
 * Resident memory (VmRSS) in kB. A process that exited or has no VmRSS line
 * (zombie) counts as 0. Returns 0 on success, -1 after printing an error.
 */
int process_rss_kb(pid_t pid, unsigned long *kb);

/* Sum of process_rss_kb over serve and all children. */
int process_total_rss_kb(const ollama_procs_t *procs, unsigned long *kb);

/*
 * Seconds since the process started. Returns 0 on success, -1 after printing
 * an error; if the process is gone that error is "ollama is not running".
 */
int process_runtime_seconds(pid_t pid, unsigned long *seconds);

/*
 * Text for the model row: names of the loaded models joined by ", " and cut
 * with "..." to fit size (at least 4), "none loaded", "unknown", or
 * "n/a (run with sudo)". Returns 0 on success, -1 after printing an error.
 */
int process_model_names(const ollama_procs_t *procs, char *buf, size_t size);

typedef struct {
    pid_t pid;
    unsigned long long ticks; /* utime + stime */
} cpu_entry_t;

typedef struct {
    cpu_entry_t *items; /* heap array, freed by process_cpu_sample_free */
    size_t count;
} cpu_sample_t;

/*
 * CPU ticks (utime + stime) of serve and every child that still exists.
 * Returns 0 on success, -1 after printing an error; if serve is gone that
 * error is "ollama is not running".
 */
int process_cpu_sample(const ollama_procs_t *procs, cpu_sample_t *out);

void process_cpu_sample_free(cpu_sample_t *sample);

/* Ticks used between two samples, counting only PIDs present in both. */
unsigned long long process_cpu_ticks_delta(const cpu_sample_t *before, const cpu_sample_t *after);

#endif
