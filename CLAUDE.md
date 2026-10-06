# CLAUDE.md

Guidance for Claude when working in this repository.

## Project

`llm-lab` is a Linux command-line tool written in C. It finds the running Ollama
server, then shows how much CPU, RAM and GPU (AMD) the LLM uses, next to the
system-wide totals. It is a school project for a systems-programming course
(4-5 weeks, no fixed deadline). The student writes the core logic; see
"Working rules" below.

Progress and the weekly plan live in `PROGRESS.md`. Update it at the end of
every session.

## Decisions (agreed, do not change without asking)

| Topic | Decision |
|---|---|
| Language | C11, English for code, comments, output and docs |
| Build | CMake (`CMakeLists.txt`), binary name `llm-lab` |
| Docker | Not used. No Dockerfile, compose file or dev container |
| LLM runtime | Ollama only |
| Process scope | `ollama serve` plus all of its child processes (the `ollama runner` that holds the model), summed |
| Process metrics | PIDs, runtime, CPU %, RAM (RSS), VRAM |
| System metrics | CPU %, RAM used/total, GPU load %, VRAM used/total |
| GPU | AMD only, `amdgpu` driver, read from sysfs and `/proc/<pid>/fdinfo` |
| Model name | Map the runner's model blob hash to a name using Ollama manifest files. No HTTP API |
| CLI | `llm-lab --help`, `llm-lab status`, `llm-lab status --watch N` |
| Output | One boxed Metric/Value table, as in the original `main.c` |
| Watch mode | Clear and redraw every N seconds; Ctrl+C exits cleanly with code 0 |
| Errors | Message to stderr, non-zero exit code |
| Format | clang-format (`.clang-format` in repo root) |
| Warnings | `-Wall -Wextra -Wpedantic`, code must build with zero warnings |
| License | MIT |
| Repo | Private GitHub repository |

Out of scope: benchmarking, GUI, database, web UI, NVIDIA/Intel GPUs, Ollama
HTTP API, LM Studio and other runtimes.

## Layout (target)

```
llm-lab/
  CMakeLists.txt
  src/      main.c cli.c process.c monitor.c gpu.c
  include/  cli.h process.h monitor.h gpu.h
  .claude/skills/  grill-me/ no-ai-slop/
  README.md PROGRESS.md CLAUDE.md LICENSE .gitignore .clang-format
```

| File | Responsibility |
|---|---|
| `main.c` | Entry point, dispatch to the chosen command |
| `cli.c/h` | Argument parsing: `--help`, `status`, `--watch N` |
| `process.c/h` | Find `ollama serve` and its children in `/proc`, read per-process stats, resolve model name |
| `monitor.c/h` | Sampling, CPU % math, system CPU/RAM, table rendering, watch loop |
| `gpu.c/h` | AMD GPU discovery and reading sysfs / fdinfo |

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/llm-lab --help
./build/llm-lab status
./build/llm-lab status --watch 2
```

Format: `clang-format -i src/*.c include/*.h`
Memory check: `valgrind --leak-check=full ./build/llm-lab status`

## Where the data comes from

Check every path on the real machine before relying on it (`cat` it first).

Process discovery
- Scan numeric entries in `/proc`. Match `/proc/<pid>/comm` == `ollama` and
  `/proc/<pid>/cmdline` containing `serve`.
- Children: scan `/proc/*/status` and collect every PID whose `PPid` chain
  leads to the serve PID.
- No `ollama serve` found: print `error: ollama is not running` to stderr, exit 1.

Per-process
- `/proc/<pid>/stat`: `utime` (field 14), `stime` (15), `starttime` (22), in
  clock ticks (`sysconf(_SC_CLK_TCK)`). The `comm` field can contain spaces
  and parentheses, so parse from the last `)`.
- `/proc/<pid>/status`: `VmRSS` in kB.
- Runtime of serve: `/proc/uptime` minus `starttime / ticks`.
- CPU %: take two samples, `delta(utime+stime) / (delta_seconds * ticks) * 100`.
  This is top-style: 100 % = one full core, so it can exceed 100 %.
- VRAM: read `/proc/<pid>/fdinfo/*`, keep entries with `drm-driver: amdgpu`,
  sum `drm-memory-vram` (KiB). Count each `drm-client-id` once, because several
  fds can point to the same client.

System
- CPU %: first line of `/proc/stat`, two samples, busy = total - (idle + iowait).
- RAM: `/proc/meminfo`, used = `MemTotal - MemAvailable`.
- GPU: find `/sys/class/drm/card*/device/` with `vendor` == `0x1002`. Do not
  hardcode `card0`. Read `gpu_busy_percent`, `mem_info_vram_used`,
  `mem_info_vram_total` (bytes).

Model name
- Model directory: `$OLLAMA_MODELS`, else `~/.ollama/models`, else
  `/usr/share/ollama/.ollama/models` (systemd service install).
- Get the blob hash from the runner's `cmdline` (`--model .../blobs/sha256-<hex>`).
  If it is not there (newer Ollama engine), look for `/blobs/sha256-` in
  `/proc/<runner>/maps`.
- Search `manifests/registry.ollama.ai/library/<name>/<tag>` for that digest.
  A `strstr` on the file text is enough; no JSON library.
- Not found: show `unknown`. No model loaded: show `none loaded`.

Permissions
- If Ollama runs as the `ollama` system user, `fdinfo` and `maps` of its
  processes are not readable by a normal user. Show `n/a (run with sudo)`
  for those fields instead of failing.

## Code rules

- C11, POSIX. No external libraries beyond libc.
- Every `fopen`/`opendir` has a matching close; every `malloc` has a `free`,
  also on error paths and on Ctrl+C.
- Check return values of every system call and `fscanf`/`strtol`.
- Use `sigaction` with a `volatile sig_atomic_t` flag for SIGINT.
- Keep functions short and named for what they do. Comments explain why, not what.
- No compiled binaries in git.

## Working rules for Claude

- Follow the `no-ai-slop` skill for all code and prose.
- The student writes the core logic (`/proc` parsing, CPU math, GPU reading,
  watch loop). Claude explains, reviews, points to man pages and asks
  questions. Claude may write boilerplate (CMake, headers, help text,
  `.gitignore`) when asked.
- Use the `grill-me` skill before starting any new feature or design change.
- At the end of a session, update `PROGRESS.md`: what was done, what is next,
  open questions.

## Submission checklist (from the course guide)

- Builds cleanly, `--help` works
- Errors go to stderr with a non-zero exit code
- Files closed, memory freed (valgrind clean)
- README: build, run, limitations, tested Linux distribution
- Code formatted with clang-format
- No binaries in the repository
- AI assistance documented as the course requires
