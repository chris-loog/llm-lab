/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#define _POSIX_C_SOURCE 200809L

#include "monitor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
