# Zero-CPU Architecture Roadmap

Zero-CPU is not intended to be a pretty CPU simulator.

Zero-CPU is a small computer platform with its own ISA, assembler, executable format, loader, virtual CPU, debugger-oriented tooling, and system-level extensions.

The project direction is:

```text
Core
→ Toolchain
→ Test Infrastructure
→ MMIO / Interrupts
→ Mini Kernel
```

This document fixes the architecture direction so future work does not drift into UI-only features.

---

## 1. Project Definition

Zero-CPU is a C++17 virtual computer platform.

Its goal is to implement and explain core computer architecture concepts through code:

```text
source assembly
→ assembler
→ virtual binary format
→ binary loader
→ virtual memory
→ fetch/decode/execute CPU
→ devices
→ interrupts
→ mini kernel
```

The final form should include:

```text
- Custom ISA
- .zasm assembly language
- Assembler
- Instruction encoder / decoder
- .zbin virtual executable format
- Binary reader / writer
- Binary loader
- Virtual memory
- Virtual CPU
- ALU
- Register file
- Flags
- Stack
- CALL / RET
- INT / IRET
- EI / DI
- MMIO bus
- Timer device
- Debug output device
- Interrupt controller
- CLI test infrastructure
- Studio debugger / visualizer
- Mini kernel demo
```

Studio is useful, but it is not the core project.

```text
Zero-CPU Core   = the actual computer platform
Zero-CPU Studio = debugger / visualizer / learning tool
```

---

## 2. Current Architecture Overview

Current execution pipeline:

```text
.zasm source file
    ↓
Assembler
    ↓
Instruction IR
    ↓
InstructionEncoder
    ↓
.zbin virtual binary
    ↓
BinaryReader
    ↓
BinaryLoader
    ↓
Virtual Memory
    ↓
CPU Fetch-Decode-Execute
```

Current system-level pipeline:

```text
CPU instruction execution
    ↓
Memory access
    ↓
MMIOBus
    ↓
Mapped device
    ↓
InterruptController
    ↓
CPU interrupt delivery
    ↓
Interrupt handler
    ↓
IRET
```

Current mini-kernel style pipeline:

```text
User program
    ↓
INT 80
    ↓
Syscall handler
    ↓
MMIO output
    ↓
IRET
    ↓
User program resumes
```

---

## 3. Core Layer

The Core layer is the most important layer of the project.

It contains the virtual hardware model.

Current components:

```text
include/zero_cpu/core/
├─ CPU.hpp
├─ CPUState.hpp
├─ RegisterFile.hpp
├─ Flags.hpp
├─ Memory.hpp
├─ ALU.hpp
├─ MMIOBus.hpp
├─ MMIODevice.hpp
├─ DebugOutputDevice.hpp
├─ InterruptController.hpp
├─ TimerDevice.hpp
└─ ClockedDevice.hpp
```

### 3.1 CPU

The CPU is responsible for:

```text
- Maintaining PC
- Maintaining SP
- Fetching instructions from memory
- Decoding instructions
- Executing instructions
- Updating registers
- Updating flags
- Reading/writing memory
- Handling stack operations
- Handling CALL / RET
- Handling INT / IRET
- Routing memory access through MMIO
- Delivering pending interrupts
- Ticking clocked devices after instruction execution
```

The CPU should remain focused on execution semantics.

It should not become responsible for:

```text
- Parsing source files
- Writing binary files
- Formatting UI output
- Managing Studio layout
```

---

## 4. ISA Layer

The ISA defines the language that Zero-CPU understands.

Current instruction categories:

```text
Control:
- NOP
- HALT

Data movement:
- MOV
- LOAD
- STORE

Arithmetic:
- ADD
- SUB
- MUL
- DIV

Comparison:
- CMP
- TEST

Branching:
- JMP
- JE
- JNE
- JG
- JL

Stack / function:
- PUSH
- POP
- CALL
- RET

Interrupt / system:
- INT
- IRET
- EI
- DI

Bitwise:
- AND
- OR
- XOR
- NOT
```

Important design rule:

```text
CALL returns with RET.
Interrupts and software interrupts return with IRET.
```

This keeps normal function calls and interrupt control flow separate.

---

## 5. Toolchain Layer

The toolchain turns human-readable source into executable virtual binary.

Current toolchain:

```text
.zasm
    ↓
Assembler
    ↓
Instruction IR
    ↓
InstructionEncoder
    ↓
.zbin
```

Related files:

```text
include/zero_cpu/assembler/
include/zero_cpu/isa/
include/zero_cpu/binary/
src/assembler/
src/isa/
src/binary/
```

### 5.1 .zasm

`.zasm` is the human-readable assembly format.

Example:

```asm
MOV R1, 65
INT 80
HALT
```

### 5.2 .zbin

`.zbin` is the Zero-CPU virtual binary format.

It contains:

```text
- Magic
- Version
- Endianness
- Entry point
- Code size
- Encoded instruction bytes
```

Each instruction is currently encoded as a fixed-size instruction.

This makes early decoding and debugging simple.

---

## 6. Memory Model

Zero-CPU memory is byte-addressable.

Important regions currently used by convention:

```text
0x0000..      data / low memory
0x0200..      binary code load area
0x0800..      stack start around 2048
0xF000..      DebugOutputDevice MMIO
0xF100..      TimerDevice MMIO
```

The CPU should access memory through helper functions, not directly everywhere.

This allows MMIO routing:

```text
STORE [0xF000], R1
    ↓
MMIOBus
    ↓
DebugOutputDevice
```

---

## 7. MMIO Layer

MMIO means memory-mapped I/O.

Instead of adding special `OUT` instructions first, Zero-CPU maps devices into memory address space.

Current MMIO components:

```text
MMIODevice
MMIOBus
DebugOutputDevice
TimerDevice
```

Current debug output mapping:

```text
0xF000..0xF00F
```

Current timer mapping:

```text
0xF100..0xF12F
```

Example:

```asm
MOV R1, 65
STORE [61440], R1
```

`61440` is `0xF000`.

This does not write to RAM. It writes to the mapped debug output device.

---

## 8. Interrupt Layer

The interrupt system currently includes:

```text
InterruptController
InterruptRequest
Vector table
Pending interrupt queue
Global enable / disable
Vector mask / unmask
CPU interrupt delivery
IRET
EI
DI
```

Basic interrupt delivery flow:

```text
1. Device requests interrupt.
2. InterruptController queues request.
3. CPU checks pending interrupts before executing an instruction.
4. CPU pushes interrupt return address.
5. CPU writes vector to R0.
6. CPU writes payload to R1.
7. CPU jumps to handler address.
8. Handler runs.
9. Handler returns with IRET.
```

Important distinction:

```text
RET  = return from CALL
IRET = return from interrupt
```

---

## 9. Timer Device

TimerDevice is a clocked MMIO device.

It counts CPU steps and requests interrupts at a configured interval.

Current behavior:

```text
CPU executes one instruction
    ↓
CPU ticks clocked devices
    ↓
TimerDevice tick count increments
    ↓
TimerDevice reaches interval
    ↓
InterruptController receives request
    ↓
CPU enters handler on next step
```

TimerDevice registers:

```text
offset 0  = tick count
offset 8  = interval
offset 16 = enabled
offset 24 = vector
offset 32 = payload
offset 40 = interrupt count
```

The timer can be disabled from an interrupt handler using MMIO.

---

## 10. Software Interrupts and Mini Kernel Direction

`INT` introduces software interrupt support.

This allows user code to request services from a handler.

Current syscall convention:

```text
R1 = syscall number
R2 = syscall argument
INT 80
```

Example:

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

Current mini-kernel style behavior:

```text
INT 80
    ↓
syscall handler
    ↓
check R1 syscall number
    ↓
use R2 as argument
    ↓
write to MMIO device
    ↓
IRET
```

This is the starting point for BIO-OS / mini kernel demos.

---

## 11. Test Infrastructure

Testing is a first-class part of Zero-CPU.

Current CLI tests include:

```text
zero_cli alu-test
zero_cli mmio-test
zero_cli interrupt-test
zero_cli cpu-interrupt-test
zero_cli timer-test
zero_cli cpu-timer-test
zero_cli cpu-ei-di-test
zero_cli software-interrupt-test
zero_cli mini-kernel-syscall-test
zero_cli binary-test
```

Current test runner:

```text
scripts/test_all.bat
```

Every major architecture feature should have:

```text
1. a focused CLI test command
2. a .zasm example when applicable
3. an entry in scripts/test_all.bat
```

This prevents regressions while the CPU grows.

---

## 12. Studio Scope

Zero-CPU Studio should remain a debugger and visualizer.

Allowed Studio features:

```text
- Load .zasm
- Edit .zasm
- Assemble
- Load .zbin
- Step
- Run
- View registers
- View flags
- View memory
- View stack
- View trace
- View MMIO mappings
- View interrupt state
```

Avoid making Studio the main project.

Do not prioritize visual polish before the core platform is stable.

---

## 13. Roadmap

### Phase 1: Core CPU

Status: mostly complete.

```text
- RegisterFile
- Flags
- Memory
- ALU
- CPUState
- CPU execution
- Stack
- CALL / RET
```

### Phase 2: Toolchain

Status: mostly complete.

```text
- .zasm
- Assembler
- InstructionEncoder
- InstructionDecoder
- .zbin
- BinaryReader
- BinaryWriter
- BinaryLoader
```

### Phase 3: Test Infrastructure

Status: active and strong.

```text
- test_all.bat
- alu-test
- binary-test
- memory expectations
- MMIO tests
- interrupt tests
- timer tests
- syscall tests
```

### Phase 4: MMIO / Interrupts

Status: implemented MVP.

```text
- MMIOBus
- DebugOutputDevice
- TimerDevice
- InterruptController
- INT
- IRET
- EI
- DI
```

### Phase 5: Mini Kernel

Status: beginning.

Next targets:

```text
- syscall 1: debug output
- syscall 2: memory write
- syscall 3: exit
- syscall error handling
- user program / kernel handler separation
- boot.zasm
- kernel.zasm
- syscall convention document
```

### Phase 6: Advanced Architecture

Future targets:

```text
- Bus abstraction beyond MMIO
- Device table
- Interrupt priority
- Nested interrupt policy
- IRET frame with FLAGS restore
- Privilege mode
- User mode / kernel mode
- Simple virtual memory
- Page table experiment
- Cache simulator
- Pipeline simulator
```

---

## 14. Near-Term Priorities

The next practical steps should be:

```text
1. README update
2. docs/syscall-convention.md
3. syscall 2 and syscall 3 examples
4. kernel/user program separation
5. better interrupt frame with FLAGS save/restore
6. Studio interrupt/MMIO view later
```

Immediate rule:

```text
Core and system behavior first.
Studio polish later.
```

---

## 15. Design Principles

Zero-CPU should follow these principles:

```text
1. Prefer small, testable architecture steps.
2. Every new CPU/system feature needs a CLI test.
3. Every new instruction must be registered in:
   - Opcode.hpp
   - Opcode.cpp
   - InstructionEncoder.cpp
   - InstructionDecoder.cpp
   - CPU.cpp
4. Every new system behavior should have a .zasm example.
5. UI should not drive architecture decisions.
6. The project should remain understandable as a learning system.
```

---

## 16. Final Direction

Zero-CPU is suitable preparation for systems software development because it connects:

```text
- computer architecture
- assembly
- binary formats
- loaders
- CPU execution
- memory
- stack
- interrupts
- MMIO
- devices
- syscall conventions
- mini kernel design
```

The final target is:

```text
A small virtual computer platform with its own ISA, assembler, executable format, loader, virtual CPU, debugger tooling, device model, interrupt system, and mini kernel.
```
