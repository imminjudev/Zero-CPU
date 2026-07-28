# Zero-CPU Trace JSON Format v2

This document defines the JSON trace format used by the v0.5 trace-based
verification layer.

The format is produced by the shared `TraceJsonWriter` component. Studio no
longer owns a private JSON serializer.

## 1. Schema Identity

```json
{
  "schema": "zero_cpu_trace",
  "schema_version": 2
}
```

Consumers must verify both fields before interpreting a trace.

A consumer should reject unsupported schema versions instead of silently
guessing field behavior.

## 2. Top-Level Object

The top-level object contains:

```text
schema
schema_version
producer
producer_version
mode
loaded_path
event_count
events
```

Example:

```json
{
  "schema": "zero_cpu_trace",
  "schema_version": 2,
  "producer": "zero_studio",
  "producer_version": "v0.5-dev",
  "mode": "Binary",
  "loaded_path": "examples\\debugger_showcase.zbin",
  "event_count": 4,
  "events": []
}
```

`producer_version` and `loaded_path` are informational. They should normally be
ignored by architectural trace comparisons.

## 3. Event Object

Every event contains:

```text
index
pc_before
pc_after
sp_before
sp_after
instruction
state_before
state_after
register_changes
flag_changes
memory_changes
stage
action
datapath
alu_detail
memory_detail
stack_detail
control_flow_detail
compact
full
has_error
error
```

Events are ordered by execution step.

## 4. Architectural State

`state_before` and `state_after` contain:

```text
pc
sp
halted
has_error
error_message
registers
flags
```

Registers use stable `R0` through `R7` keys.

Flags contain:

```text
zero
sign
carry
overflow
raw
```

Memory is not exported as a complete snapshot for every event. Event-local
mutations are recorded in `memory_changes`.

## 5. Structured Change Arrays

Register change:

```json
{
  "name": "R1",
  "before": 5,
  "after": 12
}
```

Flag change:

```json
{
  "name": "ZF",
  "before": true,
  "after": false
}
```

Memory change:

```json
{
  "address": 180,
  "before": 0,
  "after": 12
}
```

## 6. Debugger Detail Fields

The human-readable fields remain available:

```text
stage
action
datapath
alu_detail
memory_detail
stack_detail
control_flow_detail
compact
full
```

They are useful for explanation but should not be the only regression oracle.

## 7. Canonical Comparison Policy

The first Trace Diff CLI should compare by default:

```text
schema
schema_version
event_count
instruction
PC transition
SP transition
state_after
register_changes
flag_changes
memory_changes
has_error
error
```

It should ignore by default:

```text
producer
producer_version
loaded_path
```

Debugger text should be optional strict-mode data.

## 8. Migration from Schema v1

The v0.4 Studio exporter used:

```text
schema = zero_cpu_studio_trace
schema_version = 1
```

Schema v2 uses:

```text
schema = zero_cpu_trace
schema_version = 2
```

and adds a shared core serializer, full register/flag state, structured mutation
arrays, and SP transitions.

New verification tools should use schema v2.
