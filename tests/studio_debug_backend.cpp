#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/DebugOutputDevice.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/debug/DebugSnapshotJson.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/studio/StudioDebugBackend.hpp"
#include "zero_cpu/studio/StudioMultiProcessDebugBackend.hpp"

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

zero_cpu::kernel::ProcessImage makeImage() {
    const char* source = R"ASM(
.entry start

.data
value: .qword 1

.text
start:
    LOAD R0, [value]
work:
    ADD R0, 2
done:
    STORE [value], R0
)ASM";

    zero_cpu::Assembler assembler;

    const auto assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        "studio-debug-backend.zbin"
    );
}

bool mutableCpuAccess(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::studio;

    StudioDebugBackend backend;
    backend.loadImage(makeImage());

    auto bus =
        std::make_shared<MMIOBus>();

    auto output =
        std::make_shared<DebugOutputDevice>();

    bus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        output
    );

    backend.cpu().setMMIOBus(bus);

    if (
        !backend.loaded()
        || !backend.cpu().hasMMIOBus()
        || backend.session().sourceName()
            != "studio-debug-backend.zbin"
    ) {
        detail =
            "mutable debugger CPU access mismatch";
        return false;
    }

    return true;
}

bool breakpointWorkflow(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::studio;

    StudioDebugBackend backend;
    backend.loadImage(makeImage());

    const std::size_t breakpoint =
        memory_map::kBinaryCodeBase + 24;

    if (!backend.addBreakpoint(breakpoint)) {
        detail =
            "studio breakpoint was not added";
        return false;
    }

    const DebugStop stopped =
        backend.run(20);

    if (
        stopped.reason
            != DebugStopReason::Breakpoint
        || stopped.pc != breakpoint
        || stopped.total_steps != 1
        || backend.cpu().state()
            .registers().get(
                RegisterName::R0
            ) != 1
    ) {
        detail =
            "studio breakpoint stop mismatch";
        return false;
    }

    const DebugStop stepped =
        backend.step();

    if (
        stepped.reason
            != DebugStopReason::StepComplete
        || backend.cpu().state()
            .registers().get(
                RegisterName::R0
            ) != 3
    ) {
        detail =
            "studio step mismatch";
        return false;
    }

    backend.clearBreakpoints();

    const DebugStop completed =
        backend.run(20);

    if (
        !completed.reachedProgramEnd()
        || backend.cpu().state()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 3
    ) {
        detail =
            "studio run completion mismatch";
        return false;
    }

    return true;
}

bool advancedDebugControls(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::studio;

    {
        StudioDebugBackend backend;
        backend.loadImage(makeImage());

        const std::size_t workAddress =
            memory_map::kBinaryCodeBase
            + binary::kInstructionSize;

        const std::size_t id =
            backend.addConditionalBreakpoint(
                workAddress,
                "R0",
                "==",
                "1"
            );

        const DebugStop stopped = backend.run(20);
        if (
            id != 1
            || stopped.reason != DebugStopReason::ConditionalBreakpoint
            || stopped.pc != workAddress
            || !stopped.has_conditional_breakpoint
            || stopped.conditional_breakpoint_id != id
            || stopped.conditional_actual_value != 1
        ) {
            detail = "studio conditional breakpoint mismatch";
            return false;
        }

        backend.clearConditionalBreakpoints();
        if (!backend.session().conditionalBreakpoints().empty()) {
            detail = "studio conditional breakpoint clear mismatch";
            return false;
        }
    }

    {
        StudioDebugBackend backend;
        backend.loadImage(makeImage());

        const std::size_t id = backend.addWatchpoint(
            memory_map::kUserDataBase,
            sizeof(std::int64_t),
            MemoryWatchMode::Write
        );

        const DebugStop stopped = backend.run(20);
        if (
            id != 1
            || stopped.reason != DebugStopReason::Watchpoint
            || !stopped.has_watchpoint
            || stopped.watchpoint_id != id
            || stopped.access_address != memory_map::kUserDataBase
            || stopped.access_mode != MemoryWatchMode::Write
        ) {
            detail = "studio watchpoint mismatch";
            return false;
        }

        backend.clearWatchpoints();
        if (!backend.session().watchpoints().empty()) {
            detail = "studio watchpoint clear mismatch";
            return false;
        }
    }

    {
        StudioDebugBackend backend;
        backend.loadImage(makeImage());
        if (!throwsRuntimeError([&] {
            (void)backend.addConditionalBreakpoint(
                memory_map::kBinaryCodeBase,
                "R99",
                "==",
                "0"
            );
        })) {
            detail = "invalid studio condition was accepted";
            return false;
        }
    }

    return true;
}

bool multiProcessStudioBackend(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::studio;

    MultiProcessDebugOptions options;
    options.quantum = 1;
    options.default_continue_steps = 100;

    StudioMultiProcessDebugBackend backend;

    backend.loadImages(
        {makeImage(), makeImage()},
        options
    );

    if (
        !backend.loaded()
        || backend.selectedPid() != 1
        || backend.runningPid() != 1
    ) {
        detail =
            "studio multi-process initial state mismatch";
        return false;
    }

    backend.selectProcess(2);

    if (backend.selectedPid() != 2) {
        detail =
            "studio multi-process PID selection mismatch";
        return false;
    }

    const std::size_t address =
        memory_map::kBinaryCodeBase;

    if (!backend.addBreakpoint(address)) {
        detail =
            "studio multi-process breakpoint add mismatch";
        return false;
    }

    const MultiProcessDebugStop stop =
        backend.run(100);

    if (
        stop.reason
            != MultiProcessDebugStopReason::Breakpoint
        || !stop.has_debug_hit
        || stop.hit_pid != 2
        || stop.hit_address != address
        || backend.session().breakpoints(1).size() != 0
        || backend.session().breakpoints(2).size() != 1
    ) {
        detail =
            "studio multi-process breakpoint stop mismatch";
        return false;
    }

    const std::string status =
        backend.statusText();

    if (
        status.find(
            "Multi-Process Debug Session"
        ) == std::string::npos
        || status.find(
            "Selected PID = 2"
        ) == std::string::npos
        || status.find(
            "Last Stop = Breakpoint"
        ) == std::string::npos
    ) {
        detail =
            "studio multi-process status mismatch";
        return false;
    }

    const std::string path =
        "studio_multi_process_snapshot.json";

    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::remove(path.c_str());
        }
    } cleanup{path};

    DebugSnapshotOptions snapshotOptions;
    snapshotOptions.memory_address = 0;
    snapshotOptions.memory_size = 16;

    backend.exportSnapshot(
        path,
        snapshotOptions
    );

    std::FILE* file =
        std::fopen(
            path.c_str(),
            "rb"
        );

    if (file == nullptr) {
        detail =
            "studio multi-process snapshot was not created";
        return false;
    }

    std::fclose(file);

    backend.clearBreakpoints();

    if (!backend.session().breakpoints(2).empty()) {
        detail =
            "studio multi-process breakpoint clear mismatch";
        return false;
    }

    backend.reset();

    if (backend.loaded()) {
        detail =
            "studio multi-process reset mismatch";
        return false;
    }

    return true;
}


bool protectedSyscallStudioStatus(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::hardware;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::studio;

    const char* protectedSource = R"ASM(
.entry start
.text
start:
    MOV R1, 20
    MOV R2, 0
    MOV R3, 42
    INT 80

    MOV R1, 21
    MOV R2, 0
    INT 80

    MOV R1, 3
    MOV R2, 7
    INT 80
)ASM";

    const char* peerSource = R"ASM(
.entry start
.text
start:
    MOV R6, 222
)ASM";

    Assembler assembler;
    ProcessImageLoader loader;

    const auto protectedImage =
        loader.loadProgram(
            assembler
                .assembleString(protectedSource)
                .toBinaryProgram(),
            "studio-protected-a.zbin"
        );

    const auto peerImage =
        loader.loadProgram(
            assembler
                .assembleString(peerSource)
                .toBinaryProgram(),
            "studio-protected-b.zbin"
        );

    auto hardware =
        std::make_shared<MockHardwareBus>(
            "studio-backend-protected-hardware"
        );

    hardware->connect();

    auto device =
        std::make_shared<HardwareMMIODevice>(
            hardware
        );

    auto mmio =
        std::make_shared<MMIOBus>();

    mmio->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        device
    );

    MultiProcessDebugOptions options;
    options.quantum = 100;
    options.default_continue_steps = 100;
    options.mmio_bus = mmio;
    options.software_interrupt_handler =
        std::make_shared<
            ProtectedSyscallDispatcher
        >();

    StudioMultiProcessDebugBackend backend;

    backend.loadImages(
        {
            protectedImage,
            peerImage
        },
        options
    );

    const MultiProcessDebugStop firstStop =
        backend.run(100);

    if (
        firstStop.reason
            != MultiProcessDebugStopReason::
                ProcessTerminated
        || firstStop.terminated_pid != 1
        || backend.session()
            .softwareInterrupts()
            .size() != 3
    ) {
        detail =
            "Studio protected syscall runtime mismatch";
        return false;
    }

    const std::string status =
        backend.statusText();

    if (
        status.find(
            "Recent Software Interrupts"
        ) == std::string::npos
        || status.find("PID 1 INT 80 svc=20")
            == std::string::npos
        || status.find("arg1=42")
            == std::string::npos
        || status.find("PID 1 INT 80 svc=21")
            == std::string::npos
        || status.find("result=42")
            == std::string::npos
        || status.find("PID 1 INT 80 svc=3")
            == std::string::npos
        || status.find(
            "disposition=TerminateProcess"
        ) == std::string::npos
        || status.find("exit=7")
            == std::string::npos
        || hardware->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) != 42
    ) {
        detail =
            "Studio protected syscall status mismatch";
        return false;
    }

    return true;
}


bool sourceStepBackendDelegation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::studio;

    const char* source = R"ASM(.entry start
.text
start:
    MOV R0, 1
    ADD R0, 2
    MOV R1, R0
)ASM";

    const std::string binaryPath =
        "studio_source_step_backend_test.zbin";

    const std::string symbolsPath =
        debugSymbolsPathForExecutable(
            binaryPath
        );

    struct Cleanup {
        std::string binary_path;
        std::string symbols_path;

        ~Cleanup() {
            std::remove(
                symbols_path.c_str()
            );

            std::remove(
                binary_path.c_str()
            );
        }
    } cleanup{
        binaryPath,
        symbolsPath
    };

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(source);

    binary::BinaryWriter writer;

    writer.writeFile(
        binaryPath,
        assembled.toBinaryProgram()
    );

    DebugSymbols::fromAssembledProgram(
        assembled,
        memory_map::kBinaryCodeBase,
        "studio-source-step-backend.zasm"
    ).writeFile(
        symbolsPath
    );

    {
        StudioDebugBackend backend;
        backend.loadBinary(binaryPath);

        const DebugStop stop =
            backend.stepSourceLine(20);

        if (
            stop.reason
                != DebugStopReason::
                    StepComplete
            || stop.executed_steps != 1
            || stop.total_steps != 1
            || backend.cpu().state().pc()
                != memory_map::kBinaryCodeBase
                    + binary::kInstructionSize
            || backend.cpu().state()
                .registers().get(
                    RegisterName::R0
                ) != 1
        ) {
            detail =
                "single-process Studio source-step "
                "delegation mismatch";
            return false;
        }
    }

    {
        MultiProcessDebugOptions options;
        options.quantum = 1;
        options.default_continue_steps = 100;

        StudioMultiProcessDebugBackend backend;

        backend.loadBinaries(
            {
                binaryPath,
                binaryPath
            },
            options
        );

        backend.selectProcess(2);

        const MultiProcessDebugStop stop =
            backend.stepSourceLine(20);

        const ProcessDebugSnapshot selected =
            backend.session()
                .selectedProcessSnapshot();

        if (
            stop.reason
                != MultiProcessDebugStopReason::
                    StepComplete
            || stop.executed_steps == 0
            || backend.selectedPid() != 2
            || selected.context.pc
                != memory_map::kBinaryCodeBase
                    + binary::kInstructionSize
            || selected.context.registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 1
            || backend.session()
                .contextSwitches()
                .empty()
        ) {
            detail =
                "multi-process Studio source-step "
                "delegation mismatch";
            return false;
        }
    }

    return true;
}

// Patch: v1.2-studio-source-step-core-delegation-r1

bool snapshotAndStatus(std::string& detail) {
    using namespace zero_cpu::debug;
    using namespace zero_cpu::studio;

    const std::string path =
        "studio_debug_backend_snapshot.json";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(path.c_str());
        }
    } cleanup{path};

    StudioDebugBackend backend;
    backend.loadImage(makeImage());

    (void)backend.step();

    DebugSnapshotOptions options;
    options.memory_address = 0;
    options.memory_size = 16;

    backend.exportSnapshot(
        path,
        options
    );

    const std::string status =
        backend.statusText();

    if (
        status.find(
            "Binary Debug Session"
        ) == std::string::npos
        || status.find(
            "Stop Reason = StepComplete"
        ) == std::string::npos
        || status.find(
            "Total Steps = 1"
        ) == std::string::npos
    ) {
        detail =
            "studio debugger status mismatch";
        return false;
    }

    std::FILE* file =
        std::fopen(
            path.c_str(),
            "rb"
        );

    if (file == nullptr) {
        detail =
            "studio snapshot file was not created";
        return false;
    }

    std::fclose(file);
    return true;
}

bool resetAndValidation(std::string& detail) {
    using namespace zero_cpu::studio;

    StudioDebugBackend backend;

    if (
        !throwsRuntimeError(
            [&] {
                (void)backend.cpu();
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)backend.run(1);
            }
        )
    ) {
        detail =
            "unloaded studio debugger operation "
            "was accepted";
        return false;
    }

    backend.loadImage(makeImage());

    if (
        !throwsRuntimeError(
            [&] {
                (void)backend.run(0);
            }
        )
    ) {
        detail =
            "zero studio run limit was accepted";
        return false;
    }

    backend.reset();

    if (backend.loaded()) {
        detail =
            "studio debugger reset mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Studio Debug Backend "
           "Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (
                passed
                    ? "[PASS] "
                    : "[FAIL] "
            )
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";

            ++failures;
        }
    };

    {
        std::string detail;
        report(
            "Mutable debugger CPU access",
            mutableCpuAccess(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio breakpoint workflow",
            breakpointWorkflow(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio advanced debug controls",
            advancedDebugControls(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio multi-process debugger",
            multiProcessStudioBackend(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio protected syscall status",
            protectedSyscallStudioStatus(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio source-step backend delegation",
            sourceStepBackendDelegation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio snapshot and status",
            snapshotAndStatus(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Studio reset and validation",
            resetAndValidation(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Studio debug backend test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Studio debug backend test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}

// Patch: v1.5-studio-syscall-observability-r1

