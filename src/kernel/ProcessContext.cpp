#include "zero_cpu/kernel/ProcessContext.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"

#include <stdexcept>

namespace zero_cpu::kernel {
namespace {

void validatePrivilege(PrivilegeLevel privilege) {
    switch (privilege) {
    case PrivilegeLevel::Kernel:
    case PrivilegeLevel::User:
        return;
    }

    throw std::runtime_error(
        "Process context contains invalid privilege"
    );
}

void validateUserCodeRange(
    const ProcessContext& context
) {
    if (!context.has_user_code_range) {
        if (context.privilege == PrivilegeLevel::User) {
            throw std::runtime_error(
                "User process context requires a code range"
            );
        }

        return;
    }

    if (
        context.user_code_begin
        >= context.user_code_end_exclusive
    ) {
        throw std::runtime_error(
            "Process context User code range must be non-empty"
        );
    }

    if (
        context.privilege == PrivilegeLevel::User
        && (
            context.pc < context.user_code_begin
            || context.pc
                >= context.user_code_end_exclusive
        )
    ) {
        throw std::runtime_error(
            "Process context PC is outside User code range"
        );
    }
}

void validateUserStackRange(
    const ProcessContext& context
) {
    if (
        context.user_stack_begin
        >= context.user_stack_end_exclusive
    ) {
        throw std::runtime_error(
            "Process context User stack range must be non-empty"
        );
    }

    if (
        context.user_stack_begin
            < memory_map::kUserStackBase
        || context.user_stack_end_exclusive
            > memory_map::kUserStackEndExclusive
    ) {
        throw std::runtime_error(
            "Process context User stack range is outside "
            "protected User stack memory"
        );
    }

    if (
        context.user_stack_begin % CPU::kStackSlotSize != 0
        || context.user_stack_end_exclusive
            % CPU::kStackSlotSize != 0
    ) {
        throw std::runtime_error(
            "Process context User stack range is not "
            "slot-aligned"
        );
    }

    if (
        context.sp < context.user_stack_begin
        || context.sp
            > context.user_stack_end_exclusive
    ) {
        throw std::runtime_error(
            "Process context SP is outside User stack range"
        );
    }

    if (
        (context.sp - context.user_stack_begin)
            % CPU::kStackSlotSize != 0
    ) {
        throw std::runtime_error(
            "Process context SP is not slot-aligned"
        );
    }
}

void validateKernelStackPointer(
    const ProcessContext& context
) {
    if (
        !memory_map::isKernelStackPointer(
            context.kernel_stack_pointer
        )
    ) {
        throw std::runtime_error(
            "Process context Kernel SP is outside "
            "Kernel stack range"
        );
    }

    if (
        (context.kernel_stack_pointer
            - memory_map::kKernelStackBase)
            % CPU::kStackSlotSize != 0
    ) {
        throw std::runtime_error(
            "Process context Kernel SP is not slot-aligned"
        );
    }
}

void requireContextSwitchableCPU(const CPU& cpu) {
    if (cpu.usingKernelInterruptStack()) {
        throw std::runtime_error(
            "Cannot capture or restore process context "
            "while Kernel interrupt stack is active"
        );
    }
}

} // namespace

void validateProcessContext(
    const ProcessContext& context
) {
    if (context.pid == 0) {
        throw std::runtime_error(
            "Process context PID must be non-zero"
        );
    }

    validatePrivilege(context.privilege);
    validateUserCodeRange(context);
    validateUserStackRange(context);
    validateKernelStackPointer(context);
}

void validateProcessContextForCPU(
    const ProcessContext& context,
    const CPU& cpu
) {
    validateProcessContext(context);

    if (context.has_user_code_range) {
        cpu.validateUserCodeRange(
            context.user_code_begin,
            context.user_code_end_exclusive
        );
    }
}

ProcessContext captureProcessContext(
    ProcessId pid,
    const CPU& cpu
) {
    requireContextSwitchableCPU(cpu);

    ProcessContext context;
    context.pid = pid;

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        context.registers[index] =
            cpu.state().registers().get(
                static_cast<RegisterName>(index)
            );
    }

    context.flags = cpu.state().flags().raw();
    context.pc = cpu.state().pc();
    context.sp = cpu.state().sp();
    context.privilege =
        cpu.state().privilegeLevel();

    context.has_user_code_range =
        cpu.hasUserCodeRange();

    if (context.has_user_code_range) {
        context.user_code_begin =
            cpu.userCodeBegin();
        context.user_code_end_exclusive =
            cpu.userCodeEndExclusive();
    }

    context.user_stack_begin =
        memory_map::kUserStackBase;
    context.user_stack_end_exclusive =
        memory_map::kUserStackEndExclusive;

    context.kernel_stack_pointer =
        cpu.kernelStackPointer();

    validateProcessContext(context);
    return context;
}

void restoreProcessContext(
    const ProcessContext& context,
    CPU& cpu
) {
    validateProcessContextForCPU(
        context,
        cpu
    );

    requireContextSwitchableCPU(cpu);

    if (context.has_user_code_range) {
        cpu.setUserCodeRange(
            context.user_code_begin,
            context.user_code_end_exclusive
        );
    } else {
        cpu.clearUserCodeRange();
    }

    cpu.setKernelStackPointer(
        context.kernel_stack_pointer
    );

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        cpu.state().registers().set(
            static_cast<RegisterName>(index),
            context.registers[index]
        );
    }

    cpu.state().flags().setRaw(context.flags);
    cpu.state().setPc(context.pc);
    cpu.state().setSp(context.sp);
    cpu.state().setPrivilegeLevel(
        context.privilege
    );

    cpu.state().clearError();
    cpu.state().setHalted(false);
}

} // namespace zero_cpu::kernel
