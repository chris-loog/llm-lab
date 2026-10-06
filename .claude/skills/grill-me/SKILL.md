---
name: grill-me
description: Interview the user one question at a time about a plan, feature or design until both sides agree on every decision. Use before starting a new feature, when the user says "grill me", or when requirements are unclear.
---

# Grill me

Question the user about the plan until there is a shared understanding of
every decision that affects the code.

## How

1. Read `CLAUDE.md` and `PROGRESS.md` first. Do not ask about anything already
   decided there.
2. If the codebase or the machine can answer a question (a file, a `/proc`
   path, a man page), look it up instead of asking.
3. Ask one question per message. Never batch questions.
4. Give 2-4 concrete options. Put the one you recommend first and say why in
   one sentence.
5. Follow the design tree: settle a decision before asking about the ones that
   depend on it.
6. Push back when an answer conflicts with an earlier decision, the course
   guide, or how Linux works. Say what the conflict is.
7. Stop when no open decisions remain. Then write a short summary of the
   decisions and ask the user to confirm it.
8. After confirmation, record the decisions in `CLAUDE.md` (if they change
   the design) and the task in `PROGRESS.md`.

## Topics to cover for a feature

- What exactly it does, input and output
- Where the data comes from (file, field, units)
- Error cases and exit codes
- What is out of scope
- How to check it works (compare with which tool)
