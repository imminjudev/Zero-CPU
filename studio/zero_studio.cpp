#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/system/BioOSRunner.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"
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
constexpr int kWindowHeight = 900;

constexpr int kScrollableContentHeight = 1060;
constexpr int kScrollLineSize = 40;
constexpr int kScrollPageSize = 240;

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
constexpr int kIdDatapathCanvas = 1018;
constexpr int kIdExportTraceButton = 1019;

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kBinaryMemoryPreviewStart = 512;
constexpr std::size_t kBinaryMemoryPreviewCount = 96;

constexpr const char* kDefaultSourcePath = "examples\\debugger_showcase.zasm";
constexpr const char* kDefaultBinaryPath = "examples\\debugger_showcase.zbin";

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
HWND g_datapathCanvas = nullptr;
HWND g_exportTraceButton = nullptr;

zero_cpu::CPU g_cpu;
std::shared_ptr<zero_cpu::InterruptController> g_interruptController;
std::shared_ptr<zero_cpu::MMIOBus> g_mmioBus;
std::shared_ptr<zero_cpu::DebugOutputDevice> g_debugOutputDevice;
std::shared_ptr<zero_cpu::TimerDevice> g_timerDevice;
StudioMode g_mode = StudioMode::None;
bool g_programLoaded = false;
std::string g_loadedPath;
std::vector<std::size_t> g_breakpoints;
int g_scrollY = 0;

int getMainScrollMax(HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    const int clientHeight = clientRect.bottom - clientRect.top;

    if (kScrollableContentHeight <= clientHeight) {
        return 0;
    }

    return kScrollableContentHeight - clientHeight;
}

int clampMainScroll(HWND hwnd, int value) {
    const int maxScroll = getMainScrollMax(hwnd);

    if (value < 0) {
        return 0;
    }

    if (value > maxScroll) {
        return maxScroll;
    }

    return value;
}

void updateMainScrollBar(HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    SCROLLINFO info = {};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = kScrollableContentHeight - 1;
    info.nPage = static_cast<UINT>(clientRect.bottom - clientRect.top);
    info.nPos = g_scrollY;

    SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
}

void setMainScrollPosition(HWND hwnd, int requestedScrollY) {
    const int newScrollY = clampMainScroll(hwnd, requestedScrollY);
    const int deltaY = g_scrollY - newScrollY;

    if (deltaY == 0) {
        updateMainScrollBar(hwnd);

        RedrawWindow(
            hwnd,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE
        );

        return;
    }

    g_scrollY = newScrollY;

    ScrollWindowEx(
        hwnd,
        0,
        deltaY,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE
    );

    updateMainScrollBar(hwnd);

    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW
    );
}



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

std::string modeToString(StudioMode mode);
void refreshStateView();

constexpr const char* kDefaultTraceExportPath = "traces\\studio_trace_export.json";

std::string jsonEscape(const std::string& text) {
    std::ostringstream oss;

    for (const unsigned char ch : text) {
        switch (ch) {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (ch < 0x20) {
                oss << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<int>(ch)
                    << std::dec
                    << std::setfill(' ');
            } else {
                oss << static_cast<char>(ch);
            }
            break;
        }
    }

    return oss.str();
}

void jsonIndent(std::ostringstream& oss, int spaces) {
    for (int i = 0; i < spaces; ++i) {
        oss << ' ';
    }
}

void jsonStringField(
    std::ostringstream& oss,
    int indent,
    const std::string& name,
    const std::string& value,
    bool comma = true
) {
    jsonIndent(oss, indent);
    oss << "\""
        << jsonEscape(name)
        << "\": \""
        << jsonEscape(value)
        << "\"";

    if (comma) {
        oss << ",";
    }

    oss << "\n";
}

void jsonSizeField(
    std::ostringstream& oss,
    int indent,
    const std::string& name,
    std::size_t value,
    bool comma = true
) {
    jsonIndent(oss, indent);
    oss << "\""
        << jsonEscape(name)
        << "\": "
        << value;

    if (comma) {
        oss << ",";
    }

    oss << "\n";
}

void jsonBoolField(
    std::ostringstream& oss,
    int indent,
    const std::string& name,
    bool value,
    bool comma = true
) {
    jsonIndent(oss, indent);
    oss << "\""
        << jsonEscape(name)
        << "\": "
        << (value ? "true" : "false");

    if (comma) {
        oss << ",";
    }

    oss << "\n";
}

void appendTraceEventJson(
    std::ostringstream& oss,
    const zero_cpu::TraceEvent& event,
    std::size_t index,
    bool comma
) {
    jsonIndent(oss, 4);
    oss << "{\n";

    jsonSizeField(oss, 6, "index", index);
    jsonSizeField(oss, 6, "pc_before", event.pcBefore());
    jsonSizeField(oss, 6, "pc_after", event.pcAfter());
    jsonStringField(oss, 6, "instruction", event.instruction().toString());
    jsonStringField(oss, 6, "stage", event.stage());
    jsonStringField(oss, 6, "action", event.action());
    jsonStringField(oss, 6, "datapath", event.datapathString());
    jsonSizeField(oss, 6, "changed_register_count", event.changedRegisters().size());
    jsonSizeField(oss, 6, "changed_flag_count", event.changedFlags().size());
    jsonSizeField(oss, 6, "changed_memory_count", event.changedMemory().size());
    jsonStringField(oss, 6, "alu_detail", event.aluDetailString());
    jsonStringField(oss, 6, "memory_detail", event.memoryDetailString());
    jsonStringField(oss, 6, "stack_detail", event.stackDetailString());
    jsonStringField(oss, 6, "control_flow_detail", event.controlFlowDetailString());
    jsonStringField(oss, 6, "compact", event.toCompactString());
    jsonStringField(oss, 6, "full", event.toFullString());
    jsonBoolField(oss, 6, "has_error", event.hasError());
    jsonStringField(oss, 6, "error", event.errorMessage(), false);

    jsonIndent(oss, 4);
    oss << "}";

    if (comma) {
        oss << ",";
    }

    oss << "\n";
}

std::string makeTraceExportJson() {
    std::ostringstream oss;
    const auto& events = g_cpu.traceLogger().events();

    oss << "{\n";
    jsonStringField(oss, 2, "schema", "zero_cpu_studio_trace");
    jsonSizeField(oss, 2, "schema_version", 1);
    jsonStringField(oss, 2, "studio_version", "v0.25");
    jsonStringField(oss, 2, "mode", modeToString(g_mode));
    jsonStringField(oss, 2, "loaded_path", g_loadedPath);
    jsonSizeField(oss, 2, "event_count", events.size());

    jsonIndent(oss, 2);
    oss << "\"events\": [\n";

    for (std::size_t i = 0; i < events.size(); ++i) {
        appendTraceEventJson(oss, events[i], i, i + 1 < events.size());
    }

    jsonIndent(oss, 2);
    oss << "]\n";
    oss << "}\n";

    return oss.str();
}

void exportTraceToJsonFile(const std::string& path) {
    CreateDirectoryA("traces", nullptr);

    std::ofstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to open trace export file: " + path);
    }

    file << makeTraceExportJson();
}

void onExportTraceClicked() {
    const auto& events = g_cpu.traceLogger().events();

    if (events.empty()) {
        appendTraceText(
            "\nExport Trace failed: no TraceEvent recorded yet.\n"
            "Run or Step a loaded program first.\n"
        );
        refreshStateView();
        return;
    }

    try {
        exportTraceToJsonFile(kDefaultTraceExportPath);

        std::ostringstream oss;
        oss << "\n[Export Trace]\n";
        oss << "Path = " << kDefaultTraceExportPath << "\n";
        oss << "Events = " << events.size() << "\n";
        oss << "Format = JSON\n";
        appendTraceText(oss.str());
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << "\n[Export Trace Failed]\n";
        oss << "Path = " << kDefaultTraceExportPath << "\n";
        oss << "Error = " << ex.what() << "\n";
        appendTraceText(oss.str());
    }

    refreshStateView();
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

bool timelineEventHasNode(
    const zero_cpu::TraceEvent& event,
    const std::string& nodeName
) {
    const auto& nodes = event.datapathNodes();

    for (const std::string& node : nodes) {
        if (node == nodeName) {
            return true;
        }

        if (nodeName == "Memory/MMIO" && node == "Memory") {
            return true;
        }

        if (nodeName == "Memory" && node == "Memory/MMIO") {
            return true;
        }
    }

    return false;
}

std::string pipelineStatusText(bool active) {
    return active ? "[ACTIVE]" : "[idle]";
}

std::string studioHexAddress(std::size_t value) {
    std::ostringstream oss;

    oss << "0x"
        << std::uppercase
        << std::hex
        << std::setw(4)
        << std::setfill('0')
        << value
        << std::dec
        << std::setfill(' ');

    return oss.str();
}

std::string studioAddressRange(
    std::size_t begin,
    std::size_t endExclusive
) {
    std::ostringstream oss;

    oss << "["
        << studioHexAddress(begin)
        << ".."
        << studioHexAddress(endExclusive - 1)
        << "]";

    return oss.str();
}

std::string classifyStudioAddress(std::size_t address) {
    using namespace zero_cpu::memory_map;

    if (isLowMemoryAddress(address)) {
        return "Low Data / Scratch";
    }

    if (isDebugOutputAddress(address)) {
        return "DebugOutput MMIO";
    }

    if (isTimerAddress(address)) {
        return "Timer MMIO";
    }

    if (isBioOSStackAddress(address)) {
        return "BIO-OS Stack";
    }

    if (isBioOSCodeAddress(address)) {
        return "Program Code / BIO-OS Code Window";
    }

    if (address >= kDefaultMemorySize && address < kDebugOutputBase) {
        return "Outside default RAM / reserved gap";
    }

    if (isMmioAddress(address)) {
        return "MMIO";
    }

    return "Unknown / unmapped";
}

std::string makeMemoryMapViewer() {
    using namespace zero_cpu::memory_map;

    std::ostringstream oss;

    const std::size_t pc = g_cpu.state().pc();
    const std::size_t sp = g_cpu.state().sp();

    oss << "Memory Map Viewer\n";

    oss << "Current PC = "
        << studioHexAddress(pc)
        << " ("
        << pc
        << ") -> "
        << classifyStudioAddress(pc)
        << "\n";

    oss << "Current SP = "
        << studioHexAddress(sp)
        << " ("
        << sp
        << ") -> "
        << classifyStudioAddress(sp)
        << "\n";

    oss << "\n";
    oss << "Core RAM Layout\n";

    oss << "  "
        << studioAddressRange(kLowMemoryBase, kLowMemoryEndExclusive)
        << " Low Data / Scratch\n";

    oss << "  "
        << studioAddressRange(kBinaryCodeBase, kDefaultStackBase)
        << " Default Program Code Window\n";

    oss << "  "
        << studioHexAddress(kDefaultStackBase)
        << " Default Stack Base\n";

    oss << "  "
        << studioAddressRange(kBinaryCodeBase, kBioOSStackBase)
        << " BIO-OS Combined Code Window\n";

    oss << "  "
        << studioAddressRange(kBioOSStackBase, kDefaultMemorySize)
        << " BIO-OS Stack Window\n";

    oss << "  "
        << studioAddressRange(kDefaultMemorySize, kDebugOutputBase)
        << " Outside default RAM / reserved gap\n";

    oss << "\n";
    oss << "MMIO Layout\n";

    oss << "  "
        << studioAddressRange(kDebugOutputBase, kDebugOutputEndExclusive)
        << " DebugOutputDevice\n";

    oss << "  "
        << studioAddressRange(kTimerBase, kTimerEndExclusive)
        << " TimerDevice\n";

    oss << "\n";
    oss << "Timer MMIO Registers\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerTickCountOffset)
        << " tick_count\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerIntervalOffset)
        << " interval\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerEnabledOffset)
        << " enabled\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerVectorOffset)
        << " vector\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerPayloadOffset)
        << " payload\n";

    oss << "  "
        << studioHexAddress(kTimerBase + kTimerInterruptCountOffset)
        << " interrupt_count\n";

    return oss.str();
}


std::string makeRecentInstructionTraceView() {
    std::ostringstream oss;

    oss << "Recent Instruction Trace\n";

    const auto& events = g_cpu.traceLogger().events();

    if (events.empty()) {
        oss << "No TraceEvent recorded yet.\n";
        return oss.str();
    }

    constexpr std::size_t kMaxRecentTraceEvents = 16;

    const std::size_t eventCount = events.size();

    const std::size_t start =
        eventCount > kMaxRecentTraceEvents
            ? eventCount - kMaxRecentTraceEvents
            : 0;

    oss << "Showing last "
        << (eventCount - start)
        << " of "
        << eventCount
        << " events\n";

    for (std::size_t i = start; i < eventCount; ++i) {
        const auto& event = events[i];

        oss << "["
            << i
            << "] PC "
            << event.pcBefore()
            << " -> "
            << event.pcAfter()
            << " | "
            << event.instruction().toString()
            << " | "
            << event.action();

        if (event.hasError()) {
            oss << " | ERROR: "
                << event.errorMessage();
        }

        oss << "\n";
    }

    return oss.str();
}


std::string makeExecutionDetailProbeView() {
    std::ostringstream oss;

    oss << "Execution Detail Probe\n";

    const auto& events = g_cpu.traceLogger().events();

    if (events.empty()) {
        oss << "No TraceEvent recorded yet.\n";
        oss << "ALU Detail = waiting\n";
        oss << "Memory Detail = waiting\n";
        oss << "Control Flow Detail = waiting\n";
        oss << "Interrupt Detail = waiting\n";
        return oss.str();
    }

    const auto& event = events.back();

    const bool aluActive = timelineEventHasNode(event, "ALU");
    const bool memoryActive =
        timelineEventHasNode(event, "Memory/MMIO") ||
        timelineEventHasNode(event, "Memory") ||
        !event.changedMemory().empty();

    const bool stackActive = timelineEventHasNode(event, "Stack");

    const std::string action = event.action();
    const std::string instructionText = event.instruction().toString();

    const bool interruptActive =
        timelineEventHasNode(event, "InterruptController") ||
        action.find("INTERRUPT") != std::string::npos ||
        instructionText.find("INT") != std::string::npos ||
        instructionText.find("IRET") != std::string::npos;

    oss << "Instruction = "
        << instructionText
        << "\n";

    oss << "Action = "
        << action
        << "\n";

    oss << "\n";
    oss << "ALU Detail\n";

    if (aluActive) {
        oss << "  Active = true\n";
        oss << "  Operation = "
            << action
            << "\n";

        const auto& aluDetail = event.aluDetail();

        if (aluDetail.active) {
            oss << "  Operand Snapshot\n";

            if (aluDetail.has_lhs) {
                oss << "    lhs "
                    << aluDetail.lhs_text
                    << " = "
                    << aluDetail.lhs
                    << "\n";
            } else if (!aluDetail.lhs_text.empty()) {
                oss << "    lhs "
                    << aluDetail.lhs_text
                    << " = <unavailable>\n";
            }

            if (aluDetail.has_rhs) {
                oss << "    rhs "
                    << aluDetail.rhs_text
                    << " = "
                    << aluDetail.rhs
                    << "\n";
            } else if (!aluDetail.rhs_text.empty()) {
                oss << "    rhs "
                    << aluDetail.rhs_text
                    << " = <unavailable>\n";
            }

            if (aluDetail.has_result) {
                oss << "    result";

                if (!aluDetail.destination.empty()) {
                    oss << " -> "
                        << aluDetail.destination;
                }

                oss << " = "
                    << aluDetail.result
                    << "\n";
            }

            if (!aluDetail.note.empty()) {
                oss << "    note = "
                    << aluDetail.note
                    << "\n";
            }
        }

        if (event.changedRegisters().empty()) {
            oss << "  Register Result = none\n";
        } else {
            oss << "  Register Result\n";

            for (const auto& change : event.changedRegisters()) {
                oss << "    "
                    << change.name
                    << ": "
                    << change.before
                    << " -> "
                    << change.after
                    << "\n";
            }
        }

        if (event.changedFlags().empty()) {
            oss << "  Flag Result = none\n";
        } else {
            oss << "  Flag Result\n";

            for (const auto& change : event.changedFlags()) {
                oss << "    "
                    << change.name
                    << ": "
                    << (change.before ? 1 : 0)
                    << " -> "
                    << (change.after ? 1 : 0)
                    << "\n";
            }
        }
    } else {
        oss << "  Active = false\n";
        oss << "  Operation = none\n";
    }

    oss << "\n";
    oss << "Memory Detail\n";

    if (memoryActive || stackActive) {
        oss << "  Active = true\n";
        oss << "  Route = "
            << (stackActive ? "Stack" : "Memory/MMIO")
            << "\n";

        const auto& memoryDetail = event.memoryDetail();

        if (memoryDetail.active) {
            oss << "  Operation Detail\n";
            oss << "    operation = "
                << memoryDetail.operation
                << "\n";

            if (memoryDetail.has_address) {
                oss << "    address "
                    << memoryDetail.address_text
                    << " = "
                    << memoryDetail.address
                    << "\n";
            } else if (!memoryDetail.address_text.empty()) {
                oss << "    address "
                    << memoryDetail.address_text
                    << " = <unavailable>\n";
            }

            if (!memoryDetail.route.empty()) {
                oss << "    route = "
                    << memoryDetail.route
                    << "\n";
            }

            if (memoryDetail.has_value) {
                oss << "    value";

                if (!memoryDetail.value_text.empty()) {
                    oss << " "
                        << memoryDetail.value_text;
                }

                oss << " = "
                    << memoryDetail.value
                    << "\n";
            }

            if (!memoryDetail.destination.empty()) {
                oss << "    destination = "
                    << memoryDetail.destination
                    << "\n";
            }

            if (!memoryDetail.note.empty()) {
                oss << "    note = "
                    << memoryDetail.note
                    << "\n";
            }
        }

        if (event.changedMemory().empty()) {
            oss << "  Memory Mutations = none\n";
            oss << "  Note = memory path may still be active for reads.\n";
        } else {
            oss << "  Memory Mutations = "
                << event.changedMemory().size()
                << "\n";

            constexpr std::size_t kMaxProbeMemoryChanges = 8;
            const std::size_t memoryChangeCount = event.changedMemory().size();

            const std::size_t visibleCount =
                memoryChangeCount < kMaxProbeMemoryChanges
                    ? memoryChangeCount
                    : kMaxProbeMemoryChanges;

            for (std::size_t i = 0; i < visibleCount; ++i) {
                const auto& change = event.changedMemory()[i];

                oss << "    Memory["
                    << change.address
                    << "]: "
                    << change.before
                    << " -> "
                    << change.after
                    << "\n";
            }

            if (memoryChangeCount > visibleCount) {
                oss << "    ... "
                    << (memoryChangeCount - visibleCount)
                    << " more memory changes hidden\n";
            }
        }
    } else {
        oss << "  Active = false\n";
        oss << "  Route = none\n";
    }

    oss << "\n";
    oss << "Stack Detail\n";

    const auto& stackDetail = event.stackDetail();

    if (stackDetail.active) {
        oss << "  Active = true\n";
        oss << "  Operation = "
            << stackDetail.operation
            << "\n";

        if (!stackDetail.operand_text.empty()) {
            oss << "  Operand = "
                << stackDetail.operand_text
                << "\n";
        }

        if (stackDetail.has_stack_address) {
            oss << "  Stack Address = "
                << stackDetail.stack_address
                << "\n";
        }

        if (stackDetail.has_sp_change) {
            oss << "  SP = "
                << stackDetail.sp_before
                << " -> "
                << stackDetail.sp_after
                << "\n";
        }

        if (stackDetail.has_value) {
            oss << "  Value = "
                << stackDetail.value
                << "\n";
        }

        if (stackDetail.has_return_address) {
            oss << "  Return Address = "
                << stackDetail.return_address
                << "\n";
        }

        if (!stackDetail.destination.empty()) {
            oss << "  Destination = "
                << stackDetail.destination
                << "\n";
        }

        if (stackDetail.has_target) {
            oss << "  Target = "
                << stackDetail.target
                << "\n";
        }

        if (!stackDetail.note.empty()) {
            oss << "  Note = "
                << stackDetail.note
                << "\n";
        }
    } else {
        oss << "  Active = false\n";
        oss << "  Operation = none\n";
    }

    oss << "\n";
    oss << "Control Flow Detail\n";

    const auto& controlFlowDetail = event.controlFlowDetail();

    if (controlFlowDetail.active) {
        oss << "  Active = true\n";
        oss << "  Operation = "
            << controlFlowDetail.operation
            << "\n";

        if (!controlFlowDetail.operand_text.empty()) {
            oss << "  Operand = "
                << controlFlowDetail.operand_text
                << "\n";
        }

        oss << "  From PC = "
            << controlFlowDetail.from_pc
            << "\n";

        oss << "  To PC = "
            << controlFlowDetail.to_pc
            << "\n";

        if (controlFlowDetail.has_condition) {
            oss << "  Condition = "
                << controlFlowDetail.condition
                << "\n";
        }

        if (controlFlowDetail.has_taken) {
            oss << "  Taken = "
                << (controlFlowDetail.taken ? "true" : "false")
                << "\n";
        }

        if (controlFlowDetail.has_target) {
            oss << "  Target = "
                << controlFlowDetail.target
                << "\n";
        }

        if (controlFlowDetail.has_fallthrough) {
            oss << "  Fallthrough = "
                << controlFlowDetail.fallthrough
                << "\n";
        }

        if (controlFlowDetail.has_return_address) {
            oss << "  Return Address = "
                << controlFlowDetail.return_address
                << "\n";
        }

        if (controlFlowDetail.has_vector) {
            oss << "  Vector = "
                << controlFlowDetail.vector
                << "\n";
        }

        if (!controlFlowDetail.note.empty()) {
            oss << "  Note = "
                << controlFlowDetail.note
                << "\n";
        }
    } else {
        oss << "  Active = false\n";
        oss << "  Operation = none\n";
    }

    oss << "\n";
    oss << "Interrupt Detail\n";

    if (interruptActive) {
        oss << "  Active = true\n";
        oss << "  Action = "
            << action
            << "\n";
        oss << "  Instruction = "
            << instructionText
            << "\n";

        if (action == "SOFTWARE_INTERRUPT") {
            oss << "  Kind = software interrupt\n";
        } else if (action == "HARDWARE_INTERRUPT") {
            oss << "  Kind = hardware interrupt\n";
        } else if (action == "INTERRUPT_RETURN") {
            oss << "  Kind = interrupt return\n";
        } else {
            oss << "  Kind = interrupt-related control flow\n";
        }
    } else {
        oss << "  Active = false\n";
        oss << "  Action = none\n";
    }

    oss << "\n";
    oss << "Writeback Summary\n";

    if (event.changedRegisters().empty() && event.changedFlags().empty()) {
        oss << "  No register/flag writeback.\n";
    } else {
        if (!event.changedRegisters().empty()) {
            oss << "  Registers = "
                << event.changedRegisters().size()
                << "\n";
        }

        if (!event.changedFlags().empty()) {
            oss << "  Flags = "
                << event.changedFlags().size()
                << "\n";
        }
    }

    return oss.str();
}


std::string makePipelineTimelineView() {
    std::ostringstream oss;

    oss << "Pipeline Timeline\n";

    const auto& events = g_cpu.traceLogger().events();

    if (events.empty()) {
        oss << "No TraceEvent recorded yet.\n";
        oss << "[FETCH]     waiting for PC\n";
        oss << "[DECODE]    waiting for instruction\n";
        oss << "[EXECUTE]   waiting for operation\n";
        oss << "[MEMORY]    waiting for memory/bus activity\n";
        oss << "[WRITEBACK] waiting for state update\n";
        return oss.str();
    }

    const auto& event = events.back();

    const bool hasMemory =
        timelineEventHasNode(event, "Memory/MMIO") ||
        timelineEventHasNode(event, "Memory") ||
        timelineEventHasNode(event, "Stack");

    const bool hasWriteback =
        timelineEventHasNode(event, "Writeback") ||
        !event.changedRegisters().empty();

    const bool hasFlags = !event.changedFlags().empty();

    oss << "Instruction = "
        << event.instruction().toString()
        << "\n";

    oss << "Action = "
        << event.action()
        << "\n";

    oss << "PC = "
        << event.pcBefore()
        << " -> "
        << event.pcAfter()
        << "\n";

    if (event.hasError()) {
        oss << "Error = "
            << event.errorMessage()
            << "\n";
    }

    oss << "\n";

    oss << "[FETCH]     "
        << pipelineStatusText(true)
        << " PC "
        << event.pcBefore()
        << " reads instruction"
        << "\n";

    oss << "[DECODE]    "
        << pipelineStatusText(true)
        << " "
        << event.instruction().toString()
        << " -> opcode/operands"
        << "\n";

    oss << "[EXECUTE]   "
        << pipelineStatusText(true)
        << " "
        << event.action()
        << "\n";

    oss << "[MEMORY]    "
        << pipelineStatusText(hasMemory)
        << " ";

    if (hasMemory) {
        if (timelineEventHasNode(event, "Stack")) {
            oss << "stack access";
        } else {
            oss << "memory/MMIO access";
        }
    } else {
        oss << "no memory access";
    }

    oss << "\n";

    oss << "[WRITEBACK] "
        << pipelineStatusText(hasWriteback || hasFlags)
        << " ";

    if (hasWriteback) {
        if (!event.changedRegisters().empty()) {
            bool first = true;

            for (const auto& change : event.changedRegisters()) {
                if (!first) {
                    oss << ", ";
                }

                oss << change.name
                    << ": "
                    << change.before
                    << " -> "
                    << change.after;

                first = false;
            }
        } else {
            oss << "state updated";
        }
    } else if (hasFlags) {
        oss << "flags updated";
    } else {
        oss << "no register writeback";
    }

    oss << "\n";

    if (hasFlags) {
        oss << "[FLAGS]     [ACTIVE] ";

        bool first = true;

        for (const auto& change : event.changedFlags()) {
            if (!first) {
                oss << ", ";
            }

            oss << change.name
                << ": "
                << (change.before ? 1 : 0)
                << " -> "
                << (change.after ? 1 : 0);

            first = false;
        }

        oss << "\n";
    }

    return oss.str();
}


std::string makeVisualDatapathView() {
    std::ostringstream oss;

    oss << "Visual Datapath Panel\n";

    const auto& events = g_cpu.traceLogger().events();

    oss << "Trace Events = "
        << events.size()
        << "\n";

    if (events.empty()) {
        oss << "Last Event = <none>\n";
        oss << "Hint = Step or Run a program to generate TraceEvent data.\n";
        return oss.str();
    }

    const auto& event = events.back();

    oss << "Instruction = "
        << event.instruction().toString()
        << "\n";

    oss << "PC = "
        << event.pcBefore()
        << " -> "
        << event.pcAfter()
        << "\n";

    oss << "Stage = "
        << event.stage()
        << "\n";

    oss << "Action = "
        << event.action()
        << "\n";

    oss << "Path = "
        << event.datapathString()
        << "\n";

    if (event.hasError()) {
        oss << "Error = "
            << event.errorMessage()
            << "\n";
    }

    oss << "\n";
    oss << "Datapath Nodes\n";

    const auto& nodes = event.datapathNodes();

    if (nodes.empty()) {
        oss << "  None\n";
    } else {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            oss << "  ["
                << i
                << "] "
                << nodes[i]
                << "\n";
        }
    }

    oss << "\n";
    oss << "Register Changes\n";

    if (event.changedRegisters().empty()) {
        oss << "  None\n";
    } else {
        for (const auto& change : event.changedRegisters()) {
            oss << "  "
                << change.name
                << ": "
                << change.before
                << " -> "
                << change.after
                << "\n";
        }
    }

    oss << "\n";
    oss << "Flag Changes\n";

    if (event.changedFlags().empty()) {
        oss << "  None\n";
    } else {
        for (const auto& change : event.changedFlags()) {
            oss << "  "
                << change.name
                << ": "
                << (change.before ? 1 : 0)
                << " -> "
                << (change.after ? 1 : 0)
                << "\n";
        }
    }

    oss << "\n";
    oss << "Memory Changes\n";

    if (event.changedMemory().empty()) {
        oss << "  None\n";
    } else {
        constexpr std::size_t kMaxVisibleMemoryChanges = 16;
        const std::size_t memoryChangeCount = event.changedMemory().size();

        const std::size_t visibleCount =
            memoryChangeCount < kMaxVisibleMemoryChanges
                ? memoryChangeCount
                : kMaxVisibleMemoryChanges;

        for (std::size_t i = 0; i < visibleCount; ++i) {
            const auto& change = event.changedMemory()[i];

            oss << "  Memory["
                << change.address
                << "]: "
                << change.before
                << " -> "
                << change.after
                << "\n";
        }

        if (event.changedMemory().size() > visibleCount) {
            oss << "  ... "
                << (event.changedMemory().size() - visibleCount)
                << " more memory changes hidden\n";
        }
    }

    return oss.str();
}


bool latestTraceHasNode(const std::string& nodeName) {
    const auto& events = g_cpu.traceLogger().events();

    if (events.empty()) {
        return false;
    }

    const auto& nodes = events.back().datapathNodes();

    for (const std::string& node : nodes) {
        if (node == nodeName) {
            return true;
        }

        if (nodeName == "Memory/MMIO" && node == "Memory") {
            return true;
        }

        if (nodeName == "Memory" && node == "Memory/MMIO") {
            return true;
        }
    }

    return false;
}

void drawCenteredText(
    HDC hdc,
    const RECT& rect,
    const std::string& text
) {
    DrawTextA(
        hdc,
        text.c_str(),
        -1,
        const_cast<RECT*>(&rect),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );
}

void drawArrow(
    HDC hdc,
    int x1,
    int y1,
    int x2,
    int y2,
    bool active
) {
    HPEN pen = CreatePen(
        PS_SOLID,
        active ? 3 : 1,
        active ? RGB(230, 120, 20) : RGB(120, 120, 120)
    );

    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));

    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);

    const int arrowSize = active ? 7 : 5;

    POINT points[3] = {
        {x2, y2},
        {x2 - arrowSize, y2 - arrowSize},
        {x2 - arrowSize, y2 + arrowSize}
    };

    HBRUSH brush = CreateSolidBrush(
        active ? RGB(230, 120, 20) : RGB(120, 120, 120)
    );

    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));

    Polygon(hdc, points, 3);

    SelectObject(hdc, oldBrush);
    DeleteObject(brush);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void drawDatapathBox(
    HDC hdc,
    const RECT& rect,
    const std::string& label,
    bool active
) {
    HBRUSH fillBrush = CreateSolidBrush(
        active ? RGB(255, 225, 120) : RGB(245, 245, 245)
    );

    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, fillBrush));

    HPEN borderPen = CreatePen(
        PS_SOLID,
        active ? 3 : 1,
        active ? RGB(210, 95, 0) : RGB(100, 100, 100)
    );

    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));

    RoundRect(
        hdc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        14,
        14
    );

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(
        hdc,
        active ? RGB(20, 20, 20) : RGB(70, 70, 70)
    );

    drawCenteredText(hdc, rect, label);

    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    SelectObject(hdc, oldBrush);
    DeleteObject(fillBrush);
}

void drawDatapathCanvas(HDC hdc, const RECT& clientRect) {
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(32, 34, 38));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(235, 235, 235));

    RECT titleRect = {
        12,
        8,
        clientRect.right - 12,
        28
    };

    std::string title = "Zero-CPU Visual Datapath";

    const auto& events = g_cpu.traceLogger().events();

    if (!events.empty()) {
        const auto& event = events.back();

        title += " | ";
        title += event.action();
        title += " | ";
        title += event.instruction().toString();
        title += " | PC ";
        title += std::to_string(event.pcBefore());
        title += " -> ";
        title += std::to_string(event.pcAfter());
    } else {
        title += " | waiting for TraceEvent";
    }

    DrawTextA(
        hdc,
        title.c_str(),
        -1,
        &titleRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );

    const int top = 38;
    const int boxWidth = 126;
    const int boxHeight = 32;
    const int gap = 18;

    RECT pc = {20, top, 20 + boxWidth, top + boxHeight};
    RECT imem = {pc.right + gap, top, pc.right + gap + boxWidth, top + boxHeight};
    RECT decoder = {imem.right + gap, top, imem.right + gap + boxWidth, top + boxHeight};
    RECT regfile = {decoder.right + gap, top, decoder.right + gap + boxWidth, top + boxHeight};
    RECT alu = {regfile.right + gap, top, regfile.right + gap + boxWidth, top + boxHeight};
    RECT flags = {alu.right + gap, top, alu.right + gap + boxWidth, top + boxHeight};
    RECT writeback = {flags.right + gap, top, flags.right + gap + boxWidth, top + boxHeight};

    const int lowerTop = 86;

    RECT memory = {regfile.left, lowerTop, regfile.right, lowerTop + boxHeight};
    RECT stack = {alu.left, lowerTop, alu.right, lowerTop + boxHeight};
    RECT interrupt = {flags.left, lowerTop, flags.right + 46, lowerTop + boxHeight};

    drawDatapathBox(hdc, pc, "PC", latestTraceHasNode("PC"));
    drawDatapathBox(hdc, imem, "Instruction", latestTraceHasNode("InstructionMemory"));
    drawDatapathBox(hdc, decoder, "Decoder", latestTraceHasNode("Decoder"));
    drawDatapathBox(hdc, regfile, "RegisterFile", latestTraceHasNode("RegisterFile"));
    drawDatapathBox(hdc, alu, "ALU", latestTraceHasNode("ALU"));
    drawDatapathBox(hdc, flags, "Flags", latestTraceHasNode("Flags"));
    drawDatapathBox(hdc, writeback, "Writeback", latestTraceHasNode("Writeback"));

    drawDatapathBox(hdc, memory, "Memory/MMIO", latestTraceHasNode("Memory/MMIO"));
    drawDatapathBox(hdc, stack, "Stack", latestTraceHasNode("Stack"));
    drawDatapathBox(hdc, interrupt, "InterruptCtl", latestTraceHasNode("InterruptController"));

    drawArrow(hdc, pc.right, top + boxHeight / 2, imem.left, top + boxHeight / 2, latestTraceHasNode("PC"));
    drawArrow(hdc, imem.right, top + boxHeight / 2, decoder.left, top + boxHeight / 2, latestTraceHasNode("InstructionMemory"));
    drawArrow(hdc, decoder.right, top + boxHeight / 2, regfile.left, top + boxHeight / 2, latestTraceHasNode("Decoder"));
    drawArrow(hdc, regfile.right, top + boxHeight / 2, alu.left, top + boxHeight / 2, latestTraceHasNode("RegisterFile"));
    drawArrow(hdc, alu.right, top + boxHeight / 2, flags.left, top + boxHeight / 2, latestTraceHasNode("ALU"));
    drawArrow(hdc, flags.right, top + boxHeight / 2, writeback.left, top + boxHeight / 2, latestTraceHasNode("Flags"));

    drawArrow(hdc, regfile.left + boxWidth / 2, regfile.bottom, memory.left + boxWidth / 2, memory.top, latestTraceHasNode("Memory/MMIO"));
    drawArrow(hdc, alu.left + boxWidth / 2, alu.bottom, stack.left + boxWidth / 2, stack.top, latestTraceHasNode("Stack"));
    drawArrow(hdc, flags.left + boxWidth / 2, flags.bottom, interrupt.left + 60, interrupt.top, latestTraceHasNode("InterruptController"));

    SetTextColor(hdc, RGB(185, 185, 185));

    RECT hintRect = {
        20,
        124,
        clientRect.right - 20,
        144
    };

    DrawTextA(
        hdc,
        "Highlighted blocks come from the latest TraceEvent.",
        -1,
        &hintRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );
}



LRESULT CALLBACK datapathCanvasProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        drawDatapathCanvas(hdc, clientRect);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        break;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

bool registerDatapathCanvasClass(HINSTANCE instance) {
    const char* className = "ZeroCPUDatapathCanvasClass";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = datapathCanvasProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    return true;
}


std::string makeStateView() {
    std::ostringstream oss;

    oss << "Zero-CPU Studio v0.25\n";
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
    oss << makeVisualDatapathView();

    oss << "\n";
    oss << makePipelineTimelineView();

    oss << "\n";
    oss << makeExecutionDetailProbeView();

    oss << "\n";
    oss << makeRecentInstructionTraceView();

    oss << "\n";
    oss << makeMemoryMapViewer();

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

    if (g_datapathCanvas != nullptr) {
        InvalidateRect(g_datapathCanvas, nullptr, TRUE);
    }
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
        "Zero-CPU Studio v0.25\n"
        "\n"
        "Ready.\n"
        "Source editor added.\n"
        "System panel added.\n"
        "BIO-OS runner added.\n"
        "Studio BIO-OS runner uses BioOSRunner.\n"
        "Graphical datapath canvas added.\n"
        "Main window scrolling added.\n"
        "Compact datapath canvas layout added.\n"
        "Scroll repaint fixed.\n"
        "Pipeline timeline added.\n"
        "Execution detail probe added.\n"
        "Recent instruction trace added.\n"
        "Memory map viewer added.\n"
        "ALU operand/result detail added.\n"
        "Memory operand detail added.\n"
        "Stack operand detail added.\n"
        "Control flow detail added.\n"
        "Studio default example set to debugger showcase.\n"
        "Trace export JSON added.\n"
        "Datapath canvas layout fixed.\n"
        "Compact datapath canvas layout added.\n"
        "Scroll repaint fixed.\n"
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

        g_exportTraceButton = CreateWindowExA(
            0,
            "BUTTON",
            "Export Trace",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            940,
            80,
            130,
            30,
            hwnd,
            controlId(kIdExportTraceButton),
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

        CreateWindowExA(
            0,
            "STATIC",
            "Datapath Canvas:",
            WS_CHILD | WS_VISIBLE,
            20,
            835,
            260,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_datapathCanvas = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "ZeroCPUDatapathCanvasClass",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            20,
            860,
            1430,
            150,
            hwnd,
            controlId(kIdDatapathCanvas),
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
        applyFont(g_exportTraceButton, font);
        applyFont(g_sourceEdit, font);
        applyFont(g_stateEdit, font);
        applyFont(g_traceEdit, font);
        applyFont(g_datapathCanvas, font);

        onResetClicked();
        updateMainScrollBar(hwnd);

        return 0;
    }

    case WM_SIZE: {
        const int maxScroll = getMainScrollMax(hwnd);

        if (g_scrollY > maxScroll) {
            setMainScrollPosition(hwnd, maxScroll);
        } else {
            updateMainScrollBar(hwnd);
        }

        return 0;
    }

    case WM_VSCROLL: {
        int nextScrollY = g_scrollY;

        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            nextScrollY -= kScrollLineSize;
            break;

        case SB_LINEDOWN:
            nextScrollY += kScrollLineSize;
            break;

        case SB_PAGEUP:
            nextScrollY -= kScrollPageSize;
            break;

        case SB_PAGEDOWN:
            nextScrollY += kScrollPageSize;
            break;

        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO info = {};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;

            if (GetScrollInfo(hwnd, SB_VERT, &info)) {
                nextScrollY = info.nTrackPos;
            }

            break;
        }

        case SB_TOP:
            nextScrollY = 0;
            break;

        case SB_BOTTOM:
            nextScrollY = getMainScrollMax(hwnd);
            break;

        default:
            break;
        }

        setMainScrollPosition(hwnd, nextScrollY);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int scrollSteps = wheelDelta / WHEEL_DELTA;

        setMainScrollPosition(
            hwnd,
            g_scrollY - scrollSteps * kScrollLineSize * 3
        );

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

        if (controlIdValue == kIdExportTraceButton) {
            onExportTraceClicked();
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

    if (!registerDatapathCanvasClass(instance)) {
        MessageBoxA(
            nullptr,
            "Failed to register datapath canvas class.",
            "Zero-CPU Studio Error",
            MB_ICONERROR
        );
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        0,
        className,
        "Zero-CPU Studio",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
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
