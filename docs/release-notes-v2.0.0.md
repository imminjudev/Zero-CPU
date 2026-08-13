# Zero-CPU v2.0.0 Release Notes

Status: **release-candidate notes prepared; tag/release is created only after the
final v2.0-F regression pass.**

Zero-CPU v2.0.0 is the productization release of the protected virtual-computer
platform completed by the v1.9 end-to-end showcase.

## Release Summary

Zero-CPU combines:

```text
custom ISA + assembler
versioned .zbin executable format
binary/process loader
User / Kernel protection
independent processes and address spaces
timer-driven preemptive scheduling
fault isolation
protected syscalls
MMIO devices
mock / serial hardware abstraction
ZeroFS protected storage
source-aware debugging
multi-process debugging
Zero Studio
structured trace verification
invariant checks
golden trace regression
```

The release focus is not a larger feature list. It is a clear and reproducible
proof that these components work together in one protected execution.

## Canonical Showcase

After building:

```bat
.\build\Debug\zero_cli.exe showcase
```

The command runs two real assembly programs as independent processes.

```text
PID 1
  → FS_READ
  → survives PID 2 fault
  → FS_WRITE
  → ZeroFS HELLO → HELLOHELLO
  → exit 0

PID 2
  → protected HW_WRITE
  → Mock GPIO[0] = 42
  → illegal direct User-mode access to 0xF200 MMIO
  → protection fault
  → isolated termination
```

Timer quantum `1` forces observable preemption and context switching during the
scenario.

The same core scenario is consumed by the focused showcase regression and is
presented interactively through Zero Studio.

## Verification

The showcase verifies more than final output.

It checks:

```text
exactly two processes
exactly one isolated process fault
survivor exit code 0
ZeroFS final content HELLOHELLO
Mock GPIO[0] = 42
timer preemption / context switching
protected FS_READ / FS_WRITE / HW_WRITE / EXIT observations
post-fault survivor execution
multi-process invariants
deterministic JSON trace
golden trace regression
```

The canonical trace artifact is written to:

```text
build/showcase/showcase_trace.json
```

The committed golden reference is:

```text
tests/golden/end_to_end_showcase.json
```

## Public Presentation

CLI:

```bat
.\build\Debug\zero_cli.exe showcase
```

Studio:

```bat
.\build\Debug\zero_studio.exe
```

Then:

```text
Load Showcase
Run once   → intentional PID 2 protection fault
Run again  → PID 1 survivor completes
```

Portfolio screenshots and the timed presentation script are in:

```text
docs/assets/v2.0/
docs/v2.0-demo-guide.md
```

## Architecture Decisions

The main v2.0 design choices are documented in:

```text
docs/v2.0-design-decisions.md
```

Key decisions include:

```text
.zasm → .zbin → loader → CPU as the canonical executable path
core/runtime owns machine semantics
CLI and Studio consume the same execution/debugger behavior
INT 80 as the protected User→Kernel service boundary
explicit independent process state and fault isolation
simple deterministic timer-driven round-robin scheduling
minimal bounded ZeroFS syscall surface
mock hardware as the deterministic release baseline
structured observability as part of correctness
invariants + golden traces as complementary verification
feature freeze after the integrated protected showcase
```

## Build

Windows Debug build:

```bat
cmake -S . -B build
cmake --build build --config Debug
```

Full local regression:

```bat
scripts\test_all.bat
```

v2.0-F will create the release tag only after the final clean regression pass.

## Regression Baseline

The release candidate baseline is:

```text
73 stages
All Zero-CPU tests passed.
```

The final v2.0.0 tag must point to a commit that passes the same full regression
suite.

## Known Limitations

Zero-CPU v2.0 is intentionally compact.

Not included:

```text
x86 / ARM / RISC-V compatibility
Linux compatibility
paging / demand virtual memory
cache / pipeline / branch-prediction simulation
priority or multicore scheduling
general-purpose filesystem expansion
network stack
GPU / 3D
browser/Web platform
```

The protected host syscall surface is small, ZeroFS is deterministic rather than
general-purpose, and the mock hardware path is the canonical reproducible
release target.

See `docs/v2.0-design-decisions.md` for the detailed boundary.

## v2.0 Productization Work

```text
v2.0-A  documentation consistency
v2.0-B  current architecture diagram
v2.0-C  single-command showcase entry point
v2.0-D  2–3 minute demo guide and portfolio screenshots
v2.0-E  design decisions, limitations, and release notes
v2.0-F  final regression, v2.0.0 tag, GitHub Release
```

At the end of v2.0-E, only v2.0-F remains.

## Release Checklist

Before creating `v2.0.0`:

```text
[ ] working tree clean
[ ] full 73-stage regression passes
[ ] public zero_cli showcase passes
[ ] README/current docs match release state
[ ] tag points to the final release commit
[ ] GitHub Release body matches these notes
```

The checklist is intentionally left unchecked until v2.0-F is performed.

<!-- Patch: v2.0-release-notes-r1 -->
