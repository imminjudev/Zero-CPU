# Zero-CPU Syscall Convention

This document defines the syscall conventions that exist in the current
Zero-CPU codebase.

There are **two syscall layers**:

```text
A. guest mini-kernel / BIO-OS syscalls
B. protected host-dispatcher syscalls
```

They both use `INT 80`, but they are different execution models and must not be
treated as one syscall table.

---

## 1. Common INT 80 Register Convention

The current shared convention is:

```text
R0 = interrupt vector (written by CPU)
R1 = syscall / service number
R2 = argument 0 / return value
R3 = argument 1
R4 = argument 2 in legacy guest ABI / status in protected host ABI
R5 = kernel scratch
R6 = reserved / test marker
R7 = exit or status value
```

A caller normally prepares:

```asm
MOV R1, <service>
MOV R2, <arg0>
MOV R3, <arg1>
INT 80
```

The CPU writes:

```text
R0 = 80
```

during interrupt handling.

---

## 2. Protected Interrupt Transition

The current protected CPU does more than push a return address.

For a User→Kernel interrupt transition, the protected interrupt frame stores:

```text
return address
FLAGS
saved privilege
saved SP
```

The interrupt uses the separate Kernel interrupt stack.

Current protected stack layout:

```text
User stack    = 0x0800..0x0F9F
Kernel stack  = 0x0FA0..0x0FFF
```

Normal interrupt return restores:

```text
FLAGS
privilege
PC
SP
```

and returns to User mode.

A process context cannot be captured or restored while the Kernel interrupt
stack is active.

---

## 3. Layer A: Guest Mini-Kernel / BIO-OS Syscalls

This is the older assembly-implemented syscall layer.

The handler itself is guest `.zasm` code and typically dispatches on `R1`, uses
MMIO or RAM directly in Kernel mode, then returns with `IRET`.

This layer is used by:

```text
examples/mini_kernel_*.zasm
examples/bio_os/
zero_cli syscall-table
run-os
```

Current guest service table:

| Service | Name | Inputs | Output / effect |
|---:|---|---|---|
| `1` | debug output | `R2 = value` | writes to `DebugOutputDevice` |
| `2` | memory write | `R2 = address`, `R3 = value` | `Memory[R2] = R3` |
| `3` | exit | `R2 = exit code` | guest exit path, `R7 = exit code` |
| `4` | timer read | none | timer tick count in `R2` |
| `5` | timer enable | `R2 = interval`, `R3 = vector` | configures/enables timer |
| `6` | timer disable | none | disables timer |
| `7` | timer configure | `R2 = interval`, `R3 = vector`, `R4 = payload` | configures timer |

These services are useful as a guest-kernel/BIO-OS demonstration, but they are
not the protected host ABI described below.

---

## 4. Layer B: Protected Host Dispatcher

The protected multiprocess runtime uses:

```text
zero_cpu::kernel::ProtectedSyscallDispatcher
```

It handles only:

```text
vector 80
```

Current protected services:

| Service | Name | Inputs | Outputs |
|---:|---|---|---|
| `3` | process exit | `R2 = exit code` | `R7 = exit code`, `R4 = 0`, terminate process |
| `20` | hardware write | `R2 = hardware offset`, `R3 = value` | status in `R4` |
| `21` | hardware read | `R2 = hardware offset` | value in `R2`, status in `R4` |

Unknown services return an unsupported status.

---

## 5. Protected Status Codes

Protected host services use `R4` as a status register.

```text
 0  OK
-1  unsupported syscall/service
-2  invalid hardware offset
-3  hardware unavailable
-4  hardware error
```

For hardware services, callers should check:

```text
R4 == 0
```

before treating the result as successful.

---

## 6. Protected Service 20: Hardware Write

Convention:

```text
R1 = 20
R2 = hardware-register offset
R3 = value
INT 80
```

Example:

```asm
MOV R1, 20
MOV R2, 0
MOV R3, 42
INT 80
```

On success:

```text
Hardware[0] = 42
R4 = 0
```

The dispatcher validates the offset before adding it to the hardware MMIO base.

Hardware MMIO base:

```text
0xF200
```

Valid register offsets are 8-byte aligned and inside the 0x30-byte hardware
window.

Current offsets:

```text
0   GPIO output
8   GPIO input
16  PWM output
24  ADC input
32  status
40  command
```

---

## 7. Protected Service 21: Hardware Read

Convention:

```text
R1 = 21
R2 = hardware-register offset
INT 80
```

Example:

```asm
MOV R1, 21
MOV R2, 0
INT 80
```

On success:

```text
R2 = hardware value
R4 = 0
```

Example semantic result:

```text
service=21 arg0=0 status=0 result=42
```

---

## 8. Protected Service 3: Process Exit

Convention:

```text
R1 = 3
R2 = exit code
INT 80
```

Example:

```asm
MOV R1, 3
MOV R2, 7
INT 80
```

Protected behavior:

```text
R7 = 7
R4 = 0
SoftwareInterruptDisposition = TerminateProcess
process exit request = 7
current process halts
lifecycle records NormalExit
scheduler activates another ready process if available
```

This is different from simply halting the whole virtual machine.

Code after the exit syscall must not execute.

The exit syscall is also valid as the **final instruction** in an executable.
In that case the restored User PC may equal the exact one-past-end code address
as a final process snapshot. It is not fetched as an instruction because the
process is terminated immediately.

---

## 9. Protected Hardware Access Rule

User mode cannot directly access MMIO.

This includes:

```text
0xF000 DebugOutputDevice
0xF100 TimerDevice
0xF200 Hardware bridge
```

Direct User MMIO access is rejected by memory protection.

Protected hardware access therefore follows:

```text
User program
  ↓
INT 80
  ↓
CPU saves protected interrupt frame
  ↓
User → Kernel
  ↓
ProtectedSyscallDispatcher
  ↓
shared MMIOBus
  ↓
HardwareMMIODevice
  ↓
MockHardwareBus / SerialHardwareBus / physical transport
  ↓
restore User frame or terminate process
```

---

## 10. Semantic Syscall Observation

The protected host path records explicit syscall semantics.

`SoftwareInterruptResult` can describe:

```text
service number
argument 0
argument 1
status
result value
disposition
exit code
```

and `SoftwareInterruptObservation` also stores:

```text
interrupt vector
```

Example debugger output:

```text
step=4  pid=1 vector=80 service=20 arg0=0 arg1=42 status=0 disposition=ReturnToCaller
step=7  pid=1 vector=80 service=21 arg0=0 status=0 result=42 disposition=ReturnToCaller
step=11 pid=1 vector=80 service=3  arg0=7 status=0 disposition=TerminateProcess exit=7
```

These observations are semantic events. They do **not** create a new debugger
stop reason.

---

## 11. Where Protected Syscall Events Are Used

The same core observation is consumed by:

```text
MultiProcessRunner
  → software interrupt trace records

MultiProcessTraceInvariantVerifier
  → ordering / PID / termination checks

MultiProcessTraceJsonWriter
  → optional software_interrupts JSON section

TraceJsonDiff
  → architectural protected-syscall comparison

golden regression
  → deterministic protected-syscall fixture

MultiProcessDebugSession
  → debugger syscall history

MultiProcessDebugConsole
  → syscalls [pid] / sc [pid]

Zero Studio
  → Recent Software Interrupts
```

No consumer should reconstruct syscall semantics by parsing console text or
guessing from raw register changes.

---

## 12. Debugger Commands

In `debug-processes`:

```text
syscalls
syscalls <pid>
sc
sc <pid>
```

Examples:

```text
syscalls
syscalls 1
syscalls 2
```

No PID shows all recorded protected software interrupts.

A PID filters the core semantic history for that process.

---

## 13. Protected Runtime CLI Flags

`run-processes` and `debug-processes` support protected-runtime configuration.

Current important flags:

```text
--protected-syscalls
--hardware-mock
--hardware-serial PORT
--baud N
--expect-exit PID=CODE
--expect-hardware OFFSET=VALUE
```

These flags configure services around the same core runtime rather than creating
a second syscall implementation in the CLI.

---

## 14. Tests

Current focused protected-syscall coverage includes:

```text
zero_protected_syscall_hardware_test
zero_protected_process_exit_test
zero_protected_syscall_trace_test
zero_protected_debug_runtime_test
zero_multi_process_trace_test
zero_multi_process_trace_diff_test
zero_studio_debug_backend_test
```

CLI integration also verifies:

```text
run-processes protected runtime
debug-processes protected runtime
exit-code expectations
hardware-register expectations
syscall history output
PID filtering
```

The full suite runs through:

```bat
scripts\test_all.bat
```

Current suite result:

```text
70 stages
All Zero-CPU tests passed.
```

---

## 15. Design Rules

For the protected host ABI:

```text
1. core semantics live in the dispatcher/CPU, not UI code
2. User mode never bypasses protection to reach MMIO
3. semantic observations are recorded once in the core
4. CLI and Studio consume core observations
5. syscalls remain observable without changing continue semantics
6. process-exit semantics belong to lifecycle/scheduler integration
7. new protected services require focused and regression tests
```

For the guest mini-kernel ABI:

```text
1. guest handler code may continue to demonstrate assembly kernel services
2. guest syscall numbers do not automatically become protected host services
3. documentation must name which ABI a service belongs to
```

---

## 16. Current Status

```text
INT / IRET                             implemented
FLAGS save/restore                     implemented
Kernel/User privilege                  implemented
separate Kernel interrupt stack        implemented
memory/execution protection            implemented
process contexts/address spaces        implemented
protected process exit                 implemented
protected hardware write (20)          implemented
protected hardware read (21)           implemented
semantic syscall observation           implemented
trace/invariant integration            implemented
debugger core integration              implemented
debug-processes CLI integration        implemented
Zero Studio integration                implemented
```

The protected syscall path is therefore no longer only a mini-kernel demo. It is
part of the current protected multi-process virtual-computer runtime.

<!-- Patch: v1.6-docs-current-platform-r1 -->
