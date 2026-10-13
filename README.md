# llm-lab

A Linux command-line tool that finds the running Ollama server and shows how
much CPU, RAM and GPU memory it uses, next to the system-wide totals.

Status: work in progress. `status` shows real values for the Ollama processes
(PID, child count, RAM, runtime, model, CPU %) and for system CPU % and RAM.
The VRAM and GPU load rows are still hardcoded placeholders.

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

`status --watch N` redraws every N seconds (1-3600). Ctrl+C exits. The watch
loop is not implemented yet: the interval is parsed but ignored. `status`
waits 1 s to measure CPU %.

Bad input prints an error to stderr and exits with code 2.

## Limitations

- Linux only.
- Ollama only. The Ollama HTTP API is not used.
- AMD GPUs only (`amdgpu` driver).

## Tested on

Ubuntu 26.04.1 LTS, kernel 7.0.0-34-generic, gcc 15.2.0, CMake 4.2.3.

## License

MIT, see `LICENSE`.
