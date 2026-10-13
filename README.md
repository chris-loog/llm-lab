# llm-lab

A Linux command-line tool that finds the running Ollama server and shows how
much CPU, RAM and GPU memory it uses, next to the system-wide totals.

It shows, for `ollama serve` and its child processes: PIDs, runtime, model
name, CPU %, RAM and VRAM. Next to them it shows system CPU %, RAM, GPU load
and VRAM.

## Build

Requires a C11 compiler and CMake 3.16 or newer.

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/llm-lab --help
./build/llm-lab status
./build/llm-lab status --watch 2
```

`status` waits 1 s to measure CPU %. `status --watch N` redraws every N
seconds (1-3600) until Ctrl+C, which exits with code 0. When stdout is not a
terminal, the tables are appended instead of clearing the screen.

The Ollama CPU row is top-style (100 % is one core, so it can exceed 100 %).
The system CPU row is 0-100 % of the whole machine. If Ollama runs as another
user (the default systemd install), its per-process VRAM is not readable and
the row shows `n/a (run with sudo)`; run `sudo ./build/llm-lab status` to see it.

Bad input prints an error to stderr and exits with code 2.

## Limitations

- Linux only.
- Ollama only. The Ollama HTTP API is not used.
- AMD GPUs only (`amdgpu` driver). With several AMD cards the one with the most
  VRAM is shown. Without an AMD GPU the GPU rows show `n/a (no AMD GPU)`.
- One `ollama serve` only. If Ollama stops during `--watch`, the tool exits
  with an error.

## Tested on

Ubuntu 26.04.1 LTS, kernel 7.0.0-38-generic, gcc 15.2.0, CMake 4.2.3, Ollama 0.34.2
with an AMD GPU (`amdgpu`).

## AI assistance

Most of the code was written by Claude (Anthropic) through Claude Code, on the
student's request. The student decided the design (the decisions were agreed in
question-and-answer sessions before each feature) and checked the results
against `top`, `free`, `ps` and the raw `/sys` and `/proc` files. Every source
file starts with a comment that says so.

## License

MIT, see `LICENSE`.
