# Zero-CPU v0.3 Release Notes

Zero-CPU v0.3 focuses on improving the Studio visual debugger.

The main goal of this release is to move Studio from a basic execution viewer
toward an instruction execution explainer.

## Highlights

```text
- ALU Operand/Result Detail
- Memory Operand Trace Detail
- Stack Trace Detail
- Control Flow Trace Detail
- Studio default example changed to debugger_showcase
- v0.3 debugger showcase guide
- README documentation for Studio debugger v0.3
```

## Studio Debugger v0.3

Zero-CPU Studio now explains more of what happens inside each instruction.

The v0.2 debugger made CPU execution visible through:

```text
- Visual Datapath Panel
- Graphical Datapath Canvas
- Pipeline Timeline
- Execution Detail Probe
- Recent Instruction Trace
- Memory Map Viewer
```

v0.3 extends that foundation with deeper instruction-level details.

## 1. ALU Operand/Result Detail

ALU operations now show operand snapshots and result values.

Example:

```asm
ADD R1, R2
```

The debugger can explain:

```text
lhs R1 = 10
rhs R2 = 20
result -> R1 = 30
```

This makes arithmetic and logical operations easier to understand while stepping.

Covered operations include:

```text
ADD
SUB
MUL
DIV
CMP
TEST
AND
OR
XOR
NOT
```

## 2. Memory Operand Trace Detail

Memory operations now show address, route, value, and destination information.

Example:

```asm
STORE [180], R1
```

The debugger can explain:

```text
operation = MEMORY_WRITE
address [180] = 180
route = RAM
value R1 = 30
```

For MMIO writes, the route is also explained:

```text
route = DebugOutput MMIO
```

This helps distinguish normal RAM access from MMIO device access.

## 3. Stack Trace Detail

Stack operations now show the stack address, SP movement, value transfer, and
return-address behavior.

Example:

```asm
PUSH R1
```

The debugger can explain:

```text
operation = STACK_PUSH
stack address = 2048
SP = 2048 -> 2056
value = 60
```

`POP`, `CALL`, and `RET` are also covered.

Zero-CPU currently uses an upward-growing stack:

```text
PUSH/CALL:
  Memory[SP] = value
  SP = SP + 8

POP/RET:
  SP = SP - 8
  value = Memory[SP]
```

## 4. Control Flow Trace Detail

Control-flow instructions now explain PC movement, branch decisions, targets,
return addresses, and interrupt vectors.

Examples:

```asm
CALL double_value
RET
INT 80
```

The debugger can explain:

```text
from PC = ...
to PC = ...
taken = true
target = ...
return address = ...
vector = 80
```

Conditional branches include condition information:

```text
condition = ZF == 1
taken = false
fallthrough = ...
```

Covered control-flow instructions include:

```text
JMP
JE
JNE
JG
JL
CALL
RET
INT
IRET
```

## 5. Studio Default Example

Studio now defaults to the debugger showcase example:

```text
examples\debugger_showcase.zasm
examples\debugger_showcase.zbin
```

This makes it easier to open Studio and immediately test the v0.3 debugger.

## 6. v0.3 Debugger Showcase Guide

A new guide explains how to use the showcase program to verify each debugger
feature.

```text
docs/v0.3-debugger-showcase-guide.md
```

The guide covers:

```text
- where to see ALU Detail
- where to see Memory Detail
- where to see Stack Detail
- where to see Control Flow Detail
- good screenshot points
- portfolio explanation text
- current limitations
```

## How to Try It

From the repository root:

```bat
cd /d D:\Zero-CPU
cmake --build build
.\build\Debug\zero_studio.exe
```

Studio should default to:

```text
examples\debugger_showcase.zasm
examples\debugger_showcase.zbin
```

Recommended flow:

```text
Load Source
Assemble
Load BIN
Step
```

Then watch:

```text
Execution Detail Probe
Graphical Datapath Canvas
Pipeline Timeline
Recent Instruction Trace
Memory Map Viewer
```

## Recommended Screenshot Points

Good v0.3 screenshots:

```text
1. ADD R1, R2
   - ALU Detail visible
   - lhs / rhs / result shown

2. STORE [180], R1
   - Memory Detail visible
   - route = RAM

3. PUSH R1 or POP R3
   - Stack Detail visible
   - SP movement shown

4. CALL / RET / INT 80
   - Control Flow Detail visible
   - target / return address / vector shown
```

## Documentation Added or Updated

```text
README.md
docs/v0.3-debugger-showcase-guide.md
docs/v0.3-alu-trace-detail.md
docs/v0.3-memory-trace-detail.md
docs/v0.3-stack-trace-detail.md
docs/v0.3-control-flow-trace-detail.md
docs/studio-default-debugger-showcase.md
```

## Known Limitations

v0.3 details are reconstructed from `TraceEvent` before/after snapshots.

Known limitations:

```text
- It is instruction-level tracing, not cycle-accurate hardware tracing.
- Label names and target addresses are not always both available.
- INT/IRET are not yet displayed as a full interrupt-frame visualizer.
- Stack behavior is explained at instruction granularity, not micro-op granularity.
- There is no breakpoint / step-over / step-out support yet.
```

## Next Possible Work

Good follow-up work for v0.4:

```text
- Breakpoints
- Step over / step out
- Source line to PC mapping
- Trace filtering/search
- Export trace to JSON
- Full interrupt frame visualizer
- Watch expressions
```

## Summary

Zero-CPU v0.3 makes Studio more useful as a systems-learning debugger.

Instead of only showing that a component was active, Studio can now explain
what each major instruction category did:

```text
ALU    -> operands and result
Memory -> address, route, and value
Stack  -> stack address, SP movement, and return address
Control Flow -> PC movement, target, condition, vector, and return address
```
