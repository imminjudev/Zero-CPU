# Debugger Showcase Example

`examples/debugger_showcase.zasm` is designed for demonstrating the Zero-CPU
Studio v0.2 visual debugger.

## Purpose

Most examples focus on one feature.

This example intentionally walks through several CPU behaviors so Studio can
show a rich debugger state while stepping instruction by instruction.

## Demonstrated behavior

```text
- ALU datapath
- flag updates
- memory write/read
- stack PUSH/POP
- CALL/RET
- DebugOutput MMIO
- INT 80 syscall
- HALT
```

## Recommended Studio flow

```text
1. Open Zero-CPU Studio.
2. Load examples/debugger_showcase.zasm into the source editor.
3. Click Assemble.
4. Click Load Binary.
5. Step through slowly.
```

Watch these Studio sections:

```text
- Visual Datapath Panel
- Pipeline Timeline
- Execution Detail Probe
- Recent Instruction Trace
- Memory Map Viewer
- Graphical Datapath Canvas
```

## Expected checkpoints

```text
Memory[180] = 30
Memory[188] = 60
Memory[196] = 60
Memory[204] = 123
Memory[212] = 90
```

Debug output behavior:

```text
- direct MMIO write emits ASCII 'Z'
- INT 80 syscall 1 emits ASCII 'A'
```

## Why this example exists

This is primarily a portfolio/demo example.

It makes the Studio debugger show different CPU paths:

```text
ADD/SUB       -> ALU, flags, writeback
STORE/LOAD    -> memory path
PUSH/POP      -> stack path
CALL/RET      -> control flow and stack behavior
STORE [61440] -> DebugOutputDevice MMIO
INT 80        -> software interrupt / syscall path
HALT          -> final stop state
```

## CLI smoke check

After building:

```bat
cmake --build build
```

You can assemble and run the example with the current CLI flow used by the
project.

The most important check is that the example assembles and reaches `HALT`
without CPU error.
