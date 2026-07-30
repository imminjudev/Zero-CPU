# Zero-CPU Hardware Platform Roadmap

## Project Goal

Zero-CPU is evolving into a custom-ISA virtual CPU platform that can control
physical devices through memory-mapped I/O.

The first physical target is an ESP32 connected over USB serial.

## v0.6 — Hardware Abstraction

```text
HardwareBus interface
MockHardwareBus
HardwareMMIODevice adapter
hardware MMIO address map
CPU-to-hardware integration tests
connection and access error handling
```

## v0.7 — ESP32 Serial Bridge

```text
versioned request/response protocol
Windows serial-port transport
SerialHardwareBus
ESP32 bridge firmware
GPIO output
GPIO input
ADC input
PWM output
timeouts and protocol errors
```

## v0.8 — Hardware Debugging

```text
Studio COM-port selection
connect and disconnect controls
device status display
hardware transaction trace
live GPIO and sensor values
mock and physical device switching
```

## v0.9 — Embedded Runtime

```text
portable Zero-CPU execution core
.zbin transfer to ESP32
ESP32-hosted Zero-CPU interpreter
direct GPIO access without a PC
resource and instruction limits
```

## v1.0 — Zero-CPU Hardware Platform

```text
custom ISA and assembler
binary program format
trace-based verification
MMIO devices
ESP32 hardware bridge
Studio hardware debugger
PC-hosted and ESP32-hosted execution modes
```

## First Demonstration

The first end-to-end physical demonstration will be:

```text
Zero-CPU assembly program
    -> STORE to GPIO MMIO
    -> USB serial command
    -> ESP32 GPIO
    -> physical LED
```

A button or sensor input will travel in the opposite direction through a
Zero-CPU `LOAD` instruction.
