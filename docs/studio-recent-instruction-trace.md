# Studio Recent Instruction Trace

Zero-CPU Studio now shows a compact history of recently executed instructions.

## Purpose

The Visual Datapath Panel, Pipeline Timeline, and Execution Detail Probe explain
the latest instruction.

The Recent Instruction Trace shows how the CPU reached the current state.

## What it displays

Studio shows up to the latest 16 trace events:

```text
Recent Instruction Trace
Showing last 16 of 183 events

[167] PC 1488 -> 1504 | LOAD R6, [61712] | MEMORY_READ
[168] PC 1504 -> 1520 | STORE [224], R6 | MEMORY_WRITE
[169] PC 1520 -> 1568 | IRET | INTERRUPT_RETURN
[170] PC 1568 -> 1568 | HALT | HALT
```

Each row includes:

```text
- trace event index
- PC before / after
- instruction text
- visual action
- error message, when present
```

## Why this matters

The debugger now explains both:

```text
- what the latest instruction did
- how the CPU execution arrived there
```

This makes Studio more useful for debugging, demos, and portfolio screenshots.
