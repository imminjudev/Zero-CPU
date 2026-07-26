# Studio Pipeline Timeline

Zero-CPU Studio now shows a pipeline-style timeline derived from the latest
`TraceEvent`.

## Purpose

The graphical datapath canvas shows which CPU blocks are active.

The pipeline timeline explains the same execution step as a classic
instruction-flow sequence:

```text
FETCH -> DECODE -> EXECUTE -> MEMORY -> WRITEBACK
```

## What it displays

For the latest trace event, Studio shows:

```text
- instruction
- action
- PC before / after
- fetch stage
- decode stage
- execute stage
- memory stage
- writeback stage
- flag changes
```

## Example

For:

```asm
ADD R1, R1
```

the timeline can show:

```text
[FETCH]     [ACTIVE] PC 2 reads instruction
[DECODE]    [ACTIVE] ADD R1, R1 -> opcode/operands
[EXECUTE]   [ACTIVE] ALU_ADD
[MEMORY]    [idle] no memory access
[WRITEBACK] [ACTIVE] R1: 10 -> 20
[FLAGS]     [ACTIVE] ZF: 1 -> 0
```

## Why this matters

The v0.2 debugger is not only showing final register values.

It now starts explaining how an instruction flows through a CPU-like execution
model.
