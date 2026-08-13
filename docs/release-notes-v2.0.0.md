# Zero-CPU v2.0.0 Release Notes

Verification status: **final v2.0-F regression passed — 73 stages, all
Zero-CPU tests passed.**

This document is the publication source for the `v2.0.0` tag's GitHub Release.

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

The final clean regression pass was completed before release publication.

## Regression Baseline

The verified v2.0.0 release baseline is:

```text
73 stages
All Zero-CPU tests passed.
```

The release tag must point to the verified release commit containing the same
runtime/code baseline.

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
v2.0-F  final 73-stage regression PASS; publication remains
```

## Publication

The verified release commit should be published by:

```text
1. create annotated tag v2.0.0 on the verified release commit
2. push v2.0.0 to origin
3. create the GitHub Release from tag v2.0.0
4. use this document as the release-note source
```

No additional machine subsystem or feature work is part of v2.0.

<!-- Patch: v2.0-release-notes-r1 -->

<!-- Patch: v2.0-final-verification-r1 -->
