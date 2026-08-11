# Zero-CPU Architecture Roadmap

Zero-CPU is a **verifiable and observable protected virtual computer platform**.

This roadmap describes the current path from the completed protected runtime
toward the first finished platform milestone.

The architecture direction is:

```text
.zasm
  → Assembler
  → .zbin + .zsym
  → Loader
  → Protected CPU
  → Processes / Address Spaces
  → Timer Preemption / Scheduler
  → Syscalls / MMIO / Hardware
  → Debugger / Trace Verification
  → Zero Studio
```

The core rule remains:

```text
core behavior
  → automated verification
  → CLI / API
  → Studio consumption
```

Studio serves the platform. It does not define execution semantics.

---

## 1. Current Platform Baseline

The following layers are already implemented and verified.

### CPU / ISA

```text
registers, PC, SP, FLAGS
fetch / decode / execute
ALU and signed branch behavior
direct and register-indirect memory
stack operations
CALL / RET
INT / IRET / EI / DI
```

### Executable Toolchain

```text
.zasm assembler
.data / .text
labels and explicit entry points
.zbin format 0.3
legacy .zbin 0.2 compatibility
.zsym debug symbols
binary loader
process image loader
```

### Protection

```text
Kernel / User privilege
User execution-range protection
User data-range protection
User MMIO blocking
separate User and Kernel stacks
protected interrupt frames
privileged-instruction checks
fault isolation
```

### Processes and Scheduling

```text
process table / PCB
independent address spaces
context capture / restore
round-robin scheduling
timer-driven preemption
process lifecycle
fault recovery
exit-code handling
```

### Runtime Services

```text
guest Mini-Kernel / BIO-OS syscall ABI 1..7
protected host syscall ABI 3 / 20 / 21
MMIO bus
DebugOutputDevice
TimerDevice
hardware bridge MMIO
mock and serial hardware backends
```

### Debugging and Verification

```text
single-process debugger
multi-process debugger
PID-aware controls
source stepping
breakpoints / conditional breakpoints
watchpoints
snapshot JSON
semantic protected-syscall history
multi-process trace
invariant verification
trace diff
golden regression
Zero Studio debugger frontend
```

---

## 2. Current Milestone: v1.6 — Consistency and Platform Cleanup

v1.6 does not add another major execution subsystem.

Its purpose is to make the already-implemented platform internally consistent
and understandable.

Completed:

```text
v1.6-A
  README and current architecture documentation aligned with v1.5

v1.6-B
  zero_cli syscall-table split into:
    Guest Mini-Kernel / BIO-OS ABI
    Protected Host Runtime ABI

  protected service/status numbers sourced from
  ProtectedSyscallDispatcher constants

  syscall-table regression assertions added
```

Current work:

```text
v1.6-C
  current architecture roadmap cleanup
  execution-semantics documentation cleanup
  hardware roadmap cleanup
```

v1.6 is complete when non-historical documentation no longer presents already
implemented protection, scheduling, hardware, or verification work as future
features.

---

## 3. v1.7 — Hardware Demonstration Completion

The hardware abstraction and serial bridge already exist.

v1.7 focuses on making the physical path a strong, reproducible demonstration.

Target path:

```text
User Process
  → INT 80
  → protected Kernel dispatcher
  → hardware MMIO
  → SerialHardwareBus
  → Windows serial transport
  → ESP32
  → physical GPIO / sensor
```

Primary goals:

```text
reproducible ESP32 setup
real GPIO output demonstration
real GPIO or ADC input demonstration
clear timeout/error behavior
mock-vs-physical comparison
trace/debug visibility for physical transactions
```

Studio physical-device controls may be added when they expose this stable core
path. They are not a prerequisite for the core hardware path itself.

---

## 4. v1.8 — BIO-OS and Protected Runtime Integration

BIO-OS currently remains a useful guest-kernel integration demo, but it predates
the protected multi-process runtime.

v1.8 should connect the concepts without deleting the historical guest ABI.

Goals:

```text
preserve BIO-OS guest syscall demo
clarify guest Kernel vs protected host Kernel responsibilities
run BIO-OS concepts through protected process/address-space boundaries
demonstrate User→Kernel service transitions
reuse current scheduler/lifecycle infrastructure where appropriate
avoid duplicate execution semantics
```

BIO-OS should become a consumer/demo of the platform rather than the center of
the architecture.

---

## 5. v1.9 — End-to-End Platform Demo and Polish

v1.9 assembles existing pieces into one strong demonstration.

Target demonstration:

```text
Process A (.zbin)
Process B (.zbin)
  ↓
independent address spaces
  ↓
timer-driven round-robin preemption
  ↓
User / Kernel protection
  ↓
protected syscall
  ↓
hardware access
  ↓
process exit or isolated fault
  ↓
scheduler handoff
  ↓
debugger / Studio observation
  ↓
trace export
  ↓
invariant + golden verification
```

Polish goals:

```text
stable demo fixtures
clear CLI commands
source symbols included
repeatable expected results
portfolio screenshots / short demo guide
remove misleading legacy wording from current docs
```

---

## 6. v2.0 — First Complete Zero-CPU Platform

v2.0 is the first planned completion boundary.

A v2.0 Zero-CPU release should demonstrate:

```text
custom ISA + assembler
versioned executable format
protected virtual CPU
processes + address spaces
preemptive scheduler
fault isolation
guest and protected syscall models
virtual and physical hardware paths
source-aware debugger
multi-process debugger
Zero Studio frontend
trace/invariant/golden verification
end-to-end reproducible demo
```

At v2.0 the project is complete enough to stand as a systems-software portfolio
project without requiring CPU microarchitecture simulation.

---

## 7. Deferred Work

The following are intentionally **not required for v2.0**:

```text
cache simulation
5-stage pipeline simulation
hazard handling
branch prediction
page-table/MMU experiments
network stack
filesystem
ESP32-hosted full Zero-CPU interpreter
```

They can become later v2.x research or extension work.

Adding them before the protected platform is integrated would broaden the
project without strengthening its main architecture.

---

## 8. Module Boundary Direction

Possible future library boundaries:

```text
zero_cpu_arch
zero_cpu_toolchain
zero_cpu_kernel
zero_cpu_debug
zero_cpu_hardware
zero_cpu_host_windows
```

Executables:

```text
zero_cli
zero_studio
```

These boundaries are a direction for gradual cleanup, not a reason for an
immediate large refactor.

---

## 9. Design Rules

Zero-CPU should continue to follow these rules:

```text
1. Core semantics are authoritative.
2. Stable behavior receives automated tests before frontend polish.
3. The canonical executable path is .zasm → .zbin → loader → CPU.
4. CLI and Studio consume core behavior rather than reproduce it.
5. User mode must not bypass protection to access Kernel memory or MMIO.
6. Process lifecycle and scheduler behavior must remain deterministic enough
   for trace verification.
7. Semantic events should be recorded once in the core and reused by tools.
8. Historical milestone documents may remain, but current docs must identify
   implemented behavior accurately.
9. Avoid speculative microarchitecture work until the v2.0 platform is done.
```

---

## 10. Completion Perspective

The difficult core subsystems are already present:

```text
protection
processes
preemption
syscalls
hardware abstraction
debugger
verification
Studio consumption
```

The remaining path to v2.0 is primarily:

```text
consistency
physical demonstration
integration
end-to-end presentation
```

That is the current architectural roadmap.

<!-- Patch: v1.6-current-roadmap-semantics-r1 -->
