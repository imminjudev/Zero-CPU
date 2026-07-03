cmake_minimum_required(VERSION 3.20)

project(
    ZeroCPU
    VERSION 0.1.0
    DESCRIPTION "A virtual ISA-based CPU emulator with state transition tracing"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(zero_cpu_core
    src/core/Flags.cpp
    src/core/RegisterFile.cpp
    src/core/Memory.cpp
    src/core/CPUState.cpp
    src/core/CPU.cpp

    src/isa/Opcode.cpp
    src/isa/Operand.cpp
    src/isa/Instruction.cpp

    src/trace/TraceEvent.cpp
    src/trace/TraceLogger.cpp

    src/assembler/Assembler.cpp
)

target_include_directories(zero_cpu_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(zero_cpu_core PRIVATE /W4)
else()
    target_compile_options(zero_cpu_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(zero_cli
    tools/zero_cli.cpp
)

target_link_libraries(zero_cli
    PRIVATE
        zero_cpu_core
)