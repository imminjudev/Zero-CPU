# Zero-CPU v0.4 Roadmap

Zero-CPU v0.4 focuses on debugger usability.

v0.3 added instruction detail layers:

```text
ALU Detail
Memory Detail
Stack Detail
Control Flow Detail
```

v0.4 should make those details easier to navigate, inspect, export, and use
during real debugging sessions.

## Theme

```text
Studio Debugger Usability Layer
```

The goal is to move Studio from:

```text
"this instruction is explained"
```

toward:

```text
"I can actually debug a program with this tool"
```

## Candidate Features

## 1. Trace Export to JSON

Export the recorded `TraceEvent` list to a JSON file.

This should include:

```text
pc_before
pc_after
instruction
action
datapath
changed_registers
changed_flags
changed_memory
alu_detail
memory_detail
stack_detail
control_flow_detail
error
```

Why it matters:

```text
- makes traces inspectable outside Studio
- enables portfolio screenshots plus real data
- makes future testing easier
- enables trace diffing later
```

Possible UI:

```text
[Export Trace]
```

Possible output:

```text
traces/debugger_showcase_trace.json
```

Recommended priority:

```text
High
```

## 2. Trace Search / Filter

Allow the user to filter recent trace events by:

```text
instruction text
opcode/action
changed register
memory address
datapath node
error state
```

Examples:

```text
filter: ALU_ADD
filter: R1
filter: Memory[180]
filter: STACK_PUSH
filter: SOFTWARE_INTERRUPT
```

Why it matters:

```text
v0.3 generates rich trace data, but users need a way to find important events.
```

Recommended priority:

```text
Medium
```

## 3. Source Line to PC Mapping

Connect `.zasm` source lines to generated binary PC addresses.

Desired behavior:

```text
current PC -> source line highlight
source line -> approximate PC
```

Why it matters:

```text
The debugger becomes much easier to understand when the current instruction is
linked back to the original assembly source.
```

Potential challenge:

```text
The assembler/binary writer may need to preserve source location metadata.
```

Recommended priority:

```text
High, but more invasive
```

## 4. Step Over / Step Out

Add debugger commands for function-aware stepping.

Desired behavior:

```text
Step Over:
  execute CALL without entering function body

Step Out:
  continue until current function returns
```

Why it matters:

```text
CALL/RET are already visible in v0.3.
Step over/out would make function debugging practical.
```

Potential challenge:

```text
Requires reliable CALL/RET tracking and probably temporary breakpoints.
```

Recommended priority:

```text
Medium
```

## 5. Breakpoint Improvements

Studio already has breakpoint UI concepts.

v0.4 can improve them by documenting and strengthening:

```text
breakpoint list display
run until breakpoint
breakpoint hit message
invalid breakpoint validation
clear one breakpoint
clear all breakpoints
```

Why it matters:

```text
Breakpoints are one of the first features expected from a debugger.
```

Recommended priority:

```text
Medium
```

## 6. Watch Expressions

Allow the user to watch selected values while stepping.

Possible watches:

```text
R1
R2
SP
PC
Memory[180]
Memory[2048]
DebugOutput MMIO
Timer tick count
```

Why it matters:

```text
The current memory/register view is broad.
Watch expressions would make repeated inspection easier.
```

Recommended priority:

```text
Medium/Low
```

## 7. Full Interrupt Frame Visualizer

v0.3 has basic `INT` / `IRET` control-flow detail.

A v0.4 or later improvement could display the full interrupt frame:

```text
saved return PC
saved flags
interrupt vector
handler PC
R0 vector write
stack frame layout
```

Why it matters:

```text
This would make the interrupt system much easier to explain.
```

Recommended priority:

```text
Medium
```

## Recommended v0.4 Order

Suggested implementation order:

```text
1. Trace Export to JSON
2. Breakpoint polish
3. Trace Search / Filter
4. Source line to PC mapping
5. Step over / Step out
6. Watch expressions
7. Full interrupt frame visualizer
```

## First v0.4 Target

The best first task is:

```text
Trace Export to JSON
```

Reason:

```text
- low risk
- uses existing v0.3 TraceEvent data
- improves portfolio value immediately
- does not require CPU execution refactoring
- prepares for future trace diff/testing tools
```

## Expected v0.4 Result

By the end of v0.4, Studio should be able to answer:

```text
What happened?
Where did it happen?
Why did PC move?
Which values changed?
Can I find the event again?
Can I export the trace?
Can I stop at the important instruction?
```

## Non-Goals for v0.4

Avoid over-expanding the project.

Not required for v0.4:

```text
cycle-accurate hardware simulation
full IDE integration
graphical source editor rewrite
complex timeline animation
advanced symbolic debugging
```

## Summary

v0.3 made instruction execution explainable.

v0.4 should make instruction execution navigable.

```text
v0.3 = explain each instruction
v0.4 = help the user debug with those explanations
```
