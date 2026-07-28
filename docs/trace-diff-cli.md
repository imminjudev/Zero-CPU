# Zero-CPU Trace Diff CLI

The Trace Diff CLI compares two schema v2 Zero-CPU trace files.

## Command

```text
zero_cli trace-diff <expected.json> <actual.json>
zero_cli trace-diff <expected.json> <actual.json> --strict
```

## Default comparison

Default mode compares architectural execution data:

```text
schema and schema version
execution mode
event count and order
instruction text
PC transitions
SP transitions
state_before
state_after
register changes
flag changes
memory changes
error state
```

It ignores producer-specific metadata and debugger presentation strings.

## Strict comparison

`--strict` compares the complete JSON document, including metadata and debugger
detail strings.

## Output

A mismatch reports the first structural JSON path:

```text
[FAIL] 3 trace difference(s); first at $.events[4].state_after.registers.R1
Expected: 12
Actual:   13
```

## Exit codes

```text
0 = traces match
1 = invalid command, unreadable file, malformed JSON, or unsupported schema
2 = valid traces differ
```

This command is the foundation for golden trace regression tests.
