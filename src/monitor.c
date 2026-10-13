/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#include "monitor.h"

#include <stdio.h>

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
