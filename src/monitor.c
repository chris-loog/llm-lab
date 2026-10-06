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
