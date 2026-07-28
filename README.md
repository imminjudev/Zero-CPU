# Zero-CPU

![CI](https://github.com/imminjudev/Zero-CPU/actions/workflows/ci.yml/badge.svg)
![C++17](https://img.shields.io/badge/C++-17-blue)
![License: MIT](https://img.shields.io/badge/License-MIT-green)

Zero-CPU is a small virtual computer platform written in C++17.

It includes a custom ISA, assembler, `.zbin` binary format, binary loader,
virtual CPU, MMIO devices, interrupt handling, `INT 80` syscalls, a BIO-OS demo,
and a Win32 Studio debugger.

The project focuses on **observable execution**: making CPU state transitions,
memory activity, stack behavior, control flow, interrupts, and debugger traces
visible and inspectable.

---

## Why I Built This

Zero-CPU is a systems programming project for learning and demonstrating how a
computer platform fits together:

```text
assembly source
  -> assembler
  -> binary encoding
  -> virtual memory
  -> CPU execution
  -> MMIO / interrupts / syscalls
  -> debugger trace
```

The goal is not to emulate a real commercial CPU.  
The goal is to build a compact, inspectable platform where each execution step
can be traced, explained, and eventually verified.

---

## Highlights

```text
Custom ISA and .zasm assembler
.zbin virtual binary format
Fetch-decode-execute CPU core
Registers, flags, memory, stack, CALL/RET
MMIO bus and devices
Interrupt controller and timer device
INT 80 mini-kernel syscall convention
BIO-OS boot/kernel/user demo
Studio visual debugger
Trace export, filtering, breakpoints, and watch expressions
```

---

## Quick Start

Zero-CPU currently targets **Windows** for the full Studio debugger experience.

Requirements:

```text
CMake
Visual Studio Build Tools or Visual Studio
C++17 compiler
```

Build:

```bat
cmake -S . -B build
cmake --build build
```

Run the full test suite:

```bat
scripts\test_all.bat
```

Expected final output:

```text
All Zero-CPU tests passed.
```

Run the BIO-OS demo:

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Run Studio:

```bat
.\build\Debug\zero_studio.exe
```

---

## Studio Debugger

Zero-CPU Studio is a Win32 visual debugger for inspecting CPU execution.

It currently includes:

```text
Visual datapath panel
Graphical datapath canvas
Pipeline-style timeline view
Execution detail probe
Recent instruction trace
Memory map viewer
Breakpoint support
Trace filtering
Trace export to JSON
Watch expressions panel
```

Recommended Studio demo flow:

```text
1. Run zero_studio.exe
2. Load Source
3. Assemble
4. Load BIN
5. Step through examples\debugger_showcase.zasm
6. Watch ALU / Memory / Stack / Control Flow details
```

Screenshot placeholders are tracked in [`screenshots/README.md`](screenshots/README.md).

---

## Repository Structure

```text
include/      public headers
src/          core implementation
tools/        zero_cli command-line tool
studio/       Win32 Studio debugger
examples/     .zasm programs and BIO-OS demo
scripts/      local test scripts
docs/         detailed project documentation
```

---

## Documentation

Start here:

```text
docs/index.md
```

Important docs:

```text
docs/project-overview.md
docs/roadmap-v0.4.md
docs/v0.4-trace-export-json.md
docs/v0.4-breakpoint-polish.md
docs/v0.4-trace-filter.md
docs/v0.4-watch-expressions.md
docs/v0.3-debugger-showcase-guide.md
docs/studio-debugger-v0.2.md
docs/release-notes-v0.3.md
```

---

## Releases

Current milestone:

```text
v0.4 Studio Debugger Usability Layer
```

Previous release:

```text
v0.3 Studio Debugger Detail Layer
```

GitHub tags/releases are used for milestone snapshots.

---

## Roadmap

Near-term direction:

```text
Portfolio polish
Trace-based verification
Golden trace regression tests
Trace diff tooling
Invariant checking
Instruction semantics documentation
```

Longer-term ideas:

```text
5-stage pipeline simulation
hazard visualization
cache simulation
branch prediction
timer-based scheduling
mini-kernel expansion
```

---

## License

MIT License. See [`LICENSE`](LICENSE).
