#include "zero_cpu/isa/Opcode.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace zero_cpu {

namespace {

std::string toUpper(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        }
    );

    return text;
}

} // namespace

std::string opcodeToString(Opcode opcode) {
    switch (opcode) {
    case Opcode::MOV:
        return "MOV";
    case Opcode::LOAD:
        return "LOAD";
    case Opcode::STORE:
        return "STORE";

    case Opcode::ADD:
        return "ADD";
    case Opcode::SUB:
        return "SUB";
    case Opcode::MUL:
        return "MUL";
    case Opcode::DIV:
        return "DIV";

    case Opcode::AND:
        return "AND";
    case Opcode::OR:
        return "OR";
    case Opcode::XOR:
        return "XOR";
    case Opcode::NOT:
        return "NOT";

    case Opcode::CMP:
        return "CMP";
    case Opcode::TEST:
        return "TEST";

    case Opcode::JMP:
        return "JMP";
    case Opcode::JE:
        return "JE";
    case Opcode::JNE:
        return "JNE";
    case Opcode::JG:
        return "JG";
    case Opcode::JL:
        return "JL";

    case Opcode::PUSH:
        return "PUSH";
    case Opcode::POP:
        return "POP";

    case Opcode::CALL:
        return "CALL";
    case Opcode::RET:
        return "RET";
    case Opcode::IRET:
        return "IRET";
    case Opcode::EI:
        return "EI";
    case Opcode::DI:
        return "DI";
    case Opcode::INT:
        return "INT";

    case Opcode::NOP:
        return "NOP";
    case Opcode::HALT:
        return "HALT";

    case Opcode::Invalid:
        return "INVALID";
    }

    return "INVALID";
}

Opcode opcodeFromString(const std::string& text) {
    static const std::unordered_map<std::string, Opcode> table = {
        {"MOV", Opcode::MOV},
        {"LOAD", Opcode::LOAD},
        {"STORE", Opcode::STORE},

        {"ADD", Opcode::ADD},
        {"SUB", Opcode::SUB},
        {"MUL", Opcode::MUL},
        {"DIV", Opcode::DIV},

        {"AND", Opcode::AND},
        {"OR", Opcode::OR},
        {"XOR", Opcode::XOR},
        {"NOT", Opcode::NOT},

        {"CMP", Opcode::CMP},
        {"TEST", Opcode::TEST},

        {"JMP", Opcode::JMP},
        {"JE", Opcode::JE},
        {"JNE", Opcode::JNE},
        {"JG", Opcode::JG},
        {"JL", Opcode::JL},

        {"PUSH", Opcode::PUSH},
        {"POP", Opcode::POP},

        {"CALL", Opcode::CALL},
        {"RET", Opcode::RET},
        {"IRET", Opcode::IRET},
        {"EI", Opcode::EI},
        {"DI", Opcode::DI},
        {"INT", Opcode::INT},

        {"NOP", Opcode::NOP},
        {"HALT", Opcode::HALT}
    };

    const auto upper = toUpper(text);
    const auto found = table.find(upper);

    if (found == table.end()) {
        return Opcode::Invalid;
    }

    return found->second;
}

bool isValidOpcode(Opcode opcode) {
    return opcode != Opcode::Invalid;
}

bool isDataMovementOpcode(Opcode opcode) {
    return opcode == Opcode::MOV
        || opcode == Opcode::LOAD
        || opcode == Opcode::STORE;
}

bool isArithmeticOpcode(Opcode opcode) {
    return opcode == Opcode::ADD
        || opcode == Opcode::SUB
        || opcode == Opcode::MUL
        || opcode == Opcode::DIV;
}

bool isLogicalOpcode(Opcode opcode) {
    return opcode == Opcode::AND
        || opcode == Opcode::OR
        || opcode == Opcode::XOR
        || opcode == Opcode::NOT;
}

bool isComparisonOpcode(Opcode opcode) {
    return opcode == Opcode::CMP
        || opcode == Opcode::TEST;
}

bool isBranchOpcode(Opcode opcode) {
    return opcode == Opcode::JMP
        || opcode == Opcode::JE
        || opcode == Opcode::JNE
        || opcode == Opcode::JG
        || opcode == Opcode::JL;
}

bool isStackOpcode(Opcode opcode) {
    return opcode == Opcode::PUSH
        || opcode == Opcode::POP;
}

bool isFunctionCallOpcode(Opcode opcode) {
    return opcode == Opcode::CALL
        || opcode == Opcode::RET
        || opcode == Opcode::IRET
        || opcode == Opcode::INT;
}

bool isControlOpcode(Opcode opcode) {
    return opcode == Opcode::NOP
        || opcode == Opcode::HALT;
}

} // namespace zero_cpu