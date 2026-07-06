cmake_minimum_required(VERSION 3.20)

project(ZeroCPU LANGUAGES CXX)

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
    src/isa/InstructionEncoder.cpp
    src/isa/InstructionDecoder.cpp

    src/trace/TraceEvent.cpp
    src/trace/TraceLogger.cpp

    src/assembler/Assembler.cpp

    src/binary/BinaryWriter.cpp
    src/binary/BinaryReader.cpp
    src/binary/BinaryLoader.cpp
)

target_include_directories(zero_cpu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(zero_cli
    tools/zero_cli.cpp
)

target_link_libraries(zero_cli
    PRIVATE
        zero_cpu_core
)

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
endif()