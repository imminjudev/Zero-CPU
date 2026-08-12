# Zero-CPU

![CI](https://github.com/imminjudev/Zero-CPU/actions/workflows/ci.yml/badge.svg)
![C++17](https://img.shields.io/badge/C++-17-blue)
![License: MIT](https://img.shields.io/badge/License-MIT-green)

Zero-CPU is a **verifiable and observable protected virtual computer platform**
written in C++17.

It combines a custom ISA and toolchain with protected execution, processes and
address spaces, timer-driven scheduling, MMIO and hardware bridges, source-level
debugging, semantic runtime traces, and a Win32 Studio frontend.

The project is intentionally compact: the goal is not to emulate a commercial
processor, but to make the important boundaries of a computer system visible,
testable, and explainable.

---

## Architecture

The current execution path is:

```text
.zasm
  ↓
Assembler
  ↓
.zbin + .zsym
  ↓
Binary Loader
  ↓
Protected Zero-CPU Machine
  ↓
User / Kernel privilege
  ↓
Processes + Address Spaces
  ↓
Timer Preemption + Scheduler
  ↓
INT 80 Syscalls
  ↓
MMIO / Virtual or Physical Hardware
  ↓
Debugger + Trace Verification
```

The canonical executable path is `.zasm -> .zbin -> loader -> memory ->
fetch/decode/execute`. The older in-memory instruction path remains useful for
tests and compatibility.

---

## Current Capabilities

### CPU and ISA

```text
8 general-purpose registers (R0..R7)
PC, SP, FLAGS
fetch / decode / execute
arithmetic and bitwise ALU
direct and register-indirect memory access
branches and signed comparisons
PUSH / POP / CALL / RET
INT / IRET / EI / DI
```

### Protected execution

```text
Kernel and User privilege levels
User code-range enforcement
User data-memory protection
separate User and Kernel stacks
protected interrupt frames
privileged-instruction checks
fault isolation
```

### Processes and scheduling

```text
ProcessControlBlock / ProcessTable
independent process address spaces
context capture / restore
round-robin scheduling
timer-driven preemption
process lifecycle and exit codes
fault recovery and scheduler handoff
```

### Toolchain

```text
.zasm assembler
.data / .text sections
labels and explicit entry points
.zbin format 0.3
.zsym debug-symbol sidecars
binary loader and process image loader
```

### Devices and hardware

```text
MMIO bus
DebugOutputDevice
TimerDevice
mock hardware bridge
serial hardware protocol
Windows serial transport
ESP32-oriented physical bridge path
```

### Debugging and verification

```text
single-process debugger
multi-process debugger
source-line stepping
per-PID breakpoints
conditional breakpoints
watchpoints
symbol-aware inspection
debug snapshots
semantic protected-syscall history
multi-process execution timeline
context-switch timeline
trace JSON export
invariant verification
architectural trace diff
strict trace diff
golden trace regression
```

### Zero Studio

The Win32 Studio frontend consumes the same debugger core rather than
reimplementing execution semantics.

It includes:

```text
source editor and assembler flow
single-process debugging
multi-process debugging
PID selection
source highlighting
visual datapath
pipeline-style timeline
execution detail probe
memory-map viewer
recent instruction trace
breakpoints / conditional breakpoints / watchpoints
snapshot and trace export
protected syscall observations
```

---

## Protected Runtime

The current protected host syscall dispatcher uses `INT 80`.

```text
R0 = interrupt vector (set by CPU)
R1 = syscall number
R2 = argument 0 / return value
R3 = argument 1
R4 = status
R5 = kernel scratch
R6 = reserved/test marker
R7 = exit/status value
```

Protected host services:

| Service | Meaning | Inputs | Result |
|---:|---|---|---|
| `3` | process exit | `R2 = exit code` | terminates process, `R7 = exit code`, `R4 = 0` |
| `20` | hardware write | `R2 = MMIO offset`, `R3 = value` | hardware write, status in `R4` |
| `21` | hardware read | `R2 = MMIO offset` | value in `R2`, status in `R4` |

The older assembly mini-kernel/BIO-OS syscall table (`1..7`) is still supported
as a guest-kernel demo. It is separate from the protected host dispatcher.

See [`docs/syscall-convention.md`](docs/syscall-convention.md).

---

## Memory Map

Current core conventions:

```text
0x0000..0x01FF  User data / low memory
0x0200..        .zbin code load area
0x0800..0x0F9F  User stack
0x0FA0..0x0FFF  Kernel interrupt stack

0xF000..0xF00F  DebugOutputDevice MMIO
0xF100..0xF12F  TimerDevice MMIO
0xF200..0xF22F  Hardware bridge MMIO
```

Hardware bridge register offsets:

```text
+0   GPIO output
+8   GPIO input
+16  PWM output
+24  ADC input
+32  status
+40  command
```

---

## Quick Start

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

The current suite contains **72 test stages** and finishes with:

```text
All Zero-CPU tests passed.
```

Run Studio:

```bat
.\build\Debug\zero_studio.exe
```

Run the BIO-OS guest-kernel demo:

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

---

## Protected Runtime Demo

Assemble the protected runtime fixture:

```bat
.\build\Debug\zero_cli.exe assemble tests\fixtures\protected_runtime_cli.zasm build\protected_runtime_cli.zbin
```

The protected runtime supports:

```text
--protected-syscalls
--hardware-mock
--hardware-serial PORT
--baud N
--expect-exit PID=CODE
--expect-hardware OFFSET=VALUE
```

The same protected services can be consumed by the multi-process debugger with
`debug-processes`.

The protected showcase exercises:

```text
User INT 80
  → Kernel host dispatcher
  → hardware write (service 20)
  → hardware read (service 21)
  → process exit (service 3)
  → scheduler handoff
  → semantic syscall observation
```

---

## Verification Model

Zero-CPU treats observability as part of correctness.

A protected syscall is not only executed; its semantic result can also be
recorded and checked:

```text
execution
  ↓
software-interrupt observation
  ↓
multi-process trace
  ↓
invariant verifier
  ↓
JSON export
  ↓
architectural diff / strict diff
  ↓
golden regression
```

The debugger consumes the same semantic observations, and Zero Studio displays
them directly in the multi-process state view.

---

## Repository Structure

```text
include/      public headers
src/          core implementation
tools/        zero_cli
studio/       Win32 Zero Studio
examples/     assembly programs and BIO-OS demo
tests/        automated regression tests and fixtures
scripts/      local test scripts
docs/         design and architecture documentation
```

---

## Documentation

Start with:

```text
docs/index.md
docs/project-overview.md
docs/syscall-convention.md
```

The repository also keeps older milestone-specific debugger and hardware notes.
Those documents are useful historical implementation records; the files above
describe the current platform.

---

## Current Milestone

The current platform milestone is **v1.5: protected runtime observability**.

The v1.5 path is complete across:

```text
protected syscall semantics
→ trace / invariant verification
→ trace diff / golden regression
→ multi-process debugger core
→ debugger CLI
→ Zero Studio
```

A full 72-stage regression run passes on the current main branch.

---

## Direction

Near-term work focuses on completing the virtual-computer platform rather than
adding speculative CPU microarchitecture features.

Priority areas:

```text
documentation and architecture cleanup
stronger physical ESP32 / hardware demonstrations
BIO-OS integration with the protected runtime
library/module boundary cleanup
end-to-end platform demos
```

Cache simulation, pipelines, and branch prediction remain intentionally deferred
until the protected computer platform is complete.

---

## License

MIT License. See [`LICENSE`](LICENSE).

<!-- Patch: v1.6-docs-current-platform-r1 -->
