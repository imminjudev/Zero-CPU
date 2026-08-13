# Zero-CPU Project Overview

## 1. Project Identity

Zero-CPU is a **verifiable and observable protected virtual computer platform**
written in C++17.

It began as a small CPU/ISA simulator, but its current architecture is closer to
a compact virtual computer:

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
MMIO / Virtual or Physical Devices
  ↓
Debugger + Trace Verification
```

The design goal is not cycle-accurate emulation of a commercial CPU. The goal is
to keep the whole system small enough that execution, protection boundaries,
scheduling, device access, and debugger observations can be inspected and
verified end to end.

---

## 2. Architectural Principle

The core architecture is functionality-first:

```text
core behavior
  → automated verification
  → CLI / API
  → Zero Studio consumption
```

Studio is a frontend for stable core capabilities. It should not become the
source of execution semantics.

The canonical executable path is:

```text
.zasm
  → assembler
  → .zbin
  → loader
  → virtual memory
  → fetch / decode / execute
```

The in-memory instruction path remains useful for unit tests and compatibility,
but it is not the primary executable architecture.

---

## 3. CPU and ISA

The CPU exposes:

```text
R0..R7
PC
SP
FLAGS
PrivilegeLevel
byte-addressable memory
```

Current instruction groups include:

```text
Control
  NOP HALT

Data movement
  MOV LOAD STORE

Arithmetic
  ADD SUB MUL DIV

Comparison
  CMP TEST

Branches
  JMP JE JNE JG JL

Stack / functions
  PUSH POP CALL RET

Interrupt / system
  INT IRET EI DI

Bitwise
  AND OR XOR NOT
```

Both direct and register-indirect memory operands are supported.

Examples:

```asm
LOAD R1, [100]
STORE [100], R1

MOV R2, 100
LOAD R3, [R2]
STORE [R2], R3
```

---

## 4. Toolchain

The toolchain consists of:

```text
.zasm source
  ↓
Assembler
  ↓
AssembledProgram
  ↓
BinaryWriter
  ↓
.zbin format 0.3
```

Debug builds can also emit:

```text
.zsym
```

The symbol sidecar supports code/data symbols and source-line mappings used by
the debugger and Studio.

Current executable features include:

```text
.data / .text sections
.qword data
labels
explicit .entry
binary entry-point validation
independent code/data loading
process image construction
```

---

## 5. Memory and Protection

Current core memory size:

```text
0x0000..0x0FFF = 4 KiB virtual RAM
```

Protected layout:

```text
0x0000..0x01FF  User data / low memory
0x0200..        loaded .zbin code
0x0800..0x0F9F  User stack
0x0FA0..0x0FFF  Kernel interrupt stack
```

Device windows:

```text
0xF000..0xF00F  DebugOutputDevice
0xF100..0xF12F  TimerDevice
0xF200..0xF22F  Hardware bridge
```

User-mode memory access is restricted. User code cannot directly access
Kernel-only memory or MMIO.

Kernel mode can perform protected operations and service interrupts/syscalls.

Execution protection also validates User PC targets for fetches, jumps, calls,
returns, and interrupt restoration.

---

## 6. Privilege and Interrupt Frames

Zero-CPU has explicit:

```text
Kernel
User
```

privilege levels.

When a User interrupt enters Kernel mode, the CPU uses the separate Kernel
interrupt stack.

The protected interrupt frame stores:

```text
return address
FLAGS
saved privilege
saved SP
```

`IRET` restores the saved execution context.

Context switching is prohibited while the Kernel interrupt stack is active,
which prevents a process from being captured in a partially serviced interrupt.

A terminating protected host syscall is allowed to restore a User PC exactly at
the executable's one-past-end address as a final, non-executable process
snapshot. The process is halted/terminated immediately and that PC is never
fetched as an instruction.

---

## 7. Processes and Address Spaces

The kernel layer provides:

```text
ProcessImage
ProcessAddressSpace
ProcessContext
ProcessControlBlock
ProcessTable
ProcessDispatcher
ProcessLifecycleManager
RoundRobinScheduler
TimerPreemptiveScheduler
```

Each process has independent:

```text
register state
PC / SP
memory image
code range
lifecycle state
exit / fault information
```

Address-space activation reloads the process executable and restores its saved
memory/context while preserving shared runtime services such as the MMIO bus and
software interrupt handler.

The lifecycle layer classifies:

```text
normal completion
explicit process exit
CPU fault
deadlock
runtime completion
```

Faulted/terminated processes are isolated so another ready process can continue.

---

## 8. Scheduling

Zero-CPU supports deterministic round-robin scheduling and timer-driven
preemption.

The multi-process runtime records scheduler-visible events including:

```text
running PID
preemption count
context switches
termination handoffs
lifecycle step
```

The scheduler avoids preempting a process merely because its executable has
already ended.

---

## 9. Two Syscall Layers

Zero-CPU currently contains two intentionally different syscall layers.

### 9.1 Guest mini-kernel / BIO-OS syscalls

The older assembly guest kernel implements services `1..7`.

These are used by examples, BIO-OS, and the `syscall-table` CLI command.

They demonstrate guest-side interrupt handlers for:

```text
1 debug output
2 memory write
3 exit
4 timer read
5 timer enable
6 timer disable
7 timer configure
```

### 9.2 Protected host syscall dispatcher

The protected multiprocess runtime uses `ProtectedSyscallDispatcher`.

Current protected services:

```text
3   process exit
20  hardware write
21  hardware read
30  filesystem stat
31  filesystem read
32  filesystem write
```

These services execute through the host-side software interrupt handler while
the CPU performs a real protected User→Kernel→User interrupt-frame transition.

Filesystem services operate on a deterministic host-side `ZeroFS` instance and
copy only through validated request/path/buffer ranges in the User data window.
ZeroFS is separate from the 4 KiB guest RAM image.

See `docs/syscall-convention.md` for the exact ABI.

---

## 10. Protected Hardware Path

Hardware MMIO window:

```text
base 0xF200
size 0x30
```

Registers:

```text
offset 0   GPIO output
offset 8   GPIO input
offset 16  PWM output
offset 24  ADC input
offset 32  status
offset 40  command
```

Current bridge implementations include:

```text
MockHardwareBus
SerialHardwareBus
Windows serial transport
ESP32-oriented protocol/firmware path
```

User mode cannot directly access this MMIO window.

Instead, protected User code can call:

```text
service 20 = hardware write
service 21 = hardware read
```

The host dispatcher validates the offset and accesses the shared MMIO bus in
Kernel context.

---

## 11. Protected Syscall Observability

Protected host syscalls produce semantic observations.

An observation can contain:

```text
interrupt vector
service number
argument 0
argument 1
status
result
disposition
exit code
```

This means verification does not have to infer syscall meaning from raw register
changes.

The same semantic event is consumed by:

```text
MultiProcessRunner trace history
multi-process invariant verification
multi-process JSON trace export
TraceJsonDiff architectural comparison
golden trace regression
MultiProcessDebugSession
debug-processes CLI
Zero Studio
```

Syscalls remain observations, not debugger stop reasons. Normal debugger
`continue` behavior is preserved.

---

## 12. Trace Verification

The verification path is:

```text
execution
  ↓
instruction / scheduler / syscall observations
  ↓
structured trace
  ↓
invariant verifier
  ↓
JSON export
  ↓
architectural or strict diff
  ↓
golden regression
```

Architectural diff compares stable semantic fields while ignoring irrelevant
producer metadata.

Strict diff compares the complete JSON structure.

Protected syscall semantic fields participate in architectural comparison.

---

## 13. Debugger

Single-process debugging supports:

```text
step
continue
breakpoints
conditional breakpoints
watchpoints
register/memory inspection
disassembly
symbols
source locations
source-line stepping
snapshot JSON
```

Multi-process debugging adds:

```text
selected PID
running PID
per-PID debug controls
scheduler/context-switch observation
process lifecycle stops
selected-PID source stepping
protected syscall history
```

The debugger records a terminating syscall against the PID that issued it even
when lifecycle handling activates the next process before returning from the
step.

---

## 14. Zero Studio

Zero Studio is a Win32 frontend over the debugger backends.

It includes:

```text
source editor
assemble / load workflow
single-process debugger backend
multi-process debugger backend
source highlighting
PID selection
visual datapath
pipeline-style timeline
execution detail probe
recent trace
memory map
debug controls
snapshot / trace export
```

For protected multi-process sessions, Studio can configure:

```text
ProtectedSyscallDispatcher
ZeroFS
MockHardwareBus
HardwareMMIODevice
shared MMIOBus
```

and displays recent semantic software-interrupt observations in the State view.

The v1.9 **Load Showcase** path assembles the real showcase `.zasm` sources,
writes `.zsym` source maps, preloads `/data/showcase.txt = HELLO`, and presents
the two-stage fault-isolation/survivor flow through the same debugger backend.

Example output:

```text
Recent Software Interrupts
#8  PID 1 INT 80 svc=20 arg0=0 arg1=42 status=0 disposition=ReturnToCaller
#11 PID 1 INT 80 svc=21 arg0=0 status=0 result=42 disposition=ReturnToCaller
#15 PID 1 INT 80 svc=3 arg0=7 status=0 disposition=TerminateProcess exit=7
```

---

## 15. BIO-OS

BIO-OS remains an integration/demo layer built from assembly sources.

It demonstrates:

```text
boot
guest kernel
user program
INT 80 guest syscalls
timer configuration
timer interrupt
MMIO debug output
clean guest exit
```

BIO-OS predates the newer protected host dispatcher and remains a deliberately
separate guest-kernel demonstration. It is retained for architectural contrast;
the protected multi-process runtime is the primary platform path.

---

## 16. CLI

The primary CLI is:

```text
build\Debug\zero_cli.exe
```

Important command families include:

```text
assemble
run-binary
run-os
run-processes
debug
debug-processes
trace / diff related commands
test/demo commands
```

Protected multi-process runtime flags include:

```text
--protected-syscalls
--hardware-mock
--hardware-serial PORT
--baud N
--expect-exit PID=CODE
--expect-hardware OFFSET=VALUE
```

---

## 17. Test Strategy

The project uses focused tests for each layer plus CLI integration tests.

The current `scripts\test_all.bat` suite contains 73 stages.

Coverage includes:

```text
ALU and ISA
binary format / loader
interrupts and FLAGS
privilege
memory/execution protection
Kernel stack separation
process contexts/tables/address spaces
round-robin and timer preemption
process lifecycle/fault recovery
protected hardware syscalls
protected filesystem syscalls / ZeroFS
protected process exit
final-instruction protected exit
multi-process trace invariants
trace diff and golden regression
end-to-end showcase golden trace
debugger controls
protected debugger runtime
protected debugger CLI
Studio backend and showcase presentation
```

Current expected result:

```text
All Zero-CPU tests passed.
```

---

## 18. Current Release Phase

Completed platform milestone:

```text
v1.9 end-to-end protected showcase
```

The v1.9 path demonstrates, in one reproducible scenario:

```text
real .zasm → .zbin programs
independent process address spaces
timer-driven preemption
protected filesystem syscall
protected hardware syscall
intentional User-mode MMIO protection fault
fault isolation and survivor completion
Studio/debugger observation
trace JSON → invariant verification → golden regression
```

Current release phase:

```text
v2.0 productization / feature freeze
```

No new execution subsystem is required for v2.0.

---

## 19. Direction

The remaining v2.0 work is presentation and release engineering:

```text
documentation consistency
current architecture diagram
single-command showcase entry point
2–3 minute demo script
portfolio screenshots
design decisions and limitations
final regression pass
v2.0.0 tag and release
```

The mock hardware path is the reproducible demonstration baseline. Physical
ESP32 transport remains an optional extension and is not required to prove the
protected hardware architecture.

Potential future module boundaries remain:

```text
zero_cpu_arch
zero_cpu_toolchain
zero_cpu_kernel
zero_cpu_debug
zero_cpu_hardware
zero_cpu_host_windows
```

These are post-release cleanup directions, not a reason for a v2.0 refactor.

Cache simulation, pipelines, branch prediction, a network stack, broad
filesystem expansion, GPU/3D work, and Linux/x86 compatibility are outside the
v2.0 scope.

<!-- Patch: v1.6-docs-current-platform-r1 -->
<!-- Patch: v2.0-productization-docs-r1 -->
