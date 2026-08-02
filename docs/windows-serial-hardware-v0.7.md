# Windows Serial ESP32 Bridge v0.7

This milestone connects the Zero-CPU host process to a physical ESP32-S3 over a
Windows COM port.

## Architecture

```text
Zero-CPU instructions
    -> MMIOBus
    -> HardwareMMIODevice
    -> SerialHardwareBus
    -> WindowsSerialTransport
    -> COM port at 115200 baud
    -> ESP32-S3 bridge firmware
```

## Firmware

Open and upload:

```text
firmware/esp32/zero_cpu_bridge/zero_cpu_bridge.ino
```

Arduino settings used for the HG ESP32-S3 DevKitC-1 N16R8:

```text
Board: ESP32S3 Dev Module
USB CDC On Boot: Enabled
Flash Size: 16MB
PSRAM: OPI PSRAM
Upload Mode: UART0 / Hardware CDC
Baud: 115200
```

Close Arduino Serial Monitor before running the CLI. Only one process can own the
COM port at a time.

## Live test

```text
zero_cli hardware-live-test COM3
```

Optional baud override:

```text
zero_cli hardware-live-test COM3 115200
```

The command opens the port, performs the `PING`/`PONG` handshake, maps the serial
hardware bus into the CPU MMIO window, and runs a small Zero-CPU program that:

```text
writes 1 to GPIO output
reads the GPIO output back
reads device status
writes 0 to GPIO output
halts
```

The live test is intentionally not part of `scripts/test_all.bat`, because the
normal regression suite must remain deterministic and runnable without physical
hardware.

## Hardware register offsets

```text
0   GPIO output (ESP32 GPIO 2)
8   GPIO input  (ESP32 GPIO 4, pull-up)
16  PWM output  (ESP32 GPIO 5, 0..255)
24  ADC input   (ESP32 GPIO 1)
32  device status (1 = ready)
40  device command
```
