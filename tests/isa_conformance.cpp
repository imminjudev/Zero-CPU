#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/isa/Opcode.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class OperandArity {
    Zero,
    One,
    Two
};

struct OpcodeSpec {
    zero_cpu::Opcode opcode;
    const char* mnemonic;
    std::uint8_t encodedByte;
    OperandArity arity;
};

const std::vector<OpcodeSpec>& opcodeSpecs() {
    using zero_cpu::Opcode;

    static const std::vector<OpcodeSpec> specs = {
        {Opcode::NOP,   "NOP",   0x00, OperandArity::Zero},
        {Opcode::HALT,  "HALT",  0x01, OperandArity::Zero},

        {Opcode::MOV,   "MOV",   0x10, OperandArity::Two},
        {Opcode::LOAD,  "LOAD",  0x11, OperandArity::Two},
        {Opcode::STORE, "STORE", 0x12, OperandArity::Two},

        {Opcode::ADD,   "ADD",   0x20, OperandArity::Two},
        {Opcode::SUB,   "SUB",   0x21, OperandArity::Two},
        {Opcode::MUL,   "MUL",   0x22, OperandArity::Two},
        {Opcode::DIV,   "DIV",   0x23, OperandArity::Two},

        {Opcode::CMP,   "CMP",   0x30, OperandArity::Two},
        {Opcode::TEST,  "TEST",  0x31, OperandArity::Two},

        {Opcode::JMP,   "JMP",   0x40, OperandArity::One},
        {Opcode::JE,    "JE",    0x41, OperandArity::One},
        {Opcode::JNE,   "JNE",   0x42, OperandArity::One},
        {Opcode::JG,    "JG",    0x43, OperandArity::One},
        {Opcode::JL,    "JL",    0x44, OperandArity::One},

        {Opcode::PUSH,  "PUSH",  0x50, OperandArity::One},
        {Opcode::POP,   "POP",   0x51, OperandArity::One},
        {Opcode::CALL,  "CALL",  0x52, OperandArity::One},
        {Opcode::RET,   "RET",   0x53, OperandArity::Zero},
        {Opcode::IRET,  "IRET",  0x54, OperandArity::Zero},

        {Opcode::AND,   "AND",   0x60, OperandArity::Two},
        {Opcode::OR,    "OR",    0x61, OperandArity::Two},
        {Opcode::XOR,   "XOR",   0x62, OperandArity::Two},
        {Opcode::NOT,   "NOT",   0x63, OperandArity::One},

        {Opcode::EI,    "EI",    0x70, OperandArity::Zero},
        {Opcode::DI,    "DI",    0x71, OperandArity::Zero},
        {Opcode::INT,   "INT",   0x72, OperandArity::One}
    };

    return specs;
}

std::string lowercase(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    return text;
}

int opcodeCategoryCount(zero_cpu::Opcode opcode) {
    using namespace zero_cpu;

    return static_cast<int>(isDataMovementOpcode(opcode))
        + static_cast<int>(isArithmeticOpcode(opcode))
        + static_cast<int>(isLogicalOpcode(opcode))
        + static_cast<int>(isComparisonOpcode(opcode))
        + static_cast<int>(isBranchOpcode(opcode))
        + static_cast<int>(isStackOpcode(opcode))
        + static_cast<int>(isFunctionCallOpcode(opcode))
        + static_cast<int>(isControlOpcode(opcode));
}

zero_cpu::Instruction canonicalInstruction(
    zero_cpu::Opcode opcode
) {
    using namespace zero_cpu;

    switch (opcode) {
    case Opcode::MOV:
        return Instruction(
            opcode,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(7)
        );

    case Opcode::LOAD:
        return Instruction(
            opcode,
            Operand::registerOperand(RegisterName::R1),
            Operand::memoryAddress(96)
        );

    case Opcode::STORE:
        return Instruction(
            opcode,
            Operand::memoryAddress(96),
            Operand::immediate(7)
        );

    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::MUL:
    case Opcode::DIV:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::CMP:
    case Opcode::TEST:
        return Instruction(
            opcode,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(2)
        );

    case Opcode::NOT:
    case Opcode::POP:
        return Instruction(
            opcode,
            Operand::registerOperand(RegisterName::R1)
        );

    case Opcode::JMP:
    case Opcode::JE:
    case Opcode::JNE:
    case Opcode::JG:
    case Opcode::JL:
    case Opcode::CALL:
        return Instruction(
            opcode,
            Operand::label("target")
        );

    case Opcode::PUSH:
        return Instruction(
            opcode,
            Operand::immediate(123)
        );

    case Opcode::INT:
        return Instruction(
            opcode,
            Operand::immediate(32)
        );

    case Opcode::RET:
    case Opcode::IRET:
    case Opcode::EI:
    case Opcode::DI:
    case Opcode::NOP:
    case Opcode::HALT:
        return Instruction(opcode);

    case Opcode::Invalid:
        break;
    }

    return Instruction(Opcode::Invalid);
}

zero_cpu::Instruction malformedArityInstruction(
    const OpcodeSpec& spec
) {
    using namespace zero_cpu;

    switch (spec.arity) {
    case OperandArity::Zero:
        return Instruction(
            spec.opcode,
            Operand::immediate(1)
        );

    case OperandArity::One:
        return Instruction(spec.opcode);

    case OperandArity::Two:
        return Instruction(
            spec.opcode,
            Operand::registerOperand(RegisterName::R1)
        );
    }

    return Instruction(Opcode::Invalid);
}

zero_cpu::CPU::LabelTable canonicalLabels() {
    return {{"target", 1}};
}

zero_cpu::binary::BinaryProgram encodeProgram(
    const std::vector<zero_cpu::Instruction>& instructions,
    const zero_cpu::CPU::LabelTable& labels
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram(instructions, labels);

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    return program;
}

void prepareCommonState(
    zero_cpu::CPU& cpu,
    zero_cpu::Opcode opcode,
    bool binaryMode
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    cpu.state().registers().set(RegisterName::R1, 8);
    cpu.state().memory().write(96, 7);

    auto controller = std::make_shared<InterruptController>();

    const std::size_t handlerAddress = binaryMode
        ? cpu.binaryCodeBase() + kInstructionSize
        : 1;

    controller->setVectorHandler(32, handlerAddress);
    cpu.setInterruptController(controller);

    const std::size_t stackBase =
        CPUState::kDefaultStackBase;

    switch (opcode) {
    case Opcode::POP:
        cpu.state().memory().write(stackBase, 123);
        cpu.state().setSp(stackBase + CPU::kStackSlotSize);
        break;

    case Opcode::RET: {
        const std::size_t returnAddress = binaryMode
            ? cpu.binaryCodeBase() + kInstructionSize
            : 1;

        cpu.state().memory().write(
            stackBase,
            static_cast<std::int64_t>(returnAddress)
        );
        cpu.state().setSp(stackBase + CPU::kStackSlotSize);
        break;
    }

    case Opcode::IRET: {
        const std::size_t returnAddress = binaryMode
            ? cpu.binaryCodeBase() + kInstructionSize
            : 1;

        cpu.state().memory().write(
            stackBase,
            static_cast<std::int64_t>(returnAddress)
        );
        cpu.state().memory().write(
            stackBase + CPU::kStackSlotSize,
            0
        );
        cpu.state().memory().write(
            stackBase + CPU::kStackSlotSize * 2,
            privilegeLevelToRaw(PrivilegeLevel::Kernel)
        );
        cpu.state().setSp(
            stackBase + CPU::kInterruptFrameSize
        );
        break;
    }

    default:
        break;
    }
}

bool canonicalVectorExecutionAccepts(
    const OpcodeSpec& spec,
    std::string& error
) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(
        {
            canonicalInstruction(spec.opcode),
            Instruction(Opcode::HALT)
        },
        canonicalLabels()
    );

    prepareCommonState(cpu, spec.opcode, false);
    cpu.step();

    error = cpu.state().errorMessage();
    return !cpu.state().hasError();
}

bool canonicalBinaryExecutionAccepts(
    const OpcodeSpec& spec,
    std::string& error
) {
    using namespace zero_cpu;

    const binary::BinaryProgram program = encodeProgram(
        {
            canonicalInstruction(spec.opcode),
            Instruction(Opcode::HALT)
        },
        canonicalLabels()
    );

    CPU cpu;
    cpu.loadBinaryProgram(program);
    prepareCommonState(cpu, spec.opcode, true);
    cpu.step();

    error = cpu.state().errorMessage();
    return !cpu.state().hasError();
}

bool malformedVectorExecutionRejects(
    const OpcodeSpec& spec,
    std::string& error
) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(
        {
            malformedArityInstruction(spec),
            Instruction(Opcode::HALT)
        },
        canonicalLabels()
    );

    prepareCommonState(cpu, spec.opcode, false);
    cpu.step();

    error = cpu.state().errorMessage();
    return cpu.state().hasError();
}

bool malformedBinaryExecutionRejects(
    const OpcodeSpec& spec,
    std::string& error
) {
    using namespace zero_cpu;

    const binary::BinaryProgram program = encodeProgram(
        {
            malformedArityInstruction(spec),
            Instruction(Opcode::HALT)
        },
        canonicalLabels()
    );

    CPU cpu;
    cpu.loadBinaryProgram(program);
    prepareCommonState(cpu, spec.opcode, true);
    cpu.step();

    error = cpu.state().errorMessage();
    return cpu.state().hasError();
}

bool checkEncodingAndDecoding(
    const OpcodeSpec& spec,
    std::string& error
) {
    using namespace zero_cpu;

    try {
        if (encodeOpcode(spec.opcode) != spec.encodedByte) {
            error = "encoded opcode byte mismatch";
            return false;
        }

        if (decodeOpcode(spec.encodedByte) != spec.opcode) {
            error = "decoded opcode mismatch";
            return false;
        }

        InstructionEncoder encoder;
        const std::vector<std::uint8_t> bytes =
            encoder.encodeInstruction(
                canonicalInstruction(spec.opcode),
                canonicalLabels()
            );

        InstructionDecoder decoder;
        const DecodedInstruction decoded =
            decoder.decodeInstruction(bytes);

        if (decoded.opcode != spec.opcode) {
            error = "instruction round-trip opcode mismatch";
            return false;
        }

        const bool hasDestination =
            decoded.dst_type != EncodedOperandType::None;
        const bool hasSource =
            decoded.src_type != EncodedOperandType::None;

        switch (spec.arity) {
        case OperandArity::Zero:
            if (hasDestination || hasSource) {
                error = "zero-operand encoding contains operands";
                return false;
            }
            break;

        case OperandArity::One:
            if (!hasDestination || hasSource) {
                error = "one-operand encoding shape mismatch";
                return false;
            }
            break;

        case OperandArity::Two:
            if (!hasDestination || !hasSource) {
                error = "two-operand encoding shape mismatch";
                return false;
            }
            break;
        }

        error.clear();
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

const char* arityText(OperandArity arity) {
    switch (arity) {
    case OperandArity::Zero:
        return "0";
    case OperandArity::One:
        return "1";
    case OperandArity::Two:
        return "2";
    }

    return "?";
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU ISA Conformance Test ===\n\n";

    int failures = 0;
    std::set<std::string> mnemonics;
    std::set<std::uint8_t> opcodeBytes;

    for (const OpcodeSpec& spec : opcodeSpecs()) {
        bool passed = true;
        std::vector<std::string> reasons;

        auto failSpec = [&](const std::string& reason) {
            passed = false;
            reasons.push_back(reason);
        };

        if (!isValidOpcode(spec.opcode)) {
            failSpec("isValidOpcode returned false");
        }

        if (opcodeToString(spec.opcode) != spec.mnemonic) {
            failSpec("opcodeToString mismatch");
        }

        if (opcodeFromString(spec.mnemonic) != spec.opcode) {
            failSpec("uppercase mnemonic parse mismatch");
        }

        if (
            opcodeFromString(lowercase(spec.mnemonic)) !=
            spec.opcode
        ) {
            failSpec("lowercase mnemonic parse mismatch");
        }

        if (!mnemonics.insert(spec.mnemonic).second) {
            failSpec("duplicate mnemonic");
        }

        if (!opcodeBytes.insert(spec.encodedByte).second) {
            failSpec("duplicate opcode byte");
        }

        if (opcodeCategoryCount(spec.opcode) != 1) {
            failSpec("opcode must belong to exactly one category");
        }

        std::string encodingError;
        if (!checkEncodingAndDecoding(spec, encodingError)) {
            failSpec("encoding: " + encodingError);
        }

        std::string vectorError;
        if (!canonicalVectorExecutionAccepts(spec, vectorError)) {
            failSpec("vector rejected canonical form: " + vectorError);
        }

        std::string binaryError;
        if (!canonicalBinaryExecutionAccepts(spec, binaryError)) {
            failSpec("binary rejected canonical form: " + binaryError);
        }

        std::string malformedVectorError;
        if (
            !malformedVectorExecutionRejects(
                spec,
                malformedVectorError
            )
        ) {
            failSpec("vector accepted malformed arity");
        }

        std::string malformedBinaryError;
        if (
            !malformedBinaryExecutionRejects(
                spec,
                malformedBinaryError
            )
        ) {
            failSpec("binary accepted malformed arity");
        }

        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << std::left
                  << std::setw(5)
                  << spec.mnemonic
                  << " byte=0x"
                  << std::right
                  << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << static_cast<int>(spec.encodedByte)
                  << std::dec
                  << std::setfill(' ')
                  << " arity="
                  << arityText(spec.arity)
                  << "\n";

        for (const std::string& reason : reasons) {
            std::cout << "       " << reason << "\n";
        }

        if (!passed) {
            ++failures;
        }
    }

    bool invalidChecksPassed = true;

    if (isValidOpcode(Opcode::Invalid)) {
        std::cout
            << "[FAIL] Invalid opcode reported as valid\n";
        invalidChecksPassed = false;
    }

    if (opcodeFromString("NOT_A_ZERO_CPU_OPCODE") != Opcode::Invalid) {
        std::cout
            << "[FAIL] Unknown mnemonic did not map to Invalid\n";
        invalidChecksPassed = false;
    }

    if (decodeOpcode(0xFF) != Opcode::Invalid) {
        std::cout
            << "[FAIL] Unknown opcode byte did not map to Invalid\n";
        invalidChecksPassed = false;
    }

    bool invalidEncodingThrew = false;

    try {
        (void)encodeOpcode(Opcode::Invalid);
    } catch (const std::exception&) {
        invalidEncodingThrew = true;
    }

    if (!invalidEncodingThrew) {
        std::cout
            << "[FAIL] Encoding Opcode::Invalid did not throw\n";
        invalidChecksPassed = false;
    }

    if (invalidChecksPassed) {
        std::cout
            << "[PASS] Invalid opcode rejection checks\n";
    } else {
        ++failures;
    }

    std::cout << "\nOpcode count: "
              << opcodeSpecs().size()
              << "\n";

    if (failures == 0) {
        std::cout
            << "ISA conformance test finished successfully.\n";
        return 0;
    }

    std::cout
        << "ISA conformance test failed. Failure count: "
        << failures
        << "\n";
    return 1;
}
