# Zero-CPU Documentation Index

Zero-CPU is a **verifiable and observable protected virtual computer platform**.

This index separates current platform documentation from older milestone notes.

## Start Here

```text
README.md
docs/project-overview.md
docs/architecture.md
docs/v2.0-demo-guide.md
docs/v2.0-design-decisions.md
docs/release-notes-v2.0.0.md
docs/syscall-convention.md
```

`README.md` is the portfolio-friendly entry point.

`docs/project-overview.md` describes the current architecture, protected runtime,
process model, hardware path, debugger, and verification layers.

`docs/architecture.md` is the current rendered platform diagram and boundary map.

`docs/v2.0-demo-guide.md` contains the timed 2–3 minute showcase presentation
and the committed portfolio screenshots.

`docs/v2.0-design-decisions.md` records the architectural tradeoffs and explicit
v2.0 limitations.

`docs/release-notes-v2.0.0.md` is the prepared final release-note body. It
remains release-candidate documentation until the v2.0-F regression and tag.

`docs/syscall-convention.md` defines both syscall layers that currently coexist:

```text
1. guest mini-kernel / BIO-OS services
2. protected host dispatcher services
```

## Current Architecture and Semantics

```text
docs/project-overview.md
docs/architecture.md
docs/architecture-roadmap.md
docs/semantics.md
docs/syscall-convention.md
docs/end-to-end-showcase.md
```

`docs/semantics.md` remains the detailed instruction/state-transition reference.
Where an older milestone note conflicts with the current overview or source,
the current source and current overview take precedence.

## Trace and Verification

```text
docs/trace-json-format-v2.md
docs/trace-diff-cli.md
docs/golden-trace-regression.md
```

The current implementation also includes multi-process trace export, invariant
verification, architectural/strict diffing, and protected-syscall semantic
events. Some filenames retain the milestone version in which they were first
introduced.

## Storage

```text
docs/zero-fs.md
```

ZeroFS is the small deterministic virtual filesystem backing the protected
platform. It is separate from the 4 KiB guest memory image and is intended for
protected filesystem syscalls and end-to-end showcase assets.

## Hardware

```text
docs/esp32-protected-gpio-demo.md
docs/roadmap-hardware.md
docs/hardware-abstraction-v0.6.md
docs/serial-hardware-v0.6.md
docs/windows-serial-hardware-v0.7.md
```

Current hardware architecture supports:

```text
MMIO hardware window
mock hardware bus
serial hardware protocol
Windows serial transport
physical ESP32-oriented bridge path
protected hardware syscalls
```

## Studio and Debugger History

```text
docs/studio-debugger-v0.2.md
docs/v0.3-debugger-showcase-guide.md
docs/studio-default-debugger-showcase.md

docs/v0.3-alu-trace-detail.md
docs/v0.3-memory-trace-detail.md
docs/v0.3-stack-trace-detail.md
docs/v0.3-control-flow-trace-detail.md

docs/v0.4-trace-export-json.md
docs/v0.4-breakpoint-polish.md
docs/v0.4-trace-filter.md
docs/v0.4-watch-expressions.md
```

These documents are historical design records for the Studio layers. The current
Studio additionally supports:

```text
DebugSession backend delegation
MultiProcessDebugSession backend delegation
PID selection
source-line stepping/highlighting
conditional breakpoints
watchpoints
debug snapshots
protected syscall observations
```

## Historical Roadmaps and Releases

```text
docs/roadmap-v0.4.md
docs/release-notes-v0.3.md
docs/release-notes-v0.4.md
```

These files describe older milestones and should not be read as the current
project roadmap.

Current direction is summarized in the root README and project overview.

## Current Platform Snapshot

```text
.zasm
  → Assembler
  → .zbin + .zsym
  → Loader
  → Protected CPU
  → Processes / Address Spaces
  → Timer Preemption / Scheduler
  → Syscalls / MMIO / Hardware
  → Debugger
  → Trace Verification
  → Zero Studio
```

Completed platform milestone:

```text
v1.9 end-to-end protected showcase
```

Current release phase:

```text
v2.0.0 verified release candidate
final 73-stage regression PASS
publication pending: tag + GitHub Release
```

Current regression baseline:

```text
73 stages
All Zero-CPU tests passed.
```

## Documentation Policy

The root README should remain concise and portfolio-friendly.

Detailed architectural rules belong under `docs/`.

Milestone-specific documents may remain for history, but new current-state
documentation must clearly distinguish:

```text
implemented behavior
historical behavior
future direction
```

<!-- Patch: v1.6-docs-current-platform-r1 -->

<!-- Patch: v1.6-current-roadmap-semantics-r1 -->

<!-- Patch: v1.7-protected-esp32-gpio-demo-r1 -->

<!-- Patch: v1.8-zero-fs-core-r1 -->

<!-- Patch: v2.0-productization-docs-r1 -->

<!-- Patch: v2.0-current-architecture-diagram-r1 -->

<!-- Patch: v2.0-demo-guide-r1 -->

<!-- Patch: v2.0-release-docs-r1 -->

<!-- Patch: v2.0-final-verification-r1 -->
