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

Golden-trace locking and debugger/Studio presentation are the next v1.9 steps.

<!-- Patch: v1.9-end-to-end-showcase-r1 -->
