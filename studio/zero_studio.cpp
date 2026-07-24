#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/system/BioOSRunner.hpp"
#include "zero_cpu/core/DebugOutputDevice.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/TimerDevice.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kWindowWidth = 1500;
constexpr int kWindowHeight = 920;

constexpr int kIdInputEdit = 1001;
constexpr int kIdOutputEdit = 1002;
constexpr int kIdLoadSourceButton = 1003;
constexpr int kIdSaveSourceButton = 1004;
constexpr int kIdAssembleButton = 1005;
constexpr int kIdLoadAssemblyButton = 1006;
constexpr int kIdLoadBinaryButton = 1007;
constexpr int kIdStepButton = 1008;
constexpr int kIdRunButton = 1009;
constexpr int kIdResetButton = 1010;
constexpr int kIdSourceEdit = 1011;
constexpr int kIdStateEdit = 1012;
constexpr int kIdTraceEdit = 1013;
constexpr int kIdBreakpointEdit = 1014;
constexpr int kIdAddBreakpointButton = 1015;
constexpr int kIdClearBreakpointsButton = 1016;
constexpr int kIdRunBioOSButton = 1017;

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kBinaryMemoryPreviewStart = 512;
constexpr std::size_t kBinaryMemoryPreviewCount = 96;

constexpr const char* kDefaultSourcePath = "examples\\function_call.zasm";
constexpr const char* kDefaultBinaryPath = "examples\\function_call.zbin";

enum class StudioMode {
    None,
    Assembly,
    Binary
};

HWND g_inputEdit = nullptr;
HWND g_outputEdit = nullptr;
HWND g_loadSourceButton = nullptr;
HWND g_saveSourceButton = nullptr;
HWND g_assembleButton = nullptr;
HWND g_loadAssemblyButton = nullptr;
HWND g_loadBinaryButton = nullptr;
HWND g_stepButton = nullptr;
HWND g_runButton = nullptr;
HWND g_resetButton = nullptr;
HWND g_sourceEdit = nullptr;
HWND g_stateEdit = nullptr;
HWND g_traceEdit = nullptr;
HWND g_breakpointEdit = nullptr;
HWND g_addBreakpointButton = nullptr;
HWND g_clearBreakpointsButton = nullptr;
HWND g_runBioOSButton = nullptr;

zero_cpu::CPU g_cpu;
std::shared_ptr<zero_cpu::InterruptController> g_interruptController;
std::shared_ptr<zero_cpu::MMIOBus> g_mmioBus;
std::shared_ptr<zero_cpu::DebugOutputDevice> g_debugOutputDevice;
std::shared_ptr<zero_cpu::TimerDevice> g_timerDevice;
StudioMode g_mode = StudioMode::None;
bool g_programLoaded = false;
std::string g_loadedPath;
std::vector<std::size_t> g_breakpoints;

HMENU controlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::string normalizeNewlines(const std::string& text) {
    std::string result;
    result.reserve(text.size() + 64);

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];

        if (ch == '\r') {
            result += '\r';

            if (i + 1 < text.size() && text[i + 1] == '\n') {
                result += '\n';
                ++i;
            } else {
                result += '\n';
            }
        } else if (ch == '\n') {
            result += "\r\n";
        } else {
            result += ch;
        }
    }

    return result;
}

std::string normalizeForFile(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];

        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                result += '\n';
                ++i;
            } else {
                result += '\n';
            }
        } else {
            result += ch;
        }
    }

    return result;
}

std::string getWindowTextString(HWND hwnd) {
    const int length = GetWindowTextLengthA(hwnd);

    if (length <= 0) {
        return {};
    }

    std::string buffer(static_cast<std::size_t>(length) + 1, '\0');
    GetWindowTextA(hwnd, buffer.data(), length + 1);
    buffer.resize(static_cast<std::size_t>(length));

    return buffer;
}

void setEditText(HWND hwnd, const std::string& text) {
    const std::string normalized = normalizeNewlines(text);
    SetWindowTextA(hwnd, normalized.c_str());
}

void appendTraceText(const std::string& text) {
    const std::string normalized = normalizeNewlines(text);

    const int length = GetWindowTextLengthA(g_traceEdit);
    SendMessageA(g_traceEdit, EM_SETSEL, length, length);
    SendMessageA(
        g_traceEdit,
        EM_REPLACESEL,
        FALSE,
        reinterpret_cast<LPARAM>(normalized.c_str())
    );
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to open source file: " + path);
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

void writeTextFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to write source file: " + path);
    }

    file << normalizeForFile(text);
}

std::string modeToString(StudioMode mode) {
    switch (mode) {
    case StudioMode::Assembly:
        return "Assembly";
    case StudioMode::Binary:
        return "Binary";
    case StudioMode::None:
    default:
        return "None";
    }
}

bool parseSizeT(const std::string& text, std::size_t& value) {
    std::istringstream iss(text);
    std::size_t parsed = 0;
    iss >> parsed;
    iss >> std::ws;

    if (!iss || !iss.eof()) {
        return false;
    }

    value = parsed;
    return true;
}

bool hasBreakpoint(std::size_t pc) {
    return std::find(
        g_breakpoints.begin(),
        g_breakpoints.end(),
        pc
    ) != g_breakpoints.end();
}

void addBreakpoint(std::size_t pc) {
    if (!hasBreakpoint(pc)) {
        g_breakpoints.push_back(pc);
    }
}

std::string makeBreakpointView() {
    std::ostringstream oss;

    oss << "Breakpoints\n";

    if (g_breakpoints.empty()) {
        oss << "(none)\n";
        return oss.str();
    }

    for (std::size_t i = 0; i < g_breakpoints.size(); ++i) {
        oss << "[" << i << "] PC = " << g_breakpoints[i] << "\n";
    }

    return oss.str();
}
bool endsWithZbin(const std::string& path) {
    if (path.size() < 5) {
        return false;
    }

    const std::string suffix = path.substr(path.size() - 5);
    return suffix == ".zbin" || suffix == ".ZBIN";
}

std::string makeFinalCheckView() {
    std::ostringstream oss;

    oss << "Final Check\n";
    oss << "R1 = "
        << g_cpu.state().registers().get(zero_cpu::RegisterName::R1)
        << "\n";
    oss << "R2 = "
        << g_cpu.state().registers().get(zero_cpu::RegisterName::R2)
        << "\n";
    oss << "SP = "
        << g_cpu.state().sp()
        << "\n";
    oss << "Memory[100] = "
        << g_cpu.state().memory().read(100)
        << "\n";
    oss << "Memory[2048] = "
        << g_cpu.state().memory().read(2048)
        << "\n";

    return oss.str();
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

std::string currentBinaryInstructionText() {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    try {
        const std::size_t pc = g_cpu.state().pc();

        const std::vector<std::uint8_t> instructionBytes =
            g_cpu.state().memory().readBytes(pc, kInstructionSize);

        InstructionDecoder decoder;
        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        return decodedInstructionToString(decoded);
    } catch (const std::exception& ex) {
        return std::string("<decode failed: ") + ex.what() + ">";
    }
}

std::string makeRegisterView() {
    using namespace zero_cpu;

    std::ostringstream oss;

    const auto& registers = g_cpu.state().registers();

    oss << "Registers\n";
    oss << "R0 = " << registers.get(RegisterName::R0) << "\n";
    oss << "R1 = " << registers.get(RegisterName::R1) << "\n";
    oss << "R2 = " << registers.get(RegisterName::R2) << "\n";
    oss << "R3 = " << registers.get(RegisterName::R3) << "\n";
    oss << "R4 = " << registers.get(RegisterName::R4) << "\n";
    oss << "R5 = " << registers.get(RegisterName::R5) << "\n";
    oss << "R6 = " << registers.get(RegisterName::R6) << "\n";
    oss << "R7 = " << registers.get(RegisterName::R7) << "\n";

    return oss.str();
}

std::string makeMemoryView() {
    std::ostringstream oss;

    oss << "Memory View\n";

    oss << "Memory[96..111] = "
        << g_cpu.state().memory().dumpRange(
               kDataViewStart,
               kDataViewCount
           )
        << "\n";

    oss << "Stack[2048..2079] = "
        << g_cpu.state().memory().dumpRange(
               kStackViewStart,
               kStackViewCount
           )
        << "\n";

    oss << "Memory[100] = "
        << g_cpu.state().memory().read(100)
        << "\n";

    oss << "Memory[2048] = "
        << g_cpu.state().memory().read(2048)
        << "\n";

    if (g_mode == StudioMode::Binary) {
        oss << "\n";
        oss << "Binary Code Memory Preview\n";
        oss << "Memory[512..607] = "
            << g_cpu.state().memory().dumpRange(
                   kBinaryMemoryPreviewStart,
                   kBinaryMemoryPreviewCount
               )
            << "\n";
    }

    return oss.str();
}

std::string makeBinaryInfoView() {
    std::ostringstream oss;

    if (g_mode != StudioMode::Binary) {
        return {};
    }

    oss << "Binary Program Info\n";
    oss << "Has Binary Program = "
        << (g_cpu.hasBinaryProgram() ? "true" : "false")
        << "\n";
    oss << "Code Base = "
        << g_cpu.binaryCodeBase()
        << "\n";
    oss << "Entry Point = "
        << g_cpu.binaryEntryPoint()
        << "\n";
    oss << "Code Size = "
        << g_cpu.binaryCodeSize()
        << " bytes\n";

    oss << "Current Decoded Instruction = "
        << currentBinaryInstructionText()
        << "\n";

    return oss.str();
}

std::string studioBoolText(bool value) {
    return value ? "true" : "false";
}

std::string studioDebugOutputAsAscii() {
    if (!g_debugOutputDevice) {
        return {};
    }

    std::string text;

    for (const std::int64_t value : g_debugOutputDevice->writes()) {
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

void configureSystemDevices() {
    using namespace zero_cpu;

    g_interruptController = std::make_shared<InterruptController>();
    g_mmioBus = std::make_shared<MMIOBus>();
    g_debugOutputDevice = std::make_shared<DebugOutputDevice>();
    g_timerDevice = std::make_shared<TimerDevice>(
        g_interruptController,
        44,
        1000,
        0
    );

    g_timerDevice->setEnabled(false);

    g_mmioBus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        g_debugOutputDevice
    );

    g_mmioBus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        g_timerDevice
    );

    g_cpu.setInterruptController(g_interruptController);
    g_cpu.setMMIOBus(g_mmioBus);
    g_cpu.clearClockedDevices();
    g_cpu.addClockedDevice(g_timerDevice);
}

std::string makeSystemPanelView() {
    using namespace zero_cpu;

    std::ostringstream oss;

    oss << "System Panel\n";

    oss << "Debug MMIO = 0x"
        << std::hex
        << memory_map::kDebugOutputBase
        << "..0x"
        << (memory_map::kDebugOutputEndExclusive - 1)
        << std::dec
        << "\n";

    oss << "Timer MMIO = 0x"
        << std::hex
        << memory_map::kTimerBase
        << "..0x"
        << (memory_map::kTimerEndExclusive - 1)
        << std::dec
        << "\n";

    oss << "Syscall Vector = 80\n";
    oss << "Default Timer Vector = 44\n";

    if (g_interruptController) {
        oss << "Interrupts Enabled = "
            << studioBoolText(g_interruptController->globalEnabled())
            << "\n";
        oss << "Pending Interrupts = "
            << g_interruptController->pendingCount()
            << "\n";
    } else {
        oss << "Interrupt Controller = <not configured>\n";
    }

    if (g_timerDevice) {
        oss << "Timer Tick Count = "
            << g_timerDevice->tickCount()
            << "\n";
        oss << "Timer Interval = "
            << g_timerDevice->interval()
            << "\n";
        oss << "Timer Vector = "
            << static_cast<int>(g_timerDevice->vector())
            << "\n";
        oss << "Timer Payload = "
            << g_timerDevice->payload()
            << "\n";
        oss << "Timer Interrupt Count = "
            << g_timerDevice->interruptCount()
            << "\n";
        oss << "Timer Enabled = "
            << studioBoolText(g_timerDevice->enabled())
            << "\n";
    } else {
        oss << "TimerDevice = <not configured>\n";
    }

    if (g_debugOutputDevice) {
        const std::string ascii = studioDebugOutputAsAscii();

        oss << "Debug Writes = "
            << g_debugOutputDevice->writes().size()
            << "\n";
        oss << "Debug ASCII = "
            << (ascii.empty() ? "<empty>" : ascii)
            << "\n";
    } else {
        oss << "DebugOutputDevice = <not configured>\n";
    }

    return oss.str();
}

std::string makeStateView() {
    std::ostringstream oss;

    oss << "Zero-CPU Studio v0.9\n";
    oss << "Mode: " << modeToString(g_mode) << "\n";

    if (g_programLoaded) {
        oss << "Loaded: " << g_loadedPath << "\n";
    } else {
        oss << "Loaded: false\n";
    }

    oss << "\n";

    oss << makeBreakpointView();

    oss << "\n";

    oss << "CPU Core State\n";
    oss << "PC = " << g_cpu.state().pc() << "\n";
    oss << "SP = " << g_cpu.state().sp() << "\n";
    oss << "Halted = "
        << (g_cpu.state().halted() ? "true" : "false")
        << "\n";

    if (g_cpu.state().hasError()) {
        oss << "Error = "
            << g_cpu.state().errorMessage()
            << "\n";
    }

    oss << "Flags = "
        << g_cpu.state().flags().toString()
        << "\n";

    if (g_mode == StudioMode::Binary) {
        oss << "\n";
        oss << makeBinaryInfoView();
    }

    oss << "\n";
    oss << makeSystemPanelView();

    oss << "\n";
    oss << makeRegisterView();

    oss << "\n";
    oss << makeMemoryView();

    oss << "\n";
    oss << makeFinalCheckView();

    return oss.str();
}

void refreshStateView() {
    setEditText(g_stateEdit, makeStateView());
}

bool saveSourceEditorToFile(const std::string& inputPath) {
    try {
        writeTextFile(inputPath, getWindowTextString(g_sourceEdit));

        std::ostringstream oss;
        oss << "Saved source file.\n";
        oss << "Path: " << inputPath << "\n";

        setEditText(g_traceEdit, oss.str());
        return true;
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "Save source failed.\n";
        oss << "Path: " << inputPath << "\n";
        oss << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, oss.str());
        return false;
    }
}

bool loadSourceFileToEditor(const std::string& inputPath) {
    try {
        const std::string source = readTextFile(inputPath);
        setEditText(g_sourceEdit, source);

        std::ostringstream oss;
        oss << "Loaded source into editor.\n";
        oss << "Path: " << inputPath << "\n";
        oss << "Size: " << source.size() << " bytes\n";

        setEditText(g_traceEdit, oss.str());
        return true;
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "Load source failed.\n";
        oss << "Path: " << inputPath << "\n";
        oss << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, oss.str());
        return false;
    }
}

bool assembleSourceToBinary(
    const std::string& inputPath,
    const std::string& outputPath
) {
    using namespace zero_cpu;

    try {
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

        std::ostringstream oss;
        oss << "Assemble completed successfully.\n";
        oss << "Input: " << inputPath << "\n";
        oss << "Output: " << outputPath << "\n";
        oss << "Instruction count: "
            << assembled.instructions.size()
            << "\n";
        oss << "Code size: "
            << verified.header.code_size
            << " bytes\n";
        oss << "Entry point: "
            << verified.header.entry_point
            << "\n";
        oss << "\n";
        oss << "Tip: output path was copied into the input box.\n";
        oss << "Click [Load Binary], then [Step] or [Run].\n";

        setEditText(g_traceEdit, oss.str());
        SetWindowTextA(g_inputEdit, outputPath.c_str());

        return true;
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "Assemble failed.\n";
        oss << "Input: " << inputPath << "\n";
        oss << "Output: " << outputPath << "\n";
        oss << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, oss.str());
        return false;
    }
}

bool loadAssemblyProgram(const std::string& inputPath) {
    using namespace zero_cpu;

    try {
        Assembler assembler;
        AssembledProgram assembled = assembler.assembleFile(inputPath);

        g_cpu.loadProgram(assembled.instructions, assembled.labels);
        configureSystemDevices();
        g_mode = StudioMode::Assembly;
        g_programLoaded = true;
        g_loadedPath = inputPath;

        std::ostringstream oss;
        oss << "Loaded assembly program.\n";
        oss << "Path: " << inputPath << "\n";
        oss << "Instruction count: "
            << assembled.instructions.size()
            << "\n";
        oss << "Label count: "
            << assembled.labels.size()
            << "\n";

        setEditText(g_traceEdit, oss.str());
        refreshStateView();

        return true;
    } catch (const std::exception& ex) {
        g_cpu.reset();
        g_mode = StudioMode::None;
        g_programLoaded = false;
        g_loadedPath.clear();

        std::ostringstream oss;
        oss << "Assembly load failed.\n";
        oss << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, oss.str());
        refreshStateView();

        return false;
    }
}

bool loadBinaryProgram(const std::string& inputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    try {
        BinaryReader reader;
        BinaryProgram program = reader.readFile(inputPath);

        g_cpu.loadBinaryProgram(program);
        configureSystemDevices();
        g_mode = StudioMode::Binary;
        g_programLoaded = true;
        g_loadedPath = inputPath;

        std::ostringstream oss;
        oss << "Loaded binary program.\n";
        oss << "Path: " << inputPath << "\n";
        oss << "Version: "
            << static_cast<int>(program.header.major_version)
            << "."
            << static_cast<int>(program.header.minor_version)
            << "\n";
        oss << "Entry Point: "
            << program.header.entry_point
            << "\n";
        oss << "Code Size: "
            << program.header.code_size
            << " bytes\n";
        oss << "Instruction Count: "
            << program.code.size() / kInstructionSize
            << "\n";

        setEditText(g_traceEdit, oss.str());
        refreshStateView();

        return true;
    } catch (const std::exception& ex) {
        g_cpu.reset();
        g_mode = StudioMode::None;
        g_programLoaded = false;
        g_loadedPath.clear();

        std::ostringstream oss;
        oss << "Binary load failed.\n";
        oss << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, oss.str());
        refreshStateView();

        return false;
    }
}

bool autoLoadFromInputPath() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return false;
    }

    if (endsWithZbin(inputPath)) {
        return loadBinaryProgram(inputPath);
    }

    return loadAssemblyProgram(inputPath);
}

std::string studioJoinPath(const std::string& directory, const std::string& fileName) {
    if (directory.empty()) {
        return fileName;
    }

    const char last = directory.back();

    if (last == '\\' || last == '/') {
        return directory + fileName;
    }

    return directory + "\\" + fileName;
}

std::string makeBioOSCombinedSource(const std::string& bioOSDirectory) {
    std::ostringstream source;

    source << "; Generated by Zero-CPU Studio BIO-OS runner\n";
    source << "; Source directory: " << bioOSDirectory << "\n\n";

    source << "; --- boot.zasm ---\n";
    source << readTextFile(studioJoinPath(bioOSDirectory, "boot.zasm"));
    source << "\n\n";

    source << "; --- kernel.zasm ---\n";
    source << readTextFile(studioJoinPath(bioOSDirectory, "kernel.zasm"));
    source << "\n\n";

    source << "; --- user_program.zasm ---\n";
    source << readTextFile(studioJoinPath(bioOSDirectory, "user_program.zasm"));
    source << "\n";

    return source.str();
}

std::size_t bioOSHandlerAddress(
    const zero_cpu::AssembledProgram& assembled,
    const std::string& labelName
) {
    using namespace zero_cpu;

    const auto it = assembled.labels.find(labelName);

    if (it == assembled.labels.end()) {
        throw std::runtime_error("BIO-OS label not found: " + labelName);
    }

    return g_cpu.binaryCodeBase() +
        it->second * binary::kInstructionSize;
}

void runLoadedBioOSProgram(std::ostringstream& log) {
    constexpr std::size_t kMaxBioOSSteps = 5000;

    std::size_t stepCount = 0;

    while (!g_cpu.state().halted() && stepCount < kMaxBioOSSteps) {
        g_cpu.step();

        if (g_cpu.state().hasError()) {
            log << "BIO-OS execution failed: "
                << g_cpu.state().errorMessage()
                << "\n";
            return;
        }

        ++stepCount;
    }

    if (!g_cpu.state().halted()) {
        log << "BIO-OS execution stopped: step limit reached.\n";
        return;
    }

    log << "BIO-OS execution finished successfully.\n";
    log << "Step Count: " << stepCount << "\n";
}

void onRunBioOSClicked() {
    using namespace zero_cpu::system;

    const std::string bioOSDirectory = "examples\\bio_os";

    try {
        configureSystemDevices();

        BioOSRunOptions options;
        options.directory = bioOSDirectory;

        BioOSRunner runner;
        const BioOSRunResult result =
            runner.runOn(
                g_cpu,
                g_interruptController,
                g_mmioBus,
                g_debugOutputDevice,
                g_timerDevice,
                options
            );

        g_mode = StudioMode::Binary;
        g_programLoaded = true;
        g_loadedPath = result.combined_binary_path;

        SetWindowTextA(g_inputEdit, result.combined_binary_path.c_str());
        SetWindowTextA(g_outputEdit, result.combined_binary_path.c_str());
        setEditText(g_sourceEdit, result.combined_source);

        std::ostringstream log;
        log << "[Studio BIO-OS Run via BioOSRunner]\n";
        log << "Directory: " << result.directory << "\n";
        log << "Generated Source: "
            << result.combined_source_path
            << "\n";
        log << "Generated Binary: "
            << result.combined_binary_path
            << "\n";
        log << "Instruction Count: "
            << result.instruction_count
            << "\n";
        log << "Code Size: "
            << result.code_size
            << " bytes\n";
        log << "Syscall Handler PC: "
            << result.syscall_handler_pc
            << "\n";
        log << "Timer Handler PC: "
            << result.timer_handler_pc
            << "\n";
        log << "BIO-OS Stack Base: "
            << result.stack_base
            << "\n";
        log << "Step Count: "
            << result.step_count
            << "\n";
        log << "Final PC: "
            << result.final_pc
            << "\n";
        log << "Final SP: "
            << result.final_sp
            << "\n";
        log << "Exit Code: "
            << result.exit_code
            << "\n\n";

        if (!result.success()) {
            log << "BIO-OS execution failed: "
                << result.error_message
                << "\n";
        } else {
            log << "BIO-OS execution finished successfully.\n";
        }

        log << "\n";
        log << "Debug Writes: "
            << result.debug_writes.size()
            << "\n";
        log << "Debug ASCII: "
            << (result.debug_ascii.empty()
                ? std::string("<empty>")
                : result.debug_ascii)
            << "\n";
        log << "Timer Tick Count: "
            << result.timer_tick_count
            << "\n";
        log << "Timer Interval: "
            << result.timer_interval
            << "\n";
        log << "Timer Vector: "
            << result.timer_vector
            << "\n";
        log << "Timer Payload: "
            << result.timer_payload
            << "\n";
        log << "Timer Interrupt Count: "
            << result.timer_interrupt_count
            << "\n";
        log << "Timer Enabled: "
            << studioBoolText(result.timer_enabled)
            << "\n";

        setEditText(g_traceEdit, log.str());
        refreshStateView();
    } catch (const std::exception& ex) {
        std::ostringstream log;
        log << "[Studio BIO-OS Run Failed]\n";
        log << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, log.str());
        refreshStateView();
    }
}

void onLoadSourceClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return;
    }

    loadSourceFileToEditor(inputPath);
}

void onSaveSourceClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return;
    }

    saveSourceEditorToFile(inputPath);
}

void onAssembleClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);
    const std::string outputPath = getWindowTextString(g_outputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return;
    }

    if (outputPath.empty()) {
        setEditText(g_traceEdit, "Output .zbin path is empty.\n");
        return;
    }

    if (GetWindowTextLengthA(g_sourceEdit) > 0) {
        if (!saveSourceEditorToFile(inputPath)) {
            return;
        }
    }

    assembleSourceToBinary(inputPath, outputPath);
    refreshStateView();
}

void onLoadAssemblyClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return;
    }

    loadAssemblyProgram(inputPath);
}

void onLoadBinaryClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setEditText(g_traceEdit, "Input path is empty.\n");
        return;
    }

    loadBinaryProgram(inputPath);
}

void onStepClicked() {
    if (!g_programLoaded) {
        if (!autoLoadFromInputPath()) {
            return;
        }
    }

    if (g_cpu.state().halted()) {
        appendTraceText("\nProgram is already halted.\n");
        refreshStateView();
        return;
    }

    const std::size_t pcBefore = g_cpu.state().pc();

    std::ostringstream stepLog;
    stepLog << "\n[Studio Step]\n";
    stepLog << "Mode = " << modeToString(g_mode) << "\n";
    stepLog << "PC before = " << pcBefore << "\n";

    if (g_mode == StudioMode::Assembly) {
        if (pcBefore < g_cpu.program().size()) {
            stepLog << "Instruction = "
                    << g_cpu.program()[pcBefore].toString()
                    << "\n";
        } else {
            stepLog << "Instruction = <PC out of range>\n";
        }
    } else if (g_mode == StudioMode::Binary) {
        stepLog << "Instruction = "
                << currentBinaryInstructionText()
                << "\n";
    } else {
        stepLog << "Instruction = <none>\n";
    }

    g_cpu.step();

    stepLog << "PC after = "
            << g_cpu.state().pc()
            << "\n";

    if (g_cpu.state().hasError()) {
        stepLog << "Error = "
                << g_cpu.state().errorMessage()
                << "\n";
    }

    if (g_cpu.state().halted()) {
        stepLog << "\n";
        stepLog << makeFinalCheckView();
    }

    appendTraceText(stepLog.str());
    refreshStateView();
}

void onRunClicked() {
    if (!g_programLoaded) {
        if (!autoLoadFromInputPath()) {
            return;
        }
    }

    std::ostringstream runLog;
    runLog << "\n[Studio Run]\n";
    runLog << "Mode = " << modeToString(g_mode) << "\n";

    std::size_t stepCount = 0;

    while (!g_cpu.state().halted()) {
        const std::size_t pcBefore = g_cpu.state().pc();

        if (hasBreakpoint(pcBefore)) {
            runLog << "Hit breakpoint at PC="
                   << pcBefore
                   << ". Execution paused before instruction.\n";
            break;
        }

        runLog << "Step " << stepCount
               << " | PC=" << pcBefore;

        if (g_mode == StudioMode::Assembly) {
            if (pcBefore < g_cpu.program().size()) {
                runLog << " | "
                       << g_cpu.program()[pcBefore].toString();
            } else {
                runLog << " | <PC out of range>";
            }
        } else if (g_mode == StudioMode::Binary) {
            runLog << " | "
                   << currentBinaryInstructionText();
        } else {
            runLog << " | <none>";
        }

        runLog << "\n";

        g_cpu.step();

        if (g_cpu.state().hasError()) {
            runLog << "Execution failed: "
                   << g_cpu.state().errorMessage()
                   << "\n";
            break;
        }

        ++stepCount;

        if (stepCount > 1000) {
            runLog << "Step limit reached.\n";
            break;
        }
    }

    if (!g_cpu.state().hasError() && g_cpu.state().halted()) {
        runLog << "Execution finished successfully.\n";
    }

    runLog << "\n";
    runLog << makeFinalCheckView();

    appendTraceText(runLog.str());
    refreshStateView();
}

void onAddBreakpointClicked() {
    if (!g_programLoaded) {
        if (!autoLoadFromInputPath()) {
            return;
        }
    }

    const std::string text = getWindowTextString(g_breakpointEdit);
    std::size_t pc = g_cpu.state().pc();

    if (!text.empty()) {
        if (!parseSizeT(text, pc)) {
            appendTraceText("\nInvalid breakpoint PC. Use a decimal address.\n");
            refreshStateView();
            return;
        }
    }

    addBreakpoint(pc);

    std::ostringstream oss;
    oss << "\nAdded breakpoint at PC=" << pc << "\n";
    appendTraceText(oss.str());

    refreshStateView();
}

void onClearBreakpointsClicked() {
    g_breakpoints.clear();

    if (g_breakpointEdit != nullptr) {
        SetWindowTextA(g_breakpointEdit, "");
    }

    appendTraceText("\nCleared all breakpoints.\n");
    refreshStateView();
}

void onResetClicked() {
    g_cpu.reset();
    configureSystemDevices();
    g_mode = StudioMode::None;
    g_programLoaded = false;
    g_loadedPath.clear();
    g_breakpoints.clear();

    SetWindowTextA(g_inputEdit, kDefaultSourcePath);
    SetWindowTextA(g_outputEdit, kDefaultBinaryPath);

    if (g_breakpointEdit != nullptr) {
        SetWindowTextA(g_breakpointEdit, "");
    }

    try {
        setEditText(g_sourceEdit, readTextFile(kDefaultSourcePath));
    } catch (...) {
        setEditText(g_sourceEdit, "");
    }

    setEditText(
        g_traceEdit,
        "Zero-CPU Studio v0.9\n"
        "\n"
        "Ready.\n"
        "Source editor added.\n"
        "System panel added.\n"
        "BIO-OS runner added.\n"
        "Studio BIO-OS runner uses BioOSRunner.\n"
        "\n"
        "Workflow:\n"
        "  1. Edit .zasm in Source Editor\n"
        "  2. [Save Source] or [Assemble] saves it\n"
        "  3. [Assemble] .zasm -> .zbin\n"
        "  4. [Load Binary]\n"
        "  5. [Step] or [Run]\n"
        "  6. [Run BIO-OS] runs examples\\\\bio_os\n"
        "\n"
        "Breakpoints:\n"
        "  - Type a PC and click [Add BP]\n"
        "  - Empty BP field uses current PC\n"
    );

    refreshStateView();
}

HFONT createDefaultFont() {
    return CreateFontA(
        18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Consolas"
    );
}

void applyFont(HWND hwnd, HFONT font) {
    SendMessageA(
        hwnd,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE
    );
}

LRESULT CALLBACK windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    static HFONT font = nullptr;

    switch (message) {
    case WM_CREATE: {
        font = createDefaultFont();

        CreateWindowExA(
            0,
            "STATIC",
            "Input source/binary path:",
            WS_CHILD | WS_VISIBLE,
            20,
            14,
            260,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_inputEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            kDefaultSourcePath,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            20,
            40,
            500,
            30,
            hwnd,
            controlId(kIdInputEdit),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Output .zbin path:",
            WS_CHILD | WS_VISIBLE,
            540,
            14,
            220,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_outputEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            kDefaultBinaryPath,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            540,
            40,
            360,
            30,
            hwnd,
            controlId(kIdOutputEdit),
            nullptr,
            nullptr
        );

        g_loadSourceButton = CreateWindowExA(
            0,
            "BUTTON",
            "Load Source",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            920,
            40,
            115,
            30,
            hwnd,
            controlId(kIdLoadSourceButton),
            nullptr,
            nullptr
        );

        g_saveSourceButton = CreateWindowExA(
            0,
            "BUTTON",
            "Save Source",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1045,
            40,
            115,
            30,
            hwnd,
            controlId(kIdSaveSourceButton),
            nullptr,
            nullptr
        );

        g_assembleButton = CreateWindowExA(
            0,
            "BUTTON",
            "Assemble",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1170,
            40,
            100,
            30,
            hwnd,
            controlId(kIdAssembleButton),
            nullptr,
            nullptr
        );

        g_loadAssemblyButton = CreateWindowExA(
            0,
            "BUTTON",
            "Load ASM",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1280,
            40,
            90,
            30,
            hwnd,
            controlId(kIdLoadAssemblyButton),
            nullptr,
            nullptr
        );

        g_loadBinaryButton = CreateWindowExA(
            0,
            "BUTTON",
            "Load BIN",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20,
            80,
            100,
            30,
            hwnd,
            controlId(kIdLoadBinaryButton),
            nullptr,
            nullptr
        );

        g_stepButton = CreateWindowExA(
            0,
            "BUTTON",
            "Step",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            130,
            80,
            80,
            30,
            hwnd,
            controlId(kIdStepButton),
            nullptr,
            nullptr
        );

        g_runButton = CreateWindowExA(
            0,
            "BUTTON",
            "Run",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            220,
            80,
            80,
            30,
            hwnd,
            controlId(kIdRunButton),
            nullptr,
            nullptr
        );

        g_resetButton = CreateWindowExA(
            0,
            "BUTTON",
            "Reset",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            310,
            80,
            80,
            30,
            hwnd,
            controlId(kIdResetButton),
            nullptr,
            nullptr
        );


        CreateWindowExA(
            0,
            "STATIC",
            "BP PC:",
            WS_CHILD | WS_VISIBLE,
            420,
            84,
            60,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_breakpointEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            480,
            80,
            100,
            30,
            hwnd,
            controlId(kIdBreakpointEdit),
            nullptr,
            nullptr
        );

        g_addBreakpointButton = CreateWindowExA(
            0,
            "BUTTON",
            "Add BP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            590,
            80,
            90,
            30,
            hwnd,
            controlId(kIdAddBreakpointButton),
            nullptr,
            nullptr
        );

        g_clearBreakpointsButton = CreateWindowExA(
            0,
            "BUTTON",
            "Clear BP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            690,
            80,
            95,
            30,
            hwnd,
            controlId(kIdClearBreakpointsButton),
            nullptr,
            nullptr
        );

        g_runBioOSButton = CreateWindowExA(
            0,
            "BUTTON",
            "Run BIO-OS",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            800,
            80,
            130,
            30,
            hwnd,
            controlId(kIdRunBioOSButton),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Source Editor (.zasm):",
            WS_CHILD | WS_VISIBLE,
            20,
            120,
            240,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_sourceEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
                WS_VISIBLE |
                WS_VSCROLL |
                WS_HSCROLL |
                ES_LEFT |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_AUTOHSCROLL,
            20,
            146,
            450,
            680,
            hwnd,
            controlId(kIdSourceEdit),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "CPU / Register / Memory View:",
            WS_CHILD | WS_VISIBLE,
            490,
            120,
            320,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_stateEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
                WS_VISIBLE |
                WS_VSCROLL |
                WS_HSCROLL |
                ES_LEFT |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_AUTOHSCROLL |
                ES_READONLY,
            490,
            146,
            460,
            680,
            hwnd,
            controlId(kIdStateEdit),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Trace / Execution Log:",
            WS_CHILD | WS_VISIBLE,
            970,
            120,
            320,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_traceEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
                WS_VISIBLE |
                WS_VSCROLL |
                WS_HSCROLL |
                ES_LEFT |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_AUTOHSCROLL |
                ES_READONLY,
            970,
            146,
            480,
            680,
            hwnd,
            controlId(kIdTraceEdit),
            nullptr,
            nullptr
        );

        applyFont(g_inputEdit, font);
        applyFont(g_outputEdit, font);
        applyFont(g_loadSourceButton, font);
        applyFont(g_saveSourceButton, font);
        applyFont(g_assembleButton, font);
        applyFont(g_loadAssemblyButton, font);
        applyFont(g_loadBinaryButton, font);
        applyFont(g_stepButton, font);
        applyFont(g_runButton, font);
        applyFont(g_resetButton, font);
        applyFont(g_breakpointEdit, font);
        applyFont(g_addBreakpointButton, font);
        applyFont(g_clearBreakpointsButton, font);
        applyFont(g_runBioOSButton, font);
        applyFont(g_sourceEdit, font);
        applyFont(g_stateEdit, font);
        applyFont(g_traceEdit, font);

        onResetClicked();

        return 0;
    }

    case WM_COMMAND: {
        const int controlIdValue = LOWORD(wParam);

        if (controlIdValue == kIdLoadSourceButton) {
            onLoadSourceClicked();
            return 0;
        }

        if (controlIdValue == kIdSaveSourceButton) {
            onSaveSourceClicked();
            return 0;
        }

        if (controlIdValue == kIdAssembleButton) {
            onAssembleClicked();
            return 0;
        }

        if (controlIdValue == kIdLoadAssemblyButton) {
            onLoadAssemblyClicked();
            return 0;
        }

        if (controlIdValue == kIdLoadBinaryButton) {
            onLoadBinaryClicked();
            return 0;
        }

        if (controlIdValue == kIdStepButton) {
            onStepClicked();
            return 0;
        }

        if (controlIdValue == kIdRunButton) {
            onRunClicked();
            return 0;
        }

        if (controlIdValue == kIdResetButton) {
            onResetClicked();
            return 0;
        }

        if (controlIdValue == kIdAddBreakpointButton) {
            onAddBreakpointClicked();
            return 0;
        }

        if (controlIdValue == kIdClearBreakpointsButton) {
            onClearBreakpointsClicked();
            return 0;
        }

        if (controlIdValue == kIdRunBioOSButton) {
            onRunBioOSClicked();
            return 0;
        }

        break;
    }

    case WM_DESTROY:
        if (font != nullptr) {
            DeleteObject(font);
            font = nullptr;
        }

        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand
) {
    const char* className = "ZeroCPUStudioWindowClass";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(
            nullptr,
            "Failed to register window class.",
            "Zero-CPU Studio Error",
            MB_ICONERROR
        );
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        0,
        className,
        "Zero-CPU Studio",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (hwnd == nullptr) {
        MessageBoxA(
            nullptr,
            "Failed to create main window.",
            "Zero-CPU Studio Error",
            MB_ICONERROR
        );
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message = {};

    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return static_cast<int>(message.wParam);
}
