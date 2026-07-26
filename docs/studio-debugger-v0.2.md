# Zero-CPU Studio Debugger v0.2

Zero-CPU Studio v0.2 is a visual debugger layer for the Zero-CPU virtual
computer platform.

The goal of this debugger is not just to show final register values. It explains
how a custom instruction moves through the virtual CPU model:

```text
source .zasm
    -> assembler
    -> .zbin binary
    -> binary loader
    -> virtual memory
    -> CPU fetch/decode/execute
    -> ALU / memory / stack / interrupt / syscall / MMIO
    -> final state
```

---

## 1. What v0.2 Adds

Studio v0.2 focuses on making CPU execution visible.

```text
- Visual Datapath Panel
- Graphical Datapath Canvas
- Pipeline Timeline
- Execution Detail Probe
- Recent Instruction Trace
- Memory Map Viewer
- Debugger showcase assembly example
```

These features are powered by `TraceEvent` data emitted by the CPU during
stepping and running.

---

## 2. Recommended Demo File

Use:

```text
examples/debugger_showcase.zasm
```

This example intentionally exercises multiple CPU paths:

```text
- ALU ADD/SUB
- flag updates
- STORE / LOAD
- PUSH / POP
- CALL / RET
- direct DebugOutputDevice MMIO
- INT 80 syscall
- HALT
```

Expected checkpoints:

```text
Memory[180] = 30
Memory[188] = 60
Memory[196] = 60
Memory[204] = 123
Memory[212] = 90
DebugOutputDevice ASCII = ZA
```

---

## 3. Studio Demo Flow

From the project root:

```bat
cmake --build build
.\build\Debug\zero_studio.exe
```

Inside Studio:

```text
1. Set Input source/binary path:
   examples\debugger_showcase.zasm

2. Set Output .zbin path:
   examples\debugger_showcase.zbin

3. Click [Load Source]

4. Click [Assemble]

5. Click [Load BIN]

6. Click [Step]
```

Important button meanings:

```text
Load Source  -> load a .zasm file into the source editor
Assemble     -> compile .zasm into .zbin
Load BIN     -> load a .zbin binary into the virtual CPU
Step         -> execute one instruction
Run          -> execute until halt or breakpoint
Run BIO-OS   -> run the generated BIO-OS boot demo
```

Do not use `Load ASM` for `.zbin` files. A `.zbin` file begins with the `ZCPU`
binary magic header, so trying to load it as assembly will produce an invalid
opcode error.

---

## 4. Visual Datapath Panel

The Visual Datapath Panel summarizes the latest `TraceEvent`.

It shows:

```text
- trace event count
- latest instruction
- PC before / after
- stage
- action
- datapath path
- active datapath nodes
- changed registers
- changed flags
- changed memory
```

Example:

```text
Visual Datapath Panel
Trace Events = 12
Instruction = ADD R1, R1
PC = 8 -> 9
Stage = EXECUTE
Action = ALU_ADD
Path = PC -> InstructionMemory -> Decoder -> RegisterFile -> ALU -> Flags -> Writeback

Datapath Nodes
  [0] PC
  [1] InstructionMemory
  [2] Decoder
  [3] RegisterFile
  [4] ALU
  [5] Flags
  [6] Writeback
```

---

## 5. Graphical Datapath Canvas

The Graphical Datapath Canvas turns the latest `TraceEvent` into a visual CPU
datapath diagram.

Main blocks:

```text
PC -> Instruction -> Decoder -> RegisterFile -> ALU -> Flags -> Writeback
```

Lower path:

```text
Memory/MMIO
Stack
InterruptCtl
```

When an instruction runs, active blocks are highlighted.

Examples:

```text
ADD R1, R1
  highlights PC, Instruction, Decoder, RegisterFile, ALU, Flags, Writeback

STORE [180], R1
  highlights PC, Instruction, Decoder, RegisterFile, Memory/MMIO

PUSH R1
  highlights PC, Instruction, Decoder, RegisterFile, Stack

INT 80
  highlights PC, Instruction, Decoder, InterruptCtl
```

---

## 6. Pipeline Timeline

The Pipeline Timeline explains the latest instruction as a classic instruction
flow:

```text
FETCH -> DECODE -> EXECUTE -> MEMORY -> WRITEBACK
```

Example:

```text
Pipeline Timeline
Instruction = ADD R1, R1
Action = ALU_ADD
PC = 8 -> 9

[FETCH]     [ACTIVE] PC 8 reads instruction
[DECODE]    [ACTIVE] ADD R1, R1 -> opcode/operands
[EXECUTE]   [ACTIVE] ALU_ADD
[MEMORY]    [idle] no memory access
[WRITEBACK] [ACTIVE] R1: 30 -> 60
[FLAGS]     [ACTIVE] ZF: 0 -> 0
```

This makes the debugger useful for explaining computer architecture concepts,
not just for observing program output.

---

## 7. Execution Detail Probe

The Execution Detail Probe explains what happened inside the important execution
areas.

Sections:

```text
- ALU Detail
- Memory Detail
- Interrupt Detail
- Writeback Summary
```

Example for an ALU instruction:

```text
Execution Detail Probe
Instruction = ADD R1, R1
Action = ALU_ADD

ALU Detail
  Active = true
  Operation = ALU_ADD
  Register Result
    R1: 30 -> 60

Memory Detail
  Active = false
  Route = none

Interrupt Detail
  Active = false
  Action = none

Writeback Summary
  Registers = 1
  Flags = 2
```

Example for a memory instruction:

```text
Memory Detail
  Active = true
  Route = Memory/MMIO
  Memory Mutations = 1
    Memory[180]: 0 -> 30
```

Example for interrupt flow:

```text
Interrupt Detail
  Active = true
  Action = SOFTWARE_INTERRUPT
  Instruction = INT 80
  Kind = software interrupt
```

---

## 8. Recent Instruction Trace

The Recent Instruction Trace shows how the CPU reached the current state.

It displays the most recent trace events:

```text
Recent Instruction Trace
Showing last 16 of 24 events

[8]  PC 8 -> 9   | ADD R1, R1      | ALU_ADD
[9]  PC 9 -> 10  | RET             | RETURN
[10] PC 10 -> 11 | STORE [188], R1 | MEMORY_WRITE
[11] PC 11 -> 12 | PUSH R1         | STACK_PUSH
[12] PC 12 -> 13 | MOV R1, 0       | REGISTER_WRITE
[13] PC 13 -> 14 | POP R3          | STACK_POP
```

This is useful because the other panels focus on the latest instruction, while
the Recent Instruction Trace explains the path that led there.

---

## 9. Memory Map Viewer

The Memory Map Viewer gives address context for the current PC and SP.

It uses the shared constants from:

```text
include/zero_cpu/core/MemoryMap.hpp
```

Important memory regions:

```text
[0x0000..0x01FF] Low Data / Scratch
0x0200           Binary code base
0x0800           Default stack base
0x0FA0           BIO-OS stack base
0x1000           Default memory size boundary
[0xF000..0xF00F] DebugOutputDevice MMIO
[0xF100..0xF12F] TimerDevice MMIO
```

Example:

```text
Memory Map Viewer
Current PC = 0x0620 (1568) -> Program Code / BIO-OS Code Window
Current SP = 0x0FB0 (4016) -> BIO-OS Stack
```

This makes the BIO-OS demo easier to explain because the viewer can see where
code, stack, scratch memory, and MMIO live.

---

## 10. Screenshot Checklist

For portfolio screenshots, use `examples/debugger_showcase.zasm`.

Good moments to capture:

```text
1. ADD instruction
   - ALU, Flags, Writeback highlighted
   - Pipeline Timeline shows EXECUTE / WRITEBACK
   - Execution Detail Probe shows register result

2. STORE instruction
   - Memory/MMIO path highlighted
   - Memory Detail shows mutation

3. PUSH or POP instruction
   - Stack path highlighted
   - Recent Instruction Trace shows stack event

4. INT 80 instruction
   - InterruptCtl highlighted
   - Interrupt Detail shows software interrupt

5. Final HALT state
   - Recent Instruction Trace shows full ending flow
   - Memory Map Viewer shows PC/SP region context
```

Suggested screenshot title:

```text
Zero-CPU Studio v0.2 Visual Debugger
```

---

## 11. Implementation Map

Key files:

```text
include/zero_cpu/trace/TraceEvent.hpp
src/trace/TraceEvent.cpp
include/zero_cpu/core/CPU.hpp
src/core/CPU.cpp
studio/zero_studio.cpp
examples/debugger_showcase.zasm
```

Related docs:

```text
docs/visual-trace-event-model.md
docs/cpu-step-trace-recording.md
docs/studio-visual-datapath-panel.md
docs/studio-graphical-datapath-canvas.md
docs/studio-pipeline-timeline.md
docs/studio-execution-detail-probe.md
docs/studio-recent-instruction-trace.md
docs/studio-memory-map-viewer.md
docs/debugger-showcase-example.md
```

---

## 12. Current Limitations

This is still an early debugger.

Current limitations:

```text
- The datapath canvas shows only the latest instruction.
- Pipeline stages are derived from TraceEvent metadata, not from a true
  cycle-accurate microarchitecture.
- ALU operand display is currently inferred from state changes, not captured as
  explicit operand snapshots.
- There is no separate visual timeline widget yet.
- There is no interactive memory inspector grid yet.
```

These limitations are acceptable for v0.2 because the current goal is a clear
visual explanation layer for a custom virtual CPU.

---

## 13. Good Next Steps

Possible v0.3 directions:

```text
- explicit ALU operand/result capture
- interactive memory inspector
- visual timeline canvas
- breakpoints UI improvement
- source-line to TraceEvent mapping
- symbol/label viewer
- BIOS/kernel/user mode separation display
- export trace log to text/JSON
```

The most valuable next feature is likely explicit ALU operand/result capture,
because it would allow Studio to show:

```text
ALU_ADD
lhs = R1 before value
rhs = R2 or immediate value
result = R1 after value
```

That would make the debugger even better for computer architecture explanation.
