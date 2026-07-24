# Studio BioOSRunner Refactor

Studio `Run BIO-OS` now uses the shared `BioOSRunner` module.

## Before

```text
Studio Run BIO-OS
    had its own combine / assemble / zbin / load / run path
```

## After

```text
Studio Run BIO-OS
    ??BioOSRunner::runOn(...)
    ??g_cpu + g_interruptController + g_mmioBus + g_debugOutputDevice + g_timerDevice
    ??Studio trace + System Panel
```

## Why `runOn(...)`

Studio needs the final device and CPU state to remain visible in the UI.

So `BioOSRunner` now has two paths:

```text
BioOSRunner::run(...)
    creates its own CPU/devices internally

BioOSRunner::runOn(...)
    runs on caller-provided CPU/devices
```

CLI can use `run(...)`.

Studio can use `runOn(...)` so that the System Panel still shows:

```text
Debug ASCII = BU
Timer Interrupt Count = 1
Timer Enabled = false
```

## Test

```bat
cmake --build build
.\build\Debug\zero_studio.exe
```

Click:

```text
Run BIO-OS
```

Expected trace title:

```text
[Studio BIO-OS Run via BioOSRunner]
```