# Studio Memory Map Viewer

Zero-CPU Studio now shows a memory map viewer inside the CPU/Register/Memory
state panel.

## Purpose

Zero-CPU now has enough system-level behavior that raw addresses need context.

The memory map viewer explains where the current PC and SP are located and
summarizes the important memory regions used by binary execution, BIO-OS,
stack, and MMIO devices.

## What it displays

```text
Memory Map Viewer
Current PC = 0x0620 (1568) -> Program Code / BIO-OS Code Window
Current SP = 0x0FB0 (4016) -> BIO-OS Stack
```

It also lists:

```text
Core RAM Layout
- Low Data / Scratch
- Default Program Code Window
- Default Stack Base
- BIO-OS Combined Code Window
- BIO-OS Stack Window
- reserved gap outside default RAM

MMIO Layout
- DebugOutputDevice
- TimerDevice

Timer MMIO Registers
- tick_count
- interval
- enabled
- vector
- payload
- interrupt_count
```

## Memory map constants

The viewer uses the same constants from:

```cpp
include/zero_cpu/core/MemoryMap.hpp
```

Important addresses:

```text
0x0000..0x01FF Low Data / Scratch
0x0200         Binary code base
0x0800         Default stack base
0x0FA0         BIO-OS stack base
0x1000         Default memory size boundary
0xF000..0xF00F DebugOutputDevice MMIO
0xF100..0xF12F TimerDevice MMIO
```

## Why this matters

This makes BIO-OS execution easier to explain.

A viewer can see that the program counter is executing in the code region while
the stack pointer is kept high in the BIO-OS stack window to avoid code/stack
overlap.
