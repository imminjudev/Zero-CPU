# Zero-CPU

Zero-CPU is a small virtual computer platform written in C++17.

It is not just a visual CPU simulator. The goal is to build a compact systems project with its own ISA, assembler, binary format, loader, virtual CPU, MMIO devices, interrupt system, and mini-kernel style syscall flow.

```text
.zasm source
    ↓
Assembler
    ↓
InstructionEncoder
    ↓
.zbin virtual binary
    ↓
BinaryLoader
    ↓
Virtual Memory
    ↓
CPU Fetch-Decode-Execute
    ↓
MMIO / Interrupts / Mini Kernel
```

---

## Project Goal

Zero-CPU is designed as a learning and portfolio project for systems software development.

It connects concepts from:

```text
- computer architecture
- CPU execution
- assembly language
- binary formats
- loaders
- virtual memory model
- ALU and FLAGS
- stack and function calls
- MMIO
- interrupts
- timer devices
- software interrupts
- syscall conventions
- mini kernel design
```

The final direction is:

```text
A small virtual computer platform with its own ISA, assembler, executable format, loader, virtual CPU, debugger tooling, device model, interrupt system, and mini kernel.
```

---

## Current Features

### Core CPU

```text
- Register file
- FLAGS register
- Byte-addressable memory
- CPU state
- ALU module
- Fetch-Decode-Execute binary execution
- Stack pointer
- CALL / RET
- INT / IRET
- EI / DI
```

### ISA

Current instruction groups:

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

### Toolchain

```text
- .zasm assembly source
- Assembler
- InstructionEncoder
- InstructionDecoder
- .zbin virtual binary format
- BinaryWriter
- BinaryReader
- BinaryLoader
```

### System Extensions

```text
- MMIOBus
- MMIODevice interface
- DebugOutputDevice
- InterruptController
- TimerDevice
- ClockedDevice interface
- Hardware-style timer interrupt
- Software interrupt through INT
- INT 80 syscall convention
```

### Testing

```text
- ALU test
- Binary format round-trip test
- MMIO bus test
- Interrupt controller test
- CPU interrupt delivery test
- Timer device test
- CPU timer interrupt test
- EI/DI interrupt control test
- Software interrupt test
- Mini-kernel syscall test
- Full test runner script
```

---

## Repository Structure

```text
Zero-CPU/
├─ include/
│  └─ zero_cpu/
│     ├─ assembler/
│     ├─ binary/
│     ├─ core/
│     ├─ isa/
│     └─ trace/
├─ src/
│  ├─ assembler/
│  ├─ binary/
│  ├─ core/
│  ├─ isa/
│  └─ trace/
├─ tools/
│  └─ zero_cli.cpp
├─ studio/
│  └─ zero_studio.cpp
├─ examples/
├─ scripts/
├─ docs/
└─ CMakeLists.txt
```

---

## Requirements

```text
- C++17
- CMake
- Visual Studio 2022 / MSVC
- Windows command prompt or PowerShell
```

This project is currently developed and tested on Windows with MSVC.

---

## Build

From the project root:

```bat
cd /d D:\Zero-CPU
cmake --build build
```

If the build directory does not exist yet:

```bat
cd /d D:\Zero-CPU
cmake -S . -B build
cmake --build build
```

---

## CLI Usage

The main command-line tool is:

```text
zero_cli.exe
```

Typical path:

```bat
build\Debug\zero_cli.exe
```

### Help

```bat
.\build\Debug\zero_cli.exe --help
```

### Run Assembly Directly

```bat
.\build\Debug\zero_cli.exe examples\function_call.zasm
```

### Assemble `.zasm` to `.zbin`

```bat
.\build\Debug\zero_cli.exe assemble examples\function_call.zasm examples\function_call.zbin
```

### Dump Binary

```bat
.\build\Debug\zero_cli.exe dump-binary examples\function_call.zbin
```

### Load Binary

```bat
.\build\Debug\zero_cli.exe load-binary examples\function_call.zbin
```

### Run Binary

```bat
.\build\Debug\zero_cli.exe run-binary examples\function_call.zbin
```

### Run Binary with Memory Expectations

```bat
.\build\Debug\zero_cli.exe run-binary examples\function_call.zbin --expect-memory 100=20 2048=20
```

### Run Binary with Debug MMIO

```bat
.\build\Debug\zero_cli.exe run-binary examples\mmio_output.zbin --debug-mmio --expect-memory 220=66 228=2
```

---

## Test Commands

### ALU Test

```bat
.\build\Debug\zero_cli.exe alu-test
```

### MMIO Test

```bat
.\build\Debug\zero_cli.exe mmio-test
```

### Interrupt Controller Test

```bat
.\build\Debug\zero_cli.exe interrupt-test
```

### CPU Interrupt Test

```bat
.\build\Debug\zero_cli.exe cpu-interrupt-test
```

### Timer Device Test

```bat
.\build\Debug\zero_cli.exe timer-test
```

### CPU Timer Interrupt Test

```bat
.\build\Debug\zero_cli.exe cpu-timer-test
```

### EI/DI Test

```bat
.\build\Debug\zero_cli.exe cpu-ei-di-test
```

### Software Interrupt Test

```bat
.\build\Debug\zero_cli.exe software-interrupt-test
```

### Mini Kernel Syscall Test

```bat
.\build\Debug\zero_cli.exe mini-kernel-syscall-test
```

---

## Full Test Suite

Run all tests:

```bat
cd /d D:\Zero-CPU
scripts\test_all.bat
```

Expected final output:

```text
All Zero-CPU tests passed.
```

---

## Example Programs

### `function_call.zasm`

Tests stack and function-call behavior.

```text
- MOV
- CALL
- STORE
- PUSH
- POP
- RET
- HALT
```

Expected memory:

```text
Memory[100] = 20
Memory[2048] = 20
```

### `alu_flags.zasm`

Tests ALU, CMP, TEST, and conditional jumps.

```text
- ADD
- SUB
- CMP
- TEST
- JE
- JNE
- JG
- JL
```

### `mmio_output.zasm`

Tests memory-mapped debug output.

```asm
MOV R1, 65
STORE [61440], R1
```

`61440` is `0xF000`, the debug output MMIO address.

### `interrupt_basic.zasm`

Tests CPU interrupt delivery and IRET.

### `timer_interrupt.zasm`

Tests timer-generated hardware-style interrupts.

### `interrupt_ei_di.zasm`

Tests interrupt disable/enable behavior.

```asm
DI
; protected section
EI
```

### `software_interrupt.zasm`

Tests software interrupt.

```asm
INT 80
```

### `mini_kernel_syscall.zasm`

Tests the mini-kernel syscall convention.

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

---

## MMIO Map

Current MMIO address conventions:

```text
0xF000..0xF00F = DebugOutputDevice
0xF100..0xF12F = TimerDevice
```

### DebugOutputDevice

```text
offset 0 = write value / read last value
offset 8 = read write count
```

### TimerDevice

```text
offset 0  = tick count
offset 8  = interval
offset 16 = enabled
offset 24 = vector
offset 32 = payload
offset 40 = interrupt count
```

---

## Interrupt Model

Current interrupt flow:

```text
1. Device or INT instruction requests interrupt behavior.
2. CPU saves return address.
3. CPU sets R0 to interrupt vector.
4. CPU sets R1 to interrupt payload when applicable.
5. CPU jumps to handler address.
6. Handler executes.
7. Handler returns with IRET.
```

Important rule:

```text
CALL returns with RET.
Interrupts return with IRET.
```

---

## Software Interrupt and Syscall Convention

The mini-kernel style syscall convention currently uses `INT 80`.

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

Current syscall demo:

```text
syscall 1 = debug output
```

This flow is the base for future BIO-OS / mini-kernel experiments.

---

## Zero-CPU Studio

Zero-CPU Studio is a Win32-based debugger/visualizer.

Its purpose is to support the core platform, not replace it.

Current intended scope:

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
- View trace
```

Studio polish should come after the core architecture and tests are stable.

---

## Development Rule

Every new instruction should be registered in:

```text
- include/zero_cpu/isa/Opcode.hpp
- src/isa/Opcode.cpp
- src/isa/InstructionEncoder.cpp
- src/isa/InstructionDecoder.cpp
- src/core/CPU.cpp
```

Every new system feature should have:

```text
- focused CLI test
- example .zasm file when applicable
- scripts/test_all.bat entry
```

---

## Roadmap

### Near Term

```text
- docs/syscall-convention.md
- syscall 2: memory write
- syscall 3: exit
- user program / kernel handler separation
- better interrupt frame with FLAGS save/restore
- README and Velog documentation
```

### Mid Term

```text
- kernel.zasm
- boot.zasm
- BIO-OS demo
- interrupt priority
- nested interrupt policy
- device table
```

### Long Term

```text
- privilege mode
- user mode / kernel mode
- virtual memory experiment
- page table experiment
- cache simulator
- pipeline simulator
```

---

## Current Direction

The project direction is fixed as:

```text
Core
→ Toolchain
→ Test Infrastructure
→ MMIO / Interrupts
→ Mini Kernel
```

Avoid drifting into UI-only work.

Core and system behavior first. Studio polish later.
