# Zero-CPU

Zero-CPU is a small virtual computer platform written in C++17.

It is not just a visual CPU simulator. The goal is to build a compact systems project with its own ISA, assembler, virtual binary format, loader, virtual CPU, MMIO devices, interrupt system, software interrupt syscall convention, and a mini-kernel / BIO-OS style demo.

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
MMIO / Interrupts / Syscalls
    ↓
Mini Kernel / BIO-OS demo
```

---

## Current Status

```text
Core CPU                 implemented
Assembler                implemented
.zbin binary format      implemented
Binary loader            implemented
MMIO                     implemented
Interrupt controller     implemented
Timer device             implemented
Software interrupt       implemented
INT 80 syscalls          syscall 1..7 implemented
BIO-OS boot demo         implemented
run-os CLI command       implemented
```

The current project is at the early BIO-OS demo stage.

---

## Quick Start

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

Run the BIO-OS demo:

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Run the full test suite:

```bat
scripts\test_all.bat
```

Expected final output:

```text
All Zero-CPU tests passed.
```

---

## BIO-OS Demo

The BIO-OS demo is split into three assembly files:

```text
examples/bio_os/boot.zasm
examples/bio_os/kernel.zasm
examples/bio_os/user_program.zasm
```

The `run-os` command combines these files into generated files:

```text
examples/bio_os/combined_boot.zasm
examples/bio_os/combined_boot.zbin
```

Those generated files are ignored by Git.

Run:

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Current BIO-OS flow:

```text
boot.zasm
    ↓
prints boot debug output
    ↓
configures timer through syscall 7
    ↓
enables timer through syscall 5
    ↓
waits for timer interrupt
    ↓
timer_handler disables timer
    ↓
calls user_program_main
    ↓
user program uses syscalls
    ↓
boot exits through syscall 3
```

This demonstrates:

```text
- separate boot/kernel/user source files
- assembly combination
- binary generation
- binary loading
- CPU execution
- INT 80 syscall handling
- MMIO debug output
- TimerDevice configuration
- hardware-style timer interrupt
- interrupt handler execution
- user program return
- clean exit
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
│  ├─ bio_os/
│  ├─ function_call.zasm
│  ├─ alu_flags.zasm
│  ├─ mmio_output.zasm
│  └─ ...
├─ scripts/
├─ docs/
└─ CMakeLists.txt
```

---

## Core CPU Features

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
- MMIO routing
- Clocked devices
- Interrupt delivery
```

---

## ISA

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

---

## Addressing Modes

Zero-CPU currently supports:

```asm
MOV R1, 10
LOAD R2, [300]
STORE [300], R2
LOAD R4, [R2]
STORE [R2], R3
```

Register-indirect memory addressing is supported:

```asm
MOV R2, 500
MOV R3, 999
STORE [R2], R3
```

Meaning:

```text
Memory[500] = 999
```

This is used by the memory-write syscall.

---

## Toolchain

Zero-CPU has a small custom toolchain:

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

Binary execution flow:

```text
.zbin
    ↓
BinaryReader
    ↓
BinaryLoader
    ↓
Virtual Memory
    ↓
CPU Fetch-Decode-Execute
```

Current toolchain features:

```text
- .zasm assembly source
- labels
- register operands
- immediate operands
- direct memory operands
- register-indirect memory operands
- code label resolution
- .zbin writing
- .zbin reading
- binary loading at code base 0x0200
```

---

## Memory Map

Current memory conventions:

```text
0x0000..0x01FF = low data / scratch memory
0x0200..       = binary code load area
0x0800         = original default stack base
0x0FA0-ish     = BIO-OS integration demo stack area
0xF000..0xF00F = DebugOutputDevice MMIO
0xF100..0xF12F = TimerDevice MMIO
```

Important note:

```text
BIO-OS combined programs can become large enough to overlap the old default stack base at 0x0800.
The current run-os demo moves SP higher during integration execution to avoid code/stack collision.
```

A future improvement should formalize stack/code/data regions in the memory map.

---

## MMIO Map

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

Current interrupt frame is minimal:

```text
INT / interrupt:
    push return address

IRET:
    pop return address
```

Future improvement:

```text
INT / interrupt:
    push return address
    push FLAGS

IRET:
    pop FLAGS
    pop return address
```

---

## Syscall Convention

The mini-kernel syscall convention uses `INT 80`.

```text
R1 = syscall number
R2 = argument 0 / return value
R3 = argument 1
R4 = argument 2
INT 80
```

Current implemented syscalls:

| Syscall | Name | Input | Effect |
|---:|---|---|---|
| 1 | debug output | `R2 = value` | writes value to DebugOutputDevice |
| 2 | memory write | `R2 = address`, `R3 = value` | writes `Memory[R2] = R3` |
| 3 | exit | `R2 = exit code` | sets `R7`, halts CPU |
| 4 | timer read | none | returns timer tick count in `R2` |
| 5 | timer enable | `R2 = interval`, `R3 = vector` | configures and enables timer |
| 6 | timer disable | none | disables timer |
| 7 | timer configure | `R2 = interval`, `R3 = vector`, `R4 = payload` | configures timer registers |

Example:

```asm
MOV R1, 1
MOV R2, 72
INT 80
```

Memory-write syscall example:

```asm
MOV R1, 2
MOV R2, 500
MOV R3, 999
INT 80
```

Timer configure syscall example:

```asm
MOV R1, 7
MOV R2, 8
MOV R3, 44
MOV R4, 888
INT 80
```

---

## CLI Usage

The main command-line tool is:

```text
zero_cli.exe
```

Typical path:

```bat
.\build\Debug\zero_cli.exe
```

### Run BIO-OS

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
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

Focused tests include:

```bat
.\build\Debug\zero_cli.exe alu-test
.\build\Debug\zero_cli.exe mmio-test
.\build\Debug\zero_cli.exe interrupt-test
.\build\Debug\zero_cli.exe cpu-interrupt-test
.\build\Debug\zero_cli.exe timer-test
.\build\Debug\zero_cli.exe cpu-timer-test
.\build\Debug\zero_cli.exe cpu-ei-di-test
.\build\Debug\zero_cli.exe software-interrupt-test
.\build\Debug\zero_cli.exe register-indirect-test
.\build\Debug\zero_cli.exe mini-kernel-syscall-test
.\build\Debug\zero_cli.exe mini-kernel-syscall2-test
.\build\Debug\zero_cli.exe mini-kernel-syscall3-test
.\build\Debug\zero_cli.exe mini-kernel-syscall4-timer-read-test
.\build\Debug\zero_cli.exe mini-kernel-syscall5-timer-enable-test
.\build\Debug\zero_cli.exe mini-kernel-syscall6-timer-disable-test
.\build\Debug\zero_cli.exe mini-kernel-syscall7-timer-configure-test
.\build\Debug\zero_cli.exe mini-kernel-timer-lifecycle-test
.\build\Debug\zero_cli.exe bio-os-combined-boot-test
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Full test runner:

```bat
scripts\test_all.bat
```

---

## Example Programs

### `examples/function_call.zasm`

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

### `examples/alu_flags.zasm`

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

### `examples/mmio_output.zasm`

Tests memory-mapped debug output.

```asm
MOV R1, 65
STORE [61440], R1
```

`61440` is `0xF000`, the debug output MMIO address.

### `examples/software_interrupt.zasm`

Tests software interrupt.

```asm
INT 80
```

### `examples/mini_kernel_syscall*.zasm`

Tests the mini-kernel syscall convention.

### `examples/mini_kernel_timer_lifecycle.zasm`

Tests syscall-based timer configuration, timer enable, timer interrupt delivery, timer handler execution, and timer disable.

### `examples/bio_os/`

Contains the current BIO-OS demo.

```text
boot.zasm
kernel.zasm
user_program.zasm
```

---

## Generated Files

The project intentionally ignores generated artifacts:

```text
*.zbin
*.zip
*_files/
examples/bio_os/combined_boot.zasm
examples/bio_os/combined_boot.zbin
```

Cleanup helper:

```bat
scripts\clean_generated.bat
```

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

## Near-Term Roadmap

Good next steps:

```text
1. stabilize run-os
2. improve BIO-OS output readability
3. formalize memory map and stack region
4. improve interrupt frame with FLAGS save/restore
5. make boot/kernel/user loading cleaner
6. add a small syscall table abstraction
7. improve Studio/debugger after core behavior stabilizes
```

---

## Portfolio Summary

Short version:

```text
Zero-CPU is a C++17 virtual computer platform implementing a custom ISA,
assembler, virtual executable format, binary loader, fetch-decode-execute CPU,
MMIO devices, interrupts, syscall convention, and BIO-OS style mini-kernel demo.
```

Korean version:

```text
Zero-CPU는 C++17로 구현한 작은 가상 컴퓨터 플랫폼입니다.
자체 ISA, 어셈블러, 가상 실행 파일 형식, 바이너리 로더,
Fetch-Decode-Execute 기반 가상 CPU, ALU/FLAGS/스택,
MMIO 장치 모델, 인터럽트 컨트롤러, 타이머 장치,
소프트웨어 인터럽트 기반 syscall convention,
그리고 BIO-OS 스타일 미니 커널 데모를 구현했습니다.
```
