# Studio Execution Detail Probe

Zero-CPU Studio now shows an execution detail probe derived from the latest
`TraceEvent`.

## Purpose

The graphical datapath canvas shows active CPU blocks.

The pipeline timeline shows the broad instruction flow:

```text
FETCH -> DECODE -> EXECUTE -> MEMORY -> WRITEBACK
```

The execution detail probe explains what happened inside the important parts of
that step.

## Sections

```text
Execution Detail Probe
- ALU Detail
- Memory Detail
- Interrupt Detail
- Writeback Summary
```

## ALU Detail

When the latest trace event activates the ALU, Studio shows the operation name
and the resulting register/flag changes.

Example:

```text
ALU Detail
  Active = true
  Operation = ALU_ADD
  Register Result
    R1: 10 -> 20
  Flag Result
    ZF: 1 -> 0
```

## Memory Detail

When the latest trace event uses memory, MMIO, or stack routing, Studio shows
the route and memory mutations.

Reads may activate the memory route without mutating memory.

## Interrupt Detail

For software interrupt, hardware interrupt, and interrupt-return flow, Studio
shows the interrupt kind and the instruction/action that triggered it.

## Why this matters

This turns Studio from a state viewer into a CPU-execution explainer.

A portfolio viewer can now see not only that the CPU state changed, but which
part of the virtual CPU caused the change.
