@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU Test Suite
echo ========================================
echo.

echo [1/60] Building project...
cmake --build build
if errorlevel 1 goto fail

set "ZERO_CLI=build\Debug\zero_cli.exe"

if not exist "%ZERO_CLI%" (
    set "ZERO_CLI=build\Release\zero_cli.exe"
)

if not exist "%ZERO_CLI%" (
    echo.
    echo ERROR: zero_cli.exe not found.
    echo Tried:
    echo   build\Debug\zero_cli.exe
    echo   build\Release\zero_cli.exe
    goto fail
)

set "ZERO_ISA_CONFORMANCE=build\Debug\zero_isa_conformance.exe"

if not exist "%ZERO_ISA_CONFORMANCE%" (
    set "ZERO_ISA_CONFORMANCE=build\Release\zero_isa_conformance.exe"
)

if not exist "%ZERO_ISA_CONFORMANCE%" (
    echo.
    echo ERROR: zero_isa_conformance.exe not found.
    echo Tried:
    echo   build\Debug\zero_isa_conformance.exe
    echo   build\Release\zero_isa_conformance.exe
    goto fail
)

set "ZERO_PRIVILEGE_TEST=build\Debug\zero_privilege_state_test.exe"

if not exist "%ZERO_PRIVILEGE_TEST%" (
    set "ZERO_PRIVILEGE_TEST=build\Release\zero_privilege_state_test.exe"
)

if not exist "%ZERO_PRIVILEGE_TEST%" (
    echo.
    echo ERROR: zero_privilege_state_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_privilege_state_test.exe
    echo   build\Release\zero_privilege_state_test.exe
    goto fail
)

set "ZERO_PRIVILEGED_INSTRUCTION_TEST=build\Debug\zero_privileged_instruction_test.exe"

if not exist "%ZERO_PRIVILEGED_INSTRUCTION_TEST%" (
    set "ZERO_PRIVILEGED_INSTRUCTION_TEST=build\Release\zero_privileged_instruction_test.exe"
)

if not exist "%ZERO_PRIVILEGED_INSTRUCTION_TEST%" (
    echo.
    echo ERROR: zero_privileged_instruction_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_privileged_instruction_test.exe
    echo   build\Release\zero_privileged_instruction_test.exe
    goto fail
)

set "ZERO_INTERRUPT_PRIVILEGE_TEST=build\Debug\zero_interrupt_privilege_test.exe"

if not exist "%ZERO_INTERRUPT_PRIVILEGE_TEST%" (
    set "ZERO_INTERRUPT_PRIVILEGE_TEST=build\Release\zero_interrupt_privilege_test.exe"
)

if not exist "%ZERO_INTERRUPT_PRIVILEGE_TEST%" (
    echo.
    echo ERROR: zero_interrupt_privilege_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_interrupt_privilege_test.exe
    echo   build\Release\zero_interrupt_privilege_test.exe
    goto fail
)

set "ZERO_MEMORY_PROTECTION_TEST=build\Debug\zero_memory_protection_test.exe"

if not exist "%ZERO_MEMORY_PROTECTION_TEST%" (
    set "ZERO_MEMORY_PROTECTION_TEST=build\Release\zero_memory_protection_test.exe"
)

if not exist "%ZERO_MEMORY_PROTECTION_TEST%" (
    echo.
    echo ERROR: zero_memory_protection_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_memory_protection_test.exe
    echo   build\Release\zero_memory_protection_test.exe
    goto fail
)

set "ZERO_EXECUTION_PROTECTION_TEST=build\Debug\zero_execution_protection_test.exe"

if not exist "%ZERO_EXECUTION_PROTECTION_TEST%" (
    set "ZERO_EXECUTION_PROTECTION_TEST=build\Release\zero_execution_protection_test.exe"
)

if not exist "%ZERO_EXECUTION_PROTECTION_TEST%" (
    echo.
    echo ERROR: zero_execution_protection_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_execution_protection_test.exe
    echo   build\Release\zero_execution_protection_test.exe
    goto fail
)

set "ZERO_KERNEL_STACK_TEST=build\Debug\zero_kernel_stack_test.exe"

if not exist "%ZERO_KERNEL_STACK_TEST%" (
    set "ZERO_KERNEL_STACK_TEST=build\Release\zero_kernel_stack_test.exe"
)

if not exist "%ZERO_KERNEL_STACK_TEST%" (
    echo.
    echo ERROR: zero_kernel_stack_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_kernel_stack_test.exe
    echo   build\Release\zero_kernel_stack_test.exe
    goto fail
)

set "ZERO_PROCESS_CONTEXT_TEST=build\Debug\zero_process_context_test.exe"

if not exist "%ZERO_PROCESS_CONTEXT_TEST%" (
    set "ZERO_PROCESS_CONTEXT_TEST=build\Release\zero_process_context_test.exe"
)

if not exist "%ZERO_PROCESS_CONTEXT_TEST%" (
    echo.
    echo ERROR: zero_process_context_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_context_test.exe
    echo   build\Release\zero_process_context_test.exe
    goto fail
)

set "ZERO_PROCESS_TABLE_TEST=build\Debug\zero_process_table_test.exe"

if not exist "%ZERO_PROCESS_TABLE_TEST%" (
    set "ZERO_PROCESS_TABLE_TEST=build\Release\zero_process_table_test.exe"
)

if not exist "%ZERO_PROCESS_TABLE_TEST%" (
    echo.
    echo ERROR: zero_process_table_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_table_test.exe
    echo   build\Release\zero_process_table_test.exe
    goto fail
)

set "ZERO_ROUND_ROBIN_SCHEDULER_TEST=build\Debug\zero_round_robin_scheduler_test.exe"

if not exist "%ZERO_ROUND_ROBIN_SCHEDULER_TEST%" (
    set "ZERO_ROUND_ROBIN_SCHEDULER_TEST=build\Release\zero_round_robin_scheduler_test.exe"
)

if not exist "%ZERO_ROUND_ROBIN_SCHEDULER_TEST%" (
    echo.
    echo ERROR: zero_round_robin_scheduler_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_round_robin_scheduler_test.exe
    echo   build\Release\zero_round_robin_scheduler_test.exe
    goto fail
)

set "ZERO_PROCESS_DISPATCHER_TEST=build\Debug\zero_process_dispatcher_test.exe"

if not exist "%ZERO_PROCESS_DISPATCHER_TEST%" (
    set "ZERO_PROCESS_DISPATCHER_TEST=build\Release\zero_process_dispatcher_test.exe"
)

if not exist "%ZERO_PROCESS_DISPATCHER_TEST%" (
    echo.
    echo ERROR: zero_process_dispatcher_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_dispatcher_test.exe
    echo   build\Release\zero_process_dispatcher_test.exe
    goto fail
)

set "ZERO_TIMER_PREEMPTIVE_SCHEDULER_TEST=build\Debug\zero_timer_preemptive_scheduler_test.exe"

if not exist "%ZERO_TIMER_PREEMPTIVE_SCHEDULER_TEST%" (
    set "ZERO_TIMER_PREEMPTIVE_SCHEDULER_TEST=build\Release\zero_timer_preemptive_scheduler_test.exe"
)

if not exist "%ZERO_TIMER_PREEMPTIVE_SCHEDULER_TEST%" (
    echo.
    echo ERROR: zero_timer_preemptive_scheduler_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_timer_preemptive_scheduler_test.exe
    echo   build\Release\zero_timer_preemptive_scheduler_test.exe
    goto fail
)

set "ZERO_PROCESS_LIFECYCLE_TEST=build\Debug\zero_process_lifecycle_test.exe"

if not exist "%ZERO_PROCESS_LIFECYCLE_TEST%" (
    set "ZERO_PROCESS_LIFECYCLE_TEST=build\Release\zero_process_lifecycle_test.exe"
)

if not exist "%ZERO_PROCESS_LIFECYCLE_TEST%" (
    echo.
    echo ERROR: zero_process_lifecycle_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_lifecycle_test.exe
    echo   build\Release\zero_process_lifecycle_test.exe
    goto fail
)

set "ZERO_PROCESS_IMAGE_LOADER_TEST=build\Debug\zero_process_image_loader_test.exe"

if not exist "%ZERO_PROCESS_IMAGE_LOADER_TEST%" (
    set "ZERO_PROCESS_IMAGE_LOADER_TEST=build\Release\zero_process_image_loader_test.exe"
)

if not exist "%ZERO_PROCESS_IMAGE_LOADER_TEST%" (
    echo.
    echo ERROR: zero_process_image_loader_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_image_loader_test.exe
    echo   build\Release\zero_process_image_loader_test.exe
    goto fail
)

set "ZERO_PROCESS_ADDRESS_SPACE_TEST=build\Debug\zero_process_address_space_test.exe"

if not exist "%ZERO_PROCESS_ADDRESS_SPACE_TEST%" (
    set "ZERO_PROCESS_ADDRESS_SPACE_TEST=build\Release\zero_process_address_space_test.exe"
)

if not exist "%ZERO_PROCESS_ADDRESS_SPACE_TEST%" (
    echo.
    echo ERROR: zero_process_address_space_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_process_address_space_test.exe
    echo   build\Release\zero_process_address_space_test.exe
    goto fail
)

set "ZERO_BINARY_DATA_SECTION_TEST=build\Debug\zero_binary_data_section_test.exe"

if not exist "%ZERO_BINARY_DATA_SECTION_TEST%" (
    set "ZERO_BINARY_DATA_SECTION_TEST=build\Release\zero_binary_data_section_test.exe"
)

if not exist "%ZERO_BINARY_DATA_SECTION_TEST%" (
    echo.
    echo ERROR: zero_binary_data_section_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_binary_data_section_test.exe
    echo   build\Release\zero_binary_data_section_test.exe
    goto fail
)

set "ZERO_ASSEMBLER_DATA_DIRECTIVES_TEST=build\Debug\zero_assembler_data_directives_test.exe"

if not exist "%ZERO_ASSEMBLER_DATA_DIRECTIVES_TEST%" (
    set "ZERO_ASSEMBLER_DATA_DIRECTIVES_TEST=build\Release\zero_assembler_data_directives_test.exe"
)

if not exist "%ZERO_ASSEMBLER_DATA_DIRECTIVES_TEST%" (
    echo.
    echo ERROR: zero_assembler_data_directives_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_assembler_data_directives_test.exe
    echo   build\Release\zero_assembler_data_directives_test.exe
    goto fail
)

set "ZERO_EXECUTABLE_ENTRY_TEST=build\Debug\zero_executable_entry_test.exe"

if not exist "%ZERO_EXECUTABLE_ENTRY_TEST%" (
    set "ZERO_EXECUTABLE_ENTRY_TEST=build\Release\zero_executable_entry_test.exe"
)

if not exist "%ZERO_EXECUTABLE_ENTRY_TEST%" (
    echo.
    echo ERROR: zero_executable_entry_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_executable_entry_test.exe
    echo   build\Release\zero_executable_entry_test.exe
    goto fail
)

set "ZERO_MULTI_PROCESS_RUNNER_TEST=build\Debug\zero_multi_process_runner_test.exe"

if not exist "%ZERO_MULTI_PROCESS_RUNNER_TEST%" (
    set "ZERO_MULTI_PROCESS_RUNNER_TEST=build\Release\zero_multi_process_runner_test.exe"
)

if not exist "%ZERO_MULTI_PROCESS_RUNNER_TEST%" (
    echo.
    echo ERROR: zero_multi_process_runner_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_multi_process_runner_test.exe
    echo   build\Release\zero_multi_process_runner_test.exe
    goto fail
)

set "ZERO_DEBUG_SESSION_TEST=build\Debug\zero_debug_session_test.exe"

if not exist "%ZERO_DEBUG_SESSION_TEST%" (
    set "ZERO_DEBUG_SESSION_TEST=build\Release\zero_debug_session_test.exe"
)

if not exist "%ZERO_DEBUG_SESSION_TEST%" (
    echo.
    echo ERROR: zero_debug_session_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_session_test.exe
    echo   build\Release\zero_debug_session_test.exe
    goto fail
)

set "ZERO_DEBUG_INSPECTOR_TEST=build\Debug\zero_debug_inspector_test.exe"

if not exist "%ZERO_DEBUG_INSPECTOR_TEST%" (
    set "ZERO_DEBUG_INSPECTOR_TEST=build\Release\zero_debug_inspector_test.exe"
)

if not exist "%ZERO_DEBUG_INSPECTOR_TEST%" (
    echo.
    echo ERROR: zero_debug_inspector_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_inspector_test.exe
    echo   build\Release\zero_debug_inspector_test.exe
    goto fail
)

set "ZERO_DEBUG_CONSOLE_TEST=build\Debug\zero_debug_console_test.exe"

if not exist "%ZERO_DEBUG_CONSOLE_TEST%" (
    set "ZERO_DEBUG_CONSOLE_TEST=build\Release\zero_debug_console_test.exe"
)

if not exist "%ZERO_DEBUG_CONSOLE_TEST%" (
    echo.
    echo ERROR: zero_debug_console_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_console_test.exe
    echo   build\Release\zero_debug_console_test.exe
    goto fail
)

set "ZERO_DEBUG_WATCHPOINT_TEST=build\Debug\zero_debug_watchpoint_test.exe"

if not exist "%ZERO_DEBUG_WATCHPOINT_TEST%" (
    set "ZERO_DEBUG_WATCHPOINT_TEST=build\Release\zero_debug_watchpoint_test.exe"
)

if not exist "%ZERO_DEBUG_WATCHPOINT_TEST%" (
    echo.
    echo ERROR: zero_debug_watchpoint_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_watchpoint_test.exe
    echo   build\Release\zero_debug_watchpoint_test.exe
    goto fail
)

set "ZERO_DEBUG_CONDITIONAL_BREAKPOINT_TEST=build\Debug\zero_debug_conditional_breakpoint_test.exe"

if not exist "%ZERO_DEBUG_CONDITIONAL_BREAKPOINT_TEST%" (
    set "ZERO_DEBUG_CONDITIONAL_BREAKPOINT_TEST=build\Release\zero_debug_conditional_breakpoint_test.exe"
)

if not exist "%ZERO_DEBUG_CONDITIONAL_BREAKPOINT_TEST%" (
    echo.
    echo ERROR: zero_debug_conditional_breakpoint_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_conditional_breakpoint_test.exe
    echo   build\Release\zero_debug_conditional_breakpoint_test.exe
    goto fail
)

set "ZERO_DEBUG_SYMBOLS_TEST=build\Debug\zero_debug_symbols_test.exe"

if not exist "%ZERO_DEBUG_SYMBOLS_TEST%" (
    set "ZERO_DEBUG_SYMBOLS_TEST=build\Release\zero_debug_symbols_test.exe"
)

if not exist "%ZERO_DEBUG_SYMBOLS_TEST%" (
    echo.
    echo ERROR: zero_debug_symbols_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_symbols_test.exe
    echo   build\Release\zero_debug_symbols_test.exe
    goto fail
)

set "ZERO_DEBUG_MULTI_PROCESS_TEST=build\Debug\zero_debug_multi_process_test.exe"

if not exist "%ZERO_DEBUG_MULTI_PROCESS_TEST%" (
    set "ZERO_DEBUG_MULTI_PROCESS_TEST=build\Release\zero_debug_multi_process_test.exe"
)

if not exist "%ZERO_DEBUG_MULTI_PROCESS_TEST%" (
    echo.
    echo ERROR: zero_debug_multi_process_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_multi_process_test.exe
    echo   build\Release\zero_debug_multi_process_test.exe
    goto fail
)

set "ZERO_DEBUG_MULTI_PROCESS_CONTROLS_TEST=build\Debug\zero_debug_multi_process_controls_test.exe"

if not exist "%ZERO_DEBUG_MULTI_PROCESS_CONTROLS_TEST%" (
    set "ZERO_DEBUG_MULTI_PROCESS_CONTROLS_TEST=build\Release\zero_debug_multi_process_controls_test.exe"
)

if not exist "%ZERO_DEBUG_MULTI_PROCESS_CONTROLS_TEST%" (
    echo.
    echo ERROR: zero_debug_multi_process_controls_test.exe not found.
    echo Tried:
    echo   build\Debug\zero_debug_multi_process_controls_test.exe
    echo   build\Release\zero_debug_multi_process_controls_test.exe
    goto fail
)

echo.
echo Using CLI:
echo   %ZERO_CLI%
echo.

echo.
echo [2/60] Running syscall table command...
"%ZERO_CLI%" syscall-table
if errorlevel 1 goto fail

echo [3/60] Running ALU unit test...
"%ZERO_CLI%" alu-test
if errorlevel 1 goto fail

echo.
echo [4/60] Running trace JSON writer test...
"%ZERO_CLI%" trace-json-test
if errorlevel 1 goto fail

echo.
echo [5/60] Running trace JSON diff test...
"%ZERO_CLI%" trace-diff-test
if errorlevel 1 goto fail

echo.
echo [6/60] Running golden trace regression test...
"%ZERO_CLI%" trace-golden-test
if errorlevel 1 goto fail

echo.
echo [7/60] Running MMIO bus test...
"%ZERO_CLI%" mmio-test
if errorlevel 1 goto fail

echo.
echo [8/60] Running hardware bus integration test...
"%ZERO_CLI%" hardware-bus-test
if errorlevel 1 goto fail

echo.
echo [9/60] Running serial hardware protocol test...
"%ZERO_CLI%" serial-hardware-test
if errorlevel 1 goto fail

echo.
echo [10/60] Running interrupt controller test...
"%ZERO_CLI%" interrupt-test
if errorlevel 1 goto fail

echo.
echo [11/60] Running CPU interrupt delivery test...
"%ZERO_CLI%" cpu-interrupt-test
if errorlevel 1 goto fail

echo.
echo [12/60] Running timer device test...
"%ZERO_CLI%" timer-test
if errorlevel 1 goto fail

echo.
echo [13/60] Running CPU timer interrupt test...
"%ZERO_CLI%" cpu-timer-test
if errorlevel 1 goto fail

echo.
echo [14/60] Running CPU EI/DI interrupt control test...
"%ZERO_CLI%" cpu-ei-di-test
if errorlevel 1 goto fail

echo.
echo [15/60] Running software interrupt test...
"%ZERO_CLI%" software-interrupt-test
if errorlevel 1 goto fail


echo.
echo [16/60] Running interrupt FLAGS restore test...
"%ZERO_CLI%" interrupt-flags-restore-test
if errorlevel 1 goto fail

echo.
echo [17/60] Running register-indirect memory test...
"%ZERO_CLI%" register-indirect-test
if errorlevel 1 goto fail

echo.
echo [18/60] Running mini kernel syscall test...
"%ZERO_CLI%" mini-kernel-syscall-test
if errorlevel 1 goto fail

echo.
echo [19/60] Running mini kernel syscall 2 test...
"%ZERO_CLI%" mini-kernel-syscall2-test
if errorlevel 1 goto fail

echo.
echo [20/60] Running mini kernel syscall 3 exit test...
"%ZERO_CLI%" mini-kernel-syscall3-test
if errorlevel 1 goto fail

echo.
echo [21/60] Running mini kernel syscall 4 timer read test...
"%ZERO_CLI%" mini-kernel-syscall4-timer-read-test
if errorlevel 1 goto fail

echo.
echo [22/60] Running mini kernel syscall 5 timer enable test...
"%ZERO_CLI%" mini-kernel-syscall5-timer-enable-test
if errorlevel 1 goto fail

echo.
echo [23/60] Running mini kernel syscall 6 timer disable test...
"%ZERO_CLI%" mini-kernel-syscall6-timer-disable-test
if errorlevel 1 goto fail

echo.
echo [24/60] Running mini kernel syscall 7 timer configure test...
"%ZERO_CLI%" mini-kernel-syscall7-timer-configure-test
if errorlevel 1 goto fail

echo.
echo [25/60] Running mini kernel timer lifecycle test...
"%ZERO_CLI%" mini-kernel-timer-lifecycle-test
if errorlevel 1 goto fail

echo.
echo [26/60] Running BIO-OS combined boot test...
"%ZERO_CLI%" bio-os-combined-boot-test
if errorlevel 1 goto fail

echo.
echo [27/60] Running binary format round-trip test...
"%ZERO_CLI%" binary-test
if errorlevel 1 goto fail

echo.
echo [28/60] Assembling and running function_call.zasm...
"%ZERO_CLI%" assemble "examples\function_call.zasm" "examples\function_call.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\function_call.zbin" --expect-memory 100=20 2048=20
if errorlevel 1 goto fail

echo.
echo [29/60] Assembling and running alu_flags.zasm...
"%ZERO_CLI%" assemble "examples\alu_flags.zasm" "examples\alu_flags.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\alu_flags.zbin" --expect-memory 120=30 128=20 136=1 144=2 152=3 160=4 168=5 200=777
if errorlevel 1 goto fail

echo.
echo [30/60] Assembling and running mmio_output.zasm...
"%ZERO_CLI%" assemble "examples\mmio_output.zasm" "examples\mmio_output.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\mmio_output.zbin" --debug-mmio --expect-memory 220=66 228=2
if errorlevel 1 goto fail

echo.
echo [31/60] Running signed branch correctness test...
"%ZERO_CLI%" signed-branch-test
if errorlevel 1 goto fail

echo.
echo [32/60] Running vector/binary differential test...
"%ZERO_CLI%" differential-test
if errorlevel 1 goto fail

echo.
echo [33/60] Running error and invariant test...
"%ZERO_CLI%" error-invariant-test
if errorlevel 1 goto fail

echo.
echo [34/60] Running ISA conformance test...
"%ZERO_ISA_CONFORMANCE%"
if errorlevel 1 goto fail

echo.
echo [35/60] Running privilege state test...
"%ZERO_PRIVILEGE_TEST%"
if errorlevel 1 goto fail

echo.
echo [36/60] Running privileged instruction test...
"%ZERO_PRIVILEGED_INSTRUCTION_TEST%"
if errorlevel 1 goto fail

echo.
echo [37/60] Running interrupt privilege test...
"%ZERO_INTERRUPT_PRIVILEGE_TEST%"
if errorlevel 1 goto fail

echo.
echo [38/60] Running memory protection test...
"%ZERO_MEMORY_PROTECTION_TEST%"
if errorlevel 1 goto fail

echo.
echo [39/60] Running execution protection test...
"%ZERO_EXECUTION_PROTECTION_TEST%"
if errorlevel 1 goto fail

echo.
echo [40/60] Running Kernel stack separation test...
"%ZERO_KERNEL_STACK_TEST%"
if errorlevel 1 goto fail

echo.
echo [41/60] Running process context test...
"%ZERO_PROCESS_CONTEXT_TEST%"
if errorlevel 1 goto fail

echo.
echo [42/60] Running process table test...
"%ZERO_PROCESS_TABLE_TEST%"
if errorlevel 1 goto fail

echo.
echo [43/60] Running round-robin scheduler test...
"%ZERO_ROUND_ROBIN_SCHEDULER_TEST%"
if errorlevel 1 goto fail

echo.
echo [44/60] Running process dispatcher test...
"%ZERO_PROCESS_DISPATCHER_TEST%"
if errorlevel 1 goto fail

echo.
echo [45/60] Running timer preemptive scheduler test...
"%ZERO_TIMER_PREEMPTIVE_SCHEDULER_TEST%"
if errorlevel 1 goto fail

echo.
echo [46/60] Running process lifecycle test...
"%ZERO_PROCESS_LIFECYCLE_TEST%"
if errorlevel 1 goto fail

echo.
echo [47/60] Running process image loader test...
"%ZERO_PROCESS_IMAGE_LOADER_TEST%"
if errorlevel 1 goto fail

echo.
echo [48/60] Running process address space test...
"%ZERO_PROCESS_ADDRESS_SPACE_TEST%"
if errorlevel 1 goto fail

echo.
echo [49/60] Running binary data section test...
"%ZERO_BINARY_DATA_SECTION_TEST%"
if errorlevel 1 goto fail

echo.
echo [50/60] Running assembler data directives test...
"%ZERO_ASSEMBLER_DATA_DIRECTIVES_TEST%"
if errorlevel 1 goto fail

echo.
echo [51/60] Running executable entry and CLI assembly test...
"%ZERO_EXECUTABLE_ENTRY_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\executable_entry.zasm" "build\executable_entry_test.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "build\executable_entry_test.zbin" --expect-memory 0=222
if errorlevel 1 goto fail

del "build\executable_entry_test.zbin" 2>nul

echo.
echo [52/60] Running multi-process runner test...
"%ZERO_MULTI_PROCESS_RUNNER_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_a.zasm" "build\process_runner_a.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_b.zasm" "build\process_runner_b.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-processes --quantum 1 --max-steps 100 "build\process_runner_a.zbin" "build\process_runner_b.zbin"
if errorlevel 1 goto fail

del "build\process_runner_a.zbin" 2>nul
del "build\process_runner_b.zbin" 2>nul

echo.
echo [53/60] Running debug session test...
"%ZERO_DEBUG_SESSION_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_breakpoint.zasm" "build\debug_breakpoint.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-binary "build\debug_breakpoint.zbin" --break 536 --max-steps 20
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-binary "build\debug_breakpoint.zbin" --max-steps 20
if errorlevel 1 goto fail

del "build\debug_breakpoint.zbin" 2>nul

echo.
echo [54/60] Running debug inspector test...
"%ZERO_DEBUG_INSPECTOR_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_breakpoint.zasm" "build\debug_inspector.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-binary "build\debug_inspector.zbin" --step 2 --registers --memory 0 8 --disassemble 512 3
if errorlevel 1 goto fail

del "build\debug_inspector.zbin" 2>nul

echo.
echo [55/60] Running debug console test...
"%ZERO_DEBUG_CONSOLE_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_breakpoint.zasm" "build\debug_console.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-shell "build\debug_console.zbin" --commands "tests\fixtures\debug_console_commands.txt" --max-steps 20
if errorlevel 1 goto fail

del "build\debug_console.zbin" 2>nul

echo.
echo [56/60] Running debug watchpoint test...
"%ZERO_DEBUG_WATCHPOINT_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_breakpoint.zasm" "build\debug_watchpoint.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-shell "build\debug_watchpoint.zbin" --commands "tests\fixtures\debug_watchpoint_commands.txt" --max-steps 20
if errorlevel 1 goto fail

del "build\debug_watchpoint.zbin" 2>nul

echo.
echo [57/60] Running conditional breakpoint test...
"%ZERO_DEBUG_CONDITIONAL_BREAKPOINT_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_breakpoint.zasm" "build\debug_conditional.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-shell "build\debug_conditional.zbin" --commands "tests\fixtures\debug_conditional_commands.txt" --max-steps 50
if errorlevel 1 goto fail

del "build\debug_conditional.zbin" 2>nul
del "build\debug_conditional.zbin.zsym" 2>nul

echo.
echo [58/60] Running debug symbols test...
"%ZERO_DEBUG_SYMBOLS_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\debug_symbols.zasm" "build\debug_symbols.zbin"
if errorlevel 1 goto fail

if not exist "build\debug_symbols.zbin.zsym" goto fail

"%ZERO_CLI%" debug-shell "build\debug_symbols.zbin" --commands "tests\fixtures\debug_symbol_commands.txt" --max-steps 50
if errorlevel 1 goto fail

del "build\debug_symbols.zbin" 2>nul
del "build\debug_symbols.zbin.zsym" 2>nul

echo.
echo [59/60] Running multi-process debugger test...
"%ZERO_DEBUG_MULTI_PROCESS_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_a.zasm" "build\debug_process_a.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_b.zasm" "build\debug_process_b.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-processes --quantum 1 --max-steps 100 --commands "tests\fixtures\debug_multi_process_commands.txt" "build\debug_process_a.zbin" "build\debug_process_b.zbin"
if errorlevel 1 goto fail

del "build\debug_process_a.zbin" 2>nul
del "build\debug_process_a.zbin.zsym" 2>nul
del "build\debug_process_b.zbin" 2>nul
del "build\debug_process_b.zbin.zsym" 2>nul

echo.
echo [60/60] Running multi-process debug controls test...
"%ZERO_DEBUG_MULTI_PROCESS_CONTROLS_TEST%"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_a.zasm" "build\debug_controls_a.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" assemble "tests\fixtures\process_runner_b.zasm" "build\debug_controls_b.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" debug-processes --quantum 1 --max-steps 100 --commands "tests\fixtures\debug_multi_process_controls_commands.txt" "build\debug_controls_a.zbin" "build\debug_controls_b.zbin"
if errorlevel 1 goto fail

del "build\debug_controls_a.zbin" 2>nul
del "build\debug_controls_a.zbin.zsym" 2>nul
del "build\debug_controls_b.zbin" 2>nul
del "build\debug_controls_b.zbin.zsym" 2>nul

echo.
echo ========================================
echo All Zero-CPU tests passed.
echo ========================================
echo.

exit /b 0

:fail
echo.
echo ========================================
echo Zero-CPU tests failed.
echo ========================================
echo.

exit /b 1
