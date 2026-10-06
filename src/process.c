#define _POSIX_C_SOURCE 200809L

#include "process.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROC_GONE (-2)
#define FILE_BUF_SIZE 4096

typedef struct {
    pid_t pid;
    pid_t ppid;
} pid_pair_t;

typedef struct {
    pid_pair_t *items;
    size_t count;
    size_t capacity;
} pair_list_t;

static int parse_pid(const char *name, pid_t *pid)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(name, &end, 10);
    if (end == name || *end != '\0' || errno == ERANGE || value <= 0)
        return -1;

    *pid = (pid_t)value;
    return 0;
}

/*
 * Read /proc/<pid>/<name> into buf and terminate it. Returns the byte count,
 * PROC_GONE if the process exited meanwhile, or -1 after printing an error.
 */
static long read_proc_file(pid_t pid, const char *name, char *buf, size_t size)
{
    char path[64];
    FILE *file;
    size_t length;

    snprintf(path, sizeof(path), "/proc/%d/%s", (int)pid, name);
    file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT || errno == ESRCH)
            return PROC_GONE;
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    length = fread(buf, 1, size - 1, file);
    if (ferror(file)) {
        int saved = errno;

        fclose(file);
        if (saved == ENOENT || saved == ESRCH)
            return PROC_GONE;
        fprintf(stderr, "error: cannot read %s: %s\n", path, strerror(saved));
        return -1;
    }
    fclose(file);

    buf[length] = '\0';
    return (long)length;
}

/* Returns 1 for "ollama serve", 0 for anything else (also a vanished PID), -1 on error. */
static int is_serve(pid_t pid)
{
    char buf[FILE_BUF_SIZE];
    long length = read_proc_file(pid, "comm", buf, sizeof(buf));

    if (length < 0)
        return length == PROC_GONE ? 0 : -1;
    buf[strcspn(buf, "\n")] = '\0';
    if (strcmp(buf, "ollama") != 0)
        return 0;

    length = read_proc_file(pid, "cmdline", buf, sizeof(buf));
    if (length < 0)
        return length == PROC_GONE ? 0 : -1;

    /* cmdline is argv joined by NUL bytes, so strcmp on buf only sees argv[0]. */
    size_t first = strlen(buf);

    if (first + 1 >= (size_t)length)
        return 0;
    return strcmp(buf + first + 1, "serve") == 0;
}

/* Returns 0 on success, PROC_GONE if the process exited, -1 on error. */
static int read_ppid(pid_t pid, pid_t *ppid)
{
    char buf[FILE_BUF_SIZE];
    long length = read_proc_file(pid, "status", buf, sizeof(buf));
    const char *line;
    int value;

    if (length < 0)
        return (int)length;

    line = strstr(buf, "\nPPid:");
    if (line == NULL || sscanf(line + strlen("\nPPid:"), "%d", &value) != 1) {
        fprintf(stderr, "error: no PPid in /proc/%d/status\n", (int)pid);
        return -1;
    }

    *ppid = (pid_t)value;
    return 0;
}

static int pairs_add(pair_list_t *list, pid_t pid, pid_t ppid)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 256 : list->capacity * 2;
        pid_pair_t *items = realloc(list->items, capacity * sizeof(*items));

        if (items == NULL) {
            fprintf(stderr, "error: out of memory\n");
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }

    list->items[list->count].pid = pid;
    list->items[list->count].ppid = ppid;
    list->count++;
    return 0;
}

/* One pass over /proc: record every (pid, ppid) pair and count the serve processes. */
static int scan_proc(pair_list_t *pairs, pid_t *serve_pid, int *serve_count)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;

    if (dir == NULL) {
        fprintf(stderr, "error: cannot open /proc: %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        pid_t pid;
        pid_t ppid;
        int serve;
        int status;

        if (parse_pid(entry->d_name, &pid) != 0)
            continue;

        status = read_ppid(pid, &ppid);
        if (status == PROC_GONE)
            continue;
        serve = status == 0 ? is_serve(pid) : -1;
        if (serve < 0 || pairs_add(pairs, pid, ppid) != 0) {
            closedir(dir);
            return -1;
        }
        if (serve == 1) {
            if (*serve_count == 0)
                *serve_pid = pid;
            (*serve_count)++;
        }
        errno = 0;
    }

    if (errno != 0) {
        fprintf(stderr, "error: cannot read /proc: %s\n", strerror(errno));
        closedir(dir);
        return -1;
    }
    closedir(dir);
    return 0;
}

static int contains(const pid_t *list, size_t count, pid_t pid)
{
    for (size_t i = 0; i < count; i++) {
        if (list[i] == pid)
            return 1;
    }
    return 0;
}

/*
 * /proc lists PIDs in numeric order, not parent first, so a child can come
 * before its parent. Repeat until a full pass adds nothing new.
 */
static int collect_descendants(const pair_list_t *pairs, ollama_procs_t *out)
{
    int added = 1;

    out->children = malloc(pairs->count * sizeof(*out->children));
    if (out->children == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    while (added) {
        added = 0;
        for (size_t i = 0; i < pairs->count; i++) {
            pid_t pid = pairs->items[i].pid;
            pid_t ppid = pairs->items[i].ppid;

            if (contains(out->children, out->child_count, pid))
                continue;
            if (ppid == out->serve_pid || contains(out->children, out->child_count, ppid)) {
                out->children[out->child_count++] = pid;
                added = 1;
            }
        }
    }
    return 0;
}

int process_find(ollama_procs_t *out)
{
    pair_list_t pairs = {0};
    int serve_count = 0;
    int result = -1;

    out->serve_pid = 0;
    out->children = NULL;
    out->child_count = 0;

    if (scan_proc(&pairs, &out->serve_pid, &serve_count) != 0)
        goto done;
    if (serve_count == 0) {
        fprintf(stderr, "error: ollama is not running\n");
        goto done;
    }
    if (serve_count > 1) {
        fprintf(stderr, "error: multiple ollama serve processes found\n");
        goto done;
    }
    if (collect_descendants(&pairs, out) != 0)
        goto done;
    result = 0;

done:
    free(pairs.items);
    if (result != 0)
        process_free(out);
    return result;
}

void process_free(ollama_procs_t *procs)
{
    free(procs->children);
    procs->children = NULL;
    procs->child_count = 0;
}
