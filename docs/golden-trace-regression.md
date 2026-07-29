# Zero-CPU Golden Trace Regression

This is the first committed golden trace regression in the v0.5 verification layer.

The test runs `tests/golden/trace_smoke.zasm`, writes a fresh trace to
`build/test-output/trace_smoke_actual.json`, and compares it with the committed
`tests/golden/trace_smoke.json` fixture.

The smoke program covers instruction order, PC transitions, register writeback,
ALU result propagation, memory mutation, and HALT state.

Run it with:

```text
zero_cli trace-golden-test
```

Do not update the fixture only to make a failure disappear. Review whether the
change is intentional, update `docs/semantics.md` when architecture changes, and
commit the implementation, semantics, and fixture together.
