# Zero-CPU Hardware Abstraction v0.6

Zero-CPU v0.6 begins the transition from a software-only virtual CPU to a
hardware-connectable CPU platform.

## Architecture

```text
Zero-CPU program
    |
    v
CPU LOAD / STORE
    |
    v
MMIOBus
    |
    v
HardwareMMIODevice
    |
    v
HardwareBus
    |
    +-- MockHardwareBus
    +-- future SerialHardwareBus
    +-- future native embedded bus
```

The CPU continues to use ordinary `LOAD` and `STORE` instructions. It does not
need to know whether a value comes from RAM, a timer, a mock device, or a real
ESP32.

## Hardware MMIO Window

```text
Base: 0xF200
Size: 0x0030 bytes
Register width: 8 bytes
```

Registers:

```text
0xF200  GPIO output
0xF208  GPIO input
0xF210  PWM output
0xF218  ADC input
0xF220  device status
0xF228  device command
```

The initial register model is intentionally small. The serial protocol can
expand later without changing the CPU instruction set.

## HardwareBus

`HardwareBus` is the transport abstraction.

It defines:

```text
name
connected state
connect
disconnect
readRegister
writeRegister
```

The first implementation is `MockHardwareBus`. It stores register values in
memory and records every read and write transaction.

A future `SerialHardwareBus` will implement the same interface using a Windows
COM port and an ESP32 firmware protocol.

## HardwareMMIODevice

`HardwareMMIODevice` adapts a `HardwareBus` to the existing `MMIODevice`
interface.

The existing `MMIOBus` therefore remains the common routing layer for:

```text
DebugOutputDevice
TimerDevice
HardwareMMIODevice
```

The adapter rejects:

```text
access while disconnected
offsets outside the hardware window
offsets that are not 8-byte aligned
```

## Verification

The command:

```text
zero_cli hardware-bus-test
```

runs a CPU program equivalent to:

```asm
MOV R1, 1
STORE [0xF200], R1
LOAD R2, [0xF208]
STORE [300], R2
HALT
```

The mock hardware input register is preloaded with `42`.

The test verifies:

```text
the CPU writes GPIO output through MMIO
the CPU reads GPIO input through MMIO
the read value reaches R2
the read value can be stored back into RAM
the mock bus records one write and one read
disconnected access is rejected
```

## Current Limitation

This milestone does not communicate with a physical ESP32 yet.

It establishes the interface boundary and deterministic tests needed before
adding serial transport.

The next implementation milestone is:

```text
HardwareProtocol
SerialHardwareBus
Windows COM-port transport
ESP32 bridge firmware
```
