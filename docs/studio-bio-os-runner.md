# Zero-CPU Studio BIO-OS Runner

This patch updates `studio/zero_studio.cpp` in place and adds a `Run BIO-OS` button.

## What it does

The new Studio button runs the current BIO-OS demo from:

```text
examples\bio_os
```

It combines:

```text
boot.zasm
kernel.zasm
user_program.zasm
```

into:

```text
combined_boot.zasm
combined_boot.zbin
```

Then it loads the binary into Studio, attaches the system devices, registers interrupt handlers, sets the BIO-OS stack base, and runs the program.

## Expected Studio result

After clicking `Run BIO-OS`, the Trace panel should show:

```text
[Studio BIO-OS Run]
BIO-OS execution finished successfully.
Debug ASCII: BU
Timer Interrupt Count: 1
Timer Enabled: false
```

The State panel should also update the System Panel values.

## Apply

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\patch_studio_bio_os_runner.ps1
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

This patch expects the previous Studio System Panel patch to be applied first.
