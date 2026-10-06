#define _POSIX_C_SOURCE 200809L

#include "process.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROC_GONE (-2)
#define PROC_DENIED (-3)
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
 * Read a file into buf and terminate it. Returns the byte count, PROC_GONE if
 * the file or process disappeared, PROC_DENIED for a permission error when
 * allow_denied is set, or -1 after printing an error.
 */
static long read_file(const char *path, char *buf, size_t size, int allow_denied)
{
    FILE *file;
    size_t length;
    int saved;

    file = fopen(path, "r");
    if (file == NULL) {
        saved = errno;
        goto failed;
    }

    length = fread(buf, 1, size - 1, file);
    if (ferror(file)) {
        saved = errno;
        fclose(file);
        goto failed;
    }
    fclose(file);

    buf[length] = '\0';
    return (long)length;

failed:
    if (saved == ENOENT || saved == ESRCH)
        return PROC_GONE;
    if (allow_denied && (saved == EACCES || saved == EPERM))
        return PROC_DENIED;
    fprintf(stderr, "error: cannot read %s: %s\n", path, strerror(saved));
    return -1;
}

static long read_proc_file(pid_t pid, const char *name, char *buf, size_t size)
{
    char path[64];

    snprintf(path, sizeof(path), "/proc/%d/%s", (int)pid, name);
    return read_file(path, buf, size, 0);
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

int process_rss_kb(pid_t pid, unsigned long *kb)
{
    char buf[FILE_BUF_SIZE];
    long length = read_proc_file(pid, "status", buf, sizeof(buf));
    const char *line;

    *kb = 0;
    if (length == PROC_GONE)
        return 0;
    if (length < 0)
        return -1;

    /* Zombies have no address space, so the kernel omits the VmRSS line. */
    line = strstr(buf, "\nVmRSS:");
    if (line == NULL)
        return 0;
    if (sscanf(line + strlen("\nVmRSS:"), "%lu", kb) != 1) {
        fprintf(stderr, "error: bad VmRSS in /proc/%d/status\n", (int)pid);
        return -1;
    }
    return 0;
}

int process_total_rss_kb(const ollama_procs_t *procs, unsigned long *kb)
{
    unsigned long part;

    *kb = 0;
    if (process_rss_kb(procs->serve_pid, &part) != 0)
        return -1;
    *kb += part;

    for (size_t i = 0; i < procs->child_count; i++) {
        if (process_rss_kb(procs->children[i], &part) != 0)
            return -1;
        *kb += part;
    }
    return 0;
}

static int read_uptime(double *seconds)
{
    FILE *file = fopen("/proc/uptime", "r");

    if (file == NULL) {
        fprintf(stderr, "error: cannot open /proc/uptime: %s\n", strerror(errno));
        return -1;
    }
    if (fscanf(file, "%lf", seconds) != 1) {
        fprintf(stderr, "error: cannot parse /proc/uptime\n");
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

/* starttime is field 22 of /proc/<pid>/stat, in clock ticks since boot. */
static int read_start_ticks(pid_t pid, unsigned long long *ticks)
{
    char buf[FILE_BUF_SIZE];
    long length = read_proc_file(pid, "stat", buf, sizeof(buf));
    const char *rest;

    if (length == PROC_GONE) {
        fprintf(stderr, "error: ollama is not running\n");
        return -1;
    }
    if (length < 0)
        return -1;

    /* comm (field 2) may contain spaces and ')', so parse after the last ')'. */
    rest = strrchr(buf, ')');
    if (rest == NULL ||
        sscanf(rest + 1,
               " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu",
               ticks) != 1) {
        fprintf(stderr, "error: cannot parse /proc/%d/stat\n", (int)pid);
        return -1;
    }
    return 0;
}

int process_runtime_seconds(pid_t pid, unsigned long *seconds)
{
    unsigned long long start_ticks;
    double uptime;
    double runtime;
    long ticks_per_second = sysconf(_SC_CLK_TCK);

    if (ticks_per_second <= 0) {
        fprintf(stderr, "error: cannot read clock ticks per second\n");
        return -1;
    }
    if (read_start_ticks(pid, &start_ticks) != 0 || read_uptime(&uptime) != 0)
        return -1;

    runtime = uptime - (double)start_ticks / (double)ticks_per_second;
    *seconds = runtime > 0 ? (unsigned long)runtime : 0;
    return 0;
}

#define MANIFEST_BUF_SIZE 32768
#define MODEL_LIST_SIZE 512
#define BLOB_MARKER "/blobs/sha256-"
#define DIGEST_HEX_LENGTH 64

enum {
    MODEL_OK,
    MODEL_UNKNOWN,
    MODEL_DENIED,
    MODEL_NOT_RUNNER,
};

typedef struct {
    char best[PATH_MAX];
    int found;
} manifest_match_t;

/*
 * Copy the value after --model from /proc/<pid>/cmdline. Returns 1 if found,
 * 0 if the process has no --model, PROC_GONE, PROC_DENIED or -1.
 */
static int find_model_path(pid_t pid, char *path, size_t size)
{
    char file[64];
    char buf[FILE_BUF_SIZE];
    long length;

    snprintf(file, sizeof(file), "/proc/%d/cmdline", (int)pid);
    length = read_file(file, buf, sizeof(buf), 1);
    if (length < 0)
        return (int)length;

    for (const char *arg = buf; arg < buf + length; arg += strlen(arg) + 1) {
        const char *value;

        if (strcmp(arg, "--model") != 0)
            continue;
        value = arg + strlen(arg) + 1;
        if (value >= buf + length || strlen(value) >= size)
            return 0;
        snprintf(path, size, "%s", value);
        return 1;
    }
    return 0;
}

/*
 * Split ".../models/blobs/sha256-<hex>" into the models directory and the
 * "sha256:<hex>" text that manifests contain (a colon, not a dash).
 */
static int split_blob_path(const char *blob, char *dir, size_t dir_size, char *digest,
                           size_t digest_size)
{
    const char *marker = strstr(blob, BLOB_MARKER);
    const char *hex;
    size_t dir_length;

    if (marker == NULL)
        return 0;
    hex = marker + strlen(BLOB_MARKER);
    if (strlen(hex) != DIGEST_HEX_LENGTH || strspn(hex, "0123456789abcdef") != DIGEST_HEX_LENGTH)
        return 0;

    dir_length = (size_t)(marker - blob);
    if (dir_length >= dir_size)
        return 0;
    memcpy(dir, blob, dir_length);
    dir[dir_length] = '\0';
    snprintf(digest, digest_size, "sha256:%s", hex);
    return 1;
}

/* Returns 1 if the manifest mentions the digest, 0 if not or if it vanished, <0 on error. */
static int manifest_contains(const char *path, const char *digest)
{
    char buf[MANIFEST_BUF_SIZE];
    long length = read_file(path, buf, sizeof(buf), 1);

    if (length == PROC_GONE)
        return 0;
    if (length < 0)
        return (int)length;
    return strstr(buf, digest) != NULL;
}

static int walk_manifests(char *path, size_t size, const char *digest, manifest_match_t *match);

static int fail_path(const char *what, const char *path)
{
    if (errno == ENOENT)
        return 0;
    if (errno == EACCES || errno == EPERM)
        return PROC_DENIED;
    fprintf(stderr, "error: cannot %s %s: %s\n", what, path, strerror(errno));
    return -1;
}

/* Several tags can share one blob, so keep the smallest path to stay stable between runs. */
static int visit_path(char *path, size_t size, const char *digest, manifest_match_t *match)
{
    struct stat info;
    int found;

    if (lstat(path, &info) != 0)
        return fail_path("stat", path);
    if (S_ISDIR(info.st_mode))
        return walk_manifests(path, size, digest, match);
    if (!S_ISREG(info.st_mode))
        return 0;

    found = manifest_contains(path, digest);
    if (found == 1 && (!match->found || strcmp(path, match->best) < 0)) {
        snprintf(match->best, sizeof(match->best), "%s", path);
        match->found = 1;
    }
    return found < 0 ? found : 0;
}

/* path is a scratch buffer: entries are appended and cut off again, so it ends as it began. */
static int walk_manifests(char *path, size_t size, const char *digest, manifest_match_t *match)
{
    DIR *dir = opendir(path);
    size_t base = strlen(path);
    int result = 0;

    if (dir == NULL)
        return fail_path("open", path);

    while (result == 0) {
        struct dirent *entry;
        int written;

        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                fprintf(stderr, "error: cannot read %s: %s\n", path, strerror(errno));
                result = -1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        written = snprintf(path + base, size - base, "/%s", entry->d_name);
        if (written > 0 && (size_t)written < size - base)
            result = visit_path(path, size, digest, match);
        path[base] = '\0';
    }

    closedir(dir);
    return result;
}

/* manifests/<host>/<namespace>/<name>/<tag> becomes "<name>:<tag>", or "<namespace>/<name>:<tag>". */
static int model_name_from_path(const char *manifest, size_t prefix_length, char *name,
                                size_t size)
{
    const char *host_end = strchr(manifest + prefix_length + 1, '/');
    const char *model;
    const char *tag_start;

    if (host_end == NULL)
        return 0;
    model = host_end + 1;
    tag_start = strrchr(model, '/');
    if (tag_start == NULL)
        return 0;

    if (strncmp(model, "library/", strlen("library/")) == 0)
        model += strlen("library/");
    snprintf(name, size, "%.*s:%s", (int)(tag_start - model), model, tag_start + 1);
    return 1;
}

/* Returns a MODEL_* value, or -1 after printing an error. */
static int model_for_runner(pid_t pid, char *name, size_t size)
{
    char blob[PATH_MAX];
    char root[PATH_MAX];
    char digest[DIGEST_HEX_LENGTH + 16];
    manifest_match_t match = {.found = 0};
    int status = find_model_path(pid, blob, sizeof(blob));

    if (status == PROC_DENIED)
        return MODEL_DENIED;
    if (status == -1)
        return -1;
    if (status != 1)
        return MODEL_NOT_RUNNER;

    if (!split_blob_path(blob, root, sizeof(root) - strlen("/manifests"), digest, sizeof(digest)))
        return MODEL_UNKNOWN;
    strcat(root, "/manifests");

    status = walk_manifests(root, sizeof(root), digest, &match);
    if (status == PROC_DENIED)
        return MODEL_DENIED;
    if (status < 0)
        return -1;
    if (!match.found || !model_name_from_path(match.best, strlen(root), name, size))
        return MODEL_UNKNOWN;
    return MODEL_OK;
}

/* Cut with "..." when it does not fit; size must be at least 4. */
static void copy_text(char *buf, size_t size, const char *text)
{
    if (strlen(text) < size)
        snprintf(buf, size, "%s", text);
    else
        snprintf(buf, size, "%.*s...", (int)(size - 4), text);
}

int process_model_names(const ollama_procs_t *procs, char *buf, size_t size)
{
    char list[MODEL_LIST_SIZE] = "";
    size_t used = 0;
    int runners = 0;

    for (size_t i = 0; i < procs->child_count; i++) {
        char name[256];
        int status = model_for_runner(procs->children[i], name, sizeof(name));
        int written;

        if (status < 0)
            return -1;
        if (status == MODEL_NOT_RUNNER)
            continue;
        if (status == MODEL_DENIED) {
            copy_text(buf, size, "n/a (run with sudo)");
            return 0;
        }

        written = snprintf(list + used, sizeof(list) - used, "%s%s", runners > 0 ? ", " : "",
                           status == MODEL_OK ? name : "unknown");
        if (written > 0)
            used += (size_t)written;
        if (used >= sizeof(list))
            used = sizeof(list) - 1;
        runners++;
    }

    copy_text(buf, size, runners == 0 ? "none loaded" : list);
    return 0;
}
