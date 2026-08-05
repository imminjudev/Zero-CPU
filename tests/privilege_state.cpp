#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"
#include "zero_cpu/trace/TraceJsonWriter.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Privilege State Test ===\n\n";

    int failures = 0;

    auto expect = [&](const std::string& name, bool passed) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";
        if (!passed) {
            ++failures;
        }
    };

    CPUState state;

    expect(
        "CPUState starts in Kernel mode",
        state.privilegeLevel() == PrivilegeLevel::Kernel
    );
    expect("isKernelMode helper", state.isKernelMode());
    expect("isUserMode initially false", !state.isUserMode());
    expect(
        "Kernel string conversion",
        privilegeLevelToString(PrivilegeLevel::Kernel) ==
            "Kernel"
    );
    expect(
        "User string conversion",
        privilegeLevelToString(PrivilegeLevel::User) ==
            "User"
    );

    state.setPrivilegeLevel(PrivilegeLevel::User);

    expect(
        "setPrivilegeLevel enters User mode",
        state.privilegeLevel() == PrivilegeLevel::User
    );
    expect("isUserMode helper", state.isUserMode());
    expect("isKernelMode becomes false", !state.isKernelMode());
    expect(
        "summary displays User privilege",
        state.summary().find("Privilege = User") !=
            std::string::npos
    );

    const CPUState copiedState = state;
    expect(
        "CPUState copy preserves privilege",
        copiedState.privilegeLevel() ==
            PrivilegeLevel::User
    );

    state.reset();

    expect(
        "reset returns to Kernel mode",
        state.privilegeLevel() == PrivilegeLevel::Kernel
    );
    expect(
        "summary displays Kernel privilege",
        state.summary().find("Privilege = Kernel") !=
            std::string::npos
    );

    CPU cpu;
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);
    cpu.loadProgram(
        {Instruction(Opcode::HALT)},
        {}
    );

    expect(
        "loadProgram resets privilege to Kernel",
        cpu.state().privilegeLevel() ==
            PrivilegeLevel::Kernel
    );

    CPUState before;
    CPUState after = before;
    after.setPrivilegeLevel(PrivilegeLevel::User);

    TraceEvent event(
        before,
        Instruction(Opcode::NOP),
        after
    );

    expect(
        "TraceEvent preserves Kernel before-state",
        event.before().privilegeLevel() ==
            PrivilegeLevel::Kernel
    );
    expect(
        "TraceEvent preserves User after-state",
        event.after().privilegeLevel() ==
            PrivilegeLevel::User
    );

    TraceJsonMetadata metadata;
    metadata.execution_mode = "PrivilegeStateTest";

    const std::string json = TraceJsonWriter::toJson(
        std::vector<TraceEvent>{event},
        metadata
    );

    expect(
        "trace schema version is 3",
        json.find("\"schema_version\": 3") !=
            std::string::npos
    );
    expect(
        "trace JSON contains Kernel privilege",
        json.find("\"privilege\": \"Kernel\"") !=
            std::string::npos
    );
    expect(
        "trace JSON contains User privilege",
        json.find("\"privilege\": \"User\"") !=
            std::string::npos
    );

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Privilege state test finished successfully.\n";
        return 0;
    }

    std::cout
        << "Privilege state test failed. Failure count: "
        << failures
        << "\n";
    return 1;
}
