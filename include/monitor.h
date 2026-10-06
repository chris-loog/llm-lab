#ifndef MONITOR_H
#define MONITOR_H

#include <stddef.h>

/* Write kB as "34.4 MiB" below 1 GiB, "5.2 GiB" from 1 GiB up. */
void monitor_format_size(unsigned long kb, char *buf, size_t size);

/* Two largest units, truncated: "45s", "16m 26s", "3h 12m", "2d 5h". */
void monitor_format_duration(unsigned long seconds, char *buf, size_t size);

#endif
