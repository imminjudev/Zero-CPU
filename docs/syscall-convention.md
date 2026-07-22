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
R2 = syscall argument 0 / return value
R3 = syscall argument 1
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
argument 0     = 72
```

The interrupt handler registered for vector `80` acts as the mini-kernel syscall handler.

---

## 2. Register Convention

Current syscall register convention:

```text
R0 = interrupt vector, written by CPU during INT handling
R1 = syscall number
R2 = syscall argument 0 / return value
R3 = syscall argument 1
R4 = reserved / caller scratch
R5 = reserved / kernel scratch
R6 = reserved / handler marker in tests
R7 = exit code / status register in syscall 3
```

Current rule:

```text
The user program sets R1, R2, and optionally R3.
The CPU sets R0 to the interrupt vector.
The syscall handler reads R1, R2, and R3.
The syscall handler may write return values back into registers.
```

Example after `INT 80` enters the handler:

```text
R0 = 80
R1 = syscall number
R2 = argument 0
R3 = argument 1
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
IRET or HALT
    ↓
user program resumes, unless syscall intentionally exits
```

Important rule:

```text
CALL returns with RET.
INT returns with IRET.
Exit-style syscalls may HALT instead of IRET.
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

## 5. Implemented Syscall Table

Current implemented mini-kernel syscalls:

| Syscall | Name | Input | Output / Effect | Status |
|---:|---|---|---|---|
| `1` | debug output | `R2 = value` | writes `R2` to `DebugOutputDevice` | implemented |
| `2` | memory write | `R2 = address`, `R3 = value` | writes `Memory[R2] = R3` using `STORE [R2], R3` | implemented |
| `3` | exit | `R2 = exit code` | writes `R7 = R2`, then `HALT` | implemented |
| `4` | timer read | none | reads timer tick count into `R2` | implemented |

---

## 6. Syscall 1: Debug Output

### Convention

```text
R1 = 1
R2 = value
INT 80
```

### Behavior

```text
DebugOutputDevice receives R2
```

### Example

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

Expected behavior:

```text
DebugOutputDevice write[0] = 72
```

Current MMIO target:

```text
DebugOutputDevice base = 0xF000 = 61440
```

Handler pattern:

```asm
syscall_debug_output:
    STORE [61440], R2
    IRET
```

---

## 7. Syscall 2: Memory Write

### Convention

```text
R1 = 2
R2 = target memory address
R3 = value
INT 80
```

### Behavior

```text
Memory[R2] = R3
```

This syscall now depends on register-indirect memory addressing.

The handler can write to the address stored in `R2`:

```asm
STORE [R2], R3
```

### Example

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

### Handler Pattern

```asm
syscall_memory_write:
    STORE [R2], R3
    IRET
```

This is no longer a hardcoded demo like:

```asm
STORE [500], R3
```

The syscall now uses the caller-provided address.

---

## 8. Syscall 3: Exit

### Convention

```text
R1 = 3
R2 = exit code
INT 80
```

### Behavior

```text
R7 = R2
CPU halted = true
```

Unlike most syscalls, syscall 3 does not return to the user program with `IRET`.

It intentionally halts the machine.

### Example

```asm
MOV R1, 3
MOV R2, 7
INT 80

MOV R4, 999
STORE [424], R4
```

Expected result:

```text
R7 = 7
CPU halted = true
Memory[424] remains 0
```

The code after `INT 80` should not run.

### Handler Pattern

```asm
syscall_exit:
    MOV R7, R2
    HALT
```

---

## 9. Syscall 4: Timer Read

### Convention

```text
R1 = 4
INT 80
```

### Behavior

```text
R2 = TimerDevice tick count
```

The syscall handler reads the timer tick count through TimerDevice MMIO.

Current TimerDevice MMIO base:

```text
TimerDevice base = 0xF100 = 61696
```

Current TimerDevice register layout:

```text
offset 0  = tick count
offset 8  = interval
offset 16 = enabled
offset 24 = vector
offset 32 = payload
offset 40 = interrupt count
```

### Example

```asm
MOV R1, 4
MOV R2, 0
INT 80

STORE [464], R2
```

Expected result when timer tick count is `12345`:

```text
R2 = 12345
Memory[464] = 12345
```

### Handler Pattern

```asm
syscall_timer_read:
    LOAD R2, [61696]
    IRET
```

---

## 10. Register-Indirect Memory Addressing

Register-indirect addressing is now part of the ISA.

Supported forms:

```asm
LOAD R4, [R2]
STORE [R2], R3
```

Meaning:

```text
[R2] means:
    use the value inside R2 as a memory address
```

Example:

```asm
MOV R2, 500
MOV R3, 999
STORE [R2], R3
```

Result:

```text
Memory[500] = 999
```

This feature is required for syscall 2 to behave like a real memory-write syscall.

---

## 11. Example Combined Handler

A simple syscall handler can look like this:

```asm
syscall_handler:
    CMP R1, 1
    JE syscall_debug_output

    CMP R1, 2
    JE syscall_memory_write

    CMP R1, 3
    JE syscall_exit

    CMP R1, 4
    JE syscall_timer_read

    IRET

syscall_debug_output:
    STORE [61440], R2
    IRET

syscall_memory_write:
    STORE [R2], R3
    IRET

syscall_exit:
    MOV R7, R2
    HALT

syscall_timer_read:
    LOAD R2, [61696]
    IRET
```

---

## 12. Example User Program

```asm
JMP main

syscall_handler:
    CMP R1, 1
    JE syscall_debug_output

    CMP R1, 2
    JE syscall_memory_write

    CMP R1, 3
    JE syscall_exit

    CMP R1, 4
    JE syscall_timer_read

    IRET

syscall_debug_output:
    STORE [61440], R2
    IRET

syscall_memory_write:
    STORE [R2], R3
    IRET

syscall_exit:
    MOV R7, R2
    HALT

syscall_timer_read:
    LOAD R2, [61696]
    IRET

main:
    MOV R1, 1
    MOV R2, 72
    INT 80

    MOV R1, 2
    MOV R2, 500
    MOV R3, 999
    INT 80

    MOV R1, 4
    MOV R2, 0
    INT 80
    STORE [464], R2

    MOV R1, 3
    MOV R2, 0
    INT 80

    HALT
```

---

## 13. Current Tests

Current CLI tests related to syscalls:

```bat
.\build\Debug\zero_cli.exe software-interrupt-test
.\build\Debug\zero_cli.exe mini-kernel-syscall-test
.\build\Debug\zero_cli.exe mini-kernel-syscall2-test
.\build\Debug\zero_cli.exe mini-kernel-syscall3-test
.\build\Debug\zero_cli.exe mini-kernel-syscall4-timer-read-test
.\build\Debug\zero_cli.exe register-indirect-test
```

The full test suite runs them through:

```bat
scripts\test_all.bat
```

---

## 14. Current Example Files

Current syscall-related examples:

```text
examples/software_interrupt.zasm
examples/mini_kernel_syscall.zasm
examples/mini_kernel_syscall2.zasm
examples/mini_kernel_syscall3.zasm
examples/mini_kernel_syscall4_timer_read.zasm
examples/register_indirect_memory.zasm
```

---

## 15. Design Rules

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

Exit-style syscalls may use:

```asm
HALT
```

---

## 16. Future Syscalls

Possible next syscalls:

```text
syscall 5 = timer enable
syscall 6 = timer disable
syscall 7 = timer configure interval
syscall 8 = timer configure interrupt vector
syscall 9 = read timer interrupt count
```

Possible later syscalls:

```text
syscall 10 = read memory
syscall 11 = debug output string
syscall 12 = clear debug output
syscall 13 = get CPU status
```

---

## 17. Future Improvements

Planned improvements:

```text
- save/restore FLAGS in interrupt frame
- define caller-saved and callee-saved registers
- separate user program and kernel handler files
- add boot.zasm
- add kernel.zasm
- add syscall table document
- introduce kernel/user mode later
- add real memory protection later
```

---

## 18. Current Status

Current syscall support:

```text
INT instruction                         implemented
IRET instruction                        implemented
EI / DI                                 implemented
register-indirect memory addressing     implemented
INT 80 convention                       implemented
syscall 1 debug output                  implemented
syscall 2 memory write                  implemented
syscall 3 exit                          implemented
syscall 4 timer read                    implemented
```

This is the base for the future BIO-OS / mini-kernel direction.
