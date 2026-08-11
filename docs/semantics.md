# Zero-CPU Execution Semantics

This document defines the current architectural behavior of Zero-CPU.

It is intended to serve as the reference for:

- trace comparison
- golden trace regression tests
- invariant checking
- deterministic replay checks
- future microarchitecture implementations

The implementation remains the source of truth when this document and the code
disagree. A disagreement should be treated as a documentation or implementation
bug and resolved explicitly.

## 1. Scope

Zero-CPU currently supports two execution representations:

1. interpreter-style execution from an in-memory instruction vector
2. binary execution from a `.zbin` image loaded into virtual memory

Both modes implement the same instruction set and architectural state.

The important PC difference is:

- interpreter mode: PC is an instruction index
- binary mode: PC is a byte address in the loaded code section

Binary instructions are fixed-width 24-byte records.

## 2. Architectural State

A Zero-CPU architectural state is modeled as:

```text
State = {
  registers,
  flags,
  pc,
  sp,
  privilege,
  memory,
  halted,
  error
}
```

A single successful instruction step transforms one state into another:

```text
(before_state, instruction) -> after_state
```

A `TraceEvent` records the before state, instruction, after state, detected
changes, visual metadata, and an optional runtime error.

### 2.1 Registers

Zero-CPU has eight general-purpose signed 64-bit registers:

```text
R0 R1 R2 R3 R4 R5 R6 R7
```

All registers reset to zero.

Some system conventions assign special meanings to registers, but the core CPU
does not reserve them as immutable architectural registers.

Examples:

```text
R0 = interrupt vector during interrupt entry
R1 = syscall / service number in INT 80 conventions
R2 = syscall arg0 / return value
R3 = syscall arg1
R4 = guest arg2 or protected-host status
R7 = exit / status value
```

### 2.2 Program Counter

The PC identifies the next instruction.

Interpreter mode:

```text
fallthrough_pc = pc + 1
```

Binary mode:

```text
fallthrough_pc = pc + 24
```

A binary PC must:

- be inside the loaded code section
- identify a complete 24-byte instruction
- be aligned to an instruction boundary relative to the code base

The default binary code base is:

```text
0x0200 = 512
```

### 2.3 Stack Pointer

The normal default stack base is:

```text
0x0800 = 2048
```

Protected execution uses explicit stack regions:

```text
User stack    = 0x0800..0x0F9F
Kernel stack  = 0x0FA0..0x0FFF
```

The stack grows upward in memory.

Each stack slot is eight bytes.

Push behavior:

```text
Memory[SP .. SP+7] = value
SP = SP + 8
```

Pop behavior:

```text
SP = SP - 8
value = Memory[SP .. SP+7]
```

`CALL` uses the same push operation for its return address.

`RET` uses the same pop operation to restore its return address.

User-mode stack operations are checked against the User stack region.

A User→Kernel interrupt transition switches to the separate Kernel interrupt
stack and preserves the interrupted User SP in the protected interrupt frame.

Legacy BIO-OS integration may still use a high stack convention, but protected
process execution uses the explicit User/Kernel stack boundaries above.

### 2.4 Flags

Zero-CPU exposes four flags:

```text
Z = zero
S = sign
C = carry / borrow indicator
O = signed overflow
```

Flag packing for interrupt frames uses:

```text
bit 0 = Z
bit 1 = S
bit 2 = C
bit 3 = O
```

`MOV`, `LOAD`, `STORE`, and `POP` update only Z and S.

ALU instructions update Z, S, C, and O from their ALU result.

## 3. Memory Semantics

The default virtual memory size is 4096 bytes.

Addresses are byte addresses.

Core data reads and writes use signed 64-bit values and occupy eight bytes.

Default endianness is little-endian.

A normal memory access is valid only when its entire access range fits inside
virtual memory.

For an eight-byte access:

```text
0 <= address
address + 8 <= memory_size
```

Register-indirect memory addresses are read from a register. Negative indirect
addresses are runtime errors.

### 3.1 MMIO Routing

Data memory accesses first consult the MMIO bus.

```text
if MMIO device exists at address:
    route access to device
else:
    route access to virtual memory
```

Instruction fetch does not use the data MMIO routing path.

Current important MMIO regions include:

```text
0xF000..0xF00F = DebugOutputDevice
0xF100..0xF12F = TimerDevice
0xF200..0xF22F = Hardware bridge
```

These addresses are outside default RAM and are handled by mapped devices.

In User mode, normal 64-bit data LOAD/STORE access is restricted to:

```text
0x0000..0x01FF
```

Therefore User code cannot directly access MMIO. Protected hardware access uses
the Kernel-side software interrupt path.

## 4. Operand Semantics

Readable value operands may be:

```text
register
immediate
direct memory address
register-indirect memory address
```

Writable destinations for register-result instructions must be registers.

`LOAD` requires:

```text
LOAD register, memory-address
```

`STORE` requires:

```text
STORE memory-address, value
```

Branch and call targets are labels in interpreter mode and encoded code
addresses in binary mode.

Binary code targets are relocated by adding the binary code base and must point
to a valid instruction boundary in the loaded code section.

## 5. Instruction Semantics

The notation below uses:

```text
dst <- value
PC <- next
```

where `next` means the fallthrough PC for the current execution mode.

### 5.1 Data Movement

#### MOV dst, src

```text
require dst is register
dst <- value(src)
Z <- (dst == 0)
S <- (dst < 0)
PC <- next
```

C and O are unchanged.

#### LOAD dst, address

```text
require dst is register
dst <- DataMemory[address]
Z <- (dst == 0)
S <- (dst < 0)
PC <- next
```

C and O are unchanged.

#### STORE address, src

```text
value <- value(src)
DataMemory[address] <- value
Z <- (value == 0)
S <- (value < 0)
PC <- next
```

C and O are unchanged.

### 5.2 Arithmetic

The destination register is also the left operand.

#### ADD dst, src

```text
dst <- wrap64(dst + value(src))
Z,S,C,O <- ALU result
PC <- next
```

C indicates unsigned carry. O indicates signed overflow.

#### SUB dst, src

```text
dst <- wrap64(dst - value(src))
Z,S,C,O <- ALU result
PC <- next
```

The current C flag is set when the unsigned left operand is smaller than the
unsigned right operand. It therefore acts as a borrow indicator for subtraction.

#### MUL dst, src

```text
dst <- low_64_bits(dst * value(src))
Z,S,C,O <- ALU result
PC <- next
```

C and O are both set when signed multiplication overflow is detected.

#### DIV dst, src

```text
dst <- signed_divide(dst, value(src))
Z,S <- result
C <- false
O <- false
PC <- next
```

Runtime errors:

- division by zero
- `INT64_MIN / -1`

### 5.3 Logical Operations

#### AND dst, src

```text
dst <- dst & value(src)
Z,S <- result
C <- false
O <- false
PC <- next
```

#### OR dst, src

```text
dst <- dst | value(src)
Z,S <- result
C <- false
O <- false
PC <- next
```

#### XOR dst, src

```text
dst <- dst ^ value(src)
Z,S <- result
C <- false
O <- false
PC <- next
```

#### NOT dst

```text
dst <- ~dst
Z,S <- result
C <- false
O <- false
PC <- next
```

### 5.4 Comparison

#### CMP lhs, rhs

```text
temporary <- wrap64(value(lhs) - value(rhs))
Z,S,C,O <- subtraction result
PC <- next
```

No register or memory value is written.

#### TEST lhs, rhs

```text
temporary <- value(lhs) & value(rhs)
Z,S <- temporary
C <- false
O <- false
PC <- next
```

No register or memory value is written.

### 5.5 Branches

#### JMP target

```text
PC <- target
```

#### JE target

```text
if Z:
    PC <- target
else:
    PC <- next
```

#### JNE target

```text
if not Z:
    PC <- target
else:
    PC <- next
```

#### JG target

Current signed-comparison behavior:

```text
if not Z and S == O:
    PC <- target
else:
    PC <- next
```

#### JL target

Current signed-comparison behavior:

```text
if S != O:
    PC <- target
else:
    PC <- next
```

`JG` and `JL` therefore use the signed overflow flag together with the sign flag,
matching the signed comparison result produced by `CMP`.

### 5.6 Stack and Calls

#### PUSH src

```text
push(value(src))
PC <- next
```

PUSH does not update flags.

#### POP dst

```text
require dst is register
dst <- pop()
Z <- (dst == 0)
S <- (dst < 0)
PC <- next
```

C and O are unchanged.

#### CALL target

Interpreter mode:

```text
push(PC + 1)
PC <- target
```

Binary mode:

```text
push(PC + 24)
PC <- target
```

#### RET

```text
PC <- pop()
```

A negative popped return address is a runtime error.

### 5.7 Interrupt Control

The following instructions are Kernel-only when the CPU is in User mode:

```text
HALT
IRET
EI
DI
```

Attempting one of them in User mode produces a privilege violation.

#### EI

```text
require Kernel mode when currently User
require interrupt controller
global_interrupt_enable <- true
PC <- next
```

#### DI

```text
require Kernel mode when currently User
require interrupt controller
global_interrupt_enable <- false
PC <- next
```

#### INT vector

The vector must be in the range 0 through 255.

Interpreter mode return address:

```text
PC + 1
```

Binary mode return address:

```text
PC + 24
```

Software interrupt entry uses the protected interrupt-frame mechanism.

For a User→Kernel transition, the frame is written to the Kernel interrupt
stack and contains four 64-bit slots:

```text
return address
packed FLAGS
saved privilege
saved SP
```

The CPU then enters Kernel mode and writes:

```text
R0 <- vector
```

If a registered host `SoftwareInterruptHandler` handles the vector, it runs
while the protected Kernel frame is active. Otherwise the CPU transfers to the
guest vector-table handler.

The software `INT` path does not assign a hardware-interrupt payload to R1.

#### Hardware Interrupt Entry

A pending hardware interrupt is serviced before normal instruction execution.

It uses the same protected four-slot interrupt frame:

```text
return address = current PC
packed FLAGS
saved privilege
saved SP
```

After frame creation:

```text
privilege <- Kernel
R0 <- vector
R1 <- payload
PC <- vector_handler(vector)
```

The return address is the current PC because the interrupted instruction has not
executed yet.

#### IRET

`IRET` is Kernel-only.

It restores the protected frame:

```text
FLAGS <- saved FLAGS
privilege <- saved privilege
PC <- saved return address
SP <- saved SP
```

Returning to User mode also leaves the Kernel interrupt stack and restores the
saved User stack pointer.

A protected host syscall with `TerminateProcess` disposition may restore a User
PC exactly equal to the executable's one-past-end address as a final
non-executable snapshot. The process is halted immediately, so that address is
never fetched.

### 5.8 Control Instructions

#### NOP

```text
PC <- next
```

#### HALT

```text
halted <- true
```

HALT does not advance PC.

## 6. Device Clock Semantics

After a normal instruction executes successfully, each registered clocked device
ticks once.

Devices do not tick when:

- the instruction halts the CPU
- instruction execution produces a runtime error
- a pending hardware interrupt is serviced instead of executing an instruction

A hardware interrupt service step produces a trace event and returns without
ticking clocked devices.

## 7. Error Semantics

A runtime exception during a CPU step is converted into architectural error
state:

```text
has_error <- true
error_message <- exception message
halted <- true
```

A trace event is still recorded for the failed step.

Common runtime errors include:

- PC outside the loaded program or binary code section
- User execution outside the configured User code range
- User data access outside the protected low-memory range
- User execution of Kernel-only instructions
- malformed binary operands
- invalid register payload
- invalid memory range
- negative register-indirect address
- invalid branch target
- stack underflow / overflow
- invalid User or Kernel stack pointer
- division by zero
- signed division overflow
- missing interrupt controller
- invalid interrupt vector
- invalid interrupt handler address
- maximum step count reached

## 8. Trace Verification Contract

A trace-based verifier should compare architectural effects rather than UI text.

Recommended comparison fields:

```text
instruction opcode and operands
pc_before
pc_after
changed registers
changed flags
changed memory
SP before and after
halted state
error state and error message
device-observable effects where applicable
```

Visual labels such as stage names, action strings, and datapath text are useful
debugger metadata but should not be the only regression oracle.

### 8.1 Golden Trace Rule

A golden trace represents the expected transition sequence for a fixed program,
binary image, initial state, and device configuration.

The same inputs should produce the same ordered architectural transitions.

### 8.2 Initial Invariants

The first invariant checker should enforce at least:

```text
0 <= PC
0 <= SP
register count == 8
memory size == 4096 unless explicitly configured otherwise
binary PC is inside and aligned to the loaded code section
User PC remains inside the configured User execution range
User data access remains inside 0x0000..0x01FF
User stack remains inside 0x0800..0x0F9F
Kernel interrupt stack remains inside 0x0FA0..0x0FFF
stack operations move SP by exactly 8
HALT leaves halted == true
runtime error implies halted == true
branch fallthrough advances by exactly one instruction
```

Further invariants can be added as the verification layer grows.

## 9. Compatibility and Versioning

This document describes the current protected-platform architectural baseline.

The canonical binary format is currently `0.3`, with legacy `0.2` binary
compatibility retained. Binary instructions remain fixed-width 24-byte records.

Changing any of the following should be treated as an ISA or architectural
semantic change:

- flag update rules
- branch predicates
- stack growth direction
- stack slot size
- interrupt frame layout
- PC representation
- instruction width
- memory access width
- runtime error behavior

Such changes should update this document, tests, golden traces, and release notes
together.

<!-- Patch: v1.6-current-roadmap-semantics-r1 -->
