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

#endif
