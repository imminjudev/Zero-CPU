#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/core/SoftwareInterruptHandler.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using ProtectedABI =
    zero_cpu::kernel::ProtectedSyscallABI;

static_assert(ProtectedABI::kSyscallVector == 80);
static_assert(
    ProtectedABI::kVectorRegister
        == zero_cpu::RegisterName::R0
);
static_assert(
    ProtectedABI::kServiceRegister
        == zero_cpu::RegisterName::R1
);
static_assert(
    ProtectedABI::kArgument0ResultRegister
        == zero_cpu::RegisterName::R2
);
static_assert(
    ProtectedABI::kArgument1Register
        == zero_cpu::RegisterName::R3
);
static_assert(
    ProtectedABI::kStatusRegister
        == zero_cpu::RegisterName::R4
);
static_assert(
    ProtectedABI::kExitValueRegister
        == zero_cpu::RegisterName::R7
);
static_assert(ProtectedABI::kExitSyscall == 3);
static_assert(ProtectedABI::kHardwareWriteSyscall == 20);
static_assert(ProtectedABI::kHardwareReadSyscall == 21);
static_assert(ProtectedABI::kFilesystemServiceBase == 30);
static_assert(ProtectedABI::kFilesystemServiceEndExclusive == 40);
static_assert(ProtectedABI::kNetworkServiceBase == 40);
static_assert(ProtectedABI::kNetworkServiceEndExclusive == 50);
static_assert(ProtectedABI::kStatusOk == 0);
static_assert(ProtectedABI::kStatusUnsupported == -1);
static_assert(ProtectedABI::kStatusInvalidHardwareOffset == -2);
static_assert(ProtectedABI::kStatusHardwareUnavailable == -3);
static_assert(ProtectedABI::kStatusHardwareError == -4);

namespace {

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;
    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembler.assembleString(
            source
        ).toBinaryProgram(),
        name
    );
}

class ObservingSyscallHandler final
    : public zero_cpu::SoftwareInterruptHandler {
public:
    bool handles(
        std::uint8_t vector
    ) const override {
        return dispatcher_.handles(vector);
    }

    zero_cpu::SoftwareInterruptResult handle(
        std::uint8_t vector,
        zero_cpu::CPUState& state,
        zero_cpu::MMIOBus* mmioBus
    ) override {
        ++handle_count_;

        if (
            state.privilegeLevel()
            != zero_cpu::PrivilegeLevel::Kernel
        ) {
            all_kernel_mode_ = false;
        }

        return dispatcher_.handle(
            vector,
            state,
            mmioBus
        );
    }

    std::size_t handleCount() const {
        return handle_count_;
    }

    bool allKernelMode() const {
        return all_kernel_mode_;
    }

private:
    zero_cpu::kernel::ProtectedSyscallDispatcher
        dispatcher_;

    std::size_t handle_count_ = 0;
    bool all_kernel_mode_ = true;
};

struct HardwareFixture {
    std::shared_ptr<
        zero_cpu::hardware::MockHardwareBus
    > hardware;

    std::shared_ptr<zero_cpu::MMIOBus> mmio;
};

HardwareFixture makeHardwareFixture() {
    HardwareFixture fixture;

    fixture.hardware =
        std::make_shared<
            zero_cpu::hardware::MockHardwareBus
        >();

    fixture.hardware->connect();

    auto device =
        std::make_shared<
            zero_cpu::hardware::HardwareMMIODevice
        >(fixture.hardware);

    fixture.mmio =
        std::make_shared<
            zero_cpu::MMIOBus
        >();

    fixture.mmio->mapDevice(
        zero_cpu::memory_map::kHardwareBase,
        zero_cpu::memory_map::kHardwareSize,
        device
    );

    return fixture;
}

bool protectedHardwareSyscalls(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::system;

    HardwareFixture fixture =
        makeHardwareFixture();

    fixture.hardware->setRegisterValue(
        memory_map::kHardwareGpioInputOffset,
        77
    );

    auto handler =
        std::make_shared<
            ObservingSyscallHandler
        >();

    const char* firstSource = R"ASM(
.entry start
.text
start:
    MOV R1, 20
    MOV R2, 0
    MOV R3, 42
    INT 80
    MOV R5, R4

    MOV R1, 21
    MOV R2, 8
    INT 80
    STORE [100], R2
    MOV R6, R4
)ASM";

    const char* secondSource = R"ASM(
.entry start
.text
start:
    MOV R1, 20
    MOV R2, 16
    MOV R3, 99
    INT 80
    MOV R5, R4
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 100;
    options.mmio_bus = fixture.mmio;
    options.software_interrupt_handler =
        handler;

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    firstSource,
                    "syscall-hardware-a.zbin"
                ),
                makeImage(
                    secondSource,
                    "syscall-hardware-b.zbin"
                )
            },
            options
        );

    if (
        !result.success()
        || result.fault_count != 0
    ) {
        detail =
            "protected syscall workload did not "
            "complete successfully";
        return false;
    }

    if (
        handler->handleCount() != 3
        || !handler->allKernelMode()
    ) {
        detail =
            "INT 80 host dispatcher did not run "
            "three times in Kernel mode";
        return false;
    }

    if (
        fixture.hardware->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) != 42
        || fixture.hardware->registerValue(
            memory_map::kHardwarePwmOutputOffset
        ) != 99
    ) {
        detail =
            "hardware write syscalls did not reach "
            "the mapped HardwareBus";
        return false;
    }

    if (
        fixture.hardware->writeCount() != 2
        || fixture.hardware->readCount() != 1
    ) {
        detail =
            "unexpected hardware transaction counts";
        return false;
    }

    const ProcessRunSummary& first =
        result.process(1);

    const ProcessRunSummary& second =
        result.process(2);

    if (
        first.final_memory.read(100) != 77
        || first.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R5
            )
        ] != 0
        || first.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R6
            )
        ] != 0
        || second.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R5
            )
        ] != 0
    ) {
        detail =
            "syscall return value/status was not "
            "preserved in the owning process";
        return false;
    }

    if (
        first.final_context.privilege
            != PrivilegeLevel::User
        || second.final_context.privilege
            != PrivilegeLevel::User
    ) {
        detail =
            "process did not return to User mode "
            "after protected syscall";
        return false;
    }

    return true;
}

bool directUserMmioIsBlocked(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::system;

    HardwareFixture fixture =
        makeHardwareFixture();

    auto handler =
        std::make_shared<
            ObservingSyscallHandler
        >();

    const char* source = R"ASM(
.entry start
.text
start:
    MOV R1, 123
    STORE [61952], R1
    MOV R2, 1
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 20;
    options.mmio_bus = fixture.mmio;
    options.software_interrupt_handler =
        handler;

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    source,
                    "direct-user-mmio.zbin"
                )
            },
            options
        );

    if (
        !result.completed()
        || result.fault_count != 1
        || !result.process(1).faulted()
    ) {
        detail =
            "direct User-mode hardware MMIO access "
            "was not isolated as a process fault";
        return false;
    }

    if (
        fixture.hardware->writeCount() != 0
        || handler->handleCount() != 0
    ) {
        detail =
            "blocked direct MMIO access reached "
            "the kernel or hardware bus";
        return false;
    }

    return true;
}

// Patch: v1.4-protected-syscall-hardware-r1

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Protected Syscall "
           "Hardware Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
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
            "Protected syscall hardware bridge",
            protectedHardwareSyscalls(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Direct User MMIO protection",
            directUserMmioIsBlocked(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Protected syscall hardware test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Protected syscall hardware test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
