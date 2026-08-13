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

## One-Command CLI Presentation

After building, the complete protected showcase is available through:

```bat
.\build\Debug\zero_cli.exe showcase
```

The command uses the reusable core `EndToEndShowcaseRunner`; it does not launch
a test executable or implement a second execution path. It assembles the real
showcase sources, configures ZeroFS and deterministic mock hardware, runs the
protected multi-process runtime, checks the same integration expectations,
writes `build/showcase/showcase_trace.json`, verifies invariants, and compares
the trace with the committed golden regression.

Successful output highlights:

```text
.zasm -> .zbin
exactly one isolated process fault
PID 1 survivor exit 0
ZeroFS HELLO -> HELLOHELLO
Mock GPIO[0] = 42
timer preemption / context switches
FS_READ / HW_WRITE / FS_WRITE / EXIT observations
invariant verification
golden trace match
```

The same runner is consumed by `zero_end_to_end_showcase_test`, so the public
CLI command and focused regression share the scenario orchestration.

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

<!-- Patch: v2.0-single-command-showcase-r1 -->
