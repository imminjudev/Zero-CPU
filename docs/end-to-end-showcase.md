# End-to-End Showcase

v1.9 combines existing Zero-CPU subsystems into one compact demonstration.

```text
PID 1
  ZeroFS FS_READ
  timer preemption
  survives PID 2 fault
  ZeroFS FS_WRITE
  normal exit

PID 2
  protected HW_WRITE -> mock GPIO 42
  direct User-mode MMIO write
  protection fault
```

Both workloads are real `.zasm` programs. The focused test assembles them to
`.zbin`, loads them through the protected multi-process runtime, and leaves:

```text
build/showcase/showcase_fs_worker.zbin
build/showcase/showcase_hardware_fault.zbin
build/showcase/showcase_trace.json
```

The test verifies:

```text
independent protected processes
timer preemption/context switching
ZeroFS read/write through INT 80
protected hardware syscall
one-process protection fault
survivor progress after the fault
semantic syscall observations
multi-process invariants
trace JSON export
```

The deterministic trace is locked against `tests/golden/end_to_end_showcase.json` and the showcase is part of the full regression suite. Debugger/Studio presentation is the next v1.9 step.

<!-- Patch: v1.9-end-to-end-showcase-r1 -->

<!-- Patch: v1.9-showcase-golden-regression-r1 -->
