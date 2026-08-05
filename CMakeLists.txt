cmake_minimum_required(VERSION 3.20)

project(ZeroCPU LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


option(
    ZERO_CPU_ENABLE_SANITIZERS
    "Enable runtime sanitizers for Zero-CPU targets"
    OFF
)

function(zero_cpu_enable_sanitizers target_name)
    if (NOT ZERO_CPU_ENABLE_SANITIZERS)
        return()
    endif()

    get_target_property(target_type ${target_name} TYPE)

    if (MSVC)
        target_compile_options(${target_name}
            PRIVATE
                /fsanitize=address
                /Zi
        )

        if (NOT target_type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target_name}
                PRIVATE
                    /INCREMENTAL:NO
            )
        endif()
    elseif (
        CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
    )
        target_compile_options(${target_name}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
                -fno-sanitize-recover=all
        )

        if (NOT target_type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target_name}
                PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
                    -fno-sanitize-recover=all
            )
        endif()
    else()
        message(
            WARNING
            "ZERO_CPU_ENABLE_SANITIZERS is ON, but compiler "
            "${CMAKE_CXX_COMPILER_ID} is not configured."
        )
    endif()
endfunction()

add_library(zero_cpu_core
    src/core/Flags.cpp
    src/core/RegisterFile.cpp
    src/core/Memory.cpp
    src/core/PrivilegeLevel.cpp
    src/core/CPUState.cpp
    src/core/ALU.cpp
    src/core/MMIOBus.cpp
    src/core/DebugOutputDevice.cpp
    src/core/InterruptController.cpp
    src/core/TimerDevice.cpp
    src/core/CPU.cpp

    src/hardware/MockHardwareBus.cpp
    src/hardware/HardwareMMIODevice.cpp
    src/hardware/HardwareProtocol.cpp
    src/hardware/MockSerialTransport.cpp
    src/hardware/SerialHardwareBus.cpp
    src/hardware/WindowsSerialTransport.cpp

    src/isa/Opcode.cpp
    src/isa/Operand.cpp
    src/isa/Instruction.cpp
    src/isa/InstructionEncoder.cpp
    src/isa/InstructionDecoder.cpp

    src/trace/TraceEvent.cpp
    src/trace/TraceLogger.cpp
    src/trace/TraceJsonWriter.cpp
    src/trace/TraceJsonDiff.cpp

    src/assembler/Assembler.cpp

    src/binary/BinaryWriter.cpp
    src/binary/BinaryReader.cpp
    src/binary/BinaryLoader.cpp

    src/system/BioOSRunner.cpp
)

target_include_directories(zero_cpu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

zero_cpu_enable_sanitizers(zero_cpu_core)

add_executable(zero_cli
    tools/zero_cli.cpp
)

target_link_libraries(zero_cli
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_cli)


add_executable(zero_isa_conformance
    tests/isa_conformance.cpp
)

target_link_libraries(zero_isa_conformance
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_isa_conformance)


add_executable(zero_privilege_state_test
    tests/privilege_state.cpp
)

target_link_libraries(zero_privilege_state_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_privilege_state_test)

if (BUILD_TESTING)
    add_test(
        NAME zero_cpu.privilege_state
        COMMAND $<TARGET_FILE:zero_privilege_state_test>
    )

    set_tests_properties(zero_cpu.privilege_state
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;state"
    )

    add_test(
        NAME zero_cpu.isa_conformance
        COMMAND $<TARGET_FILE:zero_isa_conformance>
    )

    set_tests_properties(zero_cpu.isa_conformance
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;isa;conformance"
    )

    function(zero_cpu_add_cli_test test_name)
        add_test(
            NAME ${test_name}
            COMMAND $<TARGET_FILE:zero_cli> ${ARGN}
        )

        set_tests_properties(${test_name}
            PROPERTIES
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                TIMEOUT 60
                LABELS "zero-cpu;cli"
        )
    endfunction()

    zero_cpu_add_cli_test(zero_cpu.alu alu-test)
    zero_cpu_add_cli_test(
        zero_cpu.signed_branch
        signed-branch-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.differential
        differential-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.error_invariant
        error-invariant-test
    )

    zero_cpu_add_cli_test(
        zero_cpu.trace_json
        trace-json-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.trace_diff
        trace-diff-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.trace_golden
        trace-golden-test
    )

    zero_cpu_add_cli_test(zero_cpu.mmio mmio-test)
    zero_cpu_add_cli_test(
        zero_cpu.hardware_bus_mock
        hardware-bus-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.serial_hardware_mock
        serial-hardware-test
    )

    zero_cpu_add_cli_test(
        zero_cpu.interrupt_controller
        interrupt-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.cpu_interrupt
        cpu-interrupt-test
    )
    zero_cpu_add_cli_test(zero_cpu.timer timer-test)
    zero_cpu_add_cli_test(
        zero_cpu.cpu_timer
        cpu-timer-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.cpu_ei_di
        cpu-ei-di-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.software_interrupt
        software-interrupt-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.interrupt_flags_restore
        interrupt-flags-restore-test
    )

    zero_cpu_add_cli_test(
        zero_cpu.register_indirect
        register-indirect-test
    )

    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_1
        mini-kernel-syscall-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_2
        mini-kernel-syscall2-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_3
        mini-kernel-syscall3-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_4
        mini-kernel-syscall4-timer-read-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_5
        mini-kernel-syscall5-timer-enable-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_6
        mini-kernel-syscall6-timer-disable-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_syscall_7
        mini-kernel-syscall7-timer-configure-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.mini_kernel_timer_lifecycle
        mini-kernel-timer-lifecycle-test
    )

    zero_cpu_add_cli_test(
        zero_cpu.bio_os_combined_boot
        bio-os-combined-boot-test
    )
    zero_cpu_add_cli_test(
        zero_cpu.binary_round_trip
        binary-test
    )
endif()

if (WIN32)
    add_executable(zero_studio WIN32
        studio/zero_studio.cpp
    )

    target_link_libraries(zero_studio
        PRIVATE
            zero_cpu_core
            user32
            gdi32
    )

    zero_cpu_enable_sanitizers(zero_studio)
endif()