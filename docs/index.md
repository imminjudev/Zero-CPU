# Zero-CPU Documentation Index

This page collects the detailed Zero-CPU documentation.

The README is intentionally short. Detailed design notes, release notes, and
debugger documents live here.

## Project Overview

```text
docs/project-overview.md
```

This file preserves the original long-form README content and gives a fuller
overview of the project.

## Current Roadmap

```text
docs/roadmap-v0.4.md
```

## Architectural Semantics

```text
docs/semantics.md
```

This document defines the current CPU state transitions and instruction behavior
used as the baseline for trace-based verification.

## Trace JSON Format

```text
docs/trace-json-format-v2.md
```

This document defines the structured schema used by trace export, regression
fixtures, and the upcoming Trace Diff CLI.

## Studio Debugger

```text
docs/studio-debugger-v0.2.md
docs/v0.3-debugger-showcase-guide.md
docs/studio-default-debugger-showcase.md
```

## v0.3 Debugger Detail Layers

```text
docs/v0.3-alu-trace-detail.md
docs/v0.3-memory-trace-detail.md
docs/v0.3-stack-trace-detail.md
docs/v0.3-control-flow-trace-detail.md
```

## v0.4 Debugger Usability Layer

```text
docs/v0.4-trace-export-json.md
docs/v0.4-breakpoint-polish.md
docs/v0.4-trace-filter.md
docs/v0.4-watch-expressions.md
```

## Release Notes

```text
docs/release-notes-v0.3.md
docs/release-notes-v0.4.md
```

## Suggested Future Documentation

These are good next documentation targets:

```text
docs/isa-reference.md
docs/syscall-reference.md
docs/memory-map.md
docs/design-decisions.md
docs/research/trace-based-verification.md
```

## Documentation Policy

The root README should stay short and portfolio-friendly.

Detailed explanations should go under `docs/`.
