# Zero-CPU Studio System Panel Patch

This patch updates `studio/zero_studio.cpp` in place.

It adds a System Panel section to the existing Studio state view.

## Added runtime system objects

```text
InterruptController
MMIOBus
DebugOutputDevice
TimerDevice
```

## Added state view section

```text
System Panel
Debug MMIO
Timer MMIO
Syscall Vector
Default Timer Vector
Interrupts Enabled
Pending Interrupts
Timer Tick Count
Timer Interval
Timer Vector
Timer Payload
Timer Interrupt Count
Timer Enabled
Debug Writes
Debug ASCII
```

## Apply

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\patch_studio_system_panel.ps1
```

Then build:

```bat
cmake --build build
```

Then run Studio:

```bat
.\build\Debug\zero_studio.exe
```

## Notes

This is a conservative Studio v0.7 update.

It does not add full BIO-OS loading to Studio yet. It only attaches the core system devices and makes their current state visible in the existing state panel.
