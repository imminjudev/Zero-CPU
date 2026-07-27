# Studio Default Debugger Showcase

Zero-CPU Studio now defaults to the debugger showcase example.

## Previous Default

```text
examples\function_call.zasm
examples\function_call.zbin
```

This was useful for early CALL/RET and stack testing, but it does not exercise
the full Studio visual debugger.

## New Default

```text
examples\debugger_showcase.zasm
examples\debugger_showcase.zbin
```

The debugger showcase is better for Studio v0.3 because it exercises:

```text
ALU operations
Memory LOAD/STORE
Stack PUSH/POP
CALL/RET
MMIO write
INT 80 syscall
HALT
```

## Recommended Studio Flow

```text
Load Source
Assemble
Load BIN
Step
```

Then inspect:

```text
Visual Datapath Panel
Pipeline Timeline
Execution Detail Probe
Recent Instruction Trace
Memory Map Viewer
Graphical Datapath Canvas
```

## Path Note

Studio accepts relative paths such as:

```text
examples\debugger_showcase.zasm
examples\debugger_showcase.zbin
```

These work when Studio is launched from the repository root:

```bat
cd /d D:\Zero-CPU
.\build\Debug\zero_studio.exe
```

If Studio is launched from another working directory, use absolute paths:

```text
D:\Zero-CPU\examples\debugger_showcase.zasm
D:\Zero-CPU\examples\debugger_showcase.zbin
```

This avoids file open errors caused by the process working directory.
