# Zero-CPU Syscall Table

Zero-CPU currently has **two `INT 80` syscall ABIs**:

```text
Guest Mini-Kernel / BIO-OS ABI
Protected Host Runtime ABI
```

They share the interrupt entry mechanism but use different handlers and service
tables.

For detailed behavior and protected interrupt-frame semantics, see
`docs/syscall-convention.md`.

## Common INT 80 Convention

```text
R0 = interrupt vector (set by CPU)
R1 = syscall / service number
R2 = argument 0 / return value
R3 = argument 1
INT 80
```

## Guest Mini-Kernel / BIO-OS Syscalls

This ABI is implemented by guest `.zasm` Kernel handlers and is used by the
mini-kernel examples and BIO-OS.

Guest-specific register use:

```text
R4 = argument 2
R7 = guest exit/status value where applicable
```

| Number | Name | Inputs | Returns / State | Effect |
|---:|---|---|---|---|
| 1 | debug output | `R2 = value` | none | writes to `DebugOutputDevice` |
| 2 | memory write | `R2 = address`, `R3 = value` | none | writes `Memory[R2] = R3` |
| 3 | exit | `R2 = exit code` | `R7 = exit code` | halts guest CPU path |
| 4 | timer read | none | `R2 = tick count` | reads `TimerDevice` tick count |
| 5 | timer enable | `R2 = interval`, `R3 = vector` | none | configures/enables timer |
| 6 | timer disable | none | none | disables timer |
| 7 | timer configure | `R2 = interval`, `R3 = vector`, `R4 = payload` | none | configures timer |

These numbers remain part of the guest demo ABI. They are not automatically
services of the protected host runtime.

## Protected Host Runtime Syscalls

The canonical protected ABI is defined by:

```text
include/zero_cpu/kernel/ProtectedSyscallABI.hpp
```

Runtime behavior is implemented by:

```text
zero_cpu::kernel::ProtectedSyscallDispatcher
```

Protected-specific register use:

```text
R4 = status
R7 = exit/status value
```

| Service | Name | Inputs | Returns | Effect |
|---:|---|---|---|---|
| 3 | process exit | `R2 = exit code` | `R7 = exit code`, `R4 = status` | terminates current process |
| 20 | hardware write | `R2 = hardware offset`, `R3 = value` | `R4 = status` | protected hardware MMIO write |
| 21 | hardware read | `R2 = hardware offset` | `R2 = value`, `R4 = status` | protected hardware MMIO read |
| 30 | filesystem stat | `R2 = request pointer` | size/type outputs, `R4 = status` | inspect ZeroFS node |
| 31 | filesystem read | `R2 = request pointer` | `R2 = bytes read`, `R4 = status` | copy ZeroFS bytes into guest buffer |
| 32 | filesystem write | `R2 = request pointer` | `R2 = bytes written`, `R4 = status` | copy guest buffer into ZeroFS |

The CLI obtains protected service numbers from
`ProtectedSyscallABI` instead of duplicating numeric values.

Protected service families:

```text
30..32  implemented storage / filesystem
33..39  reserved filesystem expansion
40..49  reserved network / web
```

## Protected Status Codes

| Status | Meaning |
|---:|---|
| `0` | OK |
| `-1` | unsupported service |
| `-2` | invalid hardware offset |
| `-3` | hardware unavailable |
| `-4` | hardware error |
| `-5` | invalid guest request/buffer memory |
| `-6` | invalid filesystem path |
| `-7` | filesystem path not found |
| `-8` | filesystem node type error |
| `-9` | filesystem invalid offset |
| `-10` | filesystem runtime error |

## Protected Hardware Offsets

```text
0   GPIO output
8   GPIO input
16  PWM output
24  ADC input
32  status
40  command
```

Example protected write:

```asm
MOV R1, 20
MOV R2, 0
MOV R3, 42
INT 80
```

Example protected read:

```asm
MOV R1, 21
MOV R2, 0
INT 80
```

Example protected process exit:

```asm
MOV R1, 3
MOV R2, 7
INT 80
```

## CLI Reference

```bat
.\build\Debug\zero_cli.exe syscall-table
```

The command prints both ABI sections plus protected status codes.

The full regression suite checks that the guest table and protected service
`3/20/21` remain visible:

```bat
scripts\test_all.bat
```

## Design Rule

When adding a protected host service:

```text
1. define its stable number/register contract in ProtectedSyscallABI
2. implement core semantics in the runtime/dispatcher service layer
3. add focused tests
4. consume ABI constants from CLI/Studio/documentation
5. do not duplicate protected numeric values in consumer logic
```

Guest mini-kernel and protected host services remain separate ABIs.

<!-- Patch: v1.6-cli-syscall-abi-split-r1 -->

<!-- Patch: v1.8-protected-platform-abi-r1 -->

<!-- Patch: v1.8-protected-filesystem-syscalls-r2 -->
