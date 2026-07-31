# Zero-CPU Serial Hardware Protocol v0.6

This milestone adds the protocol and transport-independent serial hardware bus
used by the future ESP32 bridge.

## Architecture

```text
Zero-CPU LOAD / STORE
    -> MMIOBus
    -> HardwareMMIODevice
    -> SerialHardwareBus
    -> SerialTransport
    -> MockSerialTransport now
    -> Windows COM transport next
    -> ESP32 firmware
```

## Protocol version

Every line starts with:

```text
ZEROCPU/1
```

Requests:

```text
ZEROCPU/1 PING
ZEROCPU/1 READ 8
ZEROCPU/1 WRITE 0 1
```

Responses:

```text
ZEROCPU/1 PONG
ZEROCPU/1 VALUE 42
ZEROCPU/1 OK
ZEROCPU/1 ERROR device message
```

Offsets are decimal byte offsets inside the hardware MMIO window. Values are
signed 64-bit decimal integers. Messages are terminated by a newline.

## Connection handshake

`SerialHardwareBus::connect()` opens the transport and sends `PING`. The device
must return `PONG`. A malformed response, protocol error, or timeout closes the
transport and fails the connection.

## Verification

```text
zero_cli serial-hardware-test
```

The test verifies protocol parsing, handshake, CPU-to-serial GPIO output,
serial-to-CPU GPIO input, device errors, malformed responses, timeouts, and
access after disconnect.

`MockSerialTransport` acts as a deterministic ESP32 simulator. It will be
replaced by a Windows COM-port transport without changing `HardwareBus`, the CPU,
or the MMIO adapter.
