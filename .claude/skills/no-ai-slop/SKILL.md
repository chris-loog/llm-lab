---
name: no-ai-slop
description: Rules for writing code, docs and commit messages in this project without AI filler, and for keeping the student in charge of the core logic. Use for every code change, review, README edit or commit message.
---

# No AI slop

## Code

- Write only what the task needs. No unused functions, variables, includes,
  or "for future use" parameters.
- No comments that repeat the code (`// open the file`). Comment only the
  reason behind something non-obvious, such as why `comm` is parsed from the
  last `)`.
- No `TODO`, `FIXME` or placeholder values left in committed code.
- Do not invent APIs, file paths or fields. Before using a `/proc` or `/sys`
  path, check it exists on this machine (`cat` it) or in the man page
  (`man 5 proc`). If unsure, say so.
- No defensive code for cases that cannot happen; do handle every real error
  (failed `fopen`, short read, permission denied).
- Match the existing style and `.clang-format`. Do not reformat unrelated code.
- Keep diffs small. One change per commit.

## Prose (README, PROGRESS, commit messages, chat)

- Short, factual sentences. Say what it does, not how great it is.
- Banned: "robust", "seamless", "leverage", "comprehensive", "powerful",
  "elegant", "delve", "In conclusion", "It's worth noting", emoji.
- No summary paragraph that repeats what was just said.
- Commit messages: imperative subject under 60 characters ("Read VmRSS from
  /proc/pid/status"), optional body explaining why.
- Numbers need units (kB, MiB, %, seconds).

## Learning guard

The student must be able to explain every line at the presentation.

- The student writes the core logic: `/proc` and sysfs parsing, CPU %
  calculation, GPU reading, the watch loop and signal handling.
- For those parts, Claude explains the concept, points to the relevant file or
  man page, reviews the student's code and asks questions about it. Claude
  does not write the finished implementation unless the student explicitly
  asks for it in that message.
- Claude may write boilerplate when asked: CMake, header guards, help text,
  `.gitignore`, test commands.
- When reviewing, name the line and the problem. Ask "what happens if ...?"
  rather than handing over the fix.
