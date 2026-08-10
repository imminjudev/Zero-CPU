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


option(
    ZERO_CPU_ENABLE_COVERAGE
    "Enable GCC coverage instrumentation for Zero-CPU targets"
    OFF
)

if (
    ZERO_CPU_ENABLE_SANITIZERS
    AND ZERO_CPU_ENABLE_COVERAGE
)
    message(
        FATAL_ERROR
        "ZERO_CPU_ENABLE_SANITIZERS and "
        "ZERO_CPU_ENABLE_COVERAGE cannot be enabled together."
    )
endif()

function(zero_cpu_enable_coverage target_name)
    if (NOT ZERO_CPU_ENABLE_COVERAGE)
        return()
    endif()

    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(
            FATAL_ERROR
            "ZERO_CPU_ENABLE_COVERAGE requires GCC. "
            "Current compiler: ${CMAKE_CXX_COMPILER_ID}"
        )
    endif()

    get_target_property(target_type ${target_name} TYPE)

    target_compile_options(${target_name}
        PRIVATE
            --coverage
            -O0
            -g
    )

    if (NOT target_type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target_name}
            PRIVATE
                --coverage
        )
    endif()
endfunction()

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

    src/debug/DebugSymbols.cpp
    src/debug/DebugCondition.cpp
    src/debug/DebugSession.cpp
    src/debug/DebugInspector.cpp
    src/debug/DebugConsole.cpp
    src/debug/DebugSnapshotJson.cpp
    src/debug/MultiProcessDebugSession.cpp
    src/debug/MultiProcessDebugConsole.cpp

    src/studio/StudioDebugBackend.cpp
    src/studio/StudioMultiProcessDebugBackend.cpp

    src/assembler/Assembler.cpp

    src/binary/BinaryWriter.cpp
    src/binary/BinaryReader.cpp
    src/binary/BinaryLoader.cpp

    src/kernel/ProcessState.cpp
    src/kernel/ProcessContext.cpp
    src/kernel/ProcessControlBlock.cpp
    src/kernel/ProcessTable.cpp
    src/kernel/RoundRobinScheduler.cpp
    src/kernel/ProcessDispatcher.cpp
    src/kernel/TimerPreemptiveScheduler.cpp
    src/kernel/ProcessTermination.cpp
    src/kernel/ProcessLifecycleManager.cpp
    src/kernel/ExecutableMetadata.cpp
    src/kernel/ProcessImage.cpp
    src/kernel/ProcessImageLoader.cpp
    src/kernel/ProcessAddressSpace.cpp
    src/kernel/ProtectedSyscallDispatcher.cpp
    src/system/MultiProcessRunner.cpp
    src/system/MultiProcessInvariantVerifier.cpp
    src/system/MultiProcessTraceJsonWriter.cpp
    src/system/BioOSRunner.cpp
)

target_include_directories(zero_cpu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

zero_cpu_enable_sanitizers(zero_cpu_core)
zero_cpu_enable_coverage(zero_cpu_core)

add_executable(zero_cli
    tools/zero_cli.cpp
)

target_link_libraries(zero_cli
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_cli)
zero_cpu_enable_coverage(zero_cli)


add_executable(zero_isa_conformance
    tests/isa_conformance.cpp
)

target_link_libraries(zero_isa_conformance
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_isa_conformance)
zero_cpu_enable_coverage(zero_isa_conformance)


add_executable(zero_privilege_state_test
    tests/privilege_state.cpp
)

target_link_libraries(zero_privilege_state_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(zero_privilege_state_test)
zero_cpu_enable_coverage(zero_privilege_state_test)


add_executable(zero_privileged_instruction_test
    tests/privileged_instruction.cpp
)

target_link_libraries(zero_privileged_instruction_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_privileged_instruction_test
)
zero_cpu_enable_coverage(
    zero_privileged_instruction_test
)


add_executable(zero_interrupt_privilege_test
    tests/interrupt_privilege.cpp
)

target_link_libraries(zero_interrupt_privilege_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_interrupt_privilege_test
)
zero_cpu_enable_coverage(
    zero_interrupt_privilege_test
)


add_executable(zero_memory_protection_test
    tests/memory_protection.cpp
)

target_link_libraries(zero_memory_protection_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_memory_protection_test
)
zero_cpu_enable_coverage(
    zero_memory_protection_test
)


add_executable(zero_execution_protection_test
    tests/execution_protection.cpp
)

target_link_libraries(zero_execution_protection_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_execution_protection_test
)
zero_cpu_enable_coverage(
    zero_execution_protection_test
)


add_executable(zero_kernel_stack_test
    tests/kernel_stack.cpp
)

target_link_libraries(zero_kernel_stack_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_kernel_stack_test
)
zero_cpu_enable_coverage(
    zero_kernel_stack_test
)


add_executable(zero_process_context_test
    tests/process_context.cpp
)

target_link_libraries(zero_process_context_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_context_test
)

zero_cpu_enable_coverage(
    zero_process_context_test
)


add_executable(zero_process_table_test
    tests/process_table.cpp
)

target_link_libraries(zero_process_table_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_table_test
)

zero_cpu_enable_coverage(
    zero_process_table_test
)


add_executable(zero_round_robin_scheduler_test
    tests/round_robin_scheduler.cpp
)

target_link_libraries(
    zero_round_robin_scheduler_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_round_robin_scheduler_test
)

zero_cpu_enable_coverage(
    zero_round_robin_scheduler_test
)


add_executable(zero_process_dispatcher_test
    tests/process_dispatcher.cpp
)

target_link_libraries(
    zero_process_dispatcher_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_dispatcher_test
)

zero_cpu_enable_coverage(
    zero_process_dispatcher_test
)


add_executable(zero_timer_preemptive_scheduler_test
    tests/timer_preemptive_scheduler.cpp
)

target_link_libraries(
    zero_timer_preemptive_scheduler_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_timer_preemptive_scheduler_test
)

zero_cpu_enable_coverage(
    zero_timer_preemptive_scheduler_test
)


add_executable(zero_process_lifecycle_test
    tests/process_lifecycle.cpp
)

target_link_libraries(
    zero_process_lifecycle_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_lifecycle_test
)

zero_cpu_enable_coverage(
    zero_process_lifecycle_test
)


add_executable(zero_process_image_loader_test
    tests/process_image_loader.cpp
)

target_link_libraries(
    zero_process_image_loader_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_image_loader_test
)

zero_cpu_enable_coverage(
    zero_process_image_loader_test
)


add_executable(zero_process_address_space_test
    tests/process_address_space.cpp
)

target_link_libraries(
    zero_process_address_space_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_process_address_space_test
)

zero_cpu_enable_coverage(
    zero_process_address_space_test
)


add_executable(zero_binary_data_section_test
    tests/binary_data_section.cpp
)

target_link_libraries(
    zero_binary_data_section_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_binary_data_section_test
)

zero_cpu_enable_coverage(
    zero_binary_data_section_test
)


add_executable(zero_assembler_data_directives_test
    tests/assembler_data_directives.cpp
)

target_link_libraries(
    zero_assembler_data_directives_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_assembler_data_directives_test
)

zero_cpu_enable_coverage(
    zero_assembler_data_directives_test
)


add_executable(zero_executable_entry_test
    tests/executable_entry.cpp
)

target_link_libraries(
    zero_executable_entry_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_executable_entry_test
)

zero_cpu_enable_coverage(
    zero_executable_entry_test
)


add_executable(zero_multi_process_runner_test
    tests/multi_process_runner.cpp
)

target_link_libraries(
    zero_multi_process_runner_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_multi_process_runner_test
)

zero_cpu_enable_coverage(
    zero_multi_process_runner_test
)


add_executable(zero_protected_syscall_hardware_test
    tests/protected_syscall_hardware.cpp
)

target_link_libraries(
    zero_protected_syscall_hardware_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_protected_syscall_hardware_test
)

zero_cpu_enable_coverage(
    zero_protected_syscall_hardware_test
)


add_executable(zero_protected_process_exit_test
    tests/protected_process_exit.cpp
)

target_link_libraries(
    zero_protected_process_exit_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_protected_process_exit_test
)

zero_cpu_enable_coverage(
    zero_protected_process_exit_test
)


add_executable(zero_multi_process_trace_test
    tests/multi_process_trace.cpp
)

target_link_libraries(
    zero_multi_process_trace_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_multi_process_trace_test
)

zero_cpu_enable_coverage(
    zero_multi_process_trace_test
)


add_executable(zero_multi_process_trace_diff_test
    tests/multi_process_trace_diff.cpp
)

target_link_libraries(
    zero_multi_process_trace_diff_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_multi_process_trace_diff_test
)

zero_cpu_enable_coverage(
    zero_multi_process_trace_diff_test
)


add_executable(zero_debug_session_test
    tests/debug_session.cpp
)

target_link_libraries(
    zero_debug_session_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_session_test
)

zero_cpu_enable_coverage(
    zero_debug_session_test
)


add_executable(zero_debug_inspector_test
    tests/debug_inspector.cpp
)

target_link_libraries(
    zero_debug_inspector_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_inspector_test
)

zero_cpu_enable_coverage(
    zero_debug_inspector_test
)


add_executable(zero_debug_console_test
    tests/debug_console.cpp
)

target_link_libraries(
    zero_debug_console_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_console_test
)

zero_cpu_enable_coverage(
    zero_debug_console_test
)


add_executable(zero_debug_watchpoint_test
    tests/debug_watchpoint.cpp
)

target_link_libraries(
    zero_debug_watchpoint_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_watchpoint_test
)

zero_cpu_enable_coverage(
    zero_debug_watchpoint_test
)


add_executable(zero_debug_conditional_breakpoint_test
    tests/debug_conditional_breakpoint.cpp
)

target_link_libraries(
    zero_debug_conditional_breakpoint_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_conditional_breakpoint_test
)

zero_cpu_enable_coverage(
    zero_debug_conditional_breakpoint_test
)


add_executable(zero_debug_symbols_test
    tests/debug_symbols.cpp
)

target_link_libraries(
    zero_debug_symbols_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_symbols_test
)

zero_cpu_enable_coverage(
    zero_debug_symbols_test
)


add_executable(zero_debug_multi_process_test
    tests/debug_multi_process.cpp
)

target_link_libraries(
    zero_debug_multi_process_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_multi_process_test
)

zero_cpu_enable_coverage(
    zero_debug_multi_process_test
)


add_executable(zero_debug_multi_process_controls_test
    tests/debug_multi_process_controls.cpp
)

target_link_libraries(
    zero_debug_multi_process_controls_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_multi_process_controls_test
)

zero_cpu_enable_coverage(
    zero_debug_multi_process_controls_test
)


add_executable(zero_debug_snapshot_json_test
    tests/debug_snapshot_json.cpp
)

target_link_libraries(
    zero_debug_snapshot_json_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_debug_snapshot_json_test
)

zero_cpu_enable_coverage(
    zero_debug_snapshot_json_test
)


add_executable(zero_studio_debug_backend_test
    tests/studio_debug_backend.cpp
)

target_link_libraries(
    zero_studio_debug_backend_test
    PRIVATE
        zero_cpu_core
)

zero_cpu_enable_sanitizers(
    zero_studio_debug_backend_test
)

zero_cpu_enable_coverage(
    zero_studio_debug_backend_test
)

if (BUILD_TESTING)
    add_test(
        NAME zero_cpu.studio_debug_backend
        COMMAND $<TARGET_FILE:zero_studio_debug_backend_test>
    )

    set_tests_properties(
        zero_cpu.studio_debug_backend
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;studio;debugger;session;snapshot"
    )

    add_test(
        NAME zero_cpu.debug_snapshot_json
        COMMAND $<TARGET_FILE:zero_debug_snapshot_json_test>
    )

    set_tests_properties(
        zero_cpu.debug_snapshot_json
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;snapshot;json;studio"
    )

    add_test(
        NAME zero_cpu.debug_multi_process_controls
        COMMAND $<TARGET_FILE:zero_debug_multi_process_controls_test>
    )

    set_tests_properties(
        zero_cpu.debug_multi_process_controls
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;process;breakpoint;watchpoint;condition"
    )

    add_test(
        NAME zero_cpu.debug_multi_process
        COMMAND $<TARGET_FILE:zero_debug_multi_process_test>
    )

    set_tests_properties(
        zero_cpu.debug_multi_process
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;process;scheduler;multitasking"
    )

    add_test(
        NAME zero_cpu.debug_symbols
        COMMAND $<TARGET_FILE:zero_debug_symbols_test>
    )

    set_tests_properties(
        zero_cpu.debug_symbols
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;symbols;labels;sidecar"
    )

    add_test(
        NAME zero_cpu.debug_conditional_breakpoint
        COMMAND $<TARGET_FILE:zero_debug_conditional_breakpoint_test>
    )

    set_tests_properties(
        zero_cpu.debug_conditional_breakpoint
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;breakpoint;condition;expression"
    )

    add_test(
        NAME zero_cpu.debug_watchpoint
        COMMAND $<TARGET_FILE:zero_debug_watchpoint_test>
    )

    set_tests_properties(
        zero_cpu.debug_watchpoint
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;watchpoint;memory;trace"
    )

    add_test(
        NAME zero_cpu.debug_console
        COMMAND $<TARGET_FILE:zero_debug_console_test>
    )

    set_tests_properties(
        zero_cpu.debug_console
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;console;repl;interactive"
    )

    add_test(
        NAME zero_cpu.debug_inspector
        COMMAND $<TARGET_FILE:zero_debug_inspector_test>
    )

    set_tests_properties(
        zero_cpu.debug_inspector
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;registers;memory;disassembly"
    )

    add_test(
        NAME zero_cpu.debug_session
        COMMAND $<TARGET_FILE:zero_debug_session_test>
    )

    set_tests_properties(
        zero_cpu.debug_session
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;debugger;breakpoint;step;binary"
    )

    add_test(
        NAME zero_cpu.multi_process_runner
        COMMAND $<TARGET_FILE:zero_multi_process_runner_test>
    )

    set_tests_properties(
        zero_cpu.multi_process_runner
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;system;process;runner;multitasking"
    )

    add_test(
        NAME zero_cpu.protected_syscall_hardware
        COMMAND $<TARGET_FILE:zero_protected_syscall_hardware_test>
    )

    set_tests_properties(
        zero_cpu.protected_syscall_hardware
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;syscall;process;mmio;hardware"
    )

    add_test(
        NAME zero_cpu.protected_process_exit
        COMMAND $<TARGET_FILE:zero_protected_process_exit_test>
    )

    set_tests_properties(
        zero_cpu.protected_process_exit
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;syscall;process;lifecycle;scheduler"
    )

    add_test(
        NAME zero_cpu.multi_process_trace
        COMMAND $<TARGET_FILE:zero_multi_process_trace_test>
    )

    set_tests_properties(
        zero_cpu.multi_process_trace
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;system;process;trace;json;invariant"
    )

    add_test(
        NAME zero_cpu.multi_process_trace_diff
        COMMAND $<TARGET_FILE:zero_multi_process_trace_diff_test>
    )

    set_tests_properties(
        zero_cpu.multi_process_trace_diff
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;system;process;trace;diff;golden"
    )

    add_test(
        NAME zero_cpu.executable_entry
        COMMAND $<TARGET_FILE:zero_executable_entry_test>
    )

    set_tests_properties(
        zero_cpu.executable_entry
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;assembler;binary;entry;cli"
    )

    add_test(
        NAME zero_cpu.assembler_data_directives
        COMMAND $<TARGET_FILE:zero_assembler_data_directives_test>
    )

    set_tests_properties(
        zero_cpu.assembler_data_directives
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;assembler;binary;data;labels"
    )

    add_test(
        NAME zero_cpu.binary_data_section
        COMMAND $<TARGET_FILE:zero_binary_data_section_test>
    )

    set_tests_properties(
        zero_cpu.binary_data_section
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;binary;format;data;process"
    )

    add_test(
        NAME zero_cpu.process_address_space
        COMMAND $<TARGET_FILE:zero_process_address_space_test>
    )

    set_tests_properties(
        zero_cpu.process_address_space
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;memory;executable"
    )

    add_test(
        NAME zero_cpu.process_image_loader
        COMMAND $<TARGET_FILE:zero_process_image_loader_test>
    )

    set_tests_properties(
        zero_cpu.process_image_loader
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;image;executable"
    )

    add_test(
        NAME zero_cpu.process_lifecycle
        COMMAND $<TARGET_FILE:zero_process_lifecycle_test>
    )

    set_tests_properties(
        zero_cpu.process_lifecycle
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;lifecycle"
    )

    add_test(
        NAME zero_cpu.timer_preemptive_scheduler
        COMMAND $<TARGET_FILE:zero_timer_preemptive_scheduler_test>
    )

    set_tests_properties(
        zero_cpu.timer_preemptive_scheduler
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;scheduler;timer;preemption"
    )

    add_test(
        NAME zero_cpu.process_dispatcher
        COMMAND $<TARGET_FILE:zero_process_dispatcher_test>
    )

    set_tests_properties(
        zero_cpu.process_dispatcher
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;dispatch"
    )

    add_test(
        NAME zero_cpu.round_robin_scheduler
        COMMAND $<TARGET_FILE:zero_round_robin_scheduler_test>
    )

    set_tests_properties(
        zero_cpu.round_robin_scheduler
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;scheduler;round-robin"
    )

    add_test(
        NAME zero_cpu.process_table
        COMMAND $<TARGET_FILE:zero_process_table_test>
    )

    set_tests_properties(
        zero_cpu.process_table
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;table"
    )

    add_test(
        NAME zero_cpu.process_context
        COMMAND $<TARGET_FILE:zero_process_context_test>
    )

    set_tests_properties(
        zero_cpu.process_context
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;kernel;process;context"
    )

    add_test(
        NAME zero_cpu.kernel_stack
        COMMAND $<TARGET_FILE:zero_kernel_stack_test>
    )

    set_tests_properties(
        zero_cpu.kernel_stack
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;stack"
    )

    add_test(
        NAME zero_cpu.execution_protection
        COMMAND $<TARGET_FILE:zero_execution_protection_test>
    )

    set_tests_properties(
        zero_cpu.execution_protection
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;execution"
    )

    add_test(
        NAME zero_cpu.memory_protection
        COMMAND $<TARGET_FILE:zero_memory_protection_test>
    )

    set_tests_properties(
        zero_cpu.memory_protection
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;memory"
    )

    add_test(
        NAME zero_cpu.interrupt_privilege
        COMMAND $<TARGET_FILE:zero_interrupt_privilege_test>
    )

    set_tests_properties(
        zero_cpu.interrupt_privilege
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;interrupt"
    )

    add_test(
        NAME zero_cpu.privileged_instruction
        COMMAND $<TARGET_FILE:zero_privileged_instruction_test>
    )

    set_tests_properties(
        zero_cpu.privileged_instruction
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 60
            LABELS "zero-cpu;privilege;instruction"
    )

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
    zero_cpu_enable_coverage(zero_studio)
endif()