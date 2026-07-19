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
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
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

constexpr std::size_t kStackViewStart = 2048;
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

    std::cout << "Stack[2048..2079]: "
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

    std::cout << "Memory[2048] = "
              << cpu.state().memory().read(2048)
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
        mmioBus->mapDevice(0xF000, 0x10, debugOutputDevice);
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

int assembleToBinary(
    const std::string& inputPath,
    const std::string& outputPath
) {
    using namespace zero_cpu;

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(inputPath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    binary::BinaryProgram program;

    program.header.major_version = binary::kMajorVersion;
    program.header.minor_version = binary::kMinorVersion;
    program.header.endianness = binary::BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);

    binary::BinaryWriter writer;
    writer.writeFile(outputPath, program);

    binary::BinaryReader reader;
    const binary::BinaryProgram verified = reader.readFile(outputPath);

    std::cout << "Assemble completed successfully.\n";
    std::cout << "Input: " << inputPath << "\n";
    std::cout << "Output: " << outputPath << "\n";
    std::cout << "Instruction count: "
              << assembled.instructions.size()
              << "\n";
    std::cout << "Code size: "
              << verified.header.code_size
              << " bytes\n";
    std::cout << "Entry point: "
              << verified.header.entry_point
              << "\n";

    return 0;
}

int runAssemblyProgram(const std::string& inputPath) {
    using namespace zero_cpu;

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(inputPath);

    CPU cpu;
    cpu.loadProgram(assembled.instructions, assembled.labels);

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


int runMMIOTest() {
    using namespace zero_cpu;

    std::cout << "=== Zero-CPU MMIO Test ===\n\n";

    bool passed = true;

    MMIOBus bus;
    auto outputDevice = std::make_shared<DebugOutputDevice>();

    try {
        bus.mapDevice(0xF000, 16, outputDevice);
        std::cout << "[PASS] mapped DebugOutputDevice at 0xF000..0xF00F\n";
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] failed to map DebugOutputDevice: "
                  << ex.what()
                  << "\n";
        passed = false;
    }

    if (bus.hasDeviceAt(0xF000) && bus.hasDeviceAt(0xF008) && !bus.hasDeviceAt(0xEFFF)) {
        std::cout << "[PASS] MMIO address lookup\n";
    } else {
        std::cout << "[FAIL] MMIO address lookup\n";
        passed = false;
    }

    try {
        bus.write(0xF000, 65);
        bus.write(0xF000, 66);

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
        const std::int64_t lastValue = bus.read(0xF000);
        const std::int64_t writeCount = bus.read(0xF008);

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

void printUsage() {
    std::cout << "Zero-CPU CLI\n\n";
    std::cout << "Usage:\n";
    std::cout << "  zero_cli\n";
    std::cout << "  zero_cli <input.zasm>\n";
    std::cout << "  zero_cli binary-test [output.zbin]\n";
    std::cout << "  zero_cli alu-test\n";
    std::cout << "  zero_cli mmio-test\n";
    std::cout << "  zero_cli interrupt-test\n";
    std::cout << "  zero_cli cpu-interrupt-test\n";
    std::cout << "  zero_cli assemble <input.zasm> <output.zbin>\n";
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

            if (command == "mmio-test") {
                if (argc != 2) {
                    std::cerr << "Invalid mmio-test command.\n\n";
                    printUsage();
                    return 1;
                }

                return runMMIOTest();
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

            if (command == "assemble") {
                if (argc != 4) {
                    std::cerr << "Invalid assemble command.\n\n";
                    printUsage();
                    return 1;
                }

                return assembleToBinary(argv[2], argv[3]);
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
