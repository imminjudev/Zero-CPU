param(
    [string]$RepoRoot = (Get-Location).Path
)

$ErrorActionPreference = "Stop"

$studioPath = Join-Path $RepoRoot "studio\zero_studio.cpp"

if (-not (Test-Path $studioPath)) {
    throw "studio\zero_studio.cpp not found. Run this script from the Zero-CPU repository root."
}

$text = Get-Content -Raw -Path $studioPath
$text = $text -replace "`r`n", "`n"

$backupPath = "$studioPath.bak_studio_system_panel"

if (-not (Test-Path $backupPath)) {
    Copy-Item $studioPath $backupPath
}

function Add-IncludeIfMissing {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Anchor,
        [string]$Insert
    )

    if ($Text.Contains($Needle)) {
        return $Text
    }

    if (-not $Text.Contains($Anchor)) {
        throw "Anchor not found while adding include: $Anchor"
    }

    return $Text.Replace($Anchor, $Anchor + "`n" + $Insert)
}

$text = Add-IncludeIfMissing `
    -Text $text `
    -Needle 'zero_cpu/core/DebugOutputDevice.hpp' `
    -Anchor '#include "zero_cpu/core/CPU.hpp"' `
    -Insert '#include "zero_cpu/core/DebugOutputDevice.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/TimerDevice.hpp"'

$text = Add-IncludeIfMissing `
    -Text $text `
    -Needle '<memory>' `
    -Anchor '#include <iomanip>' `
    -Insert '#include <memory>'

if (-not $text.Contains('g_debugOutputDevice')) {
    $anchor = 'zero_cpu::CPU g_cpu;
'
    $insert = 'zero_cpu::CPU g_cpu;
std::shared_ptr<zero_cpu::InterruptController> g_interruptController;
std::shared_ptr<zero_cpu::MMIOBus> g_mmioBus;
std::shared_ptr<zero_cpu::DebugOutputDevice> g_debugOutputDevice;
std::shared_ptr<zero_cpu::TimerDevice> g_timerDevice;
'
    if (-not $text.Contains($anchor)) {
        throw "Global CPU anchor not found."
    }

    $text = $text.Replace($anchor, $insert)
}

$helpers = @'
std::string studioBoolText(bool value) {
    return value ? "true" : "false";
}

std::string studioDebugOutputAsAscii() {
    if (!g_debugOutputDevice) {
        return {};
    }

    std::string text;

    for (const std::int64_t value : g_debugOutputDevice->writes()) {
        if (value >= 32 && value <= 126) {
            text.push_back(static_cast<char>(value));
        } else if (value == 10) {
            text.push_back('\n');
        } else {
            text.push_back('.');
        }
    }

    return text;
}

void configureSystemDevices() {
    using namespace zero_cpu;

    g_interruptController = std::make_shared<InterruptController>();
    g_mmioBus = std::make_shared<MMIOBus>();
    g_debugOutputDevice = std::make_shared<DebugOutputDevice>();
    g_timerDevice = std::make_shared<TimerDevice>(
        g_interruptController,
        44,
        1000,
        0
    );

    g_timerDevice->setEnabled(false);

    g_mmioBus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        g_debugOutputDevice
    );

    g_mmioBus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        g_timerDevice
    );

    g_cpu.setInterruptController(g_interruptController);
    g_cpu.setMMIOBus(g_mmioBus);
    g_cpu.clearClockedDevices();
    g_cpu.addClockedDevice(g_timerDevice);
}

std::string makeSystemPanelView() {
    using namespace zero_cpu;

    std::ostringstream oss;

    oss << "System Panel\n";

    oss << "Debug MMIO = 0x"
        << std::hex
        << memory_map::kDebugOutputBase
        << "..0x"
        << (memory_map::kDebugOutputEndExclusive - 1)
        << std::dec
        << "\n";

    oss << "Timer MMIO = 0x"
        << std::hex
        << memory_map::kTimerBase
        << "..0x"
        << (memory_map::kTimerEndExclusive - 1)
        << std::dec
        << "\n";

    oss << "Syscall Vector = 80\n";
    oss << "Default Timer Vector = 44\n";

    if (g_interruptController) {
        oss << "Interrupts Enabled = "
            << studioBoolText(g_interruptController->globalEnabled())
            << "\n";
        oss << "Pending Interrupts = "
            << g_interruptController->pendingCount()
            << "\n";
    } else {
        oss << "Interrupt Controller = <not configured>\n";
    }

    if (g_timerDevice) {
        oss << "Timer Tick Count = "
            << g_timerDevice->tickCount()
            << "\n";
        oss << "Timer Interval = "
            << g_timerDevice->interval()
            << "\n";
        oss << "Timer Vector = "
            << static_cast<int>(g_timerDevice->vector())
            << "\n";
        oss << "Timer Payload = "
            << g_timerDevice->payload()
            << "\n";
        oss << "Timer Interrupt Count = "
            << g_timerDevice->interruptCount()
            << "\n";
        oss << "Timer Enabled = "
            << studioBoolText(g_timerDevice->enabled())
            << "\n";
    } else {
        oss << "TimerDevice = <not configured>\n";
    }

    if (g_debugOutputDevice) {
        const std::string ascii = studioDebugOutputAsAscii();

        oss << "Debug Writes = "
            << g_debugOutputDevice->writes().size()
            << "\n";
        oss << "Debug ASCII = "
            << (ascii.empty() ? "<empty>" : ascii)
            << "\n";
    } else {
        oss << "DebugOutputDevice = <not configured>\n";
    }

    return oss.str();
}

'@

if (-not $text.Contains('std::string makeSystemPanelView()')) {
    $anchor = 'std::string makeStateView() {'
    if (-not $text.Contains($anchor)) {
        throw "makeStateView anchor not found."
    }

    $text = $text.Replace($anchor, $helpers + "`n" + $anchor)
}

$oldStateBlock = @'
    if (g_mode == StudioMode::Binary) {
        oss << "\n";
        oss << makeBinaryInfoView();
    }

    oss << "\n";
    oss << makeRegisterView();
'@

$newStateBlock = @'
    if (g_mode == StudioMode::Binary) {
        oss << "\n";
        oss << makeBinaryInfoView();
    }

    oss << "\n";
    oss << makeSystemPanelView();

    oss << "\n";
    oss << makeRegisterView();
'@

if ($text.Contains($oldStateBlock) -and (-not $text.Contains('oss << makeSystemPanelView();'))) {
    $text = $text.Replace($oldStateBlock, $newStateBlock)
}

$text = $text.Replace(
    '        g_cpu.loadProgram(assembled.instructions, assembled.labels);
        g_mode = StudioMode::Assembly;',
    '        g_cpu.loadProgram(assembled.instructions, assembled.labels);
        configureSystemDevices();
        g_mode = StudioMode::Assembly;'
)

$text = $text.Replace(
    '        g_cpu.loadBinaryProgram(program);
        g_mode = StudioMode::Binary;',
    '        g_cpu.loadBinaryProgram(program);
        configureSystemDevices();
        g_mode = StudioMode::Binary;'
)

$text = $text.Replace(
    'void onResetClicked() {
    g_cpu.reset();',
    'void onResetClicked() {
    g_cpu.reset();
    configureSystemDevices();'
)

$text = $text.Replace('Zero-CPU Studio v0.6', 'Zero-CPU Studio v0.7')
$text = $text.Replace(
    '"Source editor added.\n"',
    '"Source editor added.\n"
        "System panel added.\n"'
)

Set-Content -Path $studioPath -Value ($text -replace "`n", "`r`n") -NoNewline

Write-Host "Patched studio\zero_studio.cpp"
Write-Host "Backup: $backupPath"
