#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/ALU.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/DebugOutputDevice.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/core/TimerDevice.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/debug/DebugInspector.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"
#include "zero_cpu/hardware/HardwareBus.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/HardwareProtocol.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/hardware/MockSerialTransport.hpp"
#include "zero_cpu/hardware/SerialHardwareBus.hpp"
#include "zero_cpu/hardware/WindowsSerialTransport.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/BioOSRunner.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/trace/TraceJsonWriter.hpp"
#include "zero_cpu/trace/TraceJsonDiff.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = zero_cpu::memory_map::kDefaultStackBase;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kLoadedMemoryPreviewCount = 96;

struct MemoryExpectation {
    std::size_t address = 0;
    std::int64_t expected = 0;
};

struct RunBinaryOptions {
    std::vector<std::size_t> watchAddresses;
    std::vector<MemoryExpectation> memoryExpectations;
    bool enableDebugMMIO = false;
};

void printProgram(
    const std::vector<zero_cpu::Instruction>& program,
    const zero_cpu::CPU::LabelTable& labels
) {
    std::cout << "=== Zero-CPU Program ===\n";

    for (std::size_t i = 0; i < program.size(); ++i) {
        for (const auto& entry : labels) {
            if (entry.second == i) {
                std::cout << entry.first << ":\n";
            }
        }

        std::cout << "[" << i << "] "
                  << program[i].toString()
                  << "\n";
    }

    std::cout << "\n";
}

void printMemoryViews(const zero_cpu::CPU& cpu) {
    std::cout << "Memory[96..111]: "
              << cpu.state().memory().dumpRange(
                     kDataViewStart,
                     kDataViewCount
                 )
              << "\n";

    std::cout << "Stack["
              << kStackViewStart
              << ".."
              << (kStackViewStart + kStackViewCount - 1)
              << "]: "
              << cpu.state().memory().dumpRange(
                     kStackViewStart,
                     kStackViewCount
                 )
              << "\n";
}

void printWatchedMemory(
    const zero_cpu::CPU& cpu,
    const std::vector<std::size_t>& watchAddresses
) {
    if (watchAddresses.empty()) {
        return;
    }

    std::cout << "Watched Memory:\n";

    for (const std::size_t address : watchAddresses) {
        std::cout << "Memory["
                  << address
                  << "] = "
                  << cpu.state().memory().read(address)
                  << "\n";
    }
}


bool checkMemoryExpectations(
    const zero_cpu::CPU& cpu,
    const std::vector<MemoryExpectation>& expectations
) {
    if (expectations.empty()) {
        return true;
    }

    bool passed = true;

    std::cout << "Memory Expectations:\n";

    for (const MemoryExpectation& expectation : expectations) {
        const std::int64_t actual = static_cast<std::int64_t>(
            cpu.state().memory().read(expectation.address)
        );

        if (actual == expectation.expected) {
            std::cout << "[PASS] Memory["
                      << expectation.address
                      << "] = "
                      << actual
                      << "\n";
        } else {
            std::cout << "[FAIL] Memory["
                      << expectation.address
                      << "] expected "
                      << expectation.expected
                      << " but got "
                      << actual
                      << "\n";
            passed = false;
        }
    }

    if (passed) {
        std::cout << "Memory expectations passed.\n";
    } else {
        std::cout << "Memory expectations failed.\n";
    }

    return passed;
}

std::size_t parseMemoryAddress(const std::string& text) {
    std::size_t parsedLength = 0;

    const unsigned long long value = std::stoull(
        text,
        &parsedLength,
        0
    );

    if (parsedLength != text.size()) {
        throw std::invalid_argument(
            "Invalid memory address: " + text
        );
    }

    if (value > static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max()
                )) {
        throw std::out_of_range(
            "Memory address is too large: " + text
        );
    }

    return static_cast<std::size_t>(value);
}

bool isCommandOption(const std::string& text) {
    return text.rfind("--", 0) == 0;
}

std::int64_t parseExpectedMemoryValue(const std::string& text) {
    std::size_t parsedLength = 0;

    const long long value = std::stoll(
        text,
        &parsedLength,
        0
    );

    if (parsedLength != text.size()) {
        throw std::invalid_argument(
            "Invalid expected memory value: " + text
        );
    }

    return static_cast<std::int64_t>(value);
}

MemoryExpectation parseMemoryExpectation(const std::string& text) {
    const std::size_t equalsPosition = text.find('=');

    if (equalsPosition == std::string::npos ||
        equalsPosition == 0 ||
        equalsPosition + 1 >= text.size()) {
        throw std::invalid_argument(
            "Invalid memory expectation. Expected format: <address>=<value>, got: " + text
        );
    }

    MemoryExpectation expectation;
    expectation.address = parseMemoryAddress(text.substr(0, equalsPosition));
    expectation.expected = parseExpectedMemoryValue(text.substr(equalsPosition + 1));
    return expectation;
}

RunBinaryOptions parseRunBinaryOptions(
    int argc,
    char* argv[],
    int startIndex
) {
    RunBinaryOptions options;

    int i = startIndex;

    while (i < argc) {
        const std::string option = argv[i];

        if (option == "--watch") {
            ++i;

            if (i >= argc || isCommandOption(argv[i])) {
                throw std::invalid_argument(
                    "--watch requires at least one memory address"
                );
            }

            while (i < argc && !isCommandOption(argv[i])) {
                options.watchAddresses.push_back(
                    parseMemoryAddress(argv[i])
                );
                ++i;
            }

            continue;
        }

        if (option == "--expect-memory") {
            ++i;

            if (i >= argc || isCommandOption(argv[i])) {
                throw std::invalid_argument(
                    "--expect-memory requires at least one <address>=<value> pair"
                );
            }

            while (i < argc && !isCommandOption(argv[i])) {
                options.memoryExpectations.push_back(
                    parseMemoryExpectation(argv[i])
                );
                ++i;
            }

            continue;
        }

        if (option == "--debug-mmio") {
            options.enableDebugMMIO = true;
            ++i;
            continue;
        }

        throw std::invalid_argument(
            "Unknown run-binary option: " + option
        );
    }

    return options;
}

void printFinalCheck(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;

    const auto finalR1 =
        cpu.state().registers().get(RegisterName::R1);

    const auto finalR2 =
        cpu.state().registers().get(RegisterName::R2);

    std::cout << "Default Final Check (function_call example):\n";
    std::cout << "R1 = " << finalR1 << "\n";
    std::cout << "R2 = " << finalR2 << "\n";
    std::cout << "SP = " << cpu.state().sp() << "\n";

    std::cout << "Memory[100] = "
              << cpu.state().memory().read(100)
              << "\n";

    std::cout << "Memory["
              << memory_map::kDefaultStackBase
              << "] = "
              << cpu.state().memory().read(memory_map::kDefaultStackBase)
              << "\n";
}

void printDebugOutputDevice(
    const zero_cpu::DebugOutputDevice& device
) {
    std::cout << "Debug MMIO Output Device:\n";
    std::cout << "Write count = "
              << device.writes().size()
              << "\n";

    const std::vector<std::int64_t>& values = device.writes();

    for (std::size_t i = 0; i < values.size(); ++i) {
        std::cout << "  [" << i << "] " << values[i] << "\n";
    }

    if (!device.outputText().empty()) {
        std::cout << "Captured text:\n"
                  << device.outputText();
    }
}


std::string debugOutputAsAscii(const zero_cpu::DebugOutputDevice& device) {
    std::string text;

    for (const std::int64_t value : device.writes()) {
        if (value >= 32 && value <= 126) {
            text.push_back(static_cast<char>(value));
        } else if (value == 10) {
            text.push_back('\n');
        } else {
            text.push_back('.');
        }
    }

    return text;
}

void printDebugOutputAscii(const zero_cpu::DebugOutputDevice& device) {
    const std::string text = debugOutputAsAscii(device);

    if (text.empty()) {
        std::cout << "ASCII view: <empty>\n";
        return;
    }

    std::cout << "ASCII view:\n";
    std::cout << text << "\n";
}

void runStepByStep(zero_cpu::CPU& cpu) {
    std::cout << "=== Step Execution With Trace ===\n";

    std::size_t stepCount = 0;

    while (!cpu.state().halted()) {
        const std::size_t pcBefore = cpu.state().pc();

        if (pcBefore >= cpu.program().size()) {
            std::cout << "PC out of program range.\n";
            break;
        }

        const zero_cpu::Instruction& instruction = cpu.program()[pcBefore];

        std::cout << "Step " << stepCount
                  << " | PC=" << pcBefore
                  << " | " << instruction.toString()
                  << "\n";

        cpu.step();

        if (!cpu.traceLogger().empty()) {
            std::cout << cpu.traceLogger().last().toCompactString()
                      << "\n";
        }

        std::cout << "Current State:\n";
        std::cout << cpu.state().summary();
        printMemoryViews(cpu);
        std::cout << "\n";

        ++stepCount;

        if (stepCount > 100) {
            std::cout << "Step limit reached in CLI test.\n";
            break;
        }
    }
}

void printHexBytes(const std::vector<std::uint8_t>& bytes) {
    std::cout << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            std::cout << " ";
        }

        std::cout << std::setw(2)
                  << static_cast<int>(bytes[i]);
    }

    std::cout << std::dec << "\n";
}

void printBinaryHeader(const zero_cpu::binary::BinaryProgram& program) {
    using namespace zero_cpu::binary;

    std::cout << "=== Binary Header ===\n";
    std::cout << "Magic: " << magicString() << "\n";

    std::cout << "Version: "
              << static_cast<int>(program.header.major_version)
              << "."
              << static_cast<int>(program.header.minor_version)
              << "\n";

    std::cout << "Endianness: "
              << (
                     program.header.endianness == BinaryEndianness::Little
                         ? "Little"
                         : "Big"
                 )
              << "\n";

    std::cout << "Entry Point: "
              << program.header.entry_point
              << "\n";

    std::cout << "Code Size: "
              << program.header.code_size
              << " bytes\n";

    std::cout << "Data Base: "
              << program.header.data_base
              << "\n";

    std::cout << "Data Size: "
              << program.header.data_size
              << " bytes\n";

    std::cout << "Instruction Count: "
              << program.code.size() / kInstructionSize
              << "\n";
}

void printDecodedInstruction(
    std::size_t index,
    const zero_cpu::DecodedInstruction& instruction
) {
    std::cout << "[" << index << "] ";

    std::cout << "opcode=0x"
              << std::hex
              << std::setw(2)
              << std::setfill('0')
              << static_cast<int>(zero_cpu::encodeOpcode(instruction.opcode))
              << std::dec;

    std::cout << " | dst_type="
              << zero_cpu::toString(instruction.dst_type)
              << " | dst_payload="
              << instruction.dst_payload;

    std::cout << " | src_type="
              << zero_cpu::toString(instruction.src_type)
              << " | src_payload="
              << instruction.src_payload;

    std::cout << "\n";
}

std::string decodedInstructionToString(
    const zero_cpu::DecodedInstruction& instruction
) {
    std::ostringstream oss;

    oss << "opcode=0x"
        << std::hex
        << std::setw(2)
        << std::setfill('0')
        << static_cast<int>(zero_cpu::encodeOpcode(instruction.opcode))
        << std::dec
        << std::setfill(' ');

    oss << " | dst_type="
        << zero_cpu::toString(instruction.dst_type)
        << " | dst_payload="
        << instruction.dst_payload;

    oss << " | src_type="
        << zero_cpu::toString(instruction.src_type)
        << " | src_payload="
        << instruction.src_payload;

    return oss.str();
}

std::string currentBinaryInstructionText(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    try {
        const std::size_t pc = cpu.state().pc();

        const std::vector<std::uint8_t> instructionBytes =
            cpu.state().memory().readBytes(pc, kInstructionSize);

        InstructionDecoder decoder;
        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        return decodedInstructionToString(decoded);
    } catch (const std::exception& ex) {
        return std::string("<decode failed: ") + ex.what() + ">";
    }
}

void printDecodedInstructions(const std::vector<std::uint8_t>& code) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Decoded Instructions ===\n";

    InstructionDecoder decoder;

    for (
        std::size_t offset = 0;
        offset < code.size();
        offset += kInstructionSize
    ) {
        std::vector<std::uint8_t> instructionBytes(
            code.begin() + offset,
            code.begin() + offset + kInstructionSize
        );

        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        const std::size_t index = offset / kInstructionSize;
        printDecodedInstruction(index, decoded);
    }
}


std::string boolText(bool value) {
    return value ? "true" : "false";
}

struct ExpectedALUResult {
    std::int64_t value = 0;
    bool zero = false;
    bool sign = false;
    bool carry = false;
    bool overflow = false;
};

bool checkALUResult(
    const std::string& name,
    const zero_cpu::ALUResult& actual,
    const ExpectedALUResult& expected
) {
    const bool passed =
        actual.value == expected.value &&
        actual.zero == expected.zero &&
        actual.sign == expected.sign &&
        actual.carry == expected.carry &&
        actual.overflow == expected.overflow;

    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << name
              << " | value=" << actual.value
              << " ZF=" << boolText(actual.zero)
              << " SF=" << boolText(actual.sign)
              << " CF=" << boolText(actual.carry)
              << " OF=" << boolText(actual.overflow)
              << "\n";

    if (!passed) {
        std::cout << "       expected"
                  << " value=" << expected.value
                  << " ZF=" << boolText(expected.zero)
                  << " SF=" << boolText(expected.sign)
                  << " CF=" << boolText(expected.carry)
                  << " OF=" << boolText(expected.overflow)
                  << "\n";
    }

    return passed;
}

bool checkThrows(
    const std::string& name,
    void (*operation)()
) {
    try {
        operation();
    } catch (const std::exception& ex) {
        std::cout << "[PASS] "
                  << name
                  << " | threw: "
                  << ex.what()
                  << "\n";
        return true;
    }

    std::cout << "[FAIL] "
              << name
              << " | expected exception, but no exception was thrown\n";
    return false;
}

void divByZeroOperation() {
    (void)zero_cpu::ALU::div(10, 0);
}

void divOverflowOperation() {
    (void)zero_cpu::ALU::div(
        std::numeric_limits<std::int64_t>::min(),
        -1
    );
}

int runAluTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU ALU Test ===\n";
    std::cout << "Testing ALU value output and ZF/SF/CF/OF flags.\n\n";

    int failures = 0;

    auto expect = [&](
        const std::string& name,
        const ALUResult& actual,
        const ExpectedALUResult& expected
    ) {
        if (!checkALUResult(name, actual, expected)) {
            ++failures;
        }
    };

    auto expectThrow = [&](
        const std::string& name,
        void (*operation)()
    ) {
        if (!checkThrows(name, operation)) {
            ++failures;
        }
    };

    expect(
        "ADD 10 + 20",
        ALU::add(10, 20),
        ExpectedALUResult{30, false, false, false, false}
    );

    expect(
        "ADD INT64_MAX + 1",
        ALU::add(std::numeric_limits<std::int64_t>::max(), 1),
        ExpectedALUResult{
            std::numeric_limits<std::int64_t>::min(),
            false,
            true,
            false,
            true
        }
    );

    expect(
        "ADD -1 + 1",
        ALU::add(-1, 1),
        ExpectedALUResult{0, true, false, true, false}
    );

    expect(
        "SUB 30 - 10",
        ALU::sub(30, 10),
        ExpectedALUResult{20, false, false, false, false}
    );

    expect(
        "SUB 10 - 20",
        ALU::sub(10, 20),
        ExpectedALUResult{-10, false, true, true, false}
    );

    expect(
        "SUB INT64_MIN - 1",
        ALU::sub(std::numeric_limits<std::int64_t>::min(), 1),
        ExpectedALUResult{
            std::numeric_limits<std::int64_t>::max(),
            false,
            false,
            false,
            true
        }
    );

    expect(
        "MUL 6 * 7",
        ALU::mul(6, 7),
        ExpectedALUResult{42, false, false, false, false}
    );

    expect(
        "MUL INT64_MAX * 2",
        ALU::mul(std::numeric_limits<std::int64_t>::max(), 2),
        ExpectedALUResult{-2, false, true, true, true}
    );

    expect(
        "DIV 42 / 7",
        ALU::div(42, 7),
        ExpectedALUResult{6, false, false, false, false}
    );

    expectThrow("DIV 10 / 0", divByZeroOperation);
    expectThrow("DIV INT64_MIN / -1", divOverflowOperation);

    expect(
        "AND 10 & 5",
        ALU::bitAnd(10, 5),
        ExpectedALUResult{0, true, false, false, false}
    );

    expect(
        "OR 8 | 2",
        ALU::bitOr(8, 2),
        ExpectedALUResult{10, false, false, false, false}
    );

    expect(
        "XOR 10 ^ 2",
        ALU::bitXor(10, 2),
        ExpectedALUResult{8, false, false, false, false}
    );

    expect(
        "NOT 0",
        ALU::bitNot(0),
        ExpectedALUResult{-1, false, true, false, false}
    );

    expect(
        "CMP 5, 5",
        ALU::compare(5, 5),
        ExpectedALUResult{0, true, false, false, false}
    );

    expect(
        "CMP 3, 8",
        ALU::compare(3, 8),
        ExpectedALUResult{-5, false, true, true, false}
    );

    expect(
        "TEST 10, 5",
        ALU::test(10, 5),
        ExpectedALUResult{0, true, false, false, false}
    );

    expect(
        "TEST 10, 2",
        ALU::test(10, 2),
        ExpectedALUResult{2, false, false, false, false}
    );

    std::cout << "\n";

    if (failures == 0) {
        std::cout << "ALU test finished successfully.\n";
        return 0;
    }

    std::cout << "ALU test failed. Failure count: "
              << failures
              << "\n";

    return 1;
}

struct SignedBranchTestCase {
    const char* name = "";
    zero_cpu::Opcode opcode = zero_cpu::Opcode::Invalid;
    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    bool expectedTaken = false;
};

std::vector<zero_cpu::Instruction> makeSignedBranchProgram(
    const SignedBranchTestCase& testCase
) {
    using namespace zero_cpu;

    return {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(testCase.lhs)
        ),
        Instruction(
            Opcode::CMP,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(testCase.rhs)
        ),
        Instruction(
            testCase.opcode,
            Operand::label("taken")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R2),
            Operand::immediate(0)
        ),
        Instruction(
            Opcode::JMP,
            Operand::label("done")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R2),
            Operand::immediate(1)
        ),
        Instruction(Opcode::HALT)
    };
}

zero_cpu::CPU::LabelTable signedBranchLabels() {
    return {
        {"taken", 5},
        {"done", 6}
    };
}

bool runSignedBranchVectorCase(const SignedBranchTestCase& testCase) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(
        makeSignedBranchProgram(testCase),
        signedBranchLabels()
    );
    cpu.run();

    if (cpu.state().hasError()) {
        throw std::runtime_error(
            std::string("Vector execution failed: ") +
            cpu.state().errorMessage()
        );
    }

    if (!cpu.state().halted()) {
        throw std::runtime_error("Vector execution did not halt");
    }

    return cpu.state().registers().get(RegisterName::R2) == 1;
}

bool runSignedBranchBinaryCase(const SignedBranchTestCase& testCase) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    const std::vector<Instruction> instructions =
        makeSignedBranchProgram(testCase);
    const CPU::LabelTable labels = signedBranchLabels();

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

    CPU cpu;
    cpu.loadBinaryProgram(program);
    cpu.run();

    if (cpu.state().hasError()) {
        throw std::runtime_error(
            std::string("Binary execution failed: ") +
            cpu.state().errorMessage()
        );
    }

    if (!cpu.state().halted()) {
        throw std::runtime_error("Binary execution did not halt");
    }

    return cpu.state().registers().get(RegisterName::R2) == 1;
}

int runSignedBranchTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU Signed Branch Correctness Test ===\n\n";
    std::cout << "Rules after CMP lhs, rhs:\n";
    std::cout << "  JL: SF != OF\n";
    std::cout << "  JG: ZF == 0 and SF == OF\n\n";

    const std::vector<SignedBranchTestCase> cases = {
        {
            "JL ordinary negative less",
            Opcode::JL,
            -5,
            2,
            true
        },
        {
            "JL ordinary positive greater",
            Opcode::JL,
            5,
            -2,
            false
        },
        {
            "JL overflow boundary: INT64_MIN < 1",
            Opcode::JL,
            std::numeric_limits<std::int64_t>::min(),
            1,
            true
        },
        {
            "JL overflow boundary: INT64_MAX < -1",
            Opcode::JL,
            std::numeric_limits<std::int64_t>::max(),
            -1,
            false
        },
        {
            "JL equality is false",
            Opcode::JL,
            42,
            42,
            false
        },
        {
            "JG ordinary positive greater",
            Opcode::JG,
            5,
            -2,
            true
        },
        {
            "JG ordinary negative less",
            Opcode::JG,
            -5,
            2,
            false
        },
        {
            "JG overflow boundary: INT64_MAX > -1",
            Opcode::JG,
            std::numeric_limits<std::int64_t>::max(),
            -1,
            true
        },
        {
            "JG overflow boundary: INT64_MIN > 1",
            Opcode::JG,
            std::numeric_limits<std::int64_t>::min(),
            1,
            false
        },
        {
            "JG equality is false",
            Opcode::JG,
            42,
            42,
            false
        }
    };

    int failures = 0;

    for (const SignedBranchTestCase& testCase : cases) {
        try {
            const bool vectorTaken =
                runSignedBranchVectorCase(testCase);
            const bool binaryTaken =
                runSignedBranchBinaryCase(testCase);

            const bool passed =
                vectorTaken == testCase.expectedTaken &&
                binaryTaken == testCase.expectedTaken &&
                vectorTaken == binaryTaken;

            std::cout << (passed ? "[PASS] " : "[FAIL] ")
                      << testCase.name
                      << " | vector="
                      << (vectorTaken ? "taken" : "not-taken")
                      << " binary="
                      << (binaryTaken ? "taken" : "not-taken")
                      << " expected="
                      << (testCase.expectedTaken ? "taken" : "not-taken")
                      << "\n";

            if (!passed) {
                ++failures;
            }
        } catch (const std::exception& ex) {
            std::cout << "[FAIL] "
                      << testCase.name
                      << " | "
                      << ex.what()
                      << "\n";
            ++failures;
        }
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Signed branch correctness test finished successfully.\n";
        return 0;
    }

    std::cout << "Signed branch correctness test failed. Failure count: "
              << failures
              << "\n";
    return 1;
}

struct DifferentialProgram {
    std::vector<zero_cpu::Instruction> instructions;
    zero_cpu::CPU::LabelTable labels;
};

DifferentialProgram makeDifferentialProgram() {
    using namespace zero_cpu;

    DifferentialProgram program;

    program.instructions = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(10)
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R2),
            Operand::immediate(3)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(RegisterName::R1),
            Operand::registerOperand(RegisterName::R2)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(96),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R3),
            Operand::memoryAddress(96)
        ),
        Instruction(
            Opcode::SUB,
            Operand::registerOperand(RegisterName::R3),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::PUSH,
            Operand::registerOperand(RegisterName::R3)
        ),
        Instruction(
            Opcode::POP,
            Operand::registerOperand(RegisterName::R4)
        ),
        Instruction(
            Opcode::CMP,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(12)
        ),
        Instruction(
            Opcode::JE,
            Operand::label("equal")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R5),
            Operand::immediate(-1)
        ),
        Instruction(
            Opcode::JMP,
            Operand::label("after_equal")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R5),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::AND,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(15)
        ),
        Instruction(
            Opcode::OR,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::XOR,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(6)
        ),
        Instruction(
            Opcode::NOT,
            Operand::registerOperand(RegisterName::R2)
        ),
        Instruction(
            Opcode::TEST,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::CMP,
            Operand::registerOperand(RegisterName::R4),
            Operand::immediate(5)
        ),
        Instruction(
            Opcode::JG,
            Operand::label("greater")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R6),
            Operand::immediate(-1)
        ),
        Instruction(
            Opcode::JMP,
            Operand::label("after_greater")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R6),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::CMP,
            Operand::registerOperand(RegisterName::R2),
            Operand::immediate(0)
        ),
        Instruction(
            Opcode::JL,
            Operand::label("negative")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R7),
            Operand::immediate(-1)
        ),
        Instruction(
            Opcode::JMP,
            Operand::label("done")
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R7),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(104),
            Operand::registerOperand(RegisterName::R7)
        ),
        Instruction(Opcode::HALT)
    };

    program.labels = {
        {"equal", 12},
        {"after_equal", 13},
        {"greater", 22},
        {"after_greater", 23},
        {"negative", 27},
        {"done", 29}
    };

    return program;
}

zero_cpu::binary::BinaryProgram encodeDifferentialProgram(
    const DifferentialProgram& source
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram(source.instructions, source.labels);

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

bool compareDifferentialStates(
    const zero_cpu::CPU& vectorCpu,
    const zero_cpu::CPU& binaryCpu,
    std::size_t step,
    const std::string& phase
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    bool passed = true;

    const std::size_t binaryPc = binaryCpu.state().pc();
    std::size_t normalizedBinaryPc =
        std::numeric_limits<std::size_t>::max();

    if (
        binaryPc >= binaryCpu.binaryCodeBase() &&
        (binaryPc - binaryCpu.binaryCodeBase()) %
                kInstructionSize ==
            0
    ) {
        normalizedBinaryPc =
            (binaryPc - binaryCpu.binaryCodeBase()) /
            kInstructionSize;
    }

    auto reportMismatch = [&](const std::string& message) {
        std::cout << "[FAIL] step "
                  << step
                  << " "
                  << phase
                  << " | "
                  << message
                  << "\n";
        passed = false;
    };

    if (vectorCpu.state().pc() != normalizedBinaryPc) {
        reportMismatch(
            "PC vector=" +
            std::to_string(vectorCpu.state().pc()) +
            " binary-index=" +
            std::to_string(normalizedBinaryPc) +
            " binary-address=" +
            std::to_string(binaryPc)
        );
    }

    const auto vectorRegisters =
        vectorCpu.state().registers().snapshot();
    const auto binaryRegisters =
        binaryCpu.state().registers().snapshot();

    for (std::size_t i = 0; i < vectorRegisters.size(); ++i) {
        if (vectorRegisters[i] != binaryRegisters[i]) {
            reportMismatch(
                "R" +
                std::to_string(i) +
                " vector=" +
                std::to_string(vectorRegisters[i]) +
                " binary=" +
                std::to_string(binaryRegisters[i])
            );
        }
    }

    if (
        vectorCpu.state().flags().raw() !=
        binaryCpu.state().flags().raw()
    ) {
        reportMismatch(
            "FLAGS vector=" +
            std::to_string(vectorCpu.state().flags().raw()) +
            " binary=" +
            std::to_string(binaryCpu.state().flags().raw())
        );
    }

    if (vectorCpu.state().sp() != binaryCpu.state().sp()) {
        reportMismatch(
            "SP vector=" +
            std::to_string(vectorCpu.state().sp()) +
            " binary=" +
            std::to_string(binaryCpu.state().sp())
        );
    }

    if (vectorCpu.state().halted() != binaryCpu.state().halted()) {
        reportMismatch("halted state differs");
    }

    if (vectorCpu.state().hasError() != binaryCpu.state().hasError()) {
        reportMismatch("error state differs");
    }

    if (
        vectorCpu.state().errorMessage() !=
        binaryCpu.state().errorMessage()
    ) {
        reportMismatch(
            "error message vector='" +
            vectorCpu.state().errorMessage() +
            "' binary='" +
            binaryCpu.state().errorMessage() +
            "'"
        );
    }

    const std::vector<std::int64_t> vectorMemory =
        vectorCpu.state().memory().snapshot();
    const std::vector<std::int64_t> binaryMemory =
        binaryCpu.state().memory().snapshot();

    if (vectorMemory.size() != binaryMemory.size()) {
        reportMismatch("memory sizes differ");
        return false;
    }

    const std::size_t codeBegin = binaryCpu.binaryCodeBase();
    const std::size_t codeEnd =
        codeBegin + binaryCpu.binaryCodeSize();

    for (std::size_t address = 0; address < vectorMemory.size(); ++address) {
        if (address >= codeBegin && address < codeEnd) {
            continue;
        }

        if (vectorMemory[address] != binaryMemory[address]) {
            reportMismatch(
                "memory[" +
                std::to_string(address) +
                "] vector=" +
                std::to_string(vectorMemory[address]) +
                " binary=" +
                std::to_string(binaryMemory[address])
            );
            break;
        }
    }

    return passed;
}

int runDifferentialTest() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Vector/Binary Differential Test ===\n\n";

    const DifferentialProgram source = makeDifferentialProgram();
    const binary::BinaryProgram encoded =
        encodeDifferentialProgram(source);

    CPU vectorCpu;
    vectorCpu.loadProgram(source.instructions, source.labels);

    CPU binaryCpu;
    binaryCpu.loadBinaryProgram(encoded);

    constexpr std::size_t kMaxDifferentialSteps = 100;
    std::size_t step = 0;

    if (
        !compareDifferentialStates(
            vectorCpu,
            binaryCpu,
            step,
            "initial"
        )
    ) {
        return 1;
    }

    while (step < kMaxDifferentialSteps) {
        if (
            vectorCpu.state().halted() ||
            binaryCpu.state().halted() ||
            vectorCpu.state().hasError() ||
            binaryCpu.state().hasError()
        ) {
            break;
        }

        const std::size_t instructionIndex =
            vectorCpu.state().pc();

        if (instructionIndex >= source.instructions.size()) {
            std::cout
                << "[FAIL] vector PC left program before halt\n";
            return 1;
        }

        const std::string instructionText =
            source.instructions[instructionIndex].toString();

        vectorCpu.step();
        binaryCpu.step();
        ++step;

        if (
            !compareDifferentialStates(
                vectorCpu,
                binaryCpu,
                step,
                instructionText
            )
        ) {
            return 1;
        }

        std::cout << "[PASS] step "
                  << step
                  << " | "
                  << instructionText
                  << "\n";
    }

    if (step >= kMaxDifferentialSteps) {
        std::cout << "[FAIL] differential step limit reached\n";
        return 1;
    }

    if (
        !vectorCpu.state().halted() ||
        !binaryCpu.state().halted()
    ) {
        std::cout << "[FAIL] both execution paths did not halt\n";
        return 1;
    }

    if (
        vectorCpu.state().hasError() ||
        binaryCpu.state().hasError()
    ) {
        std::cout << "[FAIL] execution ended with an error\n";
        return 1;
    }

    bool finalPassed = true;

    auto expect = [&](const std::string& name,
                      std::int64_t actual,
                      std::int64_t expected) {
        const bool passed = actual == expected;
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << name
                  << " = "
                  << actual
                  << " expected "
                  << expected
                  << "\n";
        if (!passed) {
            finalPassed = false;
        }
    };

    expect(
        "R1",
        vectorCpu.state().registers().get(RegisterName::R1),
        13
    );
    expect(
        "R2",
        vectorCpu.state().registers().get(RegisterName::R2),
        -4
    );
    expect(
        "R4",
        vectorCpu.state().registers().get(RegisterName::R4),
        11
    );
    expect(
        "R5",
        vectorCpu.state().registers().get(RegisterName::R5),
        1
    );
    expect(
        "R6",
        vectorCpu.state().registers().get(RegisterName::R6),
        1
    );
    expect(
        "R7",
        vectorCpu.state().registers().get(RegisterName::R7),
        1
    );
    expect(
        "Memory[96]",
        vectorCpu.state().memory().read(96),
        13
    );
    expect(
        "Memory[104]",
        vectorCpu.state().memory().read(104),
        1
    );
    expect(
        "SP",
        static_cast<std::int64_t>(vectorCpu.state().sp()),
        static_cast<std::int64_t>(CPUState::kDefaultStackBase)
    );

    if (!finalPassed) {
        std::cout
            << "\nDifferential final-state validation failed.\n";
        return 1;
    }

    std::cout
        << "\nVector/binary differential test finished successfully.\n";
    return 0;
}

zero_cpu::binary::BinaryProgram makeErrorTestBinary(
    const std::vector<zero_cpu::Instruction>& instructions,
    const zero_cpu::CPU::LabelTable& labels = {}
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

bool expectStableRuntimeError(
    const std::string& name,
    zero_cpu::CPU& cpu,
    const std::string& expectedMessagePart,
    std::size_t expectedSp =
        std::numeric_limits<std::size_t>::max()
) {
    using namespace zero_cpu;

    cpu.run(64);

    bool passed = true;

    if (!cpu.state().hasError()) {
        std::cout << "[FAIL] "
                  << name
                  << " | CPU did not enter error state\n";
        passed = false;
    }

    if (
        cpu.state().errorMessage().find(expectedMessagePart) ==
        std::string::npos
    ) {
        std::cout << "[FAIL] "
                  << name
                  << " | expected error containing '"
                  << expectedMessagePart
                  << "' but got '"
                  << cpu.state().errorMessage()
                  << "'\n";
        passed = false;
    }

    if (!cpu.state().halted()) {
        std::cout << "[FAIL] "
                  << name
                  << " | error state did not halt the CPU\n";
        passed = false;
    }

    if (
        expectedSp != std::numeric_limits<std::size_t>::max() &&
        cpu.state().sp() != expectedSp
    ) {
        std::cout << "[FAIL] "
                  << name
                  << " | SP expected "
                  << expectedSp
                  << " but got "
                  << cpu.state().sp()
                  << "\n";
        passed = false;
    }

    const std::size_t pcBefore = cpu.state().pc();
    const std::size_t spBefore = cpu.state().sp();
    const bool haltedBefore = cpu.state().halted();
    const std::string errorBefore = cpu.state().errorMessage();
    const std::uint32_t flagsBefore = cpu.state().flags().raw();
    const auto registersBefore =
        cpu.state().registers().snapshot();
    const auto memoryBefore =
        cpu.state().memory().snapshot();
    const std::size_t traceCountBefore =
        cpu.traceLogger().size();

    cpu.step();

    const bool stable =
        cpu.state().pc() == pcBefore &&
        cpu.state().sp() == spBefore &&
        cpu.state().halted() == haltedBefore &&
        cpu.state().hasError() &&
        cpu.state().errorMessage() == errorBefore &&
        cpu.state().flags().raw() == flagsBefore &&
        cpu.state().registers().snapshot() == registersBefore &&
        cpu.state().memory().snapshot() == memoryBefore &&
        cpu.traceLogger().size() == traceCountBefore;

    if (!stable) {
        std::cout << "[FAIL] "
                  << name
                  << " | state changed after step() in error state\n";
        passed = false;
    }

    if (passed) {
        std::cout << "[PASS] "
                  << name
                  << " | "
                  << cpu.state().errorMessage()
                  << "\n";
    }

    return passed;
}

int runErrorInvariantTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout
        << "=== Zero-CPU Error and Invariant Test ===\n\n";

    int failures = 0;

    auto check = [&](const std::string& name,
                     CPU& cpu,
                     const std::string& expectedMessage,
                     std::size_t expectedSp =
                         std::numeric_limits<std::size_t>::max()) {
        if (
            !expectStableRuntimeError(
                name,
                cpu,
                expectedMessage,
                expectedSp
            )
        ) {
            ++failures;
        }
    };

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::POP,
                    Operand::registerOperand(RegisterName::R1)
                )
            },
            {}
        );
        check(
            "vector empty POP",
            cpu,
            "Stack underflow",
            CPUState::kDefaultStackBase
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::PUSH,
                    Operand::immediate(1)
                )
            },
            {}
        );
        cpu.state().setSp(cpu.state().memory().size());
        check(
            "vector stack overflow",
            cpu,
            "Stack overflow",
            cpu.state().memory().size()
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::PUSH,
                    Operand::immediate(1)
                )
            },
            {}
        );
        cpu.state().setSp(CPUState::kDefaultStackBase + 1);
        check(
            "vector misaligned stack pointer",
            cpu,
            "Stack pointer is not slot-aligned",
            CPUState::kDefaultStackBase + 1
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::LOAD,
                    Operand::registerOperand(RegisterName::R1),
                    Operand::memoryAddress(
                        Memory::kDefaultMemorySize
                    )
                )
            },
            {}
        );
        check(
            "vector out-of-range memory read",
            cpu,
            "Memory access out of range"
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::MOV,
                    Operand::registerOperand(RegisterName::R1),
                    Operand::immediate(-1)
                ),
                Instruction(
                    Opcode::LOAD,
                    Operand::registerOperand(RegisterName::R2),
                    Operand::registerIndirectAddress(RegisterName::R1)
                )
            },
            {}
        );
        check(
            "vector negative indirect address",
            cpu,
            "Negative register-indirect memory address"
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::MOV,
                    Operand::memoryAddress(96),
                    Operand::immediate(1)
                )
            },
            {}
        );
        check(
            "vector non-register destination",
            cpu,
            "Destination must be register"
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {
                Instruction(
                    Opcode::ADD,
                    Operand::registerOperand(RegisterName::R1)
                )
            },
            {}
        );
        check(
            "vector missing second operand",
            cpu,
            "Instruction requires two operands"
        );
    }

    {
        CPU cpu;
        cpu.loadProgram(
            {Instruction(Opcode::NOP)},
            {}
        );
        check(
            "vector PC leaves program without HALT",
            cpu,
            "PC out of program range"
        );
    }

    {
        BinaryProgram program = makeErrorTestBinary(
            {Instruction(Opcode::HALT)}
        );
        program.code[0] = 0xFF;

        CPU cpu;
        cpu.loadBinaryProgram(program);
        check(
            "binary invalid opcode byte",
            cpu,
            "Invalid opcode byte"
        );
    }

    {
        BinaryProgram program = makeErrorTestBinary(
            {
                Instruction(
                    Opcode::MOV,
                    Operand::registerOperand(RegisterName::R1),
                    Operand::immediate(1)
                )
            }
        );
        program.code[4] = 9;

        CPU cpu;
        cpu.loadBinaryProgram(program);
        check(
            "binary invalid register payload",
            cpu,
            "Invalid binary register payload"
        );
    }

    {
        BinaryProgram program = makeErrorTestBinary(
            {
                Instruction(
                    Opcode::NOP,
                    Operand::immediate(1)
                )
            }
        );

        CPU cpu;
        cpu.loadBinaryProgram(program);
        check(
            "binary NOP with operand",
            cpu,
            "Binary instruction requires no operands"
        );
    }

    {
        BinaryProgram program = makeErrorTestBinary(
            {
                Instruction(
                    Opcode::POP,
                    Operand::registerOperand(RegisterName::R1)
                )
            }
        );

        CPU cpu;
        cpu.loadBinaryProgram(program);
        check(
            "binary empty POP",
            cpu,
            "Stack underflow",
            CPUState::kDefaultStackBase
        );
    }

    {
        BinaryProgram program = makeErrorTestBinary(
            {
                Instruction(
                    Opcode::LOAD,
                    Operand::registerOperand(RegisterName::R1),
                    Operand::memoryAddress(
                        Memory::kDefaultMemorySize
                    )
                )
            }
        );

        CPU cpu;
        cpu.loadBinaryProgram(program);
        check(
            "binary out-of-range memory read",
            cpu,
            "Memory access out of range"
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Error and invariant test finished successfully.\n";
        return 0;
    }

    std::cout
        << "Error and invariant test failed. Failure count: "
        << failures
        << "\n";
    return 1;
}

int runBinaryTest(const std::string& outputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::vector<Instruction> instructions;
    instructions.emplace_back(Opcode::NOP);
    instructions.emplace_back(Opcode::HALT);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> encodedCode =
        encoder.encodeProgram(instructions, {});

    BinaryProgram program;

    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(encodedCode.size());
    program.code = std::move(encodedCode);

    BinaryWriter writer;
    writer.writeFile(outputPath, program);

    std::cout << "Wrote binary file: " << outputPath << "\n";

    BinaryReader reader;
    BinaryProgram loaded = reader.readFile(outputPath);

    std::cout << "Read binary file successfully.\n\n";

    printBinaryHeader(loaded);
    std::cout << "\n";

    std::cout << "=== Code Bytes ===\n";
    printHexBytes(loaded.code);
    std::cout << "\n";

    printDecodedInstructions(loaded.code);
    std::cout << "\n";


    std::cout << "=== Binary Entry-Point Validation ===\n";

    struct EntryCase {
        const char* name;
        std::uint32_t entryPoint;
        std::size_t instructionCount;
        bool accepted;
    };

    const std::vector<EntryCase> entryCases = {
        {"entry 0", 0, 1, true},
        {
            "entry 24",
            static_cast<std::uint32_t>(kInstructionSize),
            2,
            true
        },
        {"misaligned entry 4", 4, 2, false},
        {"misaligned entry 20", 20, 2, false},
        {
            "entry equals code size",
            static_cast<std::uint32_t>(kInstructionSize * 2),
            2,
            false
        }
    };

    bool entryValidationPassed = true;

    for (const EntryCase& testCase : entryCases) {
        std::vector<Instruction> caseInstructions;

        for (std::size_t i = 0; i < testCase.instructionCount; ++i) {
            caseInstructions.emplace_back(
                i + 1 == testCase.instructionCount
                    ? Opcode::HALT
                    : Opcode::NOP
            );
        }

        std::vector<std::uint8_t> caseCode =
            encoder.encodeProgram(caseInstructions, {});

        BinaryProgram caseProgram;
        caseProgram.header.major_version = kMajorVersion;
        caseProgram.header.minor_version = kMinorVersion;
        caseProgram.header.endianness = BinaryEndianness::Little;
        caseProgram.header.entry_point = testCase.entryPoint;
        caseProgram.header.code_size =
            static_cast<std::uint32_t>(caseCode.size());
        caseProgram.code = std::move(caseCode);

        bool readerAccepted = false;
        bool loaderAccepted = false;

        try {
            BinaryWriter caseWriter;
            BinaryReader caseReader;
            const std::vector<std::uint8_t> caseBytes =
                caseWriter.writeToBytes(caseProgram);
            (void)caseReader.readFromBytes(caseBytes);
            readerAccepted = true;
        } catch (const std::exception&) {
            readerAccepted = false;
        }

        try {
            Memory caseMemory;
            BinaryLoader caseLoader;
            (void)caseLoader.loadIntoMemory(caseProgram, caseMemory, 0);
            loaderAccepted = true;
        } catch (const std::exception&) {
            loaderAccepted = false;
        }

        const bool passed =
            readerAccepted == testCase.accepted &&
            loaderAccepted == testCase.accepted &&
            readerAccepted == loaderAccepted;

        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << testCase.name
                  << " | reader="
                  << (readerAccepted ? "accepted" : "rejected")
                  << " loader="
                  << (loaderAccepted ? "accepted" : "rejected")
                  << " expected="
                  << (testCase.accepted ? "accepted" : "rejected")
                  << "\n";

        if (!passed) {
            entryValidationPassed = false;
        }
    }

    std::cout << "\n";

    if (!entryValidationPassed) {
        std::cout << "Binary entry-point validation failed.\n";
        return 1;
    }

    std::cout << "Binary encoder/writer/reader/decoder test finished successfully.\n";

    return 0;
}

int dumpBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram loaded = reader.readFile(inputPath);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(loaded);
    std::cout << "\n";

    std::cout << "=== Code Bytes ===\n";
    printHexBytes(loaded.code);
    std::cout << "\n";

    printDecodedInstructions(loaded.code);
    std::cout << "\n";

    std::cout << "Binary dump finished successfully.\n";

    return 0;
}

int loadBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    Memory memory;

    BinaryLoader loader;
    LoadedBinaryImage image = loader.loadIntoMemory(program, memory);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    std::cout << "=== Loaded Binary Image ===\n";
    std::cout << "Code Base: "
              << image.code_base
              << "\n";

    std::cout << "Entry Point: "
              << image.entry_point
              << "\n";

    std::cout << "Code Size: "
              << image.code_size
              << " bytes\n\n";

    const std::size_t previewCount =
        std::min(image.code_size, kLoadedMemoryPreviewCount);

    std::cout << "=== Memory Preview ===\n";
    std::cout << "Memory[0.."
              << (previewCount == 0 ? 0 : previewCount - 1)
              << "]: "
              << memory.dumpRange(0, previewCount)
              << "\n\n";

    printDecodedInstructions(program.code);
    std::cout << "\n";

    std::cout << "Binary load test finished successfully.\n";

    return 0;
}

int cpuLoadBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    CPU cpu;
    cpu.loadBinaryProgram(program);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    std::cout << "=== CPU Binary Load State ===\n";
    std::cout << "Has Binary Program: "
              << (cpu.hasBinaryProgram() ? "true" : "false")
              << "\n";

    std::cout << "Code Base: "
              << cpu.binaryCodeBase()
              << "\n";

    std::cout << "Entry Point: "
              << cpu.binaryEntryPoint()
              << "\n";

    std::cout << "Code Size: "
              << cpu.binaryCodeSize()
              << " bytes\n";

    std::cout << "Current PC: "
              << cpu.state().pc()
              << "\n\n";

    std::cout << "=== CPU State ===\n";
    std::cout << cpu.state().summary();

    const std::size_t previewCount =
        std::min(cpu.binaryCodeSize(), kLoadedMemoryPreviewCount);

    std::cout << "=== CPU Memory Preview ===\n";
    std::cout << "Memory["
              << cpu.binaryCodeBase()
              << ".."
              << (previewCount == 0
                      ? cpu.binaryCodeBase()
                      : cpu.binaryCodeBase() + previewCount - 1)
              << "]: "
              << cpu.state().memory().dumpRange(
                     cpu.binaryCodeBase(),
                     previewCount
                 )
              << "\n\n";

    printDecodedInstructions(program.code);
    std::cout << "\n";

    std::cout << "CPU binary load test finished successfully.\n";

    return 0;
}

int runBinaryFile(
    const std::string& inputPath,
    const RunBinaryOptions& options
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    CPU cpu;

    std::shared_ptr<MMIOBus> mmioBus;
    std::shared_ptr<DebugOutputDevice> debugOutputDevice;

    if (options.enableDebugMMIO) {
        mmioBus = std::make_shared<MMIOBus>();
        debugOutputDevice = std::make_shared<DebugOutputDevice>();
        mmioBus->mapDevice(
            memory_map::kDebugOutputBase,
            memory_map::kDebugOutputSize,
            debugOutputDevice
        );
        cpu.setMMIOBus(mmioBus);
    }

    cpu.loadBinaryProgram(program);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    if (!options.watchAddresses.empty()) {
        std::cout << "Memory Watch Addresses:";

        for (const std::size_t address : options.watchAddresses) {
            std::cout << " " << address;
        }

        std::cout << "\n";
    }

    if (!options.memoryExpectations.empty()) {
        std::cout << "Memory Expectations:";

        for (const MemoryExpectation& expectation : options.memoryExpectations) {
            std::cout << " "
                      << expectation.address
                      << "="
                      << expectation.expected;
        }

        std::cout << "\n";
    }

    if (options.enableDebugMMIO) {
        std::cout << "Debug MMIO: enabled at 0xF000..0xF00F\n";
    }

    if (!options.watchAddresses.empty() ||
        !options.memoryExpectations.empty() ||
        options.enableDebugMMIO) {
        std::cout << "\n";
    }

    std::cout << "=== Binary Execution ===\n";

    std::size_t stepCount = 0;

    while (!cpu.state().halted()) {
        std::cout << "Step " << stepCount
                  << " | PC=" << cpu.state().pc()
                  << " | "
                  << currentBinaryInstructionText(cpu)
                  << "\n";

        cpu.step();

        std::cout << cpu.state().summary()
                  << "\n";

        if (cpu.state().hasError()) {
            std::cout << "Execution failed: "
                      << cpu.state().errorMessage()
                      << "\n\n";

            printWatchedMemory(cpu, options.watchAddresses);
            if (!options.watchAddresses.empty()) {
                std::cout << "\n";
            }

            checkMemoryExpectations(cpu, options.memoryExpectations);
            if (!options.memoryExpectations.empty()) {
                std::cout << "\n";
            }

            if (debugOutputDevice) {
                printDebugOutputDevice(*debugOutputDevice);
                std::cout << "\n";
            }

            printFinalCheck(cpu);
            return 1;
        }

        ++stepCount;

        if (stepCount > 1000) {
            std::cout << "Step limit reached in binary execution.\n\n";

            printWatchedMemory(cpu, options.watchAddresses);
            if (!options.watchAddresses.empty()) {
                std::cout << "\n";
            }

            checkMemoryExpectations(cpu, options.memoryExpectations);
            if (!options.memoryExpectations.empty()) {
                std::cout << "\n";
            }

            if (debugOutputDevice) {
                printDebugOutputDevice(*debugOutputDevice);
                std::cout << "\n";
            }

            printFinalCheck(cpu);
            return 1;
        }
    }

    std::cout << "=== Binary Final CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    printWatchedMemory(cpu, options.watchAddresses);
    if (!options.watchAddresses.empty()) {
        std::cout << "\n";
    }

    const bool memoryExpectationsPassed = checkMemoryExpectations(
        cpu,
        options.memoryExpectations
    );

    if (!options.memoryExpectations.empty()) {
        std::cout << "\n";
    }

    if (debugOutputDevice) {
        printDebugOutputDevice(*debugOutputDevice);
        std::cout << "\n";
    }

    printFinalCheck(cpu);
    std::cout << "\n";

    if (!memoryExpectationsPassed) {
        std::cout << "Binary execution finished, but memory expectations failed.\n";
        return 1;
    }

    std::cout << "Binary execution finished successfully.\n";

    return 0;
}

std::uint64_t parseRunProcessesPositiveU64(
    const std::string& text,
    const std::string& optionName
) {
    std::size_t parsed = 0;

    const unsigned long long value =
        std::stoull(
            text,
            &parsed,
            0
        );

    if (
        parsed != text.size()
        || value == 0
    ) {
        throw std::invalid_argument(
            optionName
            + " requires a positive integer"
        );
    }

    return static_cast<std::uint64_t>(
        value
    );
}

std::size_t parseRunProcessesPositiveSize(
    const std::string& text,
    const std::string& optionName
) {
    const std::uint64_t value =
        parseRunProcessesPositiveU64(
            text,
            optionName
        );

    if (
        value
        > static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t
            >::max()
        )
    ) {
        throw std::out_of_range(
            optionName
            + " exceeds size_t"
        );
    }

    return static_cast<std::size_t>(
        value
    );
}

struct ProcessExitExpectation {
    zero_cpu::kernel::ProcessId pid = 0;
    std::int64_t exit_code = 0;
};

struct HardwareRegisterExpectation {
    std::size_t offset = 0;
    std::int64_t value = 0;
};

ProcessExitExpectation
parseProcessExitExpectation(
    const std::string& text
) {
    const std::size_t separator =
        text.find('=');

    if (
        separator == std::string::npos
        || separator == 0
        || separator + 1 >= text.size()
    ) {
        throw std::invalid_argument(
            "Invalid --expect-exit value. "
            "Expected PID=CODE, got: "
            + text
        );
    }

    const std::string pidText =
        text.substr(0, separator);

    std::size_t parsedPidLength = 0;
    const unsigned long long rawPid =
        std::stoull(
            pidText,
            &parsedPidLength,
            0
        );

    if (
        parsedPidLength != pidText.size()
        || rawPid == 0
        || rawPid
            > static_cast<unsigned long long>(
                std::numeric_limits<
                    zero_cpu::kernel::ProcessId
                >::max()
            )
    ) {
        throw std::invalid_argument(
            "Invalid process ID in --expect-exit: "
            + text
        );
    }

    const std::string codeText =
        text.substr(separator + 1);

    std::size_t parsedCodeLength = 0;
    const long long rawCode =
        std::stoll(
            codeText,
            &parsedCodeLength,
            0
        );

    if (parsedCodeLength != codeText.size()) {
        throw std::invalid_argument(
            "Invalid exit code in --expect-exit: "
            + text
        );
    }

    ProcessExitExpectation expectation;
    expectation.pid =
        static_cast<
            zero_cpu::kernel::ProcessId
        >(rawPid);
    expectation.exit_code =
        static_cast<std::int64_t>(rawCode);

    return expectation;
}

HardwareRegisterExpectation
parseHardwareRegisterExpectation(
    const std::string& text
) {
    const std::size_t separator =
        text.find('=');

    if (
        separator == std::string::npos
        || separator == 0
        || separator + 1 >= text.size()
    ) {
        throw std::invalid_argument(
            "Invalid --expect-hardware value. "
            "Expected OFFSET=VALUE, got: "
            + text
        );
    }

    const std::string offsetText =
        text.substr(0, separator);

    std::size_t parsedOffsetLength = 0;
    const unsigned long long rawOffset =
        std::stoull(
            offsetText,
            &parsedOffsetLength,
            0
        );

    if (
        parsedOffsetLength != offsetText.size()
        || rawOffset
            >= static_cast<unsigned long long>(
                zero_cpu::memory_map::
                    kHardwareSize
            )
        || rawOffset
            % zero_cpu::memory_map::
                kHardwareRegisterWidth
            != 0
    ) {
        throw std::invalid_argument(
            "Invalid hardware offset in "
            "--expect-hardware: "
            + text
        );
    }

    const std::string valueText =
        text.substr(separator + 1);

    std::size_t parsedValueLength = 0;
    const long long rawValue =
        std::stoll(
            valueText,
            &parsedValueLength,
            0
        );

    if (parsedValueLength != valueText.size()) {
        throw std::invalid_argument(
            "Invalid hardware value in "
            "--expect-hardware: "
            + text
        );
    }

    HardwareRegisterExpectation expectation;
    expectation.offset =
        static_cast<std::size_t>(rawOffset);
    expectation.value =
        static_cast<std::int64_t>(rawValue);

    return expectation;
}

// Patch: v1.4-protected-runtime-cli-r1
// Patch: v1.4-protected-debug-cli-r1

int runProcessesCommand(
    int argc,
    char* argv[],
    int startIndex
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    MultiProcessRunOptions options;
    std::vector<std::string> paths;

    bool protectedSyscalls = false;
    bool useMockHardware = false;

    std::string serialPort;
    std::uint32_t serialBaud = 115200;
    bool baudSpecified = false;

    std::vector<ProcessExitExpectation>
        exitExpectations;

    std::vector<HardwareRegisterExpectation>
        hardwareExpectations;

    int index = startIndex;

    while (index < argc) {
        const std::string argument =
            argv[index];

        if (argument == "--quantum") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--quantum requires a value"
                );
            }

            options.quantum =
                parseRunProcessesPositiveU64(
                    argv[index + 1],
                    "--quantum"
                );

            index += 2;
            continue;
        }

        if (argument == "--max-steps") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--max-steps requires a value"
                );
            }

            options.max_lifecycle_steps =
                parseRunProcessesPositiveSize(
                    argv[index + 1],
                    "--max-steps"
                );

            index += 2;
            continue;
        }

        if (argument == "--protected-syscalls") {
            protectedSyscalls = true;
            ++index;
            continue;
        }

        if (argument == "--hardware-mock") {
            if (!serialPort.empty()) {
                throw std::invalid_argument(
                    "--hardware-mock cannot be combined "
                    "with --hardware-serial"
                );
            }

            useMockHardware = true;
            protectedSyscalls = true;
            ++index;
            continue;
        }

        if (argument == "--hardware-serial") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--hardware-serial requires a port"
                );
            }

            if (useMockHardware) {
                throw std::invalid_argument(
                    "--hardware-serial cannot be combined "
                    "with --hardware-mock"
                );
            }

            serialPort = argv[index + 1];

            if (serialPort.empty()) {
                throw std::invalid_argument(
                    "--hardware-serial port must not be empty"
                );
            }

            protectedSyscalls = true;
            index += 2;
            continue;
        }

        if (argument == "--baud") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--baud requires a value"
                );
            }

            const std::uint64_t value =
                parseRunProcessesPositiveU64(
                    argv[index + 1],
                    "--baud"
                );

            if (
                value
                > static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::uint32_t
                    >::max()
                )
            ) {
                throw std::out_of_range(
                    "--baud is too large"
                );
            }

            serialBaud =
                static_cast<std::uint32_t>(
                    value
                );

            baudSpecified = true;
            index += 2;
            continue;
        }

        if (argument == "--expect-exit") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--expect-exit requires PID=CODE"
                );
            }

            exitExpectations.push_back(
                parseProcessExitExpectation(
                    argv[index + 1]
                )
            );

            index += 2;
            continue;
        }

        if (argument == "--expect-hardware") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--expect-hardware requires "
                    "OFFSET=VALUE"
                );
            }

            hardwareExpectations.push_back(
                parseHardwareRegisterExpectation(
                    argv[index + 1]
                )
            );

            index += 2;
            continue;
        }

        if (argument.rfind("--", 0) == 0) {
            throw std::invalid_argument(
                "Unknown run-processes option: "
                + argument
            );
        }

        paths.push_back(argument);
        ++index;
    }

    if (paths.empty()) {
        throw std::invalid_argument(
            "run-processes requires at least "
            "one .zbin file"
        );
    }

    if (baudSpecified && serialPort.empty()) {
        throw std::invalid_argument(
            "--baud requires --hardware-serial"
        );
    }

    if (
        !hardwareExpectations.empty()
        && !useMockHardware
    ) {
        throw std::invalid_argument(
            "--expect-hardware currently requires "
            "--hardware-mock"
        );
    }

    std::shared_ptr<
        hardware::MockHardwareBus
    > mockHardware;

    std::shared_ptr<
        hardware::HardwareBus
    > hardwareBus;

    if (useMockHardware) {
        mockHardware =
            std::make_shared<
                hardware::MockHardwareBus
            >(
                "cli-protected-hardware"
            );

        mockHardware->connect();
        hardwareBus = mockHardware;
    } else if (!serialPort.empty()) {
        auto transport =
            std::make_shared<
                hardware::WindowsSerialTransport
            >(
                serialPort,
                serialBaud
            );

        auto serialHardware =
            std::make_shared<
                hardware::SerialHardwareBus
            >(
                transport
            );

        serialHardware->connect();
        hardwareBus = serialHardware;

        std::cout
            << "Protected serial hardware: "
            << serialPort
            << " @ "
            << serialBaud
            << " baud\n";
    }

    if (hardwareBus) {
        auto device =
            std::make_shared<
                hardware::HardwareMMIODevice
            >(
                hardwareBus
            );

        auto mmio =
            std::make_shared<MMIOBus>();

        mmio->mapDevice(
            memory_map::kHardwareBase,
            memory_map::kHardwareSize,
            device
        );

        options.mmio_bus = mmio;
    }

    if (protectedSyscalls) {
        options.software_interrupt_handler =
            std::make_shared<
                kernel::ProtectedSyscallDispatcher
            >();

        std::cout
            << "Protected syscalls: enabled\n";
    }

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runFiles(paths, options);

    bool expectationsPassed = true;

    for (
        const ProcessExitExpectation& expectation :
        exitExpectations
    ) {
        bool passed = false;
        std::int64_t actual = 0;

        try {
            const ProcessRunSummary& process =
                result.process(
                    expectation.pid
                );

            if (process.has_exit_code) {
                actual = process.exit_code;
                passed =
                    actual
                    == expectation.exit_code;
            }
        } catch (const std::exception&) {
            passed = false;
        }

        if (passed) {
            std::cout
                << "[PASS] PID "
                << expectation.pid
                << " exit code = "
                << expectation.exit_code
                << "\n";
        } else {
            std::cout
                << "[FAIL] PID "
                << expectation.pid
                << " exit code expected "
                << expectation.exit_code
                << " but got "
                << actual
                << "\n";

            expectationsPassed = false;
        }
    }

    for (
        const HardwareRegisterExpectation& expectation :
        hardwareExpectations
    ) {
        const std::int64_t actual =
            mockHardware->registerValue(
                expectation.offset
            );

        if (actual == expectation.value) {
            std::cout
                << "[PASS] Hardware["
                << expectation.offset
                << "] = "
                << actual
                << "\n";
        } else {
            std::cout
                << "[FAIL] Hardware["
                << expectation.offset
                << "] expected "
                << expectation.value
                << " but got "
                << actual
                << "\n";

            expectationsPassed = false;
        }
    }

    std::cout
        << "=== Zero-CPU Multi-Process Run ==="
        << std::endl;

    std::cout
        << "Runtime state: "
        << processRuntimeStateToString(
            result.runtime_state
        )
        << std::endl;

    std::cout
        << "Processes: "
        << result.process_count
        << std::endl;

    std::cout
        << "Lifecycle steps: "
        << result.lifecycle_steps
        << std::endl;

    std::cout
        << "Terminations: "
        << result.termination_count
        << std::endl;

    std::cout
        << "Faults: "
        << result.fault_count
        << std::endl;

    std::cout
        << "Preemptions: "
        << result.preemption_count
        << std::endl;

    std::cout
        << "Context switches: "
        << result.context_switch_count
        << std::endl;

    std::cout
        << "Step limit reached: "
        << (
            result.step_limit_reached
                ? "true"
                : "false"
        )
        << std::endl;

    for (
        const ProcessRunSummary& process :
        result.processes
    ) {
        std::cout << std::endl;

        std::cout
            << "PID "
            << process.pid
            << " | "
            << process.source_name
            << std::endl;

        std::cout
            << "  State: "
            << processStateToString(
                process.state
            )
            << std::endl;

        if (process.has_exit_code) {
            std::cout
                << "  Termination: "
                << processTerminationKindToString(
                    process.termination_kind
                )
                << std::endl;

            std::cout
                << "  Exit code: "
                << process.exit_code
                << std::endl;
        }

        if (!process.termination_message.empty()) {
            std::cout
                << "  Message: "
                << process.termination_message
                << std::endl;
        }

        std::cout
            << "  PC: "
            << process.final_context.pc
            << std::endl;

        std::cout
            << "  SP: "
            << process.final_context.sp
            << std::endl;

        std::cout
            << "  Registers:";

        for (
            std::size_t registerIndex = 0;
            registerIndex
                < RegisterFile::kRegisterCount;
            ++registerIndex
        ) {
            std::cout
                << " R"
                << registerIndex
                << "="
                << process.final_context
                    .registers[registerIndex];
        }

        std::cout << std::endl;

        if (process.data_size >= 8) {
            std::cout
                << "  Data["
                << process.data_base
                << "] qword: "
                << process.final_memory.readI64(
                    process.data_base
                )
                << std::endl;
        }
    }

    if (result.step_limit_reached) {
        return 3;
    }

    if (
        result.runtime_state
        == ProcessRuntimeState::Deadlocked
    ) {
        return 4;
    }

    if (!expectationsPassed) {
        return 3;
    }

    if (result.fault_count != 0) {
        return 2;
    }

    return result.success() ? 0 : 1;
}


std::size_t parseDebuggerAddress(
    const std::string& text
) {
    std::size_t parsed = 0;
    const unsigned long long value = std::stoull(text, &parsed, 0);

    if (parsed != text.size()) {
        throw std::invalid_argument(
            "Breakpoint address must be an integer"
        );
    }

    if (
        value > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )
    ) {
        throw std::out_of_range(
            "Breakpoint address exceeds size_t"
        );
    }

    return static_cast<std::size_t>(value);
}

struct DebugMemoryRequest {
    std::size_t address = 0;
    std::size_t count = 0;
};

struct DebugDisassemblyRequest {
    std::size_t address = 0;
    std::size_t instruction_count = 0;
};

int debugProcessesCommand(
    int argc,
    char* argv[],
    int startIndex
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::hardware;
    using namespace zero_cpu::kernel;

    MultiProcessDebugOptions sessionOptions;

    bool hasCommandFile = false;
    std::string commandFilePath;

    bool protectedSyscalls = false;
    bool useMockHardware = false;

    std::string serialPort;
    std::uint32_t serialBaud = 115200;
    bool baudSpecified = false;

    std::vector<ProcessExitExpectation>
        exitExpectations;

    std::vector<HardwareRegisterExpectation>
        hardwareExpectations;

    std::vector<std::string> paths;

    int index = startIndex;

    while (index < argc) {
        const std::string argument =
            argv[index];

        if (argument == "--quantum") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--quantum requires a value"
                );
            }

            sessionOptions.quantum =
                static_cast<std::uint64_t>(
                    parseRunProcessesPositiveSize(
                        argv[index + 1],
                        "--quantum"
                    )
                );

            index += 2;
            continue;
        }

        if (argument == "--max-steps") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--max-steps requires a value"
                );
            }

            sessionOptions.default_continue_steps =
                parseRunProcessesPositiveSize(
                    argv[index + 1],
                    "--max-steps"
                );

            index += 2;
            continue;
        }

        if (argument == "--commands") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--commands requires a file path"
                );
            }

            if (hasCommandFile) {
                throw std::invalid_argument(
                    "--commands may be specified "
                    "only once"
                );
            }

            hasCommandFile = true;
            commandFilePath =
                argv[index + 1];

            index += 2;
            continue;
        }

        if (argument == "--protected-syscalls") {
            protectedSyscalls = true;
            ++index;
            continue;
        }

        if (argument == "--hardware-mock") {
            if (!serialPort.empty()) {
                throw std::invalid_argument(
                    "--hardware-mock cannot be combined "
                    "with --hardware-serial"
                );
            }

            useMockHardware = true;
            protectedSyscalls = true;
            ++index;
            continue;
        }

        if (argument == "--hardware-serial") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--hardware-serial requires a port"
                );
            }

            if (useMockHardware) {
                throw std::invalid_argument(
                    "--hardware-serial cannot be combined "
                    "with --hardware-mock"
                );
            }

            serialPort = argv[index + 1];

            if (serialPort.empty()) {
                throw std::invalid_argument(
                    "--hardware-serial port must not be empty"
                );
            }

            protectedSyscalls = true;
            index += 2;
            continue;
        }

        if (argument == "--baud") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--baud requires a value"
                );
            }

            const std::uint64_t value =
                parseRunProcessesPositiveU64(
                    argv[index + 1],
                    "--baud"
                );

            if (
                value
                > static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::uint32_t
                    >::max()
                )
            ) {
                throw std::out_of_range(
                    "--baud is too large"
                );
            }

            serialBaud =
                static_cast<std::uint32_t>(
                    value
                );

            baudSpecified = true;
            index += 2;
            continue;
        }

        if (argument == "--expect-exit") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--expect-exit requires PID=CODE"
                );
            }

            exitExpectations.push_back(
                parseProcessExitExpectation(
                    argv[index + 1]
                )
            );

            index += 2;
            continue;
        }

        if (argument == "--expect-hardware") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--expect-hardware requires "
                    "OFFSET=VALUE"
                );
            }

            hardwareExpectations.push_back(
                parseHardwareRegisterExpectation(
                    argv[index + 1]
                )
            );

            index += 2;
            continue;
        }

        if (
            argument.rfind("--", 0)
            == 0
        ) {
            throw std::invalid_argument(
                "Unknown debug-processes option: "
                + argument
            );
        }

        paths.push_back(argument);
        ++index;
    }

    if (paths.size() < 2) {
        throw std::invalid_argument(
            "debug-processes requires at least "
            "two executable paths"
        );
    }

    if (baudSpecified && serialPort.empty()) {
        throw std::invalid_argument(
            "--baud requires --hardware-serial"
        );
    }

    if (
        !hardwareExpectations.empty()
        && !useMockHardware
    ) {
        throw std::invalid_argument(
            "--expect-hardware currently requires "
            "--hardware-mock"
        );
    }

    std::shared_ptr<MockHardwareBus>
        mockHardware;

    std::shared_ptr<HardwareBus>
        hardwareBus;

    if (useMockHardware) {
        mockHardware =
            std::make_shared<MockHardwareBus>(
                "debug-cli-protected-hardware"
            );

        mockHardware->connect();
        hardwareBus = mockHardware;
    } else if (!serialPort.empty()) {
        auto transport =
            std::make_shared<
                WindowsSerialTransport
            >(
                serialPort,
                serialBaud
            );

        auto serialHardware =
            std::make_shared<
                SerialHardwareBus
            >(
                transport
            );

        serialHardware->connect();
        hardwareBus = serialHardware;

        std::cout
            << "Protected debug serial hardware: "
            << serialPort
            << " @ "
            << serialBaud
            << " baud\n";
    }

    if (hardwareBus) {
        auto device =
            std::make_shared<
                HardwareMMIODevice
            >(
                hardwareBus
            );

        auto mmio =
            std::make_shared<MMIOBus>();

        mmio->mapDevice(
            memory_map::kHardwareBase,
            memory_map::kHardwareSize,
            device
        );

        sessionOptions.mmio_bus = mmio;
    }

    if (protectedSyscalls) {
        sessionOptions.software_interrupt_handler =
            std::make_shared<
                ProtectedSyscallDispatcher
            >();

        std::cout
            << "Protected debug syscalls: enabled\n";
    }

    MultiProcessDebugSession session(
        paths,
        sessionOptions
    );

    auto validateExpectations =
        [&]() -> bool {
            bool passed = true;

            for (
                const ProcessExitExpectation& expectation :
                exitExpectations
            ) {
                bool matched = false;
                std::int64_t actual = 0;

                try {
                    const ProcessDebugSnapshot snapshot =
                        session.processSnapshot(
                            expectation.pid
                        );

                    if (snapshot.has_exit_code) {
                        actual = snapshot.exit_code;
                        matched =
                            actual
                            == expectation.exit_code;
                    }
                } catch (const std::exception&) {
                    matched = false;
                }

                if (matched) {
                    std::cout
                        << "[PASS] Debug PID "
                        << expectation.pid
                        << " exit code = "
                        << expectation.exit_code
                        << "\n";
                } else {
                    std::cout
                        << "[FAIL] Debug PID "
                        << expectation.pid
                        << " exit code expected "
                        << expectation.exit_code
                        << " but got "
                        << actual
                        << "\n";

                    passed = false;
                }
            }

            for (
                const HardwareRegisterExpectation& expectation :
                    hardwareExpectations
            ) {
                const std::int64_t actual =
                    mockHardware->registerValue(
                        expectation.offset
                    );

                if (actual == expectation.value) {
                    std::cout
                        << "[PASS] Debug Hardware["
                        << expectation.offset
                        << "] = "
                        << actual
                        << "\n";
                } else {
                    std::cout
                        << "[FAIL] Debug Hardware["
                        << expectation.offset
                        << "] expected "
                        << expectation.value
                        << " but got "
                        << actual
                        << "\n";

                    passed = false;
                }
            }

            return passed;
        };

    MultiProcessDebugConsoleOptions
        consoleOptions;

    consoleOptions.default_continue_steps =
        sessionOptions.default_continue_steps;

    if (hasCommandFile) {
        std::ifstream commandFile(
            commandFilePath
        );

        if (!commandFile) {
            throw std::runtime_error(
                "Cannot open multi-process debugger "
                "command file: "
                + commandFilePath
            );
        }

        consoleOptions.show_prompt = false;

        MultiProcessDebugConsole console(
            session,
            commandFile,
            std::cout,
            std::cerr,
            consoleOptions
        );

        const bool consoleSucceeded =
            console.run().success();

        return (
            consoleSucceeded
            && validateExpectations()
        )
            ? 0
            : 1;
    }

    MultiProcessDebugConsole console(
        session,
        std::cin,
        std::cout,
        std::cerr,
        consoleOptions
    );

    const bool consoleSucceeded =
        console.run().success();

    return (
        consoleSucceeded
        && validateExpectations()
    )
        ? 0
        : 1;
}


int debugShellCommand(
    const std::string& inputPath,
    int argc,
    char* argv[],
    int startIndex
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    std::size_t maxSteps = CPU::kDefaultMaxSteps;
    bool hasCommandFile = false;
    std::string commandFilePath;

    int index = startIndex;

    while (index < argc) {
        const std::string argument = argv[index];

        if (argument == "--commands") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--commands requires a file path"
                );
            }

            if (hasCommandFile) {
                throw std::invalid_argument(
                    "--commands may be specified only once"
                );
            }

            hasCommandFile = true;
            commandFilePath = argv[index + 1];
            index += 2;
            continue;
        }

        if (argument == "--max-steps") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--max-steps requires a value"
                );
            }

            maxSteps = parseRunProcessesPositiveSize(
                argv[index + 1],
                "--max-steps"
            );

            index += 2;
            continue;
        }

        throw std::invalid_argument(
            "Unknown debug-shell option: "
            + argument
        );
    }

    DebugSession session(inputPath);

    DebugConsoleOptions options;
    options.default_continue_steps = maxSteps;

    if (hasCommandFile) {
        std::ifstream commandFile(commandFilePath);

        if (!commandFile) {
            throw std::runtime_error(
                "Cannot open debugger command file: "
                + commandFilePath
            );
        }

        options.show_prompt = false;

        DebugConsole console(
            session,
            commandFile,
            std::cout,
            std::cerr,
            options
        );

        return console.run().success() ? 0 : 1;
    }

    DebugConsole console(
        session,
        std::cin,
        std::cout,
        std::cerr,
        options
    );

    return console.run().success() ? 0 : 1;
}


int debugBinaryCommand(
    const std::string& inputPath,
    int argc,
    char* argv[],
    int startIndex
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    std::size_t maxSteps =
        CPU::kDefaultMaxSteps;

    bool stepRequested = false;
    std::size_t stepCount = 0;

    bool showRegisters = false;

    std::vector<std::size_t>
        breakpointAddresses;

    std::vector<DebugMemoryRequest>
        memoryRequests;

    std::vector<DebugDisassemblyRequest>
        disassemblyRequests;

    int index = startIndex;

    while (index < argc) {
        const std::string argument =
            argv[index];

        if (argument == "--break") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--break requires an address"
                );
            }

            breakpointAddresses.push_back(
                parseDebuggerAddress(
                    argv[index + 1]
                )
            );

            index += 2;
            continue;
        }

        if (argument == "--max-steps") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--max-steps requires a value"
                );
            }

            maxSteps =
                parseRunProcessesPositiveSize(
                    argv[index + 1],
                    "--max-steps"
                );

            index += 2;
            continue;
        }

        if (argument == "--step") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--step requires a count"
                );
            }

            stepRequested = true;

            stepCount =
                parseRunProcessesPositiveSize(
                    argv[index + 1],
                    "--step"
                );

            index += 2;
            continue;
        }

        if (argument == "--registers") {
            showRegisters = true;
            ++index;
            continue;
        }

        if (argument == "--memory") {
            if (index + 2 >= argc) {
                throw std::invalid_argument(
                    "--memory requires an address "
                    "and byte count"
                );
            }

            DebugMemoryRequest request;

            request.address =
                parseDebuggerAddress(
                    argv[index + 1]
                );

            request.count =
                parseRunProcessesPositiveSize(
                    argv[index + 2],
                    "--memory"
                );

            memoryRequests.push_back(
                request
            );

            index += 3;
            continue;
        }

        if (argument == "--disassemble") {
            if (index + 2 >= argc) {
                throw std::invalid_argument(
                    "--disassemble requires an address "
                    "and instruction count"
                );
            }

            DebugDisassemblyRequest request;

            request.address =
                parseDebuggerAddress(
                    argv[index + 1]
                );

            request.instruction_count =
                parseRunProcessesPositiveSize(
                    argv[index + 2],
                    "--disassemble"
                );

            disassemblyRequests.push_back(
                request
            );

            index += 3;
            continue;
        }

        throw std::invalid_argument(
            "Unknown debug-binary option: "
            + argument
        );
    }

    DebugSession session(inputPath);

    for (
        const std::size_t address :
        breakpointAddresses
    ) {
        (void)session.addBreakpoint(
            address
        );
    }

    std::cout
        << "=== Zero-CPU Debug Session ==="
        << std::endl;

    std::cout
        << "Executable: "
        << session.sourceName()
        << std::endl;

    std::cout
        << "Code range: ["
        << session.metadata().code_base
        << ", "
        << session.metadata()
            .code_end_exclusive
        << ")"
        << std::endl;

    std::cout
        << "Entry point: "
        << session.metadata().entry_point
        << std::endl;

    if (breakpointAddresses.empty()) {
        std::cout
            << "Breakpoints: <none>"
            << std::endl;
    } else {
        std::cout << "Breakpoints:";

        for (
            const std::size_t address :
            session.breakpoints()
        ) {
            std::cout
                << " "
                << address;
        }

        std::cout << std::endl;
    }

    DebugStop stop =
        session.lastStop();

    if (stepRequested) {
        for (
            std::size_t stepIndex = 0;
            stepIndex < stepCount;
            ++stepIndex
        ) {
            stop = session.step();

            if (
                stop.reason
                != DebugStopReason::StepComplete
            ) {
                break;
            }
        }
    } else {
        stop = session.continueExecution(
            maxSteps
        );
    }

    std::cout
        << "Stop reason: "
        << debugStopReasonToString(
            stop.reason
        )
        << std::endl;

    std::cout
        << "PC: "
        << stop.pc
        << std::endl;

    std::cout
        << "Executed steps: "
        << stop.executed_steps
        << std::endl;

    std::cout
        << "Total steps: "
        << stop.total_steps
        << std::endl;

    if (!stop.message.empty()) {
        std::cout
            << "Message: "
            << stop.message
            << std::endl;
    }

    std::cout << std::endl;

    std::cout
        << session.cpu().state().summary();

    if (showRegisters) {
        std::cout << std::endl;

        std::cout
            << "=== Registers ==="
            << std::endl;

        std::cout
            << DebugInspector::formatRegisters(
                DebugInspector::inspectRegisters(
                    session
                )
            )
            << std::endl;
    }

    for (
        const DebugMemoryRequest& request :
        memoryRequests
    ) {
        std::cout << std::endl;

        std::cout
            << "=== Memory Inspection ==="
            << std::endl;

        std::cout
            << DebugInspector::formatMemory(
                DebugInspector::inspectMemory(
                    session,
                    request.address,
                    request.count
                )
            )
            << std::endl;
    }

    for (
        const DebugDisassemblyRequest& request :
        disassemblyRequests
    ) {
        std::cout << std::endl;

        std::cout
            << "=== Disassembly ==="
            << std::endl;

        std::cout
            << DebugInspector::formatDisassembly(
                DebugInspector::disassemble(
                    session,
                    request.address,
                    request.instruction_count
                )
            )
            << std::endl;
    }

    if (
        !session.cpu()
            .traceLogger().empty()
    ) {
        std::cout << std::endl;

        std::cout
            << "Last trace:"
            << std::endl;

        std::cout
            << session.cpu()
                .traceLogger()
                .last()
                .toCompactString()
            << std::endl;
    }

    switch (stop.reason) {
    case DebugStopReason::Breakpoint:
    case DebugStopReason::ProgramEnd:
    case DebugStopReason::Halted:
    case DebugStopReason::StepComplete:
        return 0;

    case DebugStopReason::Fault:
        return 2;

    case DebugStopReason::StepLimit:
        return 3;

    case DebugStopReason::Ready:
        return 1;
    }

    return 1;
}


int assembleToBinary(
    const std::string& inputPath,
    const std::string& outputPath
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleFile(inputPath);

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    binary::BinaryWriter writer;
    writer.writeFile(outputPath, program);

    binary::BinaryReader reader;

    const binary::BinaryProgram verified =
        reader.readFile(outputPath);

    if (
        verified.header.major_version
            != program.header.major_version
        || verified.header.minor_version
            != program.header.minor_version
        || verified.header.endianness
            != program.header.endianness
        || verified.header.entry_point
            != program.header.entry_point
        || verified.header.code_size
            != program.header.code_size
        || verified.header.data_base
            != program.header.data_base
        || verified.header.data_size
            != program.header.data_size
        || verified.code != program.code
        || verified.data != program.data
    ) {
        throw std::runtime_error(
            "Written executable failed "
            "read-back verification"
        );
    }

    const debug::DebugSymbols debugSymbols =
        debug::DebugSymbols::fromAssembledProgram(
            assembled,
            memory_map::kBinaryCodeBase
        );

    const std::string debugSymbolsPath =
        debug::debugSymbolsPathForExecutable(
            outputPath
        );

    debugSymbols.writeFile(
        debugSymbolsPath
    );

    const debug::DebugSymbols
        verifiedDebugSymbols =
            debug::DebugSymbols::readFile(
                debugSymbolsPath
            );

    if (
        !(verifiedDebugSymbols
            == debugSymbols)
    ) {
        throw std::runtime_error(
            "Written debug symbols failed "
            "read-back verification"
        );
    }

    std::cout
        << "Assemble completed successfully."
        << std::endl;

    std::cout
        << "Input: "
        << inputPath
        << std::endl;

    std::cout
        << "Output: "
        << outputPath
        << std::endl;

    std::cout
        << "Debug symbols: "
        << debugSymbolsPath
        << " ("
        << debugSymbols.size()
        << ")"
        << std::endl;

    std::cout
        << "Format: "
        << static_cast<int>(
            verified.header.major_version
        )
        << "."
        << static_cast<int>(
            verified.header.minor_version
        )
        << std::endl;

    std::cout
        << "Instruction count: "
        << assembled.instructions.size()
        << std::endl;

    std::cout
        << "Entry label: "
        << (
            assembled.has_explicit_entry
                ? assembled.entry_label
                : std::string(
                    "<default:first-instruction>"
                )
        )
        << std::endl;

    std::cout
        << "Entry instruction: "
        << assembled.resolvedEntryInstruction()
        << std::endl;

    std::cout
        << "Entry point: "
        << verified.header.entry_point
        << std::endl;

    std::cout
        << "Code size: "
        << verified.header.code_size
        << " bytes"
        << std::endl;

    std::cout
        << "Data base: "
        << verified.header.data_base
        << std::endl;

    std::cout
        << "Data size: "
        << verified.header.data_size
        << " bytes"
        << std::endl;

    return 0;
}


int runAssemblyProgram(const std::string& inputPath) {
    using namespace zero_cpu;

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(inputPath);

    CPU cpu;
    cpu.loadProgram(assembled.instructions, assembled.labels);

    cpu.state().setPc(
        assembled.resolvedEntryInstruction()
    );

    std::cout << "Input file: " << inputPath << "\n\n";

    printProgram(cpu.program(), cpu.labels());

    std::cout << "=== Initial CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    runStepByStep(cpu);

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    if (cpu.state().hasError()) {
        std::cout << "Execution failed: "
                  << cpu.state().errorMessage()
                  << "\n";

        return 1;
    }

    std::cout << "=== Compact Trace Log ===\n";
    std::cout << cpu.traceLogger().compactString() << "\n";

    printFinalCheck(cpu);

    std::cout << "\nExecution finished successfully.\n";

    return 0;
}


int runHardwareBusTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::hardware;

    std::cout << "=== Zero-CPU Hardware Bus Test ===\n\n";

    auto hardwareBus =
        std::make_shared<MockHardwareBus>("mock-esp32");

    hardwareBus->setRegisterValue(
        memory_map::kHardwareGpioInputOffset,
        42
    );
    hardwareBus->connect();

    auto hardwareDevice =
        std::make_shared<HardwareMMIODevice>(hardwareBus);

    auto mmioBus = std::make_shared<MMIOBus>();
    mmioBus->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        hardwareDevice
    );

    const std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(
                memory_map::kHardwareBase +
                memory_map::kHardwareGpioOutputOffset
            ),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R2),
            Operand::memoryAddress(
                memory_map::kHardwareBase +
                memory_map::kHardwareGpioInputOffset
            )
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(300),
            Operand::registerOperand(RegisterName::R2)
        ),
        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.setMMIOBus(mmioBus);
    cpu.loadProgram(program, {});
    cpu.run();

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect(
        "mock hardware bus connected",
        hardwareBus->connected()
    );
    expect(
        "CPU halted after hardware program",
        cpu.state().halted() &&
            !cpu.state().hasError()
    );
    expect(
        "GPIO output register received one",
        hardwareBus->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) == 1
    );
    expect(
        "GPIO input reached R2",
        cpu.state().registers().get(RegisterName::R2) == 42
    );
    expect(
        "GPIO input reached Memory[300]",
        cpu.state().memory().read(300) == 42
    );
    expect(
        "mock bus recorded one hardware write",
        hardwareBus->writeCount() == 1
    );
    expect(
        "mock bus recorded one hardware read",
        hardwareBus->readCount() == 1
    );
    expect(
        "hardware trace contains five CPU events",
        cpu.traceLogger().size() == 5
    );

    const auto& transactions = hardwareBus->transactions();

    expect(
        "first transaction is GPIO output write",
        transactions.size() >= 1 &&
            transactions[0].type ==
                HardwareAccessType::Write &&
            transactions[0].offset ==
                memory_map::kHardwareGpioOutputOffset &&
            transactions[0].value == 1
    );

    expect(
        "second transaction is GPIO input read",
        transactions.size() >= 2 &&
            transactions[1].type ==
                HardwareAccessType::Read &&
            transactions[1].offset ==
                memory_map::kHardwareGpioInputOffset &&
            transactions[1].value == 42
    );

    hardwareBus->disconnect();

    bool disconnectedAccessRejected = false;

    try {
        (void)hardwareDevice->read(
            memory_map::kHardwareGpioInputOffset
        );
    } catch (const std::exception&) {
        disconnectedAccessRejected = true;
    }

    expect(
        "disconnected hardware access is rejected",
        disconnectedAccessRejected
    );

    if (!passed) {
        std::cout << "\nHardware bus test failed.\n";
        return 1;
    }

    std::cout << "\nHardware bus test passed.\n";
    return 0;
}

int runSerialHardwareTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::hardware;

    std::cout << "=== Zero-CPU Serial Hardware Test ===\n\n";

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";
        if (!condition) {
            passed = false;
        }
    };

    const HardwareProtocolRequest parsedWrite =
        HardwareProtocol::parseRequest(
            HardwareProtocol::writeRequest(0, -17)
        );

    expect(
        "protocol parses WRITE request",
        parsedWrite.type == HardwareProtocolRequestType::Write &&
            parsedWrite.offset == 0 &&
            parsedWrite.value == -17
    );

    const HardwareProtocolResponse parsedValue =
        HardwareProtocol::parseResponse(
            HardwareProtocol::valueResponse(42)
        );

    expect(
        "protocol parses VALUE response",
        parsedValue.type == HardwareProtocolResponseType::Value &&
            parsedValue.value == 42
    );

    auto transport =
        std::make_shared<MockSerialTransport>("mock-esp32-serial");

    transport->setRegisterValue(
        memory_map::kHardwareGpioInputOffset,
        42
    );

    auto serialBus =
        std::make_shared<SerialHardwareBus>(transport, 250);

    serialBus->connect();

    expect("serial hardware bus connected", serialBus->connected());
    expect(
        "connection sent PING",
        !transport->requests().empty() &&
            transport->requests().front() ==
                HardwareProtocol::pingRequest()
    );

    auto hardwareDevice =
        std::make_shared<HardwareMMIODevice>(serialBus);
    auto mmioBus = std::make_shared<MMIOBus>();

    mmioBus->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        hardwareDevice
    );

    const std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(
                memory_map::kHardwareBase +
                memory_map::kHardwareGpioOutputOffset
            ),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R2),
            Operand::memoryAddress(
                memory_map::kHardwareBase +
                memory_map::kHardwareGpioInputOffset
            )
        ),
        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.setMMIOBus(mmioBus);
    cpu.loadProgram(program, {});
    cpu.run();

    expect(
        "CPU completed serial hardware program",
        cpu.state().halted() && !cpu.state().hasError()
    );
    expect(
        "serial GPIO output received one",
        transport->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) == 1
    );
    expect(
        "serial GPIO input reached R2",
        cpu.state().registers().get(RegisterName::R2) == 42
    );
    expect(
        "PING WRITE READ requests were recorded",
        transport->requests().size() == 3
    );

    bool deviceErrorRejected = false;
    transport->failNextWithError("device rejected request");
    try {
        (void)serialBus->readRegister(
            memory_map::kHardwareStatusOffset
        );
    } catch (const std::exception&) {
        deviceErrorRejected = true;
    }
    expect("device ERROR response is rejected", deviceErrorRejected);

    bool malformedResponseRejected = false;
    transport->setNextRawResponse("BROKEN RESPONSE\n");
    try {
        (void)serialBus->readRegister(
            memory_map::kHardwareStatusOffset
        );
    } catch (const std::exception&) {
        malformedResponseRejected = true;
    }
    expect("malformed serial response is rejected", malformedResponseRejected);

    bool timeoutRejected = false;
    transport->timeoutNextTransaction();
    try {
        (void)serialBus->readRegister(
            memory_map::kHardwareStatusOffset
        );
    } catch (const std::exception&) {
        timeoutRejected = true;
    }
    expect("serial timeout is rejected", timeoutRejected);

    serialBus->disconnect();
    expect("serial hardware bus disconnected", !serialBus->connected());

    bool disconnectedAccessRejected = false;
    try {
        serialBus->writeRegister(
            memory_map::kHardwareGpioOutputOffset,
            0
        );
    } catch (const std::exception&) {
        disconnectedAccessRejected = true;
    }
    expect("access after disconnect is rejected", disconnectedAccessRejected);

    if (!passed) {
        std::cout << "\nSerial hardware test failed.\n";
        return 1;
    }

    std::cout << "\nSerial hardware test passed.\n";
    return 0;
}

int runHardwareLiveTest(
    const std::string& portName,
    std::uint32_t baudRate
) {
    using namespace zero_cpu;
    using namespace zero_cpu::hardware;

    std::cout << "=== Zero-CPU ESP32 Live Hardware Test ===\n\n";
    std::cout << "Port: " << portName << "\n";
    std::cout << "Baud: " << baudRate << "\n\n";

    auto transport = std::make_shared<WindowsSerialTransport>(
        portName,
        baudRate
    );
    auto hardwareBus = std::make_shared<SerialHardwareBus>(
        transport,
        2000
    );

    try {
        hardwareBus->connect();
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] Could not connect to ESP32 bridge\n";
        std::cout << "Error: " << ex.what() << "\n\n";
        std::cout << "Close Arduino Serial Monitor and verify the COM port.\n";
        return 1;
    }

    std::cout << "[PASS] ZEROCPU/1 PING -> PONG\n";

    auto hardwareDevice =
        std::make_shared<HardwareMMIODevice>(hardwareBus);
    auto mmioBus = std::make_shared<MMIOBus>();

    mmioBus->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        hardwareDevice
    );

    const std::size_t gpioOutputAddress =
        memory_map::kHardwareBase +
        memory_map::kHardwareGpioOutputOffset;

    const std::size_t statusAddress =
        memory_map::kHardwareBase +
        memory_map::kHardwareStatusOffset;

    const std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(gpioOutputAddress),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R2),
            Operand::memoryAddress(gpioOutputAddress)
        ),
        Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R3),
            Operand::memoryAddress(statusAddress)
        ),
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(0)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(gpioOutputAddress),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.setMMIOBus(mmioBus);
    cpu.loadProgram(program, {});
    cpu.run();

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name << "\n";
        if (!condition) {
            passed = false;
        }
    };

    expect(
        "CPU completed live hardware program",
        cpu.state().halted() && !cpu.state().hasError()
    );
    expect(
        "ESP32 GPIO output readback was one",
        cpu.state().registers().get(RegisterName::R2) == 1
    );
    expect(
        "ESP32 device status was ready",
        cpu.state().registers().get(RegisterName::R3) == 1
    );

    try {
        expect(
            "ESP32 GPIO output was returned to zero",
            hardwareBus->readRegister(
                memory_map::kHardwareGpioOutputOffset
            ) == 0
        );
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] Final GPIO read failed: "
                  << ex.what() << "\n";
        passed = false;
    }

    hardwareBus->disconnect();

    if (!passed) {
        std::cout << "\nESP32 live hardware test failed.\n";
        return 1;
    }

    std::cout << "\nESP32 live hardware test passed.\n";
    return 0;
}

int runMMIOTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU MMIO Test ===\n\n";

    bool passed = true;

    MMIOBus bus;
    auto outputDevice = std::make_shared<DebugOutputDevice>();

    try {
        bus.mapDevice(
            memory_map::kDebugOutputBase,
            memory_map::kDebugOutputSize,
            outputDevice
        );
        std::cout << "[PASS] mapped DebugOutputDevice at 0xF000..0xF00F\n";
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] failed to map DebugOutputDevice: "
                  << ex.what()
                  << "\n";
        passed = false;
    }

    if (bus.hasDeviceAt(memory_map::kDebugOutputBase) &&
        bus.hasDeviceAt(memory_map::kDebugOutputBase + 8) &&
        !bus.hasDeviceAt(memory_map::kDebugOutputBase - 1)) {
        std::cout << "[PASS] MMIO address lookup\n";
    } else {
        std::cout << "[FAIL] MMIO address lookup\n";
        passed = false;
    }

    try {
        bus.write(memory_map::kDebugOutputBase, 65);
        bus.write(memory_map::kDebugOutputBase, 66);

        const bool valuesOk =
            outputDevice->writes().size() == 2 &&
            outputDevice->writes()[0] == 65 &&
            outputDevice->writes()[1] == 66;

        if (valuesOk) {
            std::cout << "[PASS] MMIO writes reached DebugOutputDevice\n";
        } else {
            std::cout << "[FAIL] MMIO writes reached wrong values\n";
            passed = false;
        }
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] MMIO write failed: "
                  << ex.what()
                  << "\n";
        passed = false;
    }

    try {
        const std::int64_t lastValue = bus.read(memory_map::kDebugOutputBase);
        const std::int64_t writeCount = bus.read(memory_map::kDebugOutputBase + 8);

        if (lastValue == 66 && writeCount == 2) {
            std::cout << "[PASS] MMIO reads returned last value and write count\n";
        } else {
            std::cout << "[FAIL] MMIO read mismatch: last="
                      << lastValue
                      << " count="
                      << writeCount
                      << "\n";
            passed = false;
        }
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] MMIO read failed: "
                  << ex.what()
                  << "\n";
        passed = false;
    }

    try {
        bus.read(0xE000);
        std::cout << "[FAIL] unmapped MMIO read should have thrown\n";
        passed = false;
    } catch (const std::exception&) {
        std::cout << "[PASS] unmapped MMIO read throws\n";
    }

    try {
        auto overlappingDevice = std::make_shared<DebugOutputDevice>();
        bus.mapDevice(0xF008, 16, overlappingDevice);
        std::cout << "[FAIL] overlapping MMIO mapping should have thrown\n";
        passed = false;
    } catch (const std::exception&) {
        std::cout << "[PASS] overlapping MMIO mapping throws\n";
    }

    std::cout << "\nDebugOutputDevice captured values:\n";
    std::cout << outputDevice->outputText();

    if (!passed) {
        std::cout << "\nMMIO test failed.\n";
        return 1;
    }

    std::cout << "\nMMIO test finished successfully.\n";
    return 0;
}


int runInterruptTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU Interrupt Controller Test ===\n\n";

    bool passed = true;

    auto expect = [&passed](const std::string& name, bool condition) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    auto expectThrow = [&passed](const std::string& name, auto operation) {
        try {
            operation();
        } catch (const std::exception& ex) {
            std::cout << "[PASS] "
                      << name
                      << " | threw: "
                      << ex.what()
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " | expected exception, but no exception was thrown\n";
        passed = false;
    };

    InterruptController controller;

    expect("global interrupts enabled by default", controller.globalEnabled());
    expect("no pending interrupt on reset", !controller.hasPending());
    expect("pending count is zero on reset", controller.pendingCount() == 0);

    controller.setVectorHandler(1, 0x300);
    expect("vector 1 handler installed", controller.hasVectorHandler(1));
    expect("vector 1 handler address is 0x300", controller.vectorHandler(1) == 0x300);

    controller.request(1, 123, "timer");
    expect("interrupt request queued", controller.pendingCount() == 1);
    expect("interrupt request is deliverable", controller.hasPending());

    InterruptRequest first = controller.acknowledge();
    expect("acknowledged vector is 1", first.vector == 1);
    expect("acknowledged payload is 123", first.payload == 123);
    expect("acknowledged source is timer", first.source == "timer");
    expect("pending queue empty after acknowledge", controller.pendingCount() == 0);

    controller.request(2, 222, "keyboard");
    expect("vector 2 without handler is queued", controller.pendingCount() == 1);
    expect("vector 2 without handler is not deliverable", !controller.hasPending());

    controller.setVectorHandler(2, 0x400);
    expect("vector 2 becomes deliverable after handler install", controller.hasPending());

    controller.mask(2);
    expect("masked vector 2 is not deliverable", !controller.hasPending());
    expect("vector 2 reports masked", controller.isMasked(2));

    controller.unmask(2);
    expect("unmasked vector 2 is deliverable again", controller.hasPending());

    controller.setGlobalEnabled(false);
    expect("global disabled blocks delivery", !controller.hasPending());
    expect("pending request remains queued while disabled", controller.pendingCount() == 1);

    controller.setGlobalEnabled(true);
    expect("global re-enabled restores delivery", controller.hasPending());

    InterruptRequest second = controller.acknowledge();
    expect("acknowledged vector is 2", second.vector == 2);
    expect("acknowledged payload is 222", second.payload == 222);
    expect("acknowledged source is keyboard", second.source == "keyboard");

    expectThrow(
        "acknowledge with no deliverable interrupt throws",
        [&controller]() {
            (void)controller.acknowledge();
        }
    );

    expectThrow(
        "reading missing vector handler throws",
        [&controller]() {
            (void)controller.vectorHandler(3);
        }
    );

    controller.setVectorHandler(3, 0x500);
    controller.request(3, 333, "network");
    expect("vector 3 request deliverable", controller.hasPending());

    controller.clear();
    expect("clear removes vector handlers", !controller.hasVectorHandler(3));
    expect("clear removes pending interrupts", controller.pendingCount() == 0);
    expect("clear unmasks vector 3", !controller.isMasked(3));
    expect("clear enables global interrupt flag", controller.globalEnabled());

    if (!passed) {
        std::cout << "\nInterrupt controller test failed.\n";
        return 1;
    }

    std::cout << "\nInterrupt controller test finished successfully.\n";
    return 0;
}


int runCPUInterruptTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU CPU Interrupt Delivery Test ===\n\n";

    const std::string sourcePath = "examples/interrupt_basic.zasm";
    const std::string binaryPath = "examples/interrupt_basic.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();

    cpu.setInterruptController(controller);
    cpu.loadBinaryProgram(program);

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + kInstructionSize;

    controller->setVectorHandler(7, handlerAddress);
    controller->request(7, 42, "cpu-interrupt-test");

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Vector: 7\n";
    std::cout << "Payload: 42\n";
    std::cout << "Handler PC: " << handlerAddress << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";

    if (cpu.state().hasError()) {
        std::cout << "CPU interrupt delivery failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    const std::int64_t vectorValue = cpu.state().memory().read(240);
    const std::int64_t payloadValue = cpu.state().memory().read(248);
    const std::int64_t handlerValue = cpu.state().memory().read(256);
    const std::int64_t mainValue = cpu.state().memory().read(264);

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    expect("Memory[240] interrupt vector", vectorValue, 7);
    expect("Memory[248] interrupt payload", payloadValue, 42);
    expect("Memory[256] handler marker", handlerValue, 777);
    expect("Memory[264] main marker", mainValue, 123);

    if (controller->pendingCount() != 0) {
        std::cout << "[FAIL] interrupt queue should be empty but count is "
                  << controller->pendingCount()
                  << "\n";
        passed = false;
    } else {
        std::cout << "[PASS] interrupt queue empty after delivery\n";
    }

    if (!passed) {
        std::cout << "\nCPU interrupt delivery test failed.\n";
        return 1;
    }

    std::cout << "\nCPU interrupt delivery test finished successfully.\n";
    return 0;
}


int runTimerDeviceTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU Timer Device Test ===\n\n";

    bool passed = true;

    auto expect = [&passed](const std::string& name, bool condition) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    auto expectThrow = [&passed](const std::string& name, auto operation) {
        try {
            operation();
        } catch (const std::exception& ex) {
            std::cout << "[PASS] "
                      << name
                      << " | threw: "
                      << ex.what()
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " | expected exception, but no exception was thrown\n";
        passed = false;
    };

    auto controller = std::make_shared<InterruptController>();
    controller->setVectorHandler(32, 0x700);

    TimerDevice timer(controller, 32, 3, 900);

    expect("timer starts enabled", timer.enabled());
    expect("initial tick count is zero", timer.tickCount() == 0);
    expect("initial interrupt count is zero", timer.interruptCount() == 0);
    expect("timer interval is 3", timer.interval() == 3);
    expect("timer vector is 32", timer.vector() == 32);
    expect("timer payload is 900", timer.payload() == 900);

    timer.tick();
    expect("tick 1 increments tick count", timer.tickCount() == 1);
    expect("tick 1 does not request interrupt", !controller->hasPending());

    timer.tick();
    expect("tick 2 increments tick count", timer.tickCount() == 2);
    expect("tick 2 does not request interrupt", !controller->hasPending());

    timer.tick();
    expect("tick 3 increments tick count", timer.tickCount() == 3);
    expect("tick 3 requests interrupt", controller->hasPending());
    expect("interrupt count is 1 after tick 3", timer.interruptCount() == 1);

    InterruptRequest first = controller->acknowledge();
    expect("timer interrupt vector is 32", first.vector == 32);
    expect("timer interrupt payload is 900", first.payload == 900);
    expect("timer interrupt source is timer", first.source == "timer");
    expect("queue empty after acknowledge", controller->pendingCount() == 0);

    timer.setEnabled(false);
    timer.tick(3);
    expect("disabled timer still counts ticks", timer.tickCount() == 6);
    expect("disabled timer does not request interrupt", controller->pendingCount() == 0);
    expect("disabled timer does not increment interrupt count", timer.interruptCount() == 1);

    timer.write(TimerDevice::kEnabledOffset, 1);
    timer.write(TimerDevice::kIntervalOffset, 2);
    timer.write(TimerDevice::kPayloadOffset, 1234);

    expect("MMIO read enabled register", timer.read(TimerDevice::kEnabledOffset) == 1);
    expect("MMIO read interval register", timer.read(TimerDevice::kIntervalOffset) == 2);
    expect("MMIO read payload register", timer.read(TimerDevice::kPayloadOffset) == 1234);

    timer.tick();
    expect("tick 7 does not trigger interval 2", controller->pendingCount() == 0);

    timer.tick();
    expect("tick 8 triggers interval 2", controller->hasPending());
    expect("interrupt count is 2 after tick 8", timer.interruptCount() == 2);

    InterruptRequest second = controller->acknowledge();
    expect("second interrupt vector is still 32", second.vector == 32);
    expect("second interrupt payload reflects MMIO write", second.payload == 1234);

    timer.write(TimerDevice::kVectorOffset, 33);
    controller->setVectorHandler(33, 0x800);
    timer.write(TimerDevice::kTickCountOffset, 9);
    timer.tick();

    expect("tick 10 triggers new vector 33", controller->hasPending());

    InterruptRequest third = controller->acknowledge();
    expect("third interrupt vector is 33", third.vector == 33);
    expect("third interrupt payload is 1234", third.payload == 1234);

    expect("MMIO read tick count register", timer.read(TimerDevice::kTickCountOffset) == 10);
    expect("MMIO read interrupt count register", timer.read(TimerDevice::kInterruptCountOffset) == 3);

    timer.write(TimerDevice::kInterruptCountOffset, 0);
    expect("MMIO reset interrupt count", timer.interruptCount() == 0);

    expectThrow(
        "TimerDevice rejects zero interval constructor",
        [&controller]() {
            TimerDevice invalid(controller, 32, 0, 0);
        }
    );

    expectThrow(
        "TimerDevice rejects zero interval write",
        [&timer]() {
            timer.write(TimerDevice::kIntervalOffset, 0);
        }
    );

    expectThrow(
        "TimerDevice rejects invalid vector write",
        [&timer]() {
            timer.write(TimerDevice::kVectorOffset, 999);
        }
    );

    expectThrow(
        "TimerDevice rejects unsupported read offset",
        [&timer]() {
            (void)timer.read(999);
        }
    );

    expectThrow(
        "TimerDevice rejects unsupported write offset",
        [&timer]() {
            timer.write(999, 1);
        }
    );

    if (!passed) {
        std::cout << "\nTimer device test failed.\n";
        return 1;
    }

    std::cout << "\nTimer device test finished successfully.\n";
    return 0;
}


int runCPUTimerInterruptTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU CPU Timer Interrupt Test ===\n\n";

    const std::string sourcePath = "examples/timer_interrupt.zasm";
    const std::string binaryPath = "examples/timer_interrupt.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        32,
        3,
        1234
    );

    constexpr std::size_t kTimerBase = memory_map::kTimerBase;
    constexpr std::size_t kTimerSize = memory_map::kTimerSize;

    bus->mapDevice(kTimerBase, kTimerSize, timer);

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.addClockedDevice(timer);
    cpu.loadBinaryProgram(program);

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + kInstructionSize;

    controller->setVectorHandler(32, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Timer interval: 3 instructions\n";
    std::cout << "Timer vector: 32\n";
    std::cout << "Timer payload: 1234\n";
    std::cout << "Handler PC: " << handlerAddress << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "CPU timer interrupt test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[280] timer vector", cpu.state().memory().read(280), 32);
    expect("Memory[288] timer payload", cpu.state().memory().read(288), 1234);
    expect("Memory[296] handler marker", cpu.state().memory().read(296), 999);
    expect("Memory[304] main loop result", cpu.state().memory().read(304), 10);

    expectCondition("CPU has one clocked device", cpu.clockedDeviceCount() == 1);
    expectCondition("timer requested at least one interrupt", timer->interruptCount() >= 1);
    expectCondition("timer was disabled by handler through MMIO", !timer->enabled());
    expectCondition("interrupt queue is empty after run", controller->pendingCount() == 0);

    if (!passed) {
        std::cout << "\nCPU timer interrupt test failed.\n";
        return 1;
    }

    std::cout << "\nCPU timer interrupt test finished successfully.\n";
    return 0;
}


int runCPUEiDiTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU EI/DI Interrupt Control Test ===\n\n";

    const std::string sourcePath = "examples/interrupt_ei_di.zasm";
    const std::string binaryPath = "examples/interrupt_ei_di.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        40,
        4,
        555
    );

    constexpr std::size_t kTimerBase = memory_map::kTimerBase;
    constexpr std::size_t kTimerSize = memory_map::kTimerSize;

    bus->mapDevice(kTimerBase, kTimerSize, timer);

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.addClockedDevice(timer);
    cpu.loadBinaryProgram(program);

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + kInstructionSize;

    controller->setVectorHandler(40, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Timer interval: 4 instructions\n";
    std::cout << "Timer vector: 40\n";
    std::cout << "Timer payload: 555\n";
    std::cout << "Handler PC: " << handlerAddress << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Global interrupts enabled = "
              << (controller->globalEnabled() ? "true" : "false")
              << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "CPU EI/DI interrupt test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[320] handler vector", cpu.state().memory().read(320), 40);
    expect("Memory[328] handler payload", cpu.state().memory().read(328), 555);
    expect("Memory[336] handler marker", cpu.state().memory().read(336), 888);
    expect("Memory[344] protected section marker", cpu.state().memory().read(344), 111);
    expect("Memory[352] R0 before EI", cpu.state().memory().read(352), 0);
    expect("Memory[360] R1 before EI", cpu.state().memory().read(360), 0);
    expect("Memory[368] R0 after EI", cpu.state().memory().read(368), 40);
    expect("Memory[376] R1 after EI", cpu.state().memory().read(376), 555);
    expect("Memory[384] main marker after interrupt", cpu.state().memory().read(384), 222);

    expectCondition("CPU has one clocked device", cpu.clockedDeviceCount() == 1);
    expectCondition("timer requested at least one interrupt", timer->interruptCount() >= 1);
    expectCondition("timer was disabled by handler through MMIO", !timer->enabled());
    expectCondition("global interrupts are enabled after EI", controller->globalEnabled());
    expectCondition("interrupt queue is empty after run", controller->pendingCount() == 0);

    if (!passed) {
        std::cout << "\nCPU EI/DI interrupt test failed.\n";
        return 1;
    }

    std::cout << "\nCPU EI/DI interrupt test finished successfully.\n";
    return 0;
}


int runSoftwareInterruptTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Software Interrupt Test ===\n\n";

    const std::string sourcePath = "examples/software_interrupt.zasm";
    const std::string binaryPath = "examples/software_interrupt.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto debugOutputDevice = std::make_shared<DebugOutputDevice>();

    constexpr std::size_t kDebugOutputBase = memory_map::kDebugOutputBase;
    constexpr std::size_t kDebugOutputSize = memory_map::kDebugOutputSize;
    constexpr std::uint8_t kSyscallVector = 80;

    bus->mapDevice(kDebugOutputBase, kDebugOutputSize, debugOutputDevice);

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Software interrupt vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Debug MMIO: 0xF000..0xF00F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    printDebugOutputAscii(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Software interrupt test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[400] syscall vector", cpu.state().memory().read(400), 80);
    expect("Memory[408] syscall argument R1", cpu.state().memory().read(408), 65);
    expect("Memory[416] main resumed marker", cpu.state().memory().read(416), 123);
    expect("R0 after INT", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 preserved as syscall argument", cpu.state().registers().get(RegisterName::R1), 65);
    expect("R2 after IRET", cpu.state().registers().get(RegisterName::R2), 123);

    expectCondition("DebugOutputDevice captured exactly one write", debugOutputDevice->writes().size() == 1);

    if (!debugOutputDevice->writes().empty()) {
        expect("DebugOutputDevice write[0]", debugOutputDevice->writes()[0], 65);
    } else {
        std::cout << "[FAIL] DebugOutputDevice write[0] missing\n";
        passed = false;
    }

    expectCondition("interrupt queue is empty after software interrupt", controller->pendingCount() == 0);
    expectCondition("CPU halted after software interrupt program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nSoftware interrupt test failed.\n";
        return 1;
    }

    std::cout << "\nSoftware interrupt test finished successfully.\n";
    return 0;
}


int runMiniKernelSyscallTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto debugOutputDevice = std::make_shared<DebugOutputDevice>();

    constexpr std::size_t kDebugOutputBase = memory_map::kDebugOutputBase;
    constexpr std::size_t kDebugOutputSize = memory_map::kDebugOutputSize;
    constexpr std::uint8_t kSyscallVector = 80;

    bus->mapDevice(kDebugOutputBase, kDebugOutputSize, debugOutputDevice);

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = syscall argument 0\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Debug MMIO: 0xF000..0xF00F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    printDebugOutputAscii(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[440] syscall vector", cpu.state().memory().read(440), 80);
    expect("Memory[448] syscall number", cpu.state().memory().read(448), 1);
    expect("Memory[456] last syscall argument", cpu.state().memory().read(456), 73);
    expect("Memory[464] syscall dispatch marker", cpu.state().memory().read(464), 1);
    expect("Memory[472] main resumed marker", cpu.state().memory().read(472), 321);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 1);
    expect("R2 last syscall argument preserved", cpu.state().registers().get(RegisterName::R2), 73);
    expect("R3 after mini kernel returns", cpu.state().registers().get(RegisterName::R3), 321);

    expectCondition("DebugOutputDevice captured two writes", debugOutputDevice->writes().size() == 2);

    if (debugOutputDevice->writes().size() >= 2) {
        expect("DebugOutputDevice write[0]", debugOutputDevice->writes()[0], 72);
        expect("DebugOutputDevice write[1]", debugOutputDevice->writes()[1], 73);
    } else {
        std::cout << "[FAIL] DebugOutputDevice writes missing\n";
        passed = false;
    }

    expectCondition("interrupt queue is empty after mini kernel syscalls", controller->pendingCount() == 0);
    expectCondition("CPU halted after mini kernel syscall program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall test finished successfully.\n";
    return 0;
}


int runMiniKernelSyscall2Test() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 2 Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall2.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall2.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto debugOutputDevice = std::make_shared<DebugOutputDevice>();

    constexpr std::size_t kDebugOutputBase = memory_map::kDebugOutputBase;
    constexpr std::size_t kDebugOutputSize = memory_map::kDebugOutputSize;
    constexpr std::uint8_t kSyscallVector = 80;

    bus->mapDevice(kDebugOutputBase, kDebugOutputSize, debugOutputDevice);

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = syscall argument 0\n";
    std::cout << "  R3 = syscall argument 1\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Debug MMIO: 0xF000..0xF00F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    printDebugOutputAscii(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 2 test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[500] syscall 2 write result", cpu.state().memory().read(500), 999);
    expect("Memory[300] syscall vector", cpu.state().memory().read(300), 80);
    expect("Memory[308] last syscall number", cpu.state().memory().read(308), 2);
    expect("Memory[316] syscall 2 target address argument", cpu.state().memory().read(316), 500);
    expect("Memory[324] syscall 2 value argument", cpu.state().memory().read(324), 999);
    expect("Memory[332] syscall 1 dispatch marker", cpu.state().memory().read(332), 1);
    expect("Memory[340] syscall 2 dispatch marker", cpu.state().memory().read(340), 2);
    expect("Memory[348] main resumed marker", cpu.state().memory().read(348), 333);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 2);
    expect("R2 target address preserved", cpu.state().registers().get(RegisterName::R2), 500);
    expect("R3 value preserved", cpu.state().registers().get(RegisterName::R3), 999);
    expect("R4 after mini kernel returns", cpu.state().registers().get(RegisterName::R4), 333);

    expectCondition("DebugOutputDevice captured one write", debugOutputDevice->writes().size() == 1);

    if (!debugOutputDevice->writes().empty()) {
        expect("DebugOutputDevice write[0]", debugOutputDevice->writes()[0], 74);
    } else {
        std::cout << "[FAIL] DebugOutputDevice write[0] missing\n";
        passed = false;
    }

    expectCondition("interrupt queue is empty after mini kernel syscall 2", controller->pendingCount() == 0);
    expectCondition("CPU halted after mini kernel syscall 2 program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 2 test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 2 test finished successfully.\n";
    return 0;
}



int runMiniKernelSyscall3Test() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 3 Exit Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall3.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall3.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto debugOutputDevice = std::make_shared<DebugOutputDevice>();

    constexpr std::uint8_t kSyscallVector = 80;

    bus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        debugOutputDevice
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = syscall argument 0 / exit code\n";
    std::cout << "  R3 = syscall argument 1\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Debug MMIO: 0xF000..0xF00F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    printDebugOutputAscii(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 3 exit test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[360] syscall vector", cpu.state().memory().read(360), 80);
    expect("Memory[368] last syscall number", cpu.state().memory().read(368), 3);
    expect("Memory[376] syscall 3 exit code argument", cpu.state().memory().read(376), 7);
    expect("Memory[384] syscall 3 preserved R3", cpu.state().memory().read(384), 777);
    expect("Memory[392] syscall 1 dispatch marker", cpu.state().memory().read(392), 1);
    expect("Memory[400] syscall 2 dispatch marker", cpu.state().memory().read(400), 2);
    expect("Memory[408] syscall 3 dispatch marker", cpu.state().memory().read(408), 3);
    expect("Memory[416] syscall 3 exit code marker", cpu.state().memory().read(416), 7);
    expect("Memory[424] after-exit marker not written", cpu.state().memory().read(424), 0);
    expect("Memory[500] syscall 2 write result", cpu.state().memory().read(500), 777);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 3);
    expect("R2 exit code preserved", cpu.state().registers().get(RegisterName::R2), 7);
    expect("R3 value preserved", cpu.state().registers().get(RegisterName::R3), 777);
    expect("R4 not reached after exit", cpu.state().registers().get(RegisterName::R4), 0);
    expect("R7 exit code", cpu.state().registers().get(RegisterName::R7), 7);

    expectCondition("DebugOutputDevice captured one write", debugOutputDevice->writes().size() == 1);

    if (!debugOutputDevice->writes().empty()) {
        expect("DebugOutputDevice write[0]", debugOutputDevice->writes()[0], 75);
    } else {
        std::cout << "[FAIL] DebugOutputDevice write[0] missing\n";
        passed = false;
    }

    expectCondition("interrupt queue is empty after syscall 3 exit", controller->pendingCount() == 0);
    expectCondition("CPU halted by syscall 3 exit", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 3 exit test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 3 exit test finished successfully.\n";
    return 0;
}



int runMiniKernelSyscall4TimerReadTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 4 Timer Read Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall4_timer_read.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall4_timer_read.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        32,
        1000,
        0
    );

    constexpr std::uint8_t kSyscallVector = 80;
    constexpr std::int64_t kExpectedTickCount = 12345;

    timer->setEnabled(false);
    timer->write(TimerDevice::kTickCountOffset, kExpectedTickCount);

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = return value for syscall 4\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Initial timer tick count: "
              << kExpectedTickCount
              << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 4 timer read test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[432] syscall vector", cpu.state().memory().read(432), 80);
    expect("Memory[440] syscall number", cpu.state().memory().read(440), 4);
    expect("Memory[448] timer tick read by handler", cpu.state().memory().read(448), kExpectedTickCount);
    expect("Memory[456] syscall 4 dispatch marker", cpu.state().memory().read(456), 4);
    expect("Memory[464] returned timer tick in user program", cpu.state().memory().read(464), kExpectedTickCount);
    expect("Memory[472] main resumed marker", cpu.state().memory().read(472), 444);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 4);
    expect("R2 timer tick return value", cpu.state().registers().get(RegisterName::R2), kExpectedTickCount);
    expect("R3 main resumed value", cpu.state().registers().get(RegisterName::R3), 444);

    expect("TimerDevice tick count unchanged", static_cast<std::int64_t>(timer->tickCount()), kExpectedTickCount);
    expect("TimerDevice interrupt count", static_cast<std::int64_t>(timer->interruptCount()), 0);

    expectCondition("timer remains disabled in syscall 4 test", !timer->enabled());
    expectCondition("interrupt queue is empty after syscall 4", controller->pendingCount() == 0);
    expectCondition("CPU halted after syscall 4 timer read program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 4 timer read test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 4 timer read test finished successfully.\n";
    return 0;
}



int runMiniKernelSyscall5TimerEnableTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 5 Timer Enable Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall5_timer_enable.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall5_timer_enable.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        1,
        1000,
        0
    );

    constexpr std::uint8_t kSyscallVector = 80;
    constexpr std::int64_t kExpectedInterval = 9;
    constexpr std::int64_t kExpectedTimerVector = 32;
    constexpr std::int64_t kExpectedPayload = 777;

    timer->setEnabled(false);

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = timer interval\n";
    std::cout << "  R3 = timer interrupt vector\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Expected timer interval: " << kExpectedInterval << "\n";
    std::cout << "Expected timer vector: " << kExpectedTimerVector << "\n";
    std::cout << "Expected timer payload: " << kExpectedPayload << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 5 timer enable test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[272] syscall vector", cpu.state().memory().read(272), 80);
    expect("Memory[280] syscall number", cpu.state().memory().read(280), 5);
    expect("Memory[288] timer interval argument", cpu.state().memory().read(288), kExpectedInterval);
    expect("Memory[296] timer vector argument", cpu.state().memory().read(296), kExpectedTimerVector);
    expect("Memory[304] syscall 5 dispatch marker", cpu.state().memory().read(304), 5);
    expect("Memory[312] timer interval readback", cpu.state().memory().read(312), kExpectedInterval);
    expect("Memory[320] timer vector readback", cpu.state().memory().read(320), kExpectedTimerVector);
    expect("Memory[328] timer enabled readback", cpu.state().memory().read(328), 1);
    expect("Memory[336] main resumed marker", cpu.state().memory().read(336), 555);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 5);
    expect("R2 timer interval preserved", cpu.state().registers().get(RegisterName::R2), kExpectedInterval);
    expect("R3 timer vector preserved", cpu.state().registers().get(RegisterName::R3), kExpectedTimerVector);
    expect("R7 main resumed value", cpu.state().registers().get(RegisterName::R7), 555);

    expect("TimerDevice interval", static_cast<std::int64_t>(timer->interval()), kExpectedInterval);
    expect("TimerDevice vector", static_cast<std::int64_t>(timer->vector()), kExpectedTimerVector);
    expect("TimerDevice payload", timer->payload(), kExpectedPayload);
    expect("TimerDevice interrupt count", static_cast<std::int64_t>(timer->interruptCount()), 0);

    expectCondition("TimerDevice enabled by syscall 5", timer->enabled());
    expectCondition("interrupt queue is empty after syscall 5", controller->pendingCount() == 0);
    expectCondition("CPU halted after syscall 5 timer enable program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 5 timer enable test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 5 timer enable test finished successfully.\n";
    return 0;
}



int runMiniKernelSyscall6TimerDisableTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 6 Timer Disable Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall6_timer_disable.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall6_timer_disable.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        32,
        12,
        777
    );

    constexpr std::uint8_t kSyscallVector = 80;

    timer->setEnabled(true);

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  syscall 6 disables TimerDevice\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Initial timer enabled: true\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 6 timer disable test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[344] syscall vector", cpu.state().memory().read(344), 80);
    expect("Memory[352] syscall number", cpu.state().memory().read(352), 6);
    expect("Memory[360] syscall 6 dispatch marker", cpu.state().memory().read(360), 6);
    expect("Memory[368] timer enabled readback", cpu.state().memory().read(368), 0);
    expect("Memory[376] timer interval still preserved", cpu.state().memory().read(376), 12);
    expect("Memory[384] main resumed marker", cpu.state().memory().read(384), 666);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 6);
    expect("R7 main resumed value", cpu.state().registers().get(RegisterName::R7), 666);

    expect("TimerDevice interval preserved", static_cast<std::int64_t>(timer->interval()), 12);
    expect("TimerDevice vector preserved", static_cast<std::int64_t>(timer->vector()), 32);
    expect("TimerDevice payload preserved", timer->payload(), 777);
    expect("TimerDevice interrupt count", static_cast<std::int64_t>(timer->interruptCount()), 0);

    expectCondition("TimerDevice disabled by syscall 6", !timer->enabled());
    expectCondition("interrupt queue is empty after syscall 6", controller->pendingCount() == 0);
    expectCondition("CPU halted after syscall 6 timer disable program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 6 timer disable test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 6 timer disable test finished successfully.\n";
    return 0;
}



int runMiniKernelSyscall7TimerConfigureTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Syscall 7 Timer Configure Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_syscall7_timer_configure.zasm";
    const std::string binaryPath = "examples/mini_kernel_syscall7_timer_configure.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        32,
        10,
        111
    );

    constexpr std::uint8_t kSyscallVector = 80;
    constexpr std::int64_t kNewInterval = 21;
    constexpr std::int64_t kNewVector = 44;
    constexpr std::int64_t kNewPayload = 888;

    timer->setEnabled(false);

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("syscall_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Syscall convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = timer interval\n";
    std::cout << "  R3 = timer interrupt vector\n";
    std::cout << "  R4 = timer interrupt payload\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n";
    std::cout << "Expected interval: " << kNewInterval << "\n";
    std::cout << "Expected vector: " << kNewVector << "\n";
    std::cout << "Expected payload: " << kNewPayload << "\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel syscall 7 timer configure test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[392] syscall vector", cpu.state().memory().read(392), 80);
    expect("Memory[400] syscall number", cpu.state().memory().read(400), 7);
    expect("Memory[408] interval argument", cpu.state().memory().read(408), kNewInterval);
    expect("Memory[416] vector argument", cpu.state().memory().read(416), kNewVector);
    expect("Memory[424] payload argument", cpu.state().memory().read(424), kNewPayload);
    expect("Memory[432] syscall 7 dispatch marker", cpu.state().memory().read(432), 7);
    expect("Memory[440] timer interval readback", cpu.state().memory().read(440), kNewInterval);
    expect("Memory[448] timer vector readback", cpu.state().memory().read(448), kNewVector);
    expect("Memory[456] timer payload readback", cpu.state().memory().read(456), kNewPayload);
    expect("Memory[464] timer enabled readback", cpu.state().memory().read(464), 0);
    expect("Memory[472] main resumed marker", cpu.state().memory().read(472), 777);

    expect("R0 after syscall", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R1 syscall number preserved", cpu.state().registers().get(RegisterName::R1), 7);
    expect("R2 timer interval preserved", cpu.state().registers().get(RegisterName::R2), kNewInterval);
    expect("R3 timer vector preserved", cpu.state().registers().get(RegisterName::R3), kNewVector);
    expect("R4 timer payload preserved", cpu.state().registers().get(RegisterName::R4), kNewPayload);
    expect("R7 main resumed value", cpu.state().registers().get(RegisterName::R7), 777);

    expect("TimerDevice interval", static_cast<std::int64_t>(timer->interval()), kNewInterval);
    expect("TimerDevice vector", static_cast<std::int64_t>(timer->vector()), kNewVector);
    expect("TimerDevice payload", timer->payload(), kNewPayload);
    expect("TimerDevice interrupt count", static_cast<std::int64_t>(timer->interruptCount()), 0);

    expectCondition("TimerDevice remains disabled after configure", !timer->enabled());
    expectCondition("interrupt queue is empty after syscall 7", controller->pendingCount() == 0);
    expectCondition("CPU halted after syscall 7 timer configure program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel syscall 7 timer configure test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel syscall 7 timer configure test finished successfully.\n";
    return 0;
}



int runMiniKernelTimerLifecycleTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Mini Kernel Timer Lifecycle Test ===\n\n";

    const std::string sourcePath = "examples/mini_kernel_timer_lifecycle.zasm";
    const std::string binaryPath = "examples/mini_kernel_timer_lifecycle.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        32,
        100,
        111
    );

    constexpr std::uint8_t kSyscallVector = 80;
    constexpr std::uint8_t kTimerVector = 44;
    constexpr std::int64_t kTimerInterval = 8;
    constexpr std::int64_t kTimerPayload = 888;

    timer->setEnabled(false);

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.addClockedDevice(timer);
    cpu.loadBinaryProgram(program);

    // BIO-OS combined programs can grow beyond the default stack base.
    // Use the documented integration-demo stack base from MemoryMap.hpp.
    cpu.state().setSp(memory_map::kBioOSStackBase);

    const auto syscallHandlerIt = assembled.labels.find("syscall_handler");
    if (syscallHandlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const auto timerHandlerIt = assembled.labels.find("timer_handler");
    if (timerHandlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] timer_handler label not found\n";
        return 1;
    }

    const std::size_t syscallHandlerAddress =
        cpu.binaryCodeBase() + syscallHandlerIt->second * kInstructionSize;

    const std::size_t timerHandlerAddress =
        cpu.binaryCodeBase() + timerHandlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, syscallHandlerAddress);
    controller->setVectorHandler(kTimerVector, timerHandlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: "
              << static_cast<int>(kSyscallVector)
              << "\n";
    std::cout << "Timer vector: "
              << static_cast<int>(kTimerVector)
              << "\n";
    std::cout << "Timer interval: " << kTimerInterval << "\n";
    std::cout << "Timer payload: " << kTimerPayload << "\n";
    std::cout << "Syscall handler PC: " << syscallHandlerAddress << "\n";
    std::cout << "Timer handler PC: " << timerHandlerAddress << "\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Mini kernel timer lifecycle test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[240] syscall 7 vector", cpu.state().memory().read(240), 80);
    expect("Memory[248] syscall 7 number", cpu.state().memory().read(248), 7);
    expect("Memory[256] configured interval", cpu.state().memory().read(256), kTimerInterval);
    expect("Memory[264] configured vector", cpu.state().memory().read(264), kTimerVector);

    expect("Memory[272] syscall 5 vector", cpu.state().memory().read(272), 80);
    expect("Memory[280] syscall 5 number", cpu.state().memory().read(280), 5);
    expect("Memory[288] interval before enabling", cpu.state().memory().read(288), kTimerInterval);

    expect("Memory[400] timer interrupt vector", cpu.state().memory().read(400), kTimerVector);
    expect("Memory[408] timer interrupt payload", cpu.state().memory().read(408), kTimerPayload);
    expect("Memory[416] timer handler marker", cpu.state().memory().read(416), 777);
    expect("Memory[424] timer disabled readback", cpu.state().memory().read(424), 0);

    expect("Memory[432] main loop counter", cpu.state().memory().read(432), 10);
    expect("Memory[440] main resumed marker", cpu.state().memory().read(440), 222);

    expect("R0 after timer interrupt", cpu.state().registers().get(RegisterName::R0), kTimerVector);
    expect("R1 after timer interrupt payload", cpu.state().registers().get(RegisterName::R1), kTimerPayload);
    expect("R5 loop counter", cpu.state().registers().get(RegisterName::R5), 10);
    expect("R7 main resumed value", cpu.state().registers().get(RegisterName::R7), 222);

    expect("TimerDevice interval", static_cast<std::int64_t>(timer->interval()), kTimerInterval);
    expect("TimerDevice vector", static_cast<std::int64_t>(timer->vector()), kTimerVector);
    expect("TimerDevice payload", timer->payload(), kTimerPayload);

    expectCondition("CPU has one clocked device", cpu.clockedDeviceCount() == 1);
    expectCondition("TimerDevice requested at least one interrupt", timer->interruptCount() >= 1);
    expectCondition("TimerDevice disabled by timer handler", !timer->enabled());
    expectCondition("interrupt queue is empty after timer lifecycle", controller->pendingCount() == 0);
    expectCondition("CPU halted after timer lifecycle program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nMini kernel timer lifecycle test failed.\n";
        return 1;
    }

    std::cout << "\nMini kernel timer lifecycle test finished successfully.\n";
    return 0;
}



std::string joinPath(const std::string& directory, const std::string& fileName) {
    if (directory.empty()) {
        return fileName;
    }

    const char last = directory.back();

    if (last == '/' || last == '\\') {
        return directory + fileName;
    }

    return directory + "/" + fileName;
}

int runBioOSDirectory(const std::string& directory) {
    using namespace zero_cpu::system;

    std::cout << "=== Zero-CPU BIO-OS Run ===\n\n";

    BioOSRunOptions options;
    options.directory = directory;

    BioOSRunner runner;
    const BioOSRunResult result = runner.run(options);

    std::cout << "Directory: "
              << result.directory
              << "\n";
    std::cout << "Generated Source: "
              << result.combined_source_path
              << "\n";
    std::cout << "Generated Binary: "
              << result.combined_binary_path
              << "\n";
    std::cout << "Instruction Count: "
              << result.instruction_count
              << "\n";
    std::cout << "Code Size: "
              << result.code_size
              << " bytes\n";
    std::cout << "Syscall Handler PC: "
              << result.syscall_handler_pc
              << "\n";
    std::cout << "Timer Handler PC: "
              << result.timer_handler_pc
              << "\n";
    std::cout << "BIO-OS Stack Base: "
              << result.stack_base
              << "\n";
    std::cout << "Step Count: "
              << result.step_count
              << "\n";
    std::cout << "Final PC: "
              << result.final_pc
              << "\n";
    std::cout << "Final SP: "
              << result.final_sp
              << "\n";
    std::cout << "Exit Code: "
              << result.exit_code
              << "\n\n";

    std::cout << "Debug Writes: "
              << result.debug_writes.size()
              << "\n";

    if (result.debug_ascii.empty()) {
        std::cout << "ASCII view: <empty>\n";
    } else {
        std::cout << "ASCII view:\n";
        std::cout << result.debug_ascii << "\n";
    }

    std::cout << "Timer tick count = "
              << result.timer_tick_count
              << "\n";
    std::cout << "Timer interval = "
              << result.timer_interval
              << "\n";
    std::cout << "Timer vector = "
              << result.timer_vector
              << "\n";
    std::cout << "Timer payload = "
              << result.timer_payload
              << "\n";
    std::cout << "Timer interrupt count = "
              << result.timer_interrupt_count
              << "\n";
    std::cout << "Timer enabled = "
              << (result.timer_enabled ? "true" : "false")
              << "\n\n";

    if (!result.success()) {
        std::cout << "BIO-OS run failed: "
                  << result.error_message
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("BIO-OS halted CPU", result.halted);
    expect("BIO-OS ASCII output is BU", result.debug_ascii == "BU");
    expect("BIO-OS debug output captured at least two writes", result.debug_writes.size() >= 2);
    expect("BIO-OS timer interrupt count is at least one", result.timer_interrupt_count >= 1);
    expect("BIO-OS timer disabled itself", !result.timer_enabled);
    expect("BIO-OS exit code is zero", result.exit_code == 0);

    if (!passed) {
        std::cout << "\nBIO-OS run failed checks.\n";
        return 1;
    }

    std::cout << "\nBIO-OS run finished successfully.\n";
    return 0;
}



int runBioOSCombinedBootTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU BIO-OS Combined Boot Test ===\n\n";

    const std::vector<std::string> sourceParts = {
        "examples/bio_os/boot.zasm",
        "examples/bio_os/kernel.zasm",
        "examples/bio_os/user_program.zasm"
    };

    const std::string combinedSourcePath =
        "examples/bio_os/combined_boot.zasm";
    const std::string binaryPath =
        "examples/bio_os/combined_boot.zbin";

    {
        std::ofstream combined(combinedSourcePath);

        if (!combined) {
            std::cout << "[FAIL] cannot create " << combinedSourcePath << "\n";
            return 1;
        }

        for (const auto& part : sourceParts) {
            std::ifstream input(part);

            if (!input) {
                std::cout << "[FAIL] cannot open " << part << "\n";
                return 1;
            }

            combined << "\n; === " << part << " ===\n";
            combined << input.rdbuf();
            combined << "\n";
        }
    }

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(combinedSourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();
    auto bus = std::make_shared<MMIOBus>();
    auto debugOutputDevice = std::make_shared<DebugOutputDevice>();
    auto timer = std::make_shared<TimerDevice>(
        controller,
        44,
        1000,
        0
    );

    constexpr std::uint8_t kSyscallVector = 80;
    constexpr std::uint8_t kTimerVector = 44;

    timer->setEnabled(false);

    bus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        debugOutputDevice
    );

    bus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timer
    );

    cpu.setInterruptController(controller);
    cpu.setMMIOBus(bus);
    cpu.addClockedDevice(timer);
    cpu.loadBinaryProgram(program);

    // BIO-OS combined programs can grow beyond the default stack base.
    // Use the documented integration-demo stack base from MemoryMap.hpp.
    cpu.state().setSp(memory_map::kBioOSStackBase);

    const auto syscallHandlerIt = assembled.labels.find("syscall_handler");
    if (syscallHandlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] syscall_handler label not found\n";
        return 1;
    }

    const auto timerHandlerIt = assembled.labels.find("timer_handler");
    if (timerHandlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] timer_handler label not found\n";
        return 1;
    }

    const std::size_t syscallHandlerAddress =
        cpu.binaryCodeBase() + syscallHandlerIt->second * kInstructionSize;

    const std::size_t timerHandlerAddress =
        cpu.binaryCodeBase() + timerHandlerIt->second * kInstructionSize;

    controller->setVectorHandler(kSyscallVector, syscallHandlerAddress);
    controller->setVectorHandler(kTimerVector, timerHandlerAddress);

    std::cout << "Source parts:\n";
    for (const auto& part : sourceParts) {
        std::cout << "  " << part << "\n";
    }

    std::cout << "Combined source: " << combinedSourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syscall vector: " << static_cast<int>(kSyscallVector) << "\n";
    std::cout << "Timer vector: " << static_cast<int>(kTimerVector) << "\n";
    std::cout << "Syscall handler PC: " << syscallHandlerAddress << "\n";
    std::cout << "Timer handler PC: " << timerHandlerAddress << "\n";
    std::cout << "Debug MMIO: 0xF000..0xF00F\n";
    std::cout << "Timer MMIO: 0xF100..0xF12F\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    printDebugOutputAscii(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "BIO-OS combined boot test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[100] last syscall vector", cpu.state().memory().read(100), 80);
    expect("Memory[108] last syscall number", cpu.state().memory().read(108), 3);
    const std::int64_t userObservedTick = cpu.state().memory().read(120);
    const std::int64_t finalTickCount = static_cast<std::int64_t>(timer->tickCount());

    expectCondition(
        "Memory[120] user timer read value is positive",
        userObservedTick > 0
    );
    expectCondition(
        "Memory[120] user timer read value is not after final tick",
        userObservedTick <= finalTickCount
    );
    expect("Memory[128] user memory write result", cpu.state().memory().read(128), 999);
    expect("Memory[136] user program marker", cpu.state().memory().read(136), 123);
    expect("Memory[160] user program started marker", cpu.state().memory().read(160), 1);
    expect("Memory[180] exit code marker", cpu.state().memory().read(180), 0);

    expect("Memory[200] timer interrupt vector", cpu.state().memory().read(200), 44);
    expect("Memory[208] timer interrupt payload", cpu.state().memory().read(208), 888);
    expect("Memory[216] timer handler marker", cpu.state().memory().read(216), 777);
    expect("Memory[224] timer disabled readback", cpu.state().memory().read(224), 0);
    expect("Memory[232] boot timer wait counter", cpu.state().memory().read(232), 20);

    expect("R7 exit code", cpu.state().registers().get(RegisterName::R7), 0);
    expect("TimerDevice interval", static_cast<std::int64_t>(timer->interval()), 8);
    expect("TimerDevice vector", static_cast<std::int64_t>(timer->vector()), 44);
    expect("TimerDevice payload", timer->payload(), 888);

    expectCondition("DebugOutputDevice captured at least two writes", debugOutputDevice->writes().size() >= 2);

    expectCondition("BIO-OS ASCII output is BU", debugOutputAsAscii(*debugOutputDevice) == "BU");

    if (debugOutputDevice->writes().size() >= 2) {
        expect("DebugOutputDevice write[0] boot message", debugOutputDevice->writes()[0], 66);
        expect("DebugOutputDevice write[1] user message", debugOutputDevice->writes()[1], 85);
    } else {
        std::cout << "[FAIL] DebugOutputDevice missing boot/user writes\n";
        passed = false;
    }

    expectCondition("timer tick count is positive", timer->tickCount() > 0);
    expectCondition("user program observed positive timer tick", cpu.state().memory().read(120) > 0);
    expectCondition("TimerDevice requested at least one interrupt", timer->interruptCount() >= 1);
    expectCondition("TimerDevice disabled by timer handler", !timer->enabled());
    expectCondition("interrupt queue is empty after BIO-OS demo", controller->pendingCount() == 0);
    expectCondition("CPU halted after BIO-OS combined boot demo", cpu.state().halted());

    if (!passed) {
        std::cout << "\nBIO-OS combined boot test failed.\n";
        return 1;
    }

    std::cout << "\nBIO-OS combined boot test finished successfully.\n";
    return 0;
}



struct SyscallInfo {
    int number;
    const char* name;
    const char* inputs;
    const char* returns;
    const char* effect;
};

const std::vector<SyscallInfo>& syscallTable() {
    static const std::vector<SyscallInfo> table = {
        {
            1,
            "debug output",
            "R2 = value",
            "-",
            "write value to DebugOutputDevice"
        },
        {
            2,
            "memory write",
            "R2 = address, R3 = value",
            "-",
            "write Memory[R2] = R3"
        },
        {
            3,
            "exit",
            "R2 = exit code",
            "R7 = exit code",
            "halted CPU",
        },
        {
            4,
            "timer read",
            "-",
            "R2 = tick count",
            "read TimerDevice tick count"
        },
        {
            5,
            "timer enable",
            "R2 = interval, R3 = vector",
            "-",
            "configure interval/vector and enable timer"
        },
        {
            6,
            "timer disable",
            "-",
            "-",
            "disable timer"
        },
        {
            7,
            "timer configure",
            "R2 = interval, R3 = vector, R4 = payload",
            "-",
            "configure timer interval/vector/payload"
        }
    };

    return table;
}

void printSyscallTable() {
    std::cout << "=== Zero-CPU Syscall Table ===\n\n";
    std::cout << "Convention:\n";
    std::cout << "  R1 = syscall number\n";
    std::cout << "  R2 = argument 0 / return value\n";
    std::cout << "  R3 = argument 1\n";
    std::cout << "  R4 = argument 2\n";
    std::cout << "  INT 80\n\n";

    for (const SyscallInfo& syscall : syscallTable()) {
        std::cout << "syscall "
                  << syscall.number
                  << " = "
                  << syscall.name
                  << "\n";
        std::cout << "  inputs : " << syscall.inputs << "\n";
        std::cout << "  returns: " << syscall.returns << "\n";
        std::cout << "  effect : " << syscall.effect << "\n\n";
    }
}


int runInterruptFlagsRestoreTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Interrupt FLAGS Restore Test ===\n\n";

    const std::string sourcePath = "examples/interrupt_flags_restore.zasm";
    const std::string binaryPath = "examples/interrupt_flags_restore.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    auto controller = std::make_shared<InterruptController>();

    constexpr std::uint8_t kInterruptVector = 80;

    cpu.setInterruptController(controller);
    cpu.loadBinaryProgram(program);

    const auto handlerIt = assembled.labels.find("interrupt_handler");
    if (handlerIt == assembled.labels.end()) {
        std::cout << "[FAIL] interrupt_handler label not found\n";
        return 1;
    }

    const std::size_t handlerAddress =
        cpu.binaryCodeBase() + handlerIt->second * kInstructionSize;

    controller->setVectorHandler(kInterruptVector, handlerAddress);

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Interrupt vector: "
              << static_cast<int>(kInterruptVector)
              << "\n";
    std::cout << "Handler PC: " << handlerAddress << "\n";
    std::cout << "Expected behavior:\n";
    std::cout << "  main sets ZF=1 with CMP R1, 5\n";
    std::cout << "  interrupt handler intentionally changes FLAGS\n";
    std::cout << "  IRET must restore original ZF=1\n";
    std::cout << "  JE flags_restored must be taken\n\n";

    cpu.run();

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Interrupt FLAGS restore test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[472] interrupt handler marker", cpu.state().memory().read(472), 222);
    expect("Memory[480] flags restored branch marker", cpu.state().memory().read(480), 999);
    expect("Memory[488] failure branch marker remains zero", cpu.state().memory().read(488), 0);

    expect("R0 interrupt vector", cpu.state().registers().get(RegisterName::R0), 80);
    expect("R2 restored-path value", cpu.state().registers().get(RegisterName::R2), 999);

    expectCondition("CPU halted after interrupt FLAGS restore test", cpu.state().halted());

    if (!passed) {
        std::cout << "\nInterrupt FLAGS restore test failed.\n";
        return 1;
    }

    std::cout << "\nInterrupt FLAGS restore test finished successfully.\n";
    return 0;
}



int runRegisterIndirectMemoryTest() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU Register-Indirect Memory Test ===\n\n";

    const std::string sourcePath = "examples/register_indirect_memory.zasm";
    const std::string binaryPath = "examples/register_indirect_memory.zbin";

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(sourcePath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    program.header.data_base =
        static_cast<std::uint32_t>(
            assembled.data_base
        );
    program.header.data_size =
        static_cast<std::uint32_t>(
            assembled.data.size()
        );
    program.data = assembled.data;

    BinaryWriter writer;
    writer.writeFile(binaryPath, program);

    CPU cpu;
    cpu.loadBinaryProgram(program);
    cpu.run();

    std::cout << "Source: " << sourcePath << "\n";
    std::cout << "Binary: " << binaryPath << "\n";
    std::cout << "Syntax under test:\n";
    std::cout << "  STORE [R1], R2\n";
    std::cout << "  LOAD R3, [R1]\n";
    std::cout << "  STORE [R4], R5\n";
    std::cout << "  LOAD R6, [R4]\n\n";

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "Register-indirect memory test failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        std::int64_t actual,
        std::int64_t expected
    ) {
        if (actual == expected) {
            std::cout << "[PASS] "
                      << name
                      << " = "
                      << actual
                      << "\n";
            return;
        }

        std::cout << "[FAIL] "
                  << name
                  << " expected "
                  << expected
                  << " but got "
                  << actual
                  << "\n";
        passed = false;
    };

    auto expectCondition = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect("Memory[300] written through [R1]", cpu.state().memory().read(300), 1234);
    expect("Memory[308] loaded from [R1]", cpu.state().memory().read(308), 1234);
    expect("Memory[500] written through [R4]", cpu.state().memory().read(500), 4321);
    expect("Memory[316] loaded from [R4]", cpu.state().memory().read(316), 4321);

    expect("R1 address register", cpu.state().registers().get(RegisterName::R1), 300);
    expect("R2 stored value", cpu.state().registers().get(RegisterName::R2), 1234);
    expect("R3 loaded value", cpu.state().registers().get(RegisterName::R3), 1234);
    expect("R4 second address register", cpu.state().registers().get(RegisterName::R4), 500);
    expect("R5 second stored value", cpu.state().registers().get(RegisterName::R5), 4321);
    expect("R6 second loaded value", cpu.state().registers().get(RegisterName::R6), 4321);

    expectCondition("CPU halted after register-indirect program", cpu.state().halted());

    if (!passed) {
        std::cout << "\nRegister-indirect memory test failed.\n";
        return 1;
    }

    std::cout << "\nRegister-indirect memory test finished successfully.\n";
    return 0;
}


int runBioOSRunnerModuleTest(const std::string& bioOSDirectory) {
    using namespace zero_cpu::system;

    std::cout << "=== Zero-CPU BioOSRunner Module Test ===\n\n";

    BioOSRunOptions options;
    options.directory = bioOSDirectory;

    BioOSRunner runner;
    const BioOSRunResult result = runner.run(options);

    std::cout << "Directory: " << result.directory << "\n";
    std::cout << "Generated Source: " << result.combined_source_path << "\n";
    std::cout << "Generated Binary: " << result.combined_binary_path << "\n";
    std::cout << "Instruction Count: " << result.instruction_count << "\n";
    std::cout << "Code Size: " << result.code_size << " bytes\n";
    std::cout << "Syscall Handler PC: " << result.syscall_handler_pc << "\n";
    std::cout << "Timer Handler PC: " << result.timer_handler_pc << "\n";
    std::cout << "BIO-OS Stack Base: " << result.stack_base << "\n";
    std::cout << "Step Count: " << result.step_count << "\n";
    std::cout << "Final PC: " << result.final_pc << "\n";
    std::cout << "Final SP: " << result.final_sp << "\n";
    std::cout << "Exit Code: " << result.exit_code << "\n\n";

    std::cout << "Debug Writes: " << result.debug_writes.size() << "\n";
    std::cout << "Debug ASCII: " << result.debug_ascii << "\n";
    std::cout << "Timer Tick Count: " << result.timer_tick_count << "\n";
    std::cout << "Timer Interval: " << result.timer_interval << "\n";
    std::cout << "Timer Vector: " << result.timer_vector << "\n";
    std::cout << "Timer Payload: " << result.timer_payload << "\n";
    std::cout << "Timer Interrupt Count: " << result.timer_interrupt_count << "\n";
    std::cout << "Timer Enabled: " << (result.timer_enabled ? "true" : "false") << "\n\n";

    if (!result.success()) {
        std::cout << "BIO-OS runner module failed: " << result.error_message << "\n";
        return 1;
    }

    bool passed = true;

    auto expect = [&passed](const std::string& name, bool condition) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!condition) {
            passed = false;
        }
    };

    expect("BIO-OS runner halted CPU", result.halted);
    expect("BIO-OS runner debug ASCII is BU", result.debug_ascii == "BU");
    expect("BIO-OS runner captured two debug writes", result.debug_writes.size() == 2);
    expect("BIO-OS runner timer interrupt count is at least one", result.timer_interrupt_count >= 1);
    expect("BIO-OS runner timer disabled itself", !result.timer_enabled);
    expect("BIO-OS runner exit code is zero", result.exit_code == 0);

    if (!passed) {
        std::cout << "\nBIO-OS runner module test failed.\n";
        return 1;
    }

    std::cout << "\nBIO-OS runner module finished successfully.\n";
    return 0;
}
int runTraceJsonWriterTest() {
    using namespace zero_cpu;

    std::cout << "=== Trace JSON Writer Test ===\n";

    const std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(5)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(7)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(180),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.loadProgram(program, {});
    cpu.run();

    TraceJsonMetadata metadata;
    metadata.producer = "zero_cli";
    metadata.producer_version = "v0.5-dev";
    metadata.execution_mode = "Assembly";
    metadata.loaded_path = "<trace-json-test>";

    const std::string json =
        TraceJsonWriter::toJson(
            cpu.traceLogger().events(),
            metadata
        );

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect(
        "CPU produced four trace events",
        cpu.traceLogger().size() == 4
    );
    expect(
        "program stored 12 at Memory[180]",
        cpu.state().memory().read(180) == 12
    );
    expect(
        "schema name is zero_cpu_trace",
        json.find("\"schema\": \"zero_cpu_trace\"") !=
            std::string::npos
    );
    expect(
        "schema version is 3",
        json.find("\"schema_version\": 3") !=
            std::string::npos
    );
    expect(
        "event count is serialized",
        json.find("\"event_count\": 4") !=
            std::string::npos
    );
    expect(
        "state_before is serialized",
        json.find("\"state_before\": {") !=
            std::string::npos
    );
    expect(
        "state_after is serialized",
        json.find("\"state_after\": {") !=
            std::string::npos
    );
    expect(
        "register changes are structured",
        json.find("\"register_changes\": [") !=
            std::string::npos
    );
    expect(
        "flag changes are structured",
        json.find("\"flag_changes\": [") !=
            std::string::npos
    );
    expect(
        "memory changes are structured",
        json.find("\"memory_changes\": [") !=
            std::string::npos
    );
    expect(
        "Memory[180] address is serialized",
        json.find("\"address\": 180") !=
            std::string::npos
    );

    if (!passed) {
        std::cout << "\nTrace JSON writer test failed.\n";
        return 1;
    }

    std::cout << "\nTrace JSON writer test passed.\n";
    return 0;
}

std::string makeTraceJsonDiffFixture(
    std::int64_t addend,
    const std::string& producer
) {
    using namespace zero_cpu;

    const std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(5)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(addend)
        ),
        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(180),
            Operand::registerOperand(RegisterName::R1)
        ),
        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.loadProgram(program, {});
    cpu.run();

    TraceJsonMetadata metadata;
    metadata.producer = producer;
    metadata.producer_version = "v0.5-dev";
    metadata.execution_mode = "Assembly";
    metadata.loaded_path = "<trace-diff-test>";

    return TraceJsonWriter::toJson(
        cpu.traceLogger().events(),
        metadata
    );
}

int runTraceJsonDiffTest() {
    using namespace zero_cpu;

    std::cout << "=== Trace JSON Diff Test ===\n";

    const std::string expected =
        makeTraceJsonDiffFixture(7, "expected-producer");

    const std::string sameArchitecture =
        makeTraceJsonDiffFixture(7, "actual-producer");

    const std::string changedArchitecture =
        makeTraceJsonDiffFixture(8, "actual-producer");

    const TraceJsonDiffResult defaultEqual =
        TraceJsonDiff::compareText(
            expected,
            sameArchitecture
        );

    TraceJsonDiffOptions strictOptions;
    strictOptions.strict = true;

    const TraceJsonDiffResult strictDifferent =
        TraceJsonDiff::compareText(
            expected,
            sameArchitecture,
            strictOptions
        );

    const TraceJsonDiffResult architectureDifferent =
        TraceJsonDiff::compareText(
            expected,
            changedArchitecture
        );

    bool passed = true;

    auto expect = [&passed](
        const std::string& name,
        bool condition
    ) {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!condition) {
            passed = false;
        }
    };

    expect(
        "default mode ignores producer metadata",
        defaultEqual.equal
    );
    expect(
        "strict mode compares producer metadata",
        !strictDifferent.equal
    );
    expect(
        "architectural change is detected",
        !architectureDifferent.equal
    );
    expect(
        "difference count is reported",
        architectureDifferent.difference_count > 0
    );
    expect(
        "first difference path is reported",
        !architectureDifferent.first_path.empty()
    );
    expect(
        "expected value is reported",
        !architectureDifferent.expected_value.empty()
    );
    expect(
        "actual value is reported",
        !architectureDifferent.actual_value.empty()
    );

    if (!passed) {
        std::cout << "\nTrace JSON diff test failed.\n";
        return 1;
    }

    std::cout << "\nFirst detected architecture difference:\n";
    std::cout << "Path = "
              << architectureDifferent.first_path
              << "\n";
    std::cout << "Expected = "
              << architectureDifferent.expected_value
              << "\n";
    std::cout << "Actual = "
              << architectureDifferent.actual_value
              << "\n";

    std::cout << "\nTrace JSON diff test passed.\n";
    return 0;
}

int runTraceDiffCommand(
    const std::string& expectedPath,
    const std::string& actualPath,
    bool strict
) {
    zero_cpu::TraceJsonDiffOptions options;
    options.strict = strict;

    const zero_cpu::TraceJsonDiffResult result =
        zero_cpu::TraceJsonDiff::compareFiles(
            expectedPath,
            actualPath,
            options
        );

    std::cout << "=== Zero-CPU Trace Diff ===\n";
    std::cout << "Mode: "
              << (strict ? "strict" : "architectural")
              << "\n";
    std::cout << "Expected: " << expectedPath << "\n";
    std::cout << "Actual:   " << actualPath << "\n\n";

    if (result.equal) {
        std::cout << "[PASS] " << result.message << "\n";
        return 0;
    }

    std::cout << "[FAIL] " << result.message << "\n";
    std::cout << "Path:     " << result.first_path << "\n";
    std::cout << "Expected: " << result.expected_value << "\n";
    std::cout << "Actual:   " << result.actual_value << "\n";

    return 2;
}

int runGoldenTraceRegressionTest() {
    using namespace zero_cpu;
    namespace fs = std::filesystem;

    constexpr const char* kSourcePath = "tests/golden/trace_smoke.zasm";
    constexpr const char* kExpectedPath = "tests/golden/trace_smoke.json";
    constexpr const char* kActualPath = "build/test-output/trace_smoke_actual.json";

    std::cout << "=== Golden Trace Regression Test ===\n";

    Assembler assembler;
    const AssembledProgram assembled = assembler.assembleFile(kSourcePath);

    CPU cpu;
    cpu.loadProgram(assembled.instructions, assembled.labels);
    cpu.run();

    if (cpu.state().hasError()) {
        std::cout << "[FAIL] Execution failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    fs::create_directories(fs::path(kActualPath).parent_path());

    TraceJsonMetadata metadata;
    metadata.producer = "zero_cli";
    metadata.producer_version = "v0.5-dev";
    metadata.execution_mode = "Assembly";
    metadata.loaded_path = kSourcePath;

    TraceJsonWriter::writeFile(
        kActualPath,
        cpu.traceLogger().events(),
        metadata
    );

    const TraceJsonDiffResult result =
        TraceJsonDiff::compareFiles(kExpectedPath, kActualPath);

    if (result.equal) {
        std::cout << "[PASS] Golden trace regression match\n";
        std::cout << "Events: " << cpu.traceLogger().size() << "\n";
        return 0;
    }

    std::cout << "[FAIL] " << result.message << "\n";
    std::cout << "Path:     " << result.first_path << "\n";
    std::cout << "Expected: " << result.expected_value << "\n";
    std::cout << "Actual:   " << result.actual_value << "\n";
    return 1;
}

void printUsage() {
    std::cout << "Zero-CPU CLI\n\n";
    std::cout << "Usage:\n";
    std::cout << "  zero_cli\n";
    std::cout << "  zero_cli <input.zasm>\n";
    std::cout << "  zero_cli binary-test [output.zbin]\n";
    std::cout << "  zero_cli alu-test\n";
    std::cout << "  zero_cli signed-branch-test\n";
    std::cout << "  zero_cli differential-test\n";
    std::cout << "  zero_cli error-invariant-test\n";
    std::cout << "  zero_cli trace-json-test\n";
    std::cout << "  zero_cli trace-diff-test\n";
    std::cout << "  zero_cli trace-diff <expected.json> <actual.json> [--strict]\n";
    std::cout << "  zero_cli trace-golden-test\n";
    std::cout << "  zero_cli mmio-test\n";
    std::cout << "  zero_cli hardware-bus-test\n";
    std::cout << "  zero_cli serial-hardware-test\n";
    std::cout << "  zero_cli hardware-live-test <COM-port> [baud]\n";
    std::cout << "  zero_cli interrupt-test\n";
    std::cout << "  zero_cli cpu-interrupt-test\n";
    std::cout << "  zero_cli timer-test\n";
    std::cout << "  zero_cli cpu-timer-test\n";
    std::cout << "  zero_cli cpu-ei-di-test\n";
    std::cout << "  zero_cli software-interrupt-test\n";
    std::cout << "  zero_cli interrupt-flags-restore-test\n";
    std::cout << "  zero_cli register-indirect-test\n";
    std::cout << "  zero_cli mini-kernel-syscall-test\n";
    std::cout << "  zero_cli mini-kernel-syscall2-test\n";
    std::cout << "  zero_cli mini-kernel-syscall3-test\n";
    std::cout << "  zero_cli mini-kernel-syscall4-timer-read-test\n";
    std::cout << "  zero_cli mini-kernel-syscall5-timer-enable-test\n";
    std::cout << "  zero_cli mini-kernel-syscall6-timer-disable-test\n";
    std::cout << "  zero_cli mini-kernel-syscall7-timer-configure-test\n";
    std::cout << "  zero_cli mini-kernel-timer-lifecycle-test\n";
    std::cout << "  zero_cli bio-os-combined-boot-test\n";
    std::cout << "  zero_cli run-os <bio_os_directory>\n";
    std::cout << "  zero_cli bio-os-runner-test <bio_os_directory>\n";
    std::cout << "  zero_cli syscall-table\n";
    std::cout << "  zero_cli assemble <input.zasm> <output.zbin>\n";
    std::cout << "  zero_cli run-processes [--quantum N] [--max-steps N] [--protected-syscalls] [--hardware-mock | --hardware-serial PORT] [--baud N] [--expect-exit PID=CODE] [--expect-hardware OFFSET=VALUE] <app1.zbin> [app2.zbin ...]\n";
    std::cout << "  zero_cli debug-binary <input.zbin> [--break <address>...] [--max-steps N] [--step N] [--registers] [--memory <address> <bytes>...] [--disassemble <address> <instructions>...]\n";
    std::cout << "  zero_cli debug-shell <input.zbin> [--commands <file>] [--max-steps N]\n";
    std::cout << "  zero_cli debug-processes [--quantum N] [--max-steps N] [--commands <file>] [--protected-syscalls] [--hardware-mock | --hardware-serial PORT] [--baud N] [--expect-exit PID=CODE] [--expect-hardware OFFSET=VALUE] <app1.zbin> <app2.zbin> [more.zbin ...]\n";
    std::cout << "  zero_cli dump-binary <input.zbin>\n";
    std::cout << "  zero_cli load-binary <input.zbin>\n";
    std::cout << "  zero_cli cpu-load-binary <input.zbin>\n";
    std::cout << "  zero_cli run-binary <input.zbin> [--debug-mmio] [--watch <addr>...] [--expect-memory <addr=value>...]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc >= 2) {
            const std::string command = argv[1];

            if (command == "help" || command == "--help" || command == "-h") {
                printUsage();
                return 0;
            }

            if (command == "binary-test") {
                const std::string outputPath =
                    argc >= 3
                        ? argv[2]
                        : "examples/binary_test.zbin";

                return runBinaryTest(outputPath);
            }

            if (command == "alu-test") {
                if (argc != 2) {
                    std::cerr << "Invalid alu-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runAluTest();
            }

            if (command == "error-invariant-test") {
                if (argc != 2) {
                    std::cerr
                        << "Invalid error-invariant-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runErrorInvariantTest();
            }

            if (command == "differential-test") {
                if (argc != 2) {
                    std::cerr
                        << "Invalid differential-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runDifferentialTest();
            }

            if (command == "signed-branch-test") {
                if (argc != 2) {
                    std::cerr
                        << "Invalid signed-branch-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runSignedBranchTest();
            }

            if (command == "trace-json-test") {
                if (argc != 2) {
                    std::cerr << "Invalid trace-json-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runTraceJsonWriterTest();
            }

            if (command == "trace-diff-test") {
                if (argc != 2) {
                    std::cerr << "Invalid trace-diff-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runTraceJsonDiffTest();
            }

            if (command == "trace-diff") {
                if (argc != 4 && argc != 5) {
                    std::cerr << "Invalid trace-diff command.\n\n";
                    printUsage();
                    return 1;
                }

                bool strict = false;

                if (argc == 5) {
                    if (std::string(argv[4]) != "--strict") {
                        std::cerr << "Unknown trace-diff option: "
                                  << argv[4]
                                  << "\n\n";
                        printUsage();
                        return 1;
                    }

                    strict = true;
                }

                return runTraceDiffCommand(
                    argv[2],
                    argv[3],
                    strict
                );
            }

            if (command == "trace-golden-test") {
                if (argc != 2) {
                    std::cerr << "Invalid trace-golden-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runGoldenTraceRegressionTest();
            }

            if (command == "mmio-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mmio-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMMIOTest();
            }

            if (command == "hardware-bus-test") {
                if (argc != 2) {
                    std::cerr
                        << "Invalid hardware-bus-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runHardwareBusTest();
            }

            if (command == "hardware-live-test") {
                if (argc != 3 && argc != 4) {
                    std::cerr
                        << "Invalid hardware-live-test command.\n\n";
                    printUsage();
                    return 1;
                }

                std::uint32_t baudRate = 115200;

                if (argc == 4) {
                    try {
                        const unsigned long parsed = std::stoul(argv[3]);
                        if (parsed == 0 ||
                            parsed > std::numeric_limits<std::uint32_t>::max()) {
                            throw std::out_of_range("baud rate");
                        }
                        baudRate = static_cast<std::uint32_t>(parsed);
                    } catch (const std::exception&) {
                        std::cerr << "Invalid baud rate: " << argv[3] << "\n";
                        return 1;
                    }
                }

                return runHardwareLiveTest(argv[2], baudRate);
            }

            if (command == "serial-hardware-test") {
                if (argc != 2) {
                    std::cerr
                        << "Invalid serial-hardware-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runSerialHardwareTest();
            }

            if (command == "interrupt-test") {
                if (argc != 2) {
                    std::cerr << "Invalid interrupt-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runInterruptTest();
            }


            if (command == "cpu-interrupt-test") {
                if (argc != 2) {
                    std::cerr << "Invalid cpu-interrupt-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runCPUInterruptTest();
            }



            if (command == "timer-test") {
                if (argc != 2) {
                    std::cerr << "Invalid timer-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runTimerDeviceTest();
            }

            if (command == "cpu-timer-test") {
                if (argc != 2) {
                    std::cerr << "Invalid cpu-timer-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runCPUTimerInterruptTest();
            }


            if (command == "cpu-ei-di-test") {
                if (argc != 2) {
                    std::cerr << "Invalid cpu-ei-di-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runCPUEiDiTest();
            }


            if (command == "register-indirect-test") {
                if (argc != 2) {
                    std::cerr << "Invalid register-indirect-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runRegisterIndirectMemoryTest();
            }

            if (command == "software-interrupt-test") {
                if (argc != 2) {
                    std::cerr << "Invalid software-interrupt-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runSoftwareInterruptTest();
            }

            if (command == "mini-kernel-syscall-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscallTest();
            }


            if (command == "mini-kernel-syscall2-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall2-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall2Test();
            }


            if (command == "mini-kernel-syscall3-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall3-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall3Test();
            }


            if (command == "mini-kernel-syscall4-timer-read-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall4-timer-read-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall4TimerReadTest();
            }


            if (command == "mini-kernel-syscall5-timer-enable-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall5-timer-enable-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall5TimerEnableTest();
            }


            if (command == "mini-kernel-syscall6-timer-disable-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall6-timer-disable-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall6TimerDisableTest();
            }


            if (command == "mini-kernel-syscall7-timer-configure-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-syscall7-timer-configure-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelSyscall7TimerConfigureTest();
            }


            if (command == "mini-kernel-timer-lifecycle-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mini-kernel-timer-lifecycle-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMiniKernelTimerLifecycleTest();
            }


            if (command == "bio-os-combined-boot-test") {
                if (argc != 2) {
                    std::cerr << "Invalid bio-os-combined-boot-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runBioOSCombinedBootTest();
            }


            if (command == "bio-os-runner-test") {
                if (argc != 3) {
                    std::cerr << "Invalid bio-os-runner-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runBioOSRunnerModuleTest(argv[2]);
            }
            if (command == "run-os") {
                if (argc != 3) {
                    std::cerr << "Invalid run-os command.\n\n";
                    printUsage();
                    return 1;
                }

                return runBioOSDirectory(argv[2]);
            }


            if (command == "interrupt-flags-restore-test") {
                if (argc != 2) {
                    std::cerr << "Invalid interrupt-flags-restore-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runInterruptFlagsRestoreTest();
            }


            if (command == "syscall-table") {
                if (argc != 2) {
                    std::cerr << "Invalid syscall-table command.\n\n";
                    printUsage();
                    return 1;
                }

                printSyscallTable();
                return 0;
            }

            if (command == "assemble") {
                if (argc != 4) {
                    std::cerr << "Invalid assemble command.\n\n";
                    printUsage();
                    return 1;
                }

                return assembleToBinary(argv[2], argv[3]);
            }

            if (command == "debug-processes") {
                if (argc < 4) {
                    std::cerr
                        << "Invalid debug-processes command.\n\n";

                    printUsage();
                    return 1;
                }

                return debugProcessesCommand(
                    argc,
                    argv,
                    2
                );
            }

            if (command == "debug-shell") {
                if (argc < 3) {
                    std::cerr
                        << "Invalid debug-shell command.\n\n";

                    printUsage();
                    return 1;
                }

                return debugShellCommand(
                    argv[2],
                    argc,
                    argv,
                    3
                );
            }

            if (command == "debug-binary") {
                if (argc < 3) {
                    std::cerr
                        << "Invalid debug-binary command.\n\n";

                    printUsage();
                    return 1;
                }

                return debugBinaryCommand(
                    argv[2],
                    argc,
                    argv,
                    3
                );
            }

            if (command == "run-processes") {
                if (argc < 3) {
                    std::cerr
                        << "Invalid run-processes command.\n\n";

                    printUsage();
                    return 1;
                }

                return runProcessesCommand(
                    argc,
                    argv,
                    2
                );
            }

            if (command == "dump-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid dump-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return dumpBinaryFile(argv[2]);
            }

            if (command == "load-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid load-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return loadBinaryFile(argv[2]);
            }

            if (command == "cpu-load-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid cpu-load-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return cpuLoadBinaryFile(argv[2]);
            }

            if (command == "run-binary") {
                if (argc < 3) {
                    std::cerr << "Invalid run-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                const RunBinaryOptions options =
                    parseRunBinaryOptions(argc, argv, 3);

                return runBinaryFile(argv[2], options);
            }
        }

        const std::string inputPath =
            argc >= 2
                ? argv[1]
                : "examples/function_call.zasm";

        return runAssemblyProgram(inputPath);
    } catch (const std::exception& ex) {
        std::cerr << "Error: "
                  << ex.what()
                  << "\n";

        return 1;
    }
}