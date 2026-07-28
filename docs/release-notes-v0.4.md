# Zero-CPU v0.4 Release Notes

Zero-CPU v0.4 focuses on Studio debugger usability and portfolio presentation.

v0.3 made instruction execution explainable.

v0.4 makes those explanations easier to export, search, inspect, and present.

## Theme

```text
Studio Debugger Usability Layer
```

## Highlights

```text
- Studio trace export to JSON
- Breakpoint behavior polish
- Trace search/filter
- Watch expressions panel
- Portfolio-ready README
- Documentation index
- GitHub Actions CI
- MIT license
- Screenshot directory placeholder
```

---

## 1. Trace Export to JSON

Studio can now export recorded `TraceEvent` data to JSON.

Default output path:

```text
traces\studio_trace_export.json
```

The exported trace includes:

```text
schema
schema_version
studio_version
mode
loaded_path
event_count
events
```

Each event includes:

```text
index
pc_before
pc_after
instruction
stage
action
datapath
changed_register_count
changed_flag_count
changed_memory_count
alu_detail
memory_detail
stack_detail
control_flow_detail
compact
full
has_error
error
```

Why it matters:

```text
Trace data is no longer trapped inside Studio.
It can now be inspected, archived, compared, and later used for verification.
```

---

## 2. Breakpoint Polish

Studio breakpoint behavior was improved.

Improvements:

```text
- sorted breakpoint list
- duplicate breakpoint detection
- current PC marker
- last breakpoint hit status
- reset/clear behavior cleanup
```

Example:

```text
Breakpoints
[0] PC = 536
[1] PC = 656  <current>

Last Breakpoint Hit = PC 656
```

Why it matters:

```text
Breakpoints now behave more like a real debugger feature instead of a basic
stop-list.
```

---

## 3. Trace Search / Filter

Studio now supports simple trace filtering.

Example filters:

```text
CALL
R1
STACK_PUSH
MEMORY_WRITE
SOFTWARE_INTERRUPT
ALU_ADD
INT
```

The filter matches against trace event text including:

```text
instruction
action
datapath
ALU detail
Memory detail
Stack detail
Control Flow detail
compact TraceEvent string
error message
```

Why it matters:

```text
v0.3 made trace events rich.
v0.4 makes those rich trace events searchable.
```

---

## 4. Watch Expressions

Studio now has a Watch Expressions panel.

The first version uses a fixed default watch set:

```text
PC
SP
Halted
Flags
R0
R1
R2
R3
R4
R7
Memory[180]
Memory[188]
Memory[196]
Memory[204]
Memory[212]
Memory[2048]
Memory[2056]
Memory[2064]
DebugOutput writes
DebugOutput ASCII
Timer tick
Timer interrupts
```

Why it matters:

```text
Frequently inspected values are now grouped in one place while stepping.
```

---

## 5. Portfolio Presentation Polish

The root README was shortened and made portfolio-friendly.

The previous long-form README content was moved to:

```text
docs/project-overview.md
```

New documentation index:

```text
docs/index.md
```

New screenshot placeholder directory:

```text
screenshots/README.md
```

New license:

```text
LICENSE
```

New GitHub Actions workflow:

```text
.github/workflows/ci.yml
```

Why it matters:

```text
The repository now has a better first impression for reviewers, recruiters,
and visitors.
```

---

## 6. CI

A Windows GitHub Actions workflow was added.

The workflow runs:

```text
cmake -S . -B build
scripts\test_all.bat
```

This verifies the existing build and test flow on push and pull request.

---

## How to Try v0.4

Build:

```bat
cmake -S . -B build
cmake --build build
```

Run tests:

```bat
scripts\test_all.bat
```

Run Studio:

```bat
.\build\Debug\zero_studio.exe
```

Recommended Studio flow:

```text
1. Load Source
2. Assemble
3. Load BIN
4. Step through examples\debugger_showcase.zasm
5. Try Trace Filter
6. Try Export Trace
7. Inspect Watch Expressions
```

---

## Documentation Added or Updated

```text
README.md
docs/index.md
docs/project-overview.md
docs/v0.4-trace-export-json.md
docs/v0.4-breakpoint-polish.md
docs/v0.4-trace-filter.md
docs/v0.4-watch-expressions.md
docs/roadmap-v0.4.md
LICENSE
.github/workflows/ci.yml
screenshots/README.md
```

---

## Known Limitations

```text
- Studio remains Windows/Win32 focused.
- Trace export path is currently fixed.
- Trace filter is text-based and does not support regex or structured queries.
- Watch expressions are fixed, not user-editable yet.
- Source line to PC mapping is not implemented yet.
- Step over / step out is not implemented yet.
- Pipeline simulation is not implemented yet.
```

---

## Next Direction

Recommended next milestone:

```text
v0.5 Trace-Based Verification Layer
```

Candidate v0.5 work:

```text
- instruction semantics documentation
- golden trace regression tests
- trace diff CLI
- invariant checker
- deterministic replay check
- benchmark suite v1
```

Longer-term directions:

```text
- 5-stage pipeline simulation
- hazard visualization
- branch prediction
- cache simulation
- timer-based scheduling
- interrupt vector table improvements
```

---

## Summary

v0.4 turns Zero-CPU Studio into a more usable debugger.

```text
v0.3 = explain each instruction
v0.4 = search, watch, export, and present those explanations
```
