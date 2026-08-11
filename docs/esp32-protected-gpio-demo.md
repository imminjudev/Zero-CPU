# Protected ESP32 GPIO Demo

This is the first v1.7 physical-hardware demonstration path for the protected
Zero-CPU runtime.

The demonstrated architecture is:

```text
.zasm User process
  → .zbin
  → protected MultiProcessRunner
  → INT 80
  → ProtectedSyscallDispatcher
  → MMIOBus
  → HardwareMMIODevice
  → SerialHardwareBus
  → WindowsSerialTransport
  → ESP32-S3
  → GPIO 2
```

## 1. Demo Program

Source:

```text
examples/protected_hardware_gpio_live.zasm
```

The program performs:

```text
service 20: write GPIO output offset 0 = 1
service 21: read GPIO output offset 0
verify R4 status == 0
verify readback == 1
store readback in User Memory[100]
service 3: exit 0
```

Failure exits:

```text
10 = protected hardware write failed
11 = protected hardware read failed
12 = GPIO readback mismatch
```

The same `.zasm` is exercised against `--hardware-mock` in
`scripts/test_all.bat`, so the demo remains regression-tested without an ESP32.

## 2. ESP32 Firmware

Firmware:

```text
firmware\esp32\zero_cpu_bridge\zero_cpu_bridge.ino
```

Current bridge mapping:

```text
Protocol: ZEROCPU/1
Baud: 115200

offset 0  = GPIO output → ESP32 GPIO 2
offset 8  = GPIO input  → ESP32 GPIO 4
offset 16 = PWM output  → ESP32 GPIO 5
offset 24 = ADC input   → ESP32 GPIO 1
offset 32 = status
offset 40 = command
```

The firmware replies to the handshake with `ZEROCPU/1 PONG`.

## 3. Arduino Setup

The existing ESP32-S3 bridge setup uses:

```text
Board: ESP32S3 Dev Module
USB CDC On Boot: Enabled
Flash Size: 16MB
PSRAM: OPI PSRAM
Upload Mode: UART0 / Hardware CDC
Baud: 115200
```

Upload the firmware first.

Close Arduino Serial Monitor before running Zero-CPU. Only one process can own
the COM port at a time.

## 4. LED Wiring

Do not depend on a board-specific onboard LED.

Use an external LED path:

```text
ESP32 GPIO 2
  → current-limiting series resistor
  → LED
  → GND
```

## 5. Windows Demo Command

From the repository root:

```bat
scripts\run_esp32_protected_gpio_demo.bat COM3
```

Optional baud override:

```bat
scripts\run_esp32_protected_gpio_demo.bat COM3 115200
```

Replace `COM3` with the ESP32 COM port shown by Windows.

The script:

```text
1. builds Zero-CPU
2. assembles the protected User process
3. runs with --protected-syscalls
4. selects --hardware-serial COMx
5. requires process 1 to exit with code 0
```

On success:

```text
[PASS] Protected ESP32 GPIO demo completed.
```

GPIO 2 remains HIGH so the physical result stays visible after process exit.

## 6. Return GPIO to LOW

The existing live transport/MMIO smoke test ends by writing GPIO output to zero:

```bat
.\build\Debug\zero_cli.exe hardware-live-test COM3 115200
```

For a Release build:

```bat
.\build\Release\zero_cli.exe hardware-live-test COM3 115200
```

This cleanup command is a direct transport/MMIO smoke test. The protected
architecture claim comes from the User-process demo above.

## 7. Expected Failure Classes

If the COM port cannot be opened, check:

```text
wrong COM number
Arduino Serial Monitor still open
another process owns the port
device disconnected
```

For handshake failures or timeouts, check:

```text
correct bridge firmware
ZEROCPU/1 protocol
115200 baud
USB connection
```

Process exit codes distinguish protected operation failures:

```text
10 write path
11 read path
12 readback verification
```

## 8. Verification Boundary

Automated regression verifies this exact demo program with the mock backend.

Physical completion is recorded only after the same program succeeds against an
actual ESP32 and GPIO 2 is observed HIGH.

<!-- Patch: v1.7-protected-esp32-gpio-demo-r1 -->
