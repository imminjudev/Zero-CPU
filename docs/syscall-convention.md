# Zero-CPU Syscall Convention

This document defines the current software interrupt and syscall convention for Zero-CPU.

The goal is to make `INT 80` the entry point for mini-kernel style services.

---

## 1. Overview

Zero-CPU supports software interrupts through the `INT` instruction.

The current syscall convention uses:

```asm
INT 80
```

as the syscall entry point.

Before calling `INT 80`, the user program places syscall information in registers.

```text
R1 = syscall number
R2 = first syscall argument
```

Example:

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

This means:

```text
syscall number = 1
argument       = 72
```

The interrupt handler registered for vector `80` acts as the mini-kernel syscall handler.

---

## 2. Register Convention

Current syscall register convention:

```text
R0 = interrupt vector, written by CPU during INT handling
R1 = syscall number
R2 = syscall argument 1
R3 = reserved for future argument 2
R4 = reserved for future argument 3
R5 = scratch / temporary
R6 = scratch / handler marker in tests
R7 = error / status register in some examples
```

Current rule:

```text
The user program sets R1 and R2.
The CPU sets R0 to the interrupt vector.
The syscall handler reads R1 and R2.
```

Example after `INT 80` enters the handler:

```text
R0 = 80
R1 = syscall number
R2 = syscall argument
```

---

## 3. Control Flow

Software interrupt flow:

```text
User program
    ↓
INT 80
    ↓
CPU saves return address
    ↓
CPU sets R0 = 80
    ↓
CPU jumps to syscall handler
    ↓
handler executes
    ↓
IRET
    ↓
user program resumes
```

Important rule:

```text
CALL returns with RET.
INT returns with IRET.
```

`RET` and `IRET` must remain separate.

---

## 4. Current Interrupt Frame

Current software interrupt frame is minimal.

```text
INT
    push return address

IRET
    pop return address
```

The current implementation does not yet save and restore FLAGS in the interrupt frame.

Future version:

```text
INT / hardware interrupt
    push return address
    push FLAGS

IRET
    pop FLAGS
    pop return address
```

This should be implemented after the `Flags` class exposes a safe raw-bit save/restore API.

---

## 5. Current Syscalls

### Syscall 1: Debug Output

Current demo syscall:

```text
syscall number: 1
argument: R2
behavior: write R2 to DebugOutputDevice
```

Example:

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

Expected behavior:

```text
DebugOutputDevice receives 72
```

The current mini-kernel syscall test writes two values:

```asm
MOV R1, 1
MOV R2, 72
INT 80

MOV R1, 1
MOV R2, 73
INT 80
```

Expected output:

```text
DebugOutputDevice writes:
72
73
```

---

## 6. Example Handler

A simple syscall handler can look like this:

```asm
syscall_handler:
    STORE [440], R0
    STORE [448], R1
    STORE [456], R2

    CMP R1, 1
    JE syscall_debug_output

    IRET

syscall_debug_output:
    STORE [61440], R2
    STORE [464], R1
    IRET
```

Address `61440` is `0xF000`, the current DebugOutputDevice MMIO base.

```text
0xF000 = DebugOutputDevice data register
```

---

## 7. Example User Program

```asm
JMP main

syscall_handler:
    STORE [440], R0
    STORE [448], R1
    STORE [456], R2

    CMP R1, 1
    JE syscall_debug_output

    IRET

syscall_debug_output:
    STORE [61440], R2
    STORE [464], R1
    IRET

main:
    MOV R1, 1
    MOV R2, 72
    INT 80

    MOV R1, 1
    MOV R2, 73
    INT 80

    MOV R3, 321
    STORE [472], R3
    HALT
```

This program demonstrates:

```text
- user program calls INT 80
- syscall handler reads R1 and R2
- syscall 1 writes to MMIO
- IRET resumes user program
- main continues after syscall
```

---

## 8. Current Test

The current CLI test is:

```bat
.\build\Debug\zero_cli.exe mini-kernel-syscall-test
```

Expected checks:

```text
Memory[440] = 80
Memory[448] = 1
Memory[456] = 73
Memory[464] = 1
Memory[472] = 321

DebugOutputDevice write[0] = 72
DebugOutputDevice write[1] = 73
```

The full test suite runs it through:

```bat
scripts\test_all.bat
```

---

## 9. Planned Syscalls

Near-term syscall plan:

```text
syscall 1 = debug output
syscall 2 = memory write
syscall 3 = exit / halt request
```

Possible later syscalls:

```text
syscall 4 = read timer tick count
syscall 5 = enable timer
syscall 6 = disable timer
syscall 7 = get interrupt count
```

---

## 10. Syscall 2 Proposal: Memory Write

Possible convention:

```text
R1 = 2
R2 = address
R3 = value
INT 80
```

Behavior:

```text
Memory[R2] = R3
```

Example:

```asm
MOV R1, 2
MOV R2, 500
MOV R3, 999
INT 80
```

Expected result:

```text
Memory[500] = 999
```

---

## 11. Syscall 3 Proposal: Exit

Possible convention:

```text
R1 = 3
R2 = exit code
INT 80
```

Behavior:

```text
R7 = exit code
HALT
```

Example:

```asm
MOV R1, 3
MOV R2, 0
INT 80
```

Expected result:

```text
R7 = 0
CPU halted = true
```

This syscall may require a new way for kernel code to request halt cleanly.

---

## 12. Design Rules

Every new syscall should have:

```text
1. documented register convention
2. .zasm example
3. CLI test command
4. scripts/test_all.bat entry
```

Every syscall handler should return with:

```asm
IRET
```

unless the syscall intentionally halts the machine.

---

## 13. Future Improvements

Planned improvements:

```text
- save/restore FLAGS in interrupt frame
- define caller-saved and callee-saved registers
- separate user program and kernel handler files
- add boot.zasm
- add kernel.zasm
- add syscall table document
- introduce kernel/user mode later
```

---

## 14. Current Status

Current syscall support:

```text
INT instruction        implemented
IRET instruction       implemented
EI / DI                implemented
INT 80 convention      implemented in examples
syscall 1 debug output implemented as test/demo
syscall 2              planned
syscall 3              planned
```

This is the base for the future BIO-OS / mini-kernel direction.
