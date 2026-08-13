# Zero-CPU

![CI](https://github.com/imminjudev/Zero-CPU/actions/workflows/ci.yml/badge.svg)
![C++17](https://img.shields.io/badge/C++-17-blue)
![License: MIT](https://img.shields.io/badge/License-MIT-green)

Zero-CPU is a **verifiable and observable protected virtual computer platform**
written in C++17.

It combines a custom ISA and executable toolchain with User/Kernel protection,
independent processes and address spaces, timer-driven preemption, protected
syscalls, MMIO devices, deterministic storage, source-aware debugging, and
trace-based verification.

The project is intentionally compact. Its goal is not to emulate a commercial
processor; it is to make the important boundaries of a computer system visible,
testable, and explainable end to end.

> A small virtual computer where protection, multi-process scheduling, syscalls,
> devices, storage, debugging, and verification can all be observed in one
> execution flow.

---

## 30-Second Overview

```text
.zasm
  → Assembler
  → .zbin + .zsym
  → Binary / Process Loader
  → Protected Zero-CPU Machine
  → User / Kernel privilege
  → Independent Processes + Address Spaces
  → Timer Preemption + Round-Robin Scheduler
  → INT 80 Protected Syscalls
  → MMIO / Hardware / ZeroFS
  → Multi-Process Debugger + Zero Studio
  → Trace JSON
  → Invariant Verification + Golden Regression
```

The canonical executable path is:

```text
.zasm → .zbin → loader → memory → fetch/decode/execute
```

Studio and CLI tooling consume the same core execution/debugging behavior rather
than implementing a second machine model.

See the rendered current architecture map in
[`docs/architecture.md`](docs/architecture.md).

---

## End-to-End Showcase

The v1.9 showcase is the shortest way to understand the complete platform.

Two real assembly programs are assembled and loaded as independent processes:

```text
Process 1
  → protected FS_READ
  → survives another process's fault
  → protected FS_WRITE
  → /data/showcase.txt: HELLO → HELLOHELLO
  → normal exit 0

Process 2
  → protected HW_WRITE
  → Mock GPIO[0] = 42
  → attempts direct User-mode MMIO write
  → protection fault
  → terminated in isolation
```

With timer quantum `1`, the processes alternate under preemptive scheduling.
When Process 2 faults, Process 1 remains alive and completes normally.

Zero Studio exposes the same flow with the **Load Showcase** button:

```text
Run #1
  → Process 2 faults on illegal direct MMIO
  → Process 1 is still running
  → GPIO = 42
  → ZeroFS file is still HELLO

Run #2
  → Process 1 performs FS_WRITE
  → ZeroFS file becomes HELLOHELLO
  → Process 1 exits with code 0
  → runtime state becomes Completed
```

The deterministic execution is also exported to JSON, checked by multi-process
invariants, and locked against:

```text
tests/golden/end_to_end_showcase.json
```

See [`docs/end-to-end-showcase.md`](docs/end-to-end-showcase.md).

---

## Current Capabilities

### CPU / ISA

```text
8 general-purpose registers (R0..R7)
PC, SP, FLAGS
fetch / decode / execute
arithmetic and bitwise ALU
direct and register-indirect memory access
signed comparisons and branches
PUSH / POP / CALL / RET
INT / IRET / EI / DI
```

### Protection / Processes / Scheduling

```text
Kernel and User privilege levels
User execution-range protection
User data-memory protection
User direct-MMIO blocking
separate User and Kernel stacks
protected interrupt frames
privileged-instruction checks

ProcessControlBlock / ProcessTable
independent process address spaces
context capture / restore
round-robin scheduling
timer-driven preemption
process lifecycle and exit codes
fault isolation and scheduler handoff
```

### Toolchain

```text
.zasm assembler
.data / .text sections
.qword data
labels and explicit entry points
.zbin format 0.3
legacy .zbin 0.2 compatibility
.zsym debug-symbol sidecars
binary loader and process image loader
```

### Runtime Services

The protected host runtime uses `INT 80`.

| Service | Meaning |
|---:|---|
| `3` | process exit |
| `20` | hardware write |
| `21` | hardware read |
| `30` | filesystem stat |
| `31` | filesystem read |
| `32` | filesystem write |

Protected filesystem operations use bounded request blocks and guest buffers in
the User data window. ZeroFS remains separate from the 4 KiB guest RAM image.

The older assembly mini-kernel/BIO-OS services `1..7` remain as a distinct guest
kernel demonstration. They are not the protected host ABI.

See [`docs/syscall-convention.md`](docs/syscall-convention.md).

### Devices / Storage / Hardware

```text
MMIO bus
DebugOutputDevice
TimerDevice
HardwareMMIODevice
MockHardwareBus
SerialHardwareBus
Windows serial transport
ESP32-oriented physical bridge path
ZeroFS deterministic virtual filesystem
```

The mock hardware path is the reproducible default for tests and the final
showcase. The physical ESP32 path is an optional hardware extension.

### Debugging / Verification

```text
single-process debugger
multi-process debugger
source-line stepping
PID-aware breakpoints
conditional breakpoints
watchpoints
symbol-aware inspection
debug snapshots
semantic protected-syscall history
context-switch history
trace JSON export
multi-process invariant verification
architectural trace diff
strict trace diff
golden trace regression
```

### Zero Studio

Zero Studio is a Win32 frontend over the debugger backends.

It includes:

```text
source editor and assembler flow
single-process and multi-process debugging
PID selection and source highlighting
visual datapath
pipeline-style timeline
execution detail probe
memory-map viewer
recent instruction trace
breakpoints / conditional breakpoints / watchpoints
snapshot and trace export
protected syscall observations
one-click end-to-end showcase loading
```

---

## Memory Map

```text
0x0000..0x01FF  User data / low memory
0x0200..        loaded .zbin code
0x0800..0x0F9F  User stack
0x0FA0..0x0FFF  Kernel interrupt stack

0xF000..0xF00F  DebugOutputDevice MMIO
0xF100..0xF12F  TimerDevice MMIO
0xF200..0xF22F  Hardware bridge MMIO
```

Hardware bridge offsets:

```text
+0   GPIO output
+8   GPIO input
+16  PWM output
+24  ADC input
+32  status
+40  command
```

---

## Build and Run

Zero-CPU currently targets Windows for the full Studio and physical serial
experience.

Requirements:

```text
CMake
Visual Studio or Visual Studio Build Tools
C++17 compiler
```

Configure and build:

```bat
cmake -S . -B build
cmake --build build --config Debug
```

Run the complete regression suite:

```bat
scripts\test_all.bat
```

Current regression baseline:

```text
73 stages
All Zero-CPU tests passed.
```

Run Studio:

```bat
.\build\Debug\zero_studio.exe
```

Then click **Load Showcase** for the end-to-end protected platform demo.

Run the BIO-OS guest-kernel demo:

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

---

## Verification Model

Zero-CPU treats observability as part of correctness.

```text
execution
  ↓
instruction / scheduler / syscall observations
  ↓
structured multi-process trace
  ↓
invariant verifier
  ↓
JSON export
  ↓
architectural diff / strict diff
  ↓
golden regression
```

This makes claims such as fault isolation, syscall success, preemption,
termination handoff, and deterministic execution directly testable.

---

## Repository Structure

```text
include/      public headers
src/          core implementation
tools/        zero_cli
studio/       Win32 Zero Studio
examples/     assembly programs and BIO-OS/showcase sources
tests/        automated regression tests, fixtures, and golden traces
scripts/      local build/test helpers
docs/         current design docs and historical milestone notes
```

---

## Documentation

Start with:

```text
docs/index.md
docs/project-overview.md
docs/architecture.md
docs/architecture-roadmap.md
docs/syscall-convention.md
docs/end-to-end-showcase.md
docs/zero-fs.md
```

Older milestone-specific documents are retained as implementation history. When a
historical note conflicts with the current overview or source, the current
source and current-state documentation take precedence.

---

## Current Phase: v2.0 Productization

The v1.9 end-to-end platform showcase is complete.

The **v2.0 feature scope is frozen**. Productization completed so far:

```text
documentation consistency
current architecture diagram
```

Remaining release work:

```text
single-command showcase entry point
2–3 minute demo script
portfolio screenshots
design decisions and limitations
final regression pass
v2.0.0 tag and release
```

Not planned for v2.0:

```text
cache simulator
5-stage pipeline
branch predictor
network stack
general-purpose filesystem expansion
GPU / 3D
Linux or x86 compatibility
```

The release boundary is intentionally about presenting and verifying the
existing protected virtual-computer platform well.

---

## License

MIT License. See [`LICENSE`](LICENSE).

<!-- Patch: v2.0-productization-docs-r1 -->

<!-- Patch: v2.0-current-architecture-diagram-r1 -->
