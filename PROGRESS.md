# PROGRESS

Update this file at the end of every work session.

## Current state

- CMake project with a placeholder `main.c` that prints a hardcoded table.
- Scope and design agreed (see `CLAUDE.md`, "Decisions").
- `CLAUDE.md`, `PROGRESS.md`, `.gitignore`, `.clang-format`, `LICENSE` and the
  `grill-me` / `no-ai-slop` skills added.
- Next: create the git repository and start week 1.

## Plan

### Week 1: Structure and CLI
- [ ] `git init`, first commit, private GitHub repo
- [ ] Create `src/` and `include/`, move `main.c` to `src/`
- [ ] Update `CMakeLists.txt` for the new layout (C11, warnings on)
- [ ] `cli.c/h`: parse `--help`, `status`, `status --watch N`
- [ ] Unknown argument or bad N: message to stderr, exit 2
- [ ] `README.md`: what it is, how to build and run

Done when: `./build/llm-lab --help` prints usage and bad input gives an error.

### Week 2: Find Ollama, RAM, runtime, model name
- [ ] Scan `/proc`, find `ollama serve`; error + exit 1 if missing
- [ ] Collect child PIDs through `PPid`
- [ ] Sum `VmRSS` across serve + children
- [ ] Runtime from `/proc/<pid>/stat` and `/proc/uptime`
- [ ] Model name from runner cmdline/maps + manifests
- [ ] `status` prints the table with these real values

Done when: `status` shows real PIDs, RAM, runtime and model name.

### Week 3: CPU and system RAM
- [ ] Two-sample process CPU % (utime + stime)
- [ ] Two-sample system CPU % from `/proc/stat`
- [ ] System RAM used/total from `/proc/meminfo`
- [ ] Check numbers against `top` / `htop` while a model is generating

Done when: process and system CPU/RAM roughly match `htop`.

### Week 4: GPU and watch mode
- [ ] Find the AMD card in `/sys/class/drm` by vendor `0x1002`
- [ ] System GPU load %, VRAM used/total
- [ ] Per-process VRAM from `fdinfo` (dedupe by `drm-client-id`)
- [ ] `n/a (run with sudo)` when fdinfo is not readable
- [ ] `--watch N`: redraw loop, SIGINT handler, clean exit
- [ ] Check GPU numbers against `amdgpu_top` or `radeontop`

Done when: the full table works and `--watch 2` runs until Ctrl+C with no leaks.

### Week 5: Polish
- [ ] Edge cases: Ollama stops during watch, no AMD GPU, no model loaded
- [ ] valgrind clean on `status` and `--watch`
- [ ] clang-format on all files
- [ ] README: limitations, tested distribution, AI assistance note
- [ ] Go through the submission checklist in `CLAUDE.md`

Done when: the repo is ready to present.

## Session log

### Session 1 (2026-10-06)
- Read the course guide, agreed on scope with Claude through Q&A.
- Changes from the guide: Docker dropped; GPU/VRAM is now required (AMD
  only); system-wide stats added next to process stats; CMake instead of a
  Makefile; binary stays `llm-lab`.
- Added project docs, skills and repo config files.

## Open questions

- Which Linux distribution and kernel is this tested on? (needed for README)
- Is Ollama installed as a systemd service (runs as user `ollama`) or run by
  hand? This decides whether VRAM per process needs sudo.
- Does the installed Ollama version put `--model` in the runner's command line?
