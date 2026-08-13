# Zero-CPU Architecture Roadmap

Zero-CPU is a **verifiable and observable protected virtual computer platform**.

```text
.zasm
  → Assembler
  → .zbin + .zsym
  → Loader
  → Protected CPU
  → Processes / Address Spaces
  → Timer Preemption / Scheduler
  → Protected Syscalls / MMIO / ZeroFS / Hardware
  → Debugger / Trace Verification
  → Zero Studio
```

Core rule:

```text
core behavior → automated verification → CLI / API → Studio consumption
```

Studio serves the platform. It does not define execution semantics.

---

## 1. Current Platform Baseline

### CPU / Toolchain

```text
custom ISA; R0..R7, PC, SP, FLAGS
fetch / decode / execute
ALU, branches, stack, functions, interrupts
.zasm assembler; .data / .text / .qword
.zbin format 0.3 + legacy 0.2 compatibility
.zsym debug symbols
binary and process-image loaders
```

### Protection / Processes

```text
Kernel / User privilege
execution and data-range protection
User direct-MMIO blocking
separate User and Kernel stacks
protected interrupt frames
privileged-instruction checks
independent process address spaces
context capture / restore
round-robin + timer-driven preemption
process lifecycle, exit codes, fault isolation
```

### Runtime Services

```text
guest Mini-Kernel / BIO-OS ABI 1..7

protected host ABI:
  3   process exit
  20  hardware write
  21  hardware read
  30  filesystem stat
  31  filesystem read
  32  filesystem write

MMIO bus
DebugOutputDevice / TimerDevice
hardware bridge MMIO
MockHardwareBus / SerialHardwareBus
ZeroFS deterministic storage
```

### Debugging / Verification

```text
single-process and multi-process debugger
PID-aware controls and source stepping
breakpoints / conditional breakpoints / watchpoints
snapshot JSON
semantic syscall and context-switch history
multi-process trace + invariant verification
architectural / strict trace diff
golden regression
Zero Studio frontend
```

---

## 2. Completed Milestone Path

```text
v1.5  protected runtime observability
      syscall semantics → trace → debugger → Studio

v1.6  architecture / ABI consistency cleanup

v1.7  protected hardware path and reproducible mock demonstration
      optional ESP32 physical transport path retained

v1.8  ZeroFS + protected filesystem services 30 / 31 / 32

v1.9  end-to-end protected showcase
      real .zasm programs
      preemptive multi-process execution
      filesystem + hardware syscalls
      intentional protection fault
      survivor process completion
      Studio presentation
      invariant + golden trace regression
```

The v1.9 showcase is the integration boundary proving that the major subsystems
work together rather than only in isolated unit tests.

---

## 3. Current Phase: v2.0 Productization

The v2.0 feature scope is frozen.

```text
v2.0-A  README and current-document consistency
v2.0-B  current architecture diagram
v2.0-C  single-command showcase entry point
v2.0-D  2–3 minute demo script and portfolio screenshots
v2.0-E  design decisions / limitations / final release notes
v2.0-F  final regression, tag v2.0.0, release
```

These are packaging/release tasks, not new machine subsystems.

---

## 4. v2.0 Exit Criteria

A short review should make these points clear:

```text
1. Zero-CPU's identity and canonical execution path.
2. User/Kernel protection boundaries.
3. Independent process scheduling and isolation.
4. Protected syscall access to storage and hardware.
5. Faulted-process isolation while another process survives.
6. Debugger/Studio visibility into that flow.
7. Trace invariants and golden regression for the same run.
```

Release artifacts:

```text
concise README
current architecture diagram
one showcase command
2–3 minute demo sequence
Studio screenshots
design decisions and limitations
73-stage regression baseline
v2.0.0 tag/release
```

---

## 5. Canonical Showcase

```text
Process 1 (.zasm → .zbin)
  → FS_READ("/data/showcase.txt")
  → survives Process 2 fault
  → FS_WRITE
  → HELLOHELLO
  → exit 0

Process 2 (.zasm → .zbin)
  → protected HW_WRITE(GPIO=42)
  → illegal direct User MMIO write
  → protection fault
  → isolated termination
```

Both use timer quantum `1`. The same execution is consumed by the runtime,
debugger, Studio, trace JSON, invariant verifier, and golden regression.

---

## 6. Hardware Scope

```text
MockHardwareBus
  = deterministic, automated, reproducible baseline

SerialHardwareBus / Windows serial / ESP32 path
  = optional physical extension
```

Physical hardware is useful evidence but is not required for the protected
hardware architecture to be reproducible or testable.

---

## 7. Feature Freeze / Deferred Work

```text
cache simulator
5-stage pipeline simulator
hazard / branch-prediction experiments
page-table/MMU research
network stack
general-purpose filesystem expansion
browser / Web platform
GPU / 3D
Linux compatibility
x86 compatibility
ESP32-hosted full Zero-CPU interpreter
```

ZeroFS itself is implemented and part of v2.0. The deferred filesystem item is
broad general-purpose expansion.

---

## 8. Module Boundary Direction

Possible later boundaries:

```text
zero_cpu_arch
zero_cpu_toolchain
zero_cpu_kernel
zero_cpu_debug
zero_cpu_hardware
zero_cpu_host_windows
```

Executables remain `zero_cli` and `zero_studio`. These are post-v2.0 cleanup
directions, not a reason for a large pre-release refactor.

---

## 9. Design Rules

```text
1. Core semantics are authoritative.
2. Stable behavior receives automated tests before frontend presentation.
3. Canonical execution is .zasm → .zbin → loader → CPU.
4. CLI and Studio consume core behavior rather than reproduce it.
5. User mode cannot bypass protection to access Kernel memory or MMIO.
6. Scheduling/lifecycle behavior stays deterministic enough for verification.
7. Semantic events are recorded once in the core and reused by tools.
8. Historical notes may remain; current docs describe current behavior.
9. The mock path must reproduce the showcase without physical hardware.
10. v2.0 work must improve clarity, reproducibility, or release evidence rather
    than add unrelated features.
```

---

## 10. Completion Perspective

The integrated system layers are already present:

```text
protection
multi-process execution
preemption
syscalls
hardware abstraction
ZeroFS storage
fault isolation
debugger
Studio
trace verification
golden regression
```

The remaining path is:

```text
explain it clearly
run it easily
show it quickly
verify it completely
freeze it
release it
```

<!-- Patch: v2.0-productization-docs-r1 -->
