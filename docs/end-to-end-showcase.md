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

The deterministic trace is locked against
`tests/golden/end_to_end_showcase.json` and the showcase is part of the full
regression suite.

## Studio Presentation

Build and launch Studio:

```bat
cmake --build build --config Debug
.\build\Debug\zero_studio.exe
```

Then use the **Load Showcase** button. Studio assembles both showcase sources
into `build/showcase/`, writes `.zsym` source maps, preloads ZeroFS with
`/data/showcase.txt = HELLO`, and starts the same protected two-process scenario
with quantum `1` and mock hardware.

The intended two-click presentation is:

```text
Load Showcase

Run #1
  PID 1 -> FS_READ
  PID 2 -> protected HW_WRITE (GPIO = 42)
  PID 2 -> illegal direct User MMIO access
  debugger stops: ProcessFaulted
  PID 1 is still alive

Run #2
  PID 1 -> FS_WRITE
  /data/showcase.txt becomes HELLOHELLO
  PID 1 -> normal exit
  runtime completes
```

The existing Studio state panel exposes the important evidence without a
separate execution model:

```text
process states / selected PID / running PID
source-line mapping
recent protected syscall service/status/result
recent context switches
faulted PID
ZeroFS showcase file content
mock GPIO value
```

The Studio backend regression runs the same real `.zasm` showcase sources and
checks the fault-phase and completion-phase presentation state.

<!-- Patch: v1.9-end-to-end-showcase-r1 -->

<!-- Patch: v1.9-showcase-golden-regression-r1 -->

<!-- Patch: v1.9-studio-showcase-presentation-r1 -->
