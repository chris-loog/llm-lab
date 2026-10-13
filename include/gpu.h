/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#ifndef GPU_H
#define GPU_H

#include "process.h"

#define GPU_DENIED 1

typedef struct {
    int found;        /* 0 when the machine has no AMD GPU with the amdgpu files */
    char device[256]; /* /sys/class/drm/card<N>/device */
} gpu_t;

typedef struct {
    unsigned long long busy_percent;
    unsigned long long vram_used; /* bytes */
    unsigned long long vram_total;
} gpu_stats_t;

/*
 * Look for AMD cards (vendor 0x1002) in /sys/class/drm and keep the one with
 * the most VRAM. Finding none is not an error (out->found is 0). Returns 0 on
 * success, -1 after printing an error.
 */
int gpu_find(gpu_t *out);

/* Load and VRAM of the card found by gpu_find. Returns 0, or -1 after printing an error. */
int gpu_read_stats(const gpu_t *gpu, gpu_stats_t *out);

/*
 * VRAM in kB used by serve and its children, each drm-client-id counted once.
 * Returns 0 on success, GPU_DENIED if fdinfo is not readable (then *kb is 0),
 * or -1 after printing an error.
 */
int gpu_process_vram_kb(const ollama_procs_t *procs, unsigned long *kb);

#endif
