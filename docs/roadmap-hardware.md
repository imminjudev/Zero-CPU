# Zero-CPU Hardware Platform Roadmap

## 1. Goal

Zero-CPU connects protected virtual programs to external devices through a
replaceable hardware transport.

Current host-side path:

```text
Zero-CPU process
  → INT 80 protected hardware service
  → ProtectedSyscallDispatcher
  → MMIOBus
  → HardwareMMIODevice
  → HardwareBus
  → mock or serial backend
```

Physical serial path:

```text
SerialHardwareBus
  → WindowsSerialTransport
  → USB serial
  → ESP32-oriented bridge
```

---

## 2. Implemented Foundation

The original v0.6/v0.7 hardware milestones are no longer future work.

Implemented:

```text
HardwareBus interface
MockHardwareBus
HardwareMMIODevice
hardware MMIO address map
versioned serial request/response protocol
MockSerialTransport
SerialHardwareBus
WindowsSerialTransport
CLI hardware integration tests
hardware-live-test command
protected hardware write syscall
protected hardware read syscall
protected syscall semantic observations
```

Current hardware MMIO window:

```text
0xF200..0xF22F
```

Register offsets:

```text
0   GPIO output
8   GPIO input
16  PWM output
24  ADC input
32  status
40  command
```

---

## 3. Protection Model

User mode cannot directly LOAD/STORE the hardware MMIO region.

Protected access uses:

```text
service 20 = hardware write
service 21 = hardware read
```

Example write:

```text
User
  → INT 80 service 20
  → Kernel
  → validated hardware offset
  → shared MMIOBus
  → HardwareMMIODevice
  → HardwareBus
```

This keeps physical-device access behind the same User/Kernel boundary as the
rest of the protected runtime.

---

## 4. Current Backend Modes

### Mock backend

The deterministic mock backend is used for:

```text
unit tests
integration tests
protected runtime tests
debugger tests
Zero Studio protected multiprocess sessions
```

It is the default backend for repeatable verification.

### Serial backend

The serial backend provides the physical host boundary:

```text
SerialHardwareBus
WindowsSerialTransport
COM port
baud rate
protocol handshake / request-response handling
```

The CLI can exercise the physical path directly.

The existence of the transport does not by itself mean every physical demo is
finished or reproducible on every machine.

---

## 5. Current Verification

Automated coverage includes:

```text
hardware bus integration
serial hardware protocol
protected hardware syscall dispatch
invalid hardware offsets
unavailable hardware handling
hardware error status
multi-process protected runtime
protected syscall tracing
debugger syscall history
Zero Studio mock-hardware path
```

Mock hardware remains essential because deterministic regressions cannot depend
on a physical ESP32 being connected.

---

## 6. v1.7 Hardware Completion Target

The next hardware milestone is not to invent another abstraction layer.

It is to make the existing physical path a strong end-to-end demonstration.

Target:

```text
.zasm / .zbin User process
  → service 20
  → physical GPIO output
  → visible LED result

physical GPIO/ADC input
  → service 21
  → User process result
  → debugger / trace observation
```

Required work:

```text
document exact ESP32 wiring/setup
verify firmware/protocol compatibility
make one GPIO output demo reproducible
make one input demo reproducible
document COM/baud workflow
show useful failure messages for unplugged/wrong-port cases
capture expected CLI/debugger output
```

---

## 7. Studio Hardware Scope

Zero Studio already consumes the protected runtime with deterministic mock
hardware.

A later Studio physical-hardware layer may add:

```text
COM-port selection
connect / disconnect
mock / physical backend selection
device status
hardware transaction history
live GPIO / ADC / PWM values
```

These UI controls should wrap the existing hardware core. They should not create
a second hardware protocol implementation.

---

## 8. Optional Later Work

The following is outside the v2.0 completion requirement:

```text
transfer .zbin directly to ESP32
ESP32-hosted Zero-CPU interpreter
PC-free execution
resource-limited embedded runtime
```

This can be explored after the PC-hosted protected platform and physical bridge
demo are complete.

---

## 9. Hardware Completion Rule

For the first complete Zero-CPU platform, the important hardware claim is:

```text
A protected Zero-CPU User process can access a real external device through the
same tested syscall/MMIO abstraction used by the deterministic mock backend.
```

That is stronger and more relevant to the current architecture than adding new
device abstractions before the existing path is demonstrated end to end.

<!-- Patch: v1.6-current-roadmap-semantics-r1 -->
