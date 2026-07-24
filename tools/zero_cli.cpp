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
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
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

int runBioOSDirectory(const std::string& osDirectory) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Zero-CPU BIO-OS Run ===\n\n";

    const std::vector<std::string> sourceParts = {
        joinPath(osDirectory, "boot.zasm"),
        joinPath(osDirectory, "kernel.zasm"),
        joinPath(osDirectory, "user_program.zasm")
    };

    const std::string combinedSourcePath =
        joinPath(osDirectory, "combined_boot.zasm");
    const std::string binaryPath =
        joinPath(osDirectory, "combined_boot.zbin");

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

    std::cout << "OS directory: " << osDirectory << "\n";
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

    std::cout << "=== BIO-OS Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";
    printDebugOutputDevice(*debugOutputDevice);
    std::cout << "\n";
    std::cout << "Timer tick count = " << timer->tickCount() << "\n";
    std::cout << "Timer interval = " << timer->interval() << "\n";
    std::cout << "Timer vector = " << static_cast<int>(timer->vector()) << "\n";
    std::cout << "Timer payload = " << timer->payload() << "\n";
    std::cout << "Timer interrupt count = " << timer->interruptCount() << "\n";
    std::cout << "Timer enabled = " << (timer->enabled() ? "true" : "false") << "\n";
    std::cout << "Pending interrupts = " << controller->pendingCount() << "\n\n";

    if (cpu.state().hasError()) {
        std::cout << "BIO-OS run failed: "
                  << cpu.state().errorMessage()
                  << "\n";
        return 1;
    }

    if (!cpu.state().halted()) {
        std::cout << "BIO-OS run failed: CPU did not halt.\n";
        return 1;
    }

    if (debugOutputDevice->writes().empty()) {
        std::cout << "BIO-OS run failed: no debug output captured.\n";
        return 1;
    }

    std::cout << "BIO-OS run finished successfully.\n";
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
    std::cout << "  zero_cli timer-test\n";
    std::cout << "  zero_cli cpu-timer-test\n";
    std::cout << "  zero_cli cpu-ei-di-test\n";
    std::cout << "  zero_cli software-interrupt-test\n";
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


            if (command == "run-os") {
                if (argc != 3) {
                    std::cerr << "Invalid run-os command.\n\n";
                    printUsage();
                    return 1;
                }

                return runBioOSDirectory(argv[2]);
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