#pragma once

#include <string>

namespace zero_cpu {

enum class Opcode {
    MOV,
    LOAD,
    STORE,

    ADD,
    SUB,
    MUL,
    DIV,

    AND,
    OR,
    XOR,
    NOT,

    CMP,
    TEST,

    JMP,
    JE,
    JNE,
    JG,
    JL,

    PUSH,
    POP,

    CALL,
    RET,
    IRET,
    EI,
    DI,
    INT,

    NOP,
    HALT,

    Invalid
};

std::string opcodeToString(Opcode opcode);
Opcode opcodeFromString(const std::string& text);

bool isValidOpcode(Opcode opcode);
bool isDataMovementOpcode(Opcode opcode);
bool isArithmeticOpcode(Opcode opcode);
bool isLogicalOpcode(Opcode opcode);
bool isComparisonOpcode(Opcode opcode);
bool isBranchOpcode(Opcode opcode);
bool isStackOpcode(Opcode opcode);
bool isFunctionCallOpcode(Opcode opcode);
bool isControlOpcode(Opcode opcode);

} // namespace zero_cpu