/* SPDX-License-Identifier: MIT */
/* AI assistance: written by Claude (Anthropic) on request, design decided by the student. */

#define _POSIX_C_SOURCE 200809L

#include "gpu.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRM_DIR "/sys/class/drm"
#define AMD_VENDOR "0x1002"
#define MISSING 2

/* Reads one number from a sysfs file. Returns 0, MISSING if the file does not exist, or -1. */
static int read_number(const char *path, unsigned long long *value)
{
    FILE *file = fopen(path, "r");

    if (file == NULL) {
        if (errno == ENOENT)
            return MISSING;
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fscanf(file, "%llu", value) != 1) {
        fprintf(stderr, "error: cannot parse %s\n", path);
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

/* Returns 1 for an AMD vendor id, 0 for another vendor or no file, -1 on error. */
static int is_amd(const char *device)
{
    char path[300];
    char vendor[16];
    FILE *file;

    snprintf(path, sizeof(path), "%s/vendor", device);
    file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT)
            return 0;
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fscanf(file, "%15s", vendor) != 1) {
        fprintf(stderr, "error: cannot parse %s\n", path);
        fclose(file);
        return -1;
    }
    fclose(file);
    return strcmp(vendor, AMD_VENDOR) == 0;
}

/* "card1" yes; "card1-DP-1" and "renderD128" no. */
static int is_card_name(const char *name)
{
    if (strncmp(name, "card", 4) != 0 || name[4] == '\0')
        return 0;
    for (const char *c = name + 4; *c != '\0'; c++) {
        if (!isdigit((unsigned char)*c))
            return 0;
    }
    return 1;
}

/*
 * Checks one card. Returns 0 and sets *total if it is an amdgpu card, 1 if it
 * should be skipped (other vendor, or no amdgpu files), -1 on error.
 */
static int check_card(const char *name, char *device, size_t size, unsigned long long *total)
{
    char path[300];
    int status;

    snprintf(device, size, "%s/%s/device", DRM_DIR, name);
    status = is_amd(device);
    if (status < 0)
        return -1;
    if (status == 0)
        return 1;

    snprintf(path, sizeof(path), "%s/mem_info_vram_total", device);
    status = read_number(path, total);
    if (status == MISSING)
        return 1;
    return status;
}

int gpu_find(gpu_t *out)
{
    DIR *dir = opendir(DRM_DIR);
    struct dirent *entry;
    unsigned long long best_total = 0;

    out->found = 0;
    out->device[0] = '\0';
    if (dir == NULL) {
        if (errno == ENOENT)
            return 0;
        fprintf(stderr, "error: cannot open %s: %s\n", DRM_DIR, strerror(errno));
        return -1;
    }

    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        char device[256];
        unsigned long long total;
        int status;

        if (!is_card_name(entry->d_name))
            continue;
        status = check_card(entry->d_name, device, sizeof(device), &total);
        if (status < 0) {
            closedir(dir);
            return -1;
        }
        if (status == 0 && (!out->found || total > best_total)) {
            out->found = 1;
            best_total = total;
            snprintf(out->device, sizeof(out->device), "%s", device);
        }
        errno = 0;
    }
    if (errno != 0) {
        fprintf(stderr, "error: cannot read %s: %s\n", DRM_DIR, strerror(errno));
        closedir(dir);
        return -1;
    }
    closedir(dir);
    return 0;
}

int gpu_read_stats(const gpu_t *gpu, gpu_stats_t *out)
{
    static const char *const names[] = {"gpu_busy_percent", "mem_info_vram_used",
                                        "mem_info_vram_total"};
    unsigned long long *const values[] = {&out->busy_percent, &out->vram_used, &out->vram_total};

    for (int i = 0; i < 3; i++) {
        char path[300];
        int status;

        snprintf(path, sizeof(path), "%s/%s", gpu->device, names[i]);
        status = read_number(path, values[i]);
        if (status == MISSING)
            fprintf(stderr, "error: %s does not exist\n", path);
        if (status != 0)
            return -1;
    }
    return 0;
}

typedef struct {
    unsigned long long *ids;
    size_t count;
    size_t capacity;
} id_list_t;

static int ids_contain(const id_list_t *list, unsigned long long id)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->ids[i] == id)
            return 1;
    }
    return 0;
}

static int ids_add(id_list_t *list, unsigned long long id)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        unsigned long long *ids = realloc(list->ids, capacity * sizeof(*ids));

        if (ids == NULL) {
            fprintf(stderr, "error: out of memory\n");
            return -1;
        }
        list->ids = ids;
        list->capacity = capacity;
    }
    list->ids[list->count++] = id;
    return 0;
}

/*
 * Reads one fdinfo file. Adds its VRAM to *kb unless the client was already
 * counted. Returns 0, GPU_DENIED, or -1.
 */
static int read_fdinfo(const char *path, id_list_t *seen, unsigned long *kb)
{
    char line[256];
    char driver[32] = "";
    unsigned long long client_id = 0;
    unsigned long vram = 0;
    int have_client = 0;
    FILE *file = fopen(path, "r");

    if (file == NULL) {
        if (errno == ENOENT || errno == ESRCH)
            return 0;
        if (errno == EACCES || errno == EPERM)
            return GPU_DENIED;
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "drm-driver: %31s", driver) == 1)
            continue;
        if (sscanf(line, "drm-client-id: %llu", &client_id) == 1)
            have_client = 1;
        else
            (void)sscanf(line, "drm-memory-vram: %lu KiB", &vram);
    }
    fclose(file);

    if (strcmp(driver, "amdgpu") != 0 || !have_client || ids_contain(seen, client_id))
        return 0;
    if (ids_add(seen, client_id) != 0)
        return -1;
    *kb += vram;
    return 0;
}

/* Every fd of one process. Returns 0, GPU_DENIED, or -1. */
static int read_process_fdinfo(pid_t pid, id_list_t *seen, unsigned long *kb)
{
    char dir_path[64];
    DIR *dir;
    struct dirent *entry;
    int result = 0;

    snprintf(dir_path, sizeof(dir_path), "/proc/%d/fdinfo", (int)pid);
    dir = opendir(dir_path);
    if (dir == NULL) {
        if (errno == ENOENT || errno == ESRCH)
            return 0;
        if (errno == EACCES || errno == EPERM)
            return GPU_DENIED;
        fprintf(stderr, "error: cannot open %s: %s\n", dir_path, strerror(errno));
        return -1;
    }

    while (result == 0 && (entry = readdir(dir)) != NULL) {
        char path[512];

        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        result = read_fdinfo(path, seen, kb);
    }
    closedir(dir);
    return result;
}

int gpu_process_vram_kb(const ollama_procs_t *procs, unsigned long *kb)
{
    id_list_t seen = {0};
    int result = 0;

    *kb = 0;
    for (size_t i = 0; i <= procs->child_count && result == 0; i++) {
        pid_t pid = i == 0 ? procs->serve_pid : procs->children[i - 1];

        result = read_process_fdinfo(pid, &seen, kb);
    }
    free(seen.ids);
    if (result != 0)
        *kb = 0;
    return result;
}
