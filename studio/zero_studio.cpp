#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/studio/StudioDebugBackend.hpp"
#include "zero_cpu/system/BioOSRunner.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"
#include "zero_cpu/trace/TraceJsonWriter.hpp"
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
#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
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
constexpr int kIdTraceFilterEdit = 1020;
constexpr int kIdApplyTraceFilterButton = 1021;
constexpr int kIdExportSnapshotButton = 1022;
constexpr int kIdDebugSpecEdit = 1023;
constexpr int kIdAddConditionalBreakpointButton = 1024;
constexpr int kIdClearConditionalBreakpointsButton = 1025;
constexpr int kIdAddWatchpointButton = 1026;
constexpr int kIdClearWatchpointsButton = 1027;

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kBinaryMemoryPreviewStart = 512;
constexpr std::size_t kBinaryMemoryPreviewCount = 96;

constexpr const char* kDefaultSourcePath = "examples\\debugger_protected_showcase.zasm";
constexpr const char* kDefaultBinaryPath = "examples\\debugger_protected_showcase.zbin";

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
HWND g_traceFilterEdit = nullptr;
HWND g_applyTraceFilterButton = nullptr;
HWND g_exportSnapshotButton = nullptr;
HWND g_debugSpecEdit = nullptr;
HWND g_addConditionalBreakpointButton = nullptr;
HWND g_clearConditionalBreakpointsButton = nullptr;
HWND g_addWatchpointButton = nullptr;
HWND g_clearWatchpointsButton = nullptr;

zero_cpu::CPU g_cpu;

std::unique_ptr<
    zero_cpu::studio::StudioDebugBackend
> g_binaryDebugger;
std::shared_ptr<zero_cpu::InterruptController> g_interruptController;
std::shared_ptr<zero_cpu::MMIOBus> g_mmioBus;
std::shared_ptr<zero_cpu::DebugOutputDevice> g_debugOutputDevice;
std::shared_ptr<zero_cpu::TimerDevice> g_timerDevice;
StudioMode g_mode = StudioMode::None;
bool g_programLoaded = false;
std::string g_loadedPath;
std::vector<std::size_t> g_breakpoints;
bool g_breakpointHit = false;
std::size_t g_lastBreakpointPc = 0;
std::string g_traceFilter;
int g_scrollY = 0;

bool binaryDebugActive() {
    return g_mode == StudioMode::Binary
        && g_binaryDebugger
        && g_binaryDebugger->loaded();
}

const zero_cpu::CPU& studioCPU() {
    if (binaryDebugActive()) {
        return g_binaryDebugger->cpu();
    }

    return g_cpu;
}

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
constexpr const char* kDefaultDebugSnapshotPath = "traces\\studio_debug_snapshot.json";

void exportTraceToJsonFile(const std::string& path) {
    CreateDirectoryA("traces", nullptr);

    zero_cpu::TraceJsonMetadata metadata;
    metadata.producer = "zero_studio";
    metadata.producer_version = "v0.29";
    metadata.execution_mode = modeToString(g_mode);
    metadata.loaded_path = g_loadedPath;

    zero_cpu::TraceJsonWriter::writeFile(
        path,
        studioCPU().traceLogger().events(),
        metadata
    );
}


void onExportSnapshotClicked() {
    if (!binaryDebugActive()) {
        appendTraceText(
            "\nExport Snapshot failed: "
            "load a .zbin with [Load BIN] first.\n"
        );

        refreshStateView();
        return;
    }

    try {
        CreateDirectoryA(
            "traces",
            nullptr
        );

        zero_cpu::debug::DebugSnapshotOptions
            options;

        options.memory_address = 0;
        options.memory_size = 64;

        g_binaryDebugger->exportSnapshot(
            kDefaultDebugSnapshotPath,
            options
        );

        std::ostringstream output;

        output
            << "\n[Export Debug Snapshot]\n"
            << "Path = "
            << kDefaultDebugSnapshotPath
            << "\n"
            << "Mode = Binary DebugSession\n"
            << "Memory Preview = [0, 64)\n"
            << "Format = JSON\n";

        appendTraceText(
            output.str()
        );
    } catch (const std::exception& ex) {
        std::ostringstream output;

        output
            << "\n[Export Debug Snapshot Failed]\n"
            << "Path = "
            << kDefaultDebugSnapshotPath
            << "\n"
            << "Error = "
            << ex.what()
            << "\n";

        appendTraceText(
            output.str()
        );
    }

    refreshStateView();
}

void onExportTraceClicked() {
    const auto& events = studioCPU().traceLogger().events();

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

bool parseSizeT(
    const std::string& text,
    std::size_t& value
) {
    const auto first =
        std::find_if_not(
            text.begin(),
            text.end(),
            [](unsigned char ch) {
                return std::isspace(ch) != 0;
            }
        );

    const auto last =
        std::find_if_not(
            text.rbegin(),
            text.rend(),
            [](unsigned char ch) {
                return std::isspace(ch) != 0;
            }
        ).base();

    if (first >= last) {
        return false;
    }

    const std::string trimmed(
        first,
        last
    );

    if (
        trimmed.empty()
        || trimmed.front() == '-'
    ) {
        return false;
    }

    try {
        std::size_t consumed = 0;

        const unsigned long long parsed =
            std::stoull(
                trimmed,
                &consumed,
                0
            );

        if (
            consumed != trimmed.size()
            || parsed
                > static_cast<unsigned long long>(
                    (std::numeric_limits<std::size_t>::max)()
                )
        ) {
            return false;
        }

        value =
            static_cast<std::size_t>(
                parsed
            );

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string trimStudioText(const std::string& text) {
    const auto first = std::find_if_not(
        text.begin(), text.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    );
    const auto last = std::find_if_not(
        text.rbegin(), text.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    ).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::size_t resolveStudioCodeAddress(const std::string& text) {
    const std::string token = trimStudioText(text);
    if (token.empty()) {
        throw std::runtime_error("Code location is empty");
    }

    std::size_t address = 0;
    if (parseSizeT(token, address)) {
        return address;
    }

    if (!binaryDebugActive()) {
        throw std::runtime_error(
            "Code labels require an active binary DebugSession"
        );
    }

    return g_binaryDebugger->resolveCodeSymbol(token);
}

std::size_t resolveStudioDataAddress(const std::string& text) {
    const std::string token = trimStudioText(text);
    if (token.empty()) {
        throw std::runtime_error("Data location is empty");
    }

    std::size_t address = 0;
    if (parseSizeT(token, address)) {
        return address;
    }

    if (!binaryDebugActive()) {
        throw std::runtime_error(
            "Data labels require an active binary DebugSession"
        );
    }

    return g_binaryDebugger->resolveDataSymbol(token);
}

zero_cpu::debug::MemoryWatchMode parseStudioWatchMode(
    const std::string& text
) {
    std::string mode = trimStudioText(text);
    std::transform(
        mode.begin(), mode.end(), mode.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    if (mode == "read" || mode == "r") {
        return zero_cpu::debug::MemoryWatchMode::Read;
    }
    if (mode == "write" || mode == "w") {
        return zero_cpu::debug::MemoryWatchMode::Write;
    }
    if (mode == "access" || mode == "rw" || mode == "readwrite") {
        return zero_cpu::debug::MemoryWatchMode::Access;
    }

    throw std::runtime_error(
        "Watch mode must be read, write, or access"
    );
}

bool hasBreakpoint(std::size_t pc) {
    if (binaryDebugActive()) {
        return g_binaryDebugger
            ->session()
            .hasBreakpoint(pc);
    }

    return std::find(
        g_breakpoints.begin(),
        g_breakpoints.end(),
        pc
    ) != g_breakpoints.end();
}

bool addBreakpoint(std::size_t pc) {
    if (binaryDebugActive()) {
        return g_binaryDebugger
            ->addBreakpoint(pc);
    }

    if (hasBreakpoint(pc)) {
        return false;
    }

    g_breakpoints.push_back(pc);

    std::sort(
        g_breakpoints.begin(),
        g_breakpoints.end()
    );

    return true;
}

std::string makeBreakpointView() {
    std::ostringstream output;
    output << "Breakpoints\n";

    std::vector<std::size_t> values;
    if (binaryDebugActive()) {
        values = g_binaryDebugger->session().breakpoints();
    } else {
        values = g_breakpoints;
    }

    if (values.empty()) {
        output << "(none)\n";
    } else {
        for (std::size_t index = 0; index < values.size(); ++index) {
            output << "[" << index << "] PC = " << values[index];
            if (
                g_programLoaded
                && values[index] == studioCPU().state().pc()
            ) {
                output << "  <current>";
            }
            output << "\n";
        }
    }

    if (!binaryDebugActive()) {
        return output.str();
    }

    const auto conditional =
        g_binaryDebugger->session().conditionalBreakpoints();
    output << "\nConditional Breakpoints\n";
    if (conditional.empty()) {
        output << "(none)\n";
    } else {
        for (const auto& breakpoint : conditional) {
            output
                << "[id=" << breakpoint.id << "] PC = "
                << breakpoint.address << " if "
                << breakpoint.condition.expression << "\n";
        }
    }

    const auto watchpoints =
        g_binaryDebugger->session().watchpoints();
    output << "\nWatchpoints\n";
    if (watchpoints.empty()) {
        output << "(none)\n";
    } else {
        for (const auto& watchpoint : watchpoints) {
            output
                << "[id=" << watchpoint.id << "] "
                << zero_cpu::debug::memoryWatchModeToString(watchpoint.mode)
                << " [" << watchpoint.address << ", "
                << watchpoint.endExclusive() << ") size="
                << watchpoint.size << "\n";
        }
    }

    return output.str();
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
        << studioCPU().state().registers().get(zero_cpu::RegisterName::R1)
        << "\n";
    oss << "R2 = "
        << studioCPU().state().registers().get(zero_cpu::RegisterName::R2)
        << "\n";
    oss << "SP = "
        << studioCPU().state().sp()
        << "\n";
    oss << "Memory[100] = "
        << studioCPU().state().memory().read(100)
        << "\n";
    oss << "Memory[2048] = "
        << studioCPU().state().memory().read(2048)
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
        const std::size_t pc = studioCPU().state().pc();

        const std::vector<std::uint8_t> instructionBytes =
            studioCPU().state().memory().readBytes(pc, kInstructionSize);

        InstructionDecoder decoder;
        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        return decodedInstructionToString(decoded);
    } catch (const std::exception& ex) {
        return std::string("<decode failed: ") + ex.what() + ">";
    }
}

std::string studioDebugOutputAsAscii();

std::string readWatchMemoryValue(std::size_t address) {
    try {
        return std::to_string(studioCPU().state().memory().read(address));
    } catch (const std::exception& ex) {
        return std::string("<error: ") + ex.what() + ">";
    }
}

std::string makeWatchExpressionsView() {
    using namespace zero_cpu;

    std::ostringstream oss;

    const auto& state = studioCPU().state();
    const auto& registers = state.registers();

    oss << "Watch Expressions\n";

    oss << "Core\n";
    oss << "  PC = " << state.pc() << "\n";
    oss << "  SP = " << state.sp() << "\n";
    oss << "  Halted = "
        << (state.halted() ? "true" : "false")
        << "\n";
    oss << "  Flags = "
        << state.flags().toString()
        << "\n";

    oss << "\n";
    oss << "Registers\n";
    oss << "  R0 = " << registers.get(RegisterName::R0) << "\n";
    oss << "  R1 = " << registers.get(RegisterName::R1) << "\n";
    oss << "  R2 = " << registers.get(RegisterName::R2) << "\n";
    oss << "  R3 = " << registers.get(RegisterName::R3) << "\n";
    oss << "  R4 = " << registers.get(RegisterName::R4) << "\n";
    oss << "  R7 = " << registers.get(RegisterName::R7) << "\n";

    oss << "\n";
    oss << "Debugger Showcase Memory\n";
    oss << "  Memory[180] = " << readWatchMemoryValue(180) << "\n";
    oss << "  Memory[188] = " << readWatchMemoryValue(188) << "\n";
    oss << "  Memory[196] = " << readWatchMemoryValue(196) << "\n";
    oss << "  Memory[204] = " << readWatchMemoryValue(204) << "\n";
    oss << "  Memory[212] = " << readWatchMemoryValue(212) << "\n";

    oss << "\n";
    oss << "Stack Watch\n";
    oss << "  Memory[2048] = " << readWatchMemoryValue(2048) << "\n";
    oss << "  Memory[2056] = " << readWatchMemoryValue(2056) << "\n";
    oss << "  Memory[2064] = " << readWatchMemoryValue(2064) << "\n";

    oss << "\n";
    oss << "Devices\n";

    if (g_debugOutputDevice) {
        oss << "  DebugOutput writes = "
            << g_debugOutputDevice->writes().size()
            << "\n";
        oss << "  DebugOutput ASCII = "
            << (studioDebugOutputAsAscii().empty()
                ? std::string("<empty>")
                : studioDebugOutputAsAscii())
            << "\n";
    } else {
        oss << "  DebugOutput = <not configured>\n";
    }

    if (g_timerDevice) {
        oss << "  Timer tick = "
            << g_timerDevice->tickCount()
            << "\n";
        oss << "  Timer interrupts = "
            << g_timerDevice->interruptCount()
            << "\n";
    } else {
        oss << "  Timer = <not configured>\n";
    }

    return oss.str();
}



std::string makeRegisterView() {
    using namespace zero_cpu;

    std::ostringstream oss;

    const auto& registers = studioCPU().state().registers();

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
        << studioCPU().state().memory().dumpRange(
               kDataViewStart,
               kDataViewCount
           )
        << "\n";

    oss << "Stack[2048..2079] = "
        << studioCPU().state().memory().dumpRange(
               kStackViewStart,
               kStackViewCount
           )
        << "\n";

    oss << "Memory[100] = "
        << studioCPU().state().memory().read(100)
        << "\n";

    oss << "Memory[2048] = "
        << studioCPU().state().memory().read(2048)
        << "\n";

    if (g_mode == StudioMode::Binary) {
        oss << "\n";
        oss << "Binary Code Memory Preview\n";
        oss << "Memory[512..607] = "
            << studioCPU().state().memory().dumpRange(
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
        << (studioCPU().hasBinaryProgram() ? "true" : "false")
        << "\n";
    oss << "Code Base = "
        << studioCPU().binaryCodeBase()
        << "\n";
    oss << "Entry Point = "
        << studioCPU().binaryEntryPoint()
        << "\n";
    oss << "Code Size = "
        << studioCPU().binaryCodeSize()
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

void configureSystemDevices(
    zero_cpu::CPU& cpu
) {
    using namespace zero_cpu;

    g_interruptController =
        std::make_shared<InterruptController>();

    g_mmioBus =
        std::make_shared<MMIOBus>();

    g_debugOutputDevice =
        std::make_shared<DebugOutputDevice>();

    g_timerDevice =
        std::make_shared<TimerDevice>(
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

    cpu.setInterruptController(
        g_interruptController
    );

    cpu.setMMIOBus(
        g_mmioBus
    );

    cpu.clearClockedDevices();

    cpu.addClockedDevice(
        g_timerDevice
    );
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

std::string toLowerText(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (const unsigned char ch : text) {
        result.push_back(
            static_cast<char>(std::tolower(ch))
        );
    }

    return result;
}

std::string makeTraceFilterHaystack(const zero_cpu::TraceEvent& event) {
    std::ostringstream oss;

    oss << event.instruction().toString()
        << " "
        << event.action()
        << " "
        << event.datapathString()
        << " "
        << event.aluDetailString()
        << " "
        << event.memoryDetailString()
        << " "
        << event.stackDetailString()
        << " "
        << event.controlFlowDetailString()
        << " "
        << event.toCompactString();

    if (event.hasError()) {
        oss << " "
            << event.errorMessage();
    }

    return toLowerText(oss.str());
}

bool traceEventMatchesFilter(
    const zero_cpu::TraceEvent& event,
    const std::string& loweredFilter
) {
    if (loweredFilter.empty()) {
        return true;
    }

    return makeTraceFilterHaystack(event).find(loweredFilter) !=
        std::string::npos;
}



std::string makeMemoryMapViewer() {
    using namespace zero_cpu::memory_map;

    std::ostringstream oss;

    const std::size_t pc = studioCPU().state().pc();
    const std::size_t sp = studioCPU().state().sp();

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

    const auto& events = studioCPU().traceLogger().events();

    if (events.empty()) {
        oss << "No TraceEvent recorded yet.\n";
        return oss.str();
    }

    constexpr std::size_t kMaxRecentTraceEvents = 16;
    const std::size_t eventCount = events.size();

    if (g_traceFilter.empty()) {
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

    const std::string loweredFilter = toLowerText(g_traceFilter);

    std::vector<std::size_t> matches;

    for (std::size_t i = 0; i < eventCount; ++i) {
        if (traceEventMatchesFilter(events[i], loweredFilter)) {
            matches.push_back(i);
        }
    }

    oss << "Filter = "
        << g_traceFilter
        << "\n";

    oss << "Matched "
        << matches.size()
        << " of "
        << eventCount
        << " events\n";

    if (matches.empty()) {
        oss << "No trace events matched the filter.\n";
        return oss.str();
    }

    const std::size_t start =
        matches.size() > kMaxRecentTraceEvents
            ? matches.size() - kMaxRecentTraceEvents
            : 0;

    oss << "Showing last "
        << (matches.size() - start)
        << " matches\n";

    for (std::size_t j = start; j < matches.size(); ++j) {
        const std::size_t i = matches[j];
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

        if (event.aluDetail().active) {
            oss << " | "
                << event.aluDetailString();
        }

        if (event.memoryDetail().active) {
            oss << " | "
                << event.memoryDetailString();
        }

        if (event.stackDetail().active) {
            oss << " | "
                << event.stackDetailString();
        }

        if (event.controlFlowDetail().active) {
            oss << " | "
                << event.controlFlowDetailString();
        }

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

    const auto& events = studioCPU().traceLogger().events();

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

    const auto& events = studioCPU().traceLogger().events();

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

    const auto& events = studioCPU().traceLogger().events();

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
    const auto& events = studioCPU().traceLogger().events();

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

    const auto& events = studioCPU().traceLogger().events();

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

    oss << "Zero-CPU Studio v0.30\n";
    oss << "Mode: " << modeToString(g_mode) << "\n";

    if (g_programLoaded) {
        oss << "Loaded: " << g_loadedPath << "\n";
    } else {
        oss << "Loaded: false\n";
    }

    oss << "\n";

    if (g_breakpointHit) {
        oss << "Last Breakpoint Hit = PC "
            << g_lastBreakpointPc
            << "\n\n";
    }

    oss << makeBreakpointView();

    oss << "\n";

    if (binaryDebugActive()) {
        oss << g_binaryDebugger->statusText();
        oss << "\n";
    }

    oss << "CPU Core State\n";
    oss << "PC = " << studioCPU().state().pc() << "\n";
    oss << "SP = " << studioCPU().state().sp() << "\n";
    oss << "Halted = "
        << (studioCPU().state().halted() ? "true" : "false")
        << "\n";

    if (studioCPU().state().hasError()) {
        oss << "Error = "
            << studioCPU().state().errorMessage()
            << "\n";
    }

    oss << "Flags = "
        << studioCPU().state().flags().toString()
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
    oss << makeWatchExpressionsView();

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

        const binary::BinaryProgram program =
            assembled.toBinaryProgram();

        binary::BinaryWriter writer;

        writer.writeFile(
            outputPath,
            program
        );

        const debug::DebugSymbols symbols =
            debug::DebugSymbols::
                fromAssembledProgram(
                    assembled,
                    memory_map::kBinaryCodeBase
                );

        const std::string symbolsPath =
            debug::
                debugSymbolsPathForExecutable(
                    outputPath
                );

        symbols.writeFile(
            symbolsPath
        );

        binary::BinaryReader reader;

        const binary::BinaryProgram verified =
            reader.readFile(outputPath);

        std::ostringstream oss;
        oss << "Assemble completed successfully.\n";
        oss << "Input: " << inputPath << "\n";
        oss << "Output: " << outputPath << "\n";
        oss << "Debug Symbols: "
            << symbolsPath
            << " ("
            << symbols.size()
            << ")\n";
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

        g_binaryDebugger.reset();

        g_cpu.loadProgram(
            assembled.instructions,
            assembled.labels
        );

        configureSystemDevices(g_cpu);
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

bool loadBinaryProgram(
    const std::string& inputPath
) {
    using namespace zero_cpu;
    using namespace zero_cpu::studio;

    try {
        auto debugger =
            std::make_unique<
                StudioDebugBackend
            >();

        debugger->loadBinary(
            inputPath
        );

        configureSystemDevices(
            debugger->cpu()
        );

        const kernel::ExecutableMetadata&
            metadata =
                debugger
                    ->session()
                    .metadata();

        g_binaryDebugger =
            std::move(debugger);

        g_mode = StudioMode::Binary;
        g_programLoaded = true;
        g_loadedPath = inputPath;

        std::ostringstream output;

        output
            << "Loaded binary debug session.\n"
            << "Path: "
            << inputPath
            << "\n"
            << "Entry Point: "
            << metadata.entry_point
            << "\n"
            << "Code Base: "
            << metadata.code_base
            << "\n"
            << "Code Size: "
            << metadata.code_size
            << " bytes\n"
            << "Instruction Count: "
            << (
                metadata.code_size
                / binary::kInstructionSize
            )
            << "\n"
            << "Debug Symbols: "
            << (
                g_binaryDebugger
                    ->session()
                    .hasSymbols()
                    ? "loaded"
                    : "not found"
            )
            << "\n"
            << "Execution Backend: DebugSession\n";

        setEditText(
            g_traceEdit,
            output.str()
        );

        refreshStateView();
        return true;
    } catch (const std::exception& ex) {
        g_binaryDebugger.reset();
        g_cpu.reset();

        g_mode = StudioMode::None;
        g_programLoaded = false;
        g_loadedPath.clear();

        std::ostringstream output;

        output
            << "Binary debug session load failed.\n"
            << "Error: "
            << ex.what()
            << "\n";

        setEditText(
            g_traceEdit,
            output.str()
        );

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

    while (!studioCPU().state().halted() && stepCount < kMaxBioOSSteps) {
        g_cpu.step();

        if (studioCPU().state().hasError()) {
            log << "BIO-OS execution failed: "
                << studioCPU().state().errorMessage()
                << "\n";
            return;
        }

        ++stepCount;
    }

    if (!studioCPU().state().halted()) {
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
        g_binaryDebugger.reset();
        configureSystemDevices(g_cpu);

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

    if (binaryDebugActive()) {
        const std::size_t pcBefore =
            g_binaryDebugger
                ->cpu()
                .state()
                .pc();

        const zero_cpu::debug::DebugStop stop =
            g_binaryDebugger->step();

        g_breakpointHit =
            stop.reason
                == zero_cpu::debug::
                    DebugStopReason::Breakpoint
            || stop.reason
                == zero_cpu::debug::
                    DebugStopReason::
                        ConditionalBreakpoint
            || stop.reason
                == zero_cpu::debug::
                    DebugStopReason::Watchpoint;

        if (g_breakpointHit) {
            g_lastBreakpointPc = stop.pc;
        }

        std::ostringstream output;

        output
            << "\n[Studio Debug Step]\n"
            << "Backend = DebugSession\n"
            << "PC before = "
            << pcBefore
            << "\n"
            << "PC after = "
            << g_binaryDebugger
                ->cpu()
                .state()
                .pc()
            << "\n"
            << "Stop Reason = "
            << zero_cpu::debug::
                debugStopReasonToString(
                    stop.reason
                )
            << "\n"
            << "Executed Steps = "
            << stop.executed_steps
            << "\n"
            << "Total Steps = "
            << stop.total_steps
            << "\n";

        if (!stop.message.empty()) {
            output
                << "Message = "
                << stop.message
                << "\n";
        }

        appendTraceText(
            output.str()
        );

        refreshStateView();
        return;
    }

    if (studioCPU().state().halted()) {
        appendTraceText("\nProgram is already halted.\n");
        refreshStateView();
        return;
    }

    const std::size_t pcBefore = studioCPU().state().pc();

    std::ostringstream stepLog;
    stepLog << "\n[Studio Step]\n";
    stepLog << "Mode = " << modeToString(g_mode) << "\n";
    stepLog << "PC before = " << pcBefore << "\n";

    if (g_mode == StudioMode::Assembly) {
        if (pcBefore < studioCPU().program().size()) {
            stepLog << "Instruction = "
                    << studioCPU().program()[pcBefore].toString()
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
            << studioCPU().state().pc()
            << "\n";

    if (studioCPU().state().hasError()) {
        stepLog << "Error = "
                << studioCPU().state().errorMessage()
                << "\n";
    }

    if (studioCPU().state().halted()) {
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

    if (binaryDebugActive()) {
        constexpr std::size_t
            kStudioDebugRunLimit = 1000;

        const zero_cpu::debug::DebugStop stop =
            g_binaryDebugger->run(
                kStudioDebugRunLimit
            );

        g_breakpointHit =
            stop.reason
                == zero_cpu::debug::
                    DebugStopReason::Breakpoint
            || stop.reason
                == zero_cpu::debug::
                    DebugStopReason::
                        ConditionalBreakpoint
            || stop.reason
                == zero_cpu::debug::
                    DebugStopReason::Watchpoint;

        if (g_breakpointHit) {
            g_lastBreakpointPc = stop.pc;
        }

        std::ostringstream output;

        output
            << "\n[Studio Debug Run]\n"
            << "Backend = DebugSession\n"
            << "Stop Reason = "
            << zero_cpu::debug::
                debugStopReasonToString(
                    stop.reason
                )
            << "\n"
            << "Stop PC = "
            << stop.pc
            << "\n"
            << "Executed Steps = "
            << stop.executed_steps
            << "\n"
            << "Total Steps = "
            << stop.total_steps
            << "\n";

        if (!stop.message.empty()) {
            output
                << "Message = "
                << stop.message
                << "\n";
        }

        appendTraceText(
            output.str()
        );

        refreshStateView();
        return;
    }

    std::ostringstream runLog;
    runLog << "\n[Studio Run]\n";
    runLog << "Mode = " << modeToString(g_mode) << "\n";

    std::size_t stepCount = 0;

    while (!studioCPU().state().halted()) {
        const std::size_t pcBefore = studioCPU().state().pc();

        if (hasBreakpoint(pcBefore)) {
            g_breakpointHit = true;
            g_lastBreakpointPc = pcBefore;

            runLog << "Hit breakpoint at PC="
                   << pcBefore
                   << ". Execution paused before instruction.\n";
            break;
        }

        runLog << "Step " << stepCount
               << " | PC=" << pcBefore;

        if (g_mode == StudioMode::Assembly) {
            if (pcBefore < studioCPU().program().size()) {
                runLog << " | "
                       << studioCPU().program()[pcBefore].toString();
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

        if (studioCPU().state().hasError()) {
            runLog << "Execution failed: "
                   << studioCPU().state().errorMessage()
                   << "\n";
            break;
        }

        ++stepCount;

        if (stepCount > 1000) {
            runLog << "Step limit reached.\n";
            break;
        }
    }

    if (!studioCPU().state().hasError() && studioCPU().state().halted()) {
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
    std::size_t pc = studioCPU().state().pc();
    bool resolvedSymbol = false;

    if (!trimStudioText(text).empty()) {
        std::size_t numericAddress = 0;
        if (parseSizeT(text, numericAddress)) {
            pc = numericAddress;
        } else {
            try {
                pc = resolveStudioCodeAddress(text);
                resolvedSymbol = true;
            } catch (const std::exception& ex) {
                std::ostringstream output;
                output
                    << "\nInvalid breakpoint location.\n"
                    << "Input = " << text << "\n"
                    << "Error = " << ex.what() << "\n";
                appendTraceText(output.str());
                refreshStateView();
                return;
            }
        }
    }

    try {
        const bool added = addBreakpoint(pc);
        std::ostringstream output;

        if (resolvedSymbol) {
            output
                << "\nResolved code label '"
                << trimStudioText(text)
                << "' -> PC=" << pc << "\n";
        }

        if (added) {
            output << "Added breakpoint at PC=" << pc << "\n";
        } else {
            output << "Breakpoint already exists at PC=" << pc << "\n";
        }

        appendTraceText(output.str());
    } catch (const std::exception& ex) {
        std::ostringstream output;
        output
            << "\nBreakpoint rejected.\n"
            << "PC = " << pc << "\n"
            << "Error = " << ex.what() << "\n";
        appendTraceText(output.str());
    }

    refreshStateView();
}

void onAddConditionalBreakpointClicked() {
    if (!binaryDebugActive()) {
        appendTraceText(
            "\nConditional breakpoints require an active binary DebugSession.\n"
        );
        refreshStateView();
        return;
    }

    const std::string spec = getWindowTextString(g_debugSpecEdit);
    std::istringstream input(spec);
    std::string locationText, source, operation, value, extra;

    if (
        !(input >> locationText >> source >> operation >> value)
        || (input >> extra)
    ) {
        appendTraceText(
            "\nInvalid conditional breakpoint spec.\n"
            "Format: <pc|code-label> <source> <op> <value>\n"
            "Example: work R0 == 3\n"
        );
        refreshStateView();
        return;
    }

    try {
        const std::size_t address = resolveStudioCodeAddress(locationText);
        const std::size_t id =
            g_binaryDebugger->addConditionalBreakpoint(
                address, source, operation, value
            );

        std::ostringstream output;
        output
            << "\nAdded conditional breakpoint.\n"
            << "ID = " << id << "\n"
            << "PC = " << address << "\n"
            << "Condition = " << source << " "
            << operation << " " << value << "\n";
        appendTraceText(output.str());
    } catch (const std::exception& ex) {
        std::ostringstream output;
        output
            << "\nConditional breakpoint rejected.\n"
            << "Spec = " << spec << "\n"
            << "Error = " << ex.what() << "\n";
        appendTraceText(output.str());
    }

    refreshStateView();
}

void onClearConditionalBreakpointsClicked() {
    if (!binaryDebugActive()) {
        appendTraceText("\nNo active binary DebugSession.\n");
        refreshStateView();
        return;
    }

    g_binaryDebugger->clearConditionalBreakpoints();
    appendTraceText("\nCleared conditional breakpoints.\n");
    refreshStateView();
}

void onAddWatchpointClicked() {
    if (!binaryDebugActive()) {
        appendTraceText(
            "\nWatchpoints require an active binary DebugSession.\n"
        );
        refreshStateView();
        return;
    }

    const std::string spec = getWindowTextString(g_debugSpecEdit);
    std::istringstream input(spec);
    std::string locationText, sizeText, modeText, extra;

    if (
        !(input >> locationText >> sizeText >> modeText)
        || (input >> extra)
    ) {
        appendTraceText(
            "\nInvalid watchpoint spec.\n"
            "Format: <addr|data-label> <size> <read|write|access>\n"
            "Example: value 8 write\n"
        );
        refreshStateView();
        return;
    }

    try {
        const std::size_t address = resolveStudioDataAddress(locationText);
        std::size_t size = 0;
        if (!parseSizeT(sizeText, size) || size == 0) {
            throw std::runtime_error(
                "Watchpoint size must be a positive integer"
            );
        }

        const auto mode = parseStudioWatchMode(modeText);
        const std::size_t id =
            g_binaryDebugger->addWatchpoint(address, size, mode);

        std::ostringstream output;
        output
            << "\nAdded watchpoint.\n"
            << "ID = " << id << "\n"
            << "Range = [" << address << ", "
            << (address + size) << ")\n"
            << "Mode = "
            << zero_cpu::debug::memoryWatchModeToString(mode)
            << "\n";
        appendTraceText(output.str());
    } catch (const std::exception& ex) {
        std::ostringstream output;
        output
            << "\nWatchpoint rejected.\n"
            << "Spec = " << spec << "\n"
            << "Error = " << ex.what() << "\n";
        appendTraceText(output.str());
    }

    refreshStateView();
}

void onClearWatchpointsClicked() {
    if (!binaryDebugActive()) {
        appendTraceText("\nNo active binary DebugSession.\n");
        refreshStateView();
        return;
    }

    g_binaryDebugger->clearWatchpoints();
    appendTraceText("\nCleared watchpoints.\n");
    refreshStateView();
}

void onApplyTraceFilterClicked() {
    g_traceFilter = getWindowTextString(g_traceFilterEdit);

    std::ostringstream oss;

    if (g_traceFilter.empty()) {
        oss << "\nTrace filter cleared.\n";
    } else {
        oss << "\nTrace filter applied: "
            << g_traceFilter
            << "\n";
    }

    appendTraceText(oss.str());
    refreshStateView();
}

void onClearBreakpointsClicked() {
    g_breakpoints.clear();

    if (binaryDebugActive()) {
        g_binaryDebugger->clearBreakpoints();
    }
    g_breakpointHit = false;
    g_lastBreakpointPc = 0;
    g_traceFilter.clear();

    if (g_breakpointEdit != nullptr) {
        SetWindowTextA(g_breakpointEdit, "");
    }

    if (g_traceFilterEdit != nullptr) {
        SetWindowTextA(g_traceFilterEdit, "");
    }

    appendTraceText("\nCleared all breakpoints and breakpoint hit state.\n");
    refreshStateView();
}

void onResetClicked() {
    g_binaryDebugger.reset();
    g_cpu.reset();
    configureSystemDevices(g_cpu);
    g_mode = StudioMode::None;
    g_programLoaded = false;
    g_loadedPath.clear();
    g_breakpoints.clear();

    SetWindowTextA(g_inputEdit, kDefaultSourcePath);
    SetWindowTextA(g_outputEdit, kDefaultBinaryPath);

    if (g_breakpointEdit != nullptr) {
        SetWindowTextA(g_breakpointEdit, "");
    }

    if (g_debugSpecEdit != nullptr) {
        SetWindowTextA(g_debugSpecEdit, "");
    }

    try {
        setEditText(g_sourceEdit, readTextFile(kDefaultSourcePath));
    } catch (...) {
        setEditText(g_sourceEdit, "");
    }

    setEditText(
        g_traceEdit,
        "Zero-CPU Studio v0.30\n"
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
        "Breakpoint polish added.\n"
        "Trace filter added.\n"
        "Watch expressions panel added.\n"
        "DebugSession binary backend added.\n"
        "Protected DebugSession showcase set as default.\n"
        "Breakpoint input accepts decimal and 0x hex.\n"
        "Debugger snapshot export added.\n"
        "Symbol sidecar assembly added.\n"
        "Advanced debugger controls added.\n"
        "BP field accepts code labels.\n"
        "Conditional breakpoint and watchpoint GUI added.\n"
        "Patch: v1.2-studio-advanced-debug-controls-r1\n"
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
        "  - Type a PC or code label and click [Add BP]\n"
        "  - Empty BP field uses current PC\n"
        "\n"
        "Advanced Debug Controls:\n"
        "  - Cond: work R0 == 3\n"
        "  - Watch: value 8 write\n"
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

        g_exportSnapshotButton = CreateWindowExA(
            0,
            "BUTTON",
            "Snapshot",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1380,
            40,
            80,
            30,
            hwnd,
            controlId(kIdExportSnapshotButton),
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
            "Trace Filter:",
            WS_CHILD | WS_VISIBLE,
            1080,
            84,
            100,
            24,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_traceFilterEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            1185,
            80,
            165,
            30,
            hwnd,
            controlId(kIdTraceFilterEdit),
            nullptr,
            nullptr
        );

        g_applyTraceFilterButton = CreateWindowExA(
            0,
            "BUTTON",
            "Apply Filter",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            1360,
            80,
            100,
            30,
            hwnd,
            controlId(kIdApplyTraceFilterButton),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Debug spec:",
            WS_CHILD | WS_VISIBLE,
            20, 124, 90, 24,
            hwnd, nullptr, nullptr, nullptr
        );

        g_debugSpecEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            110, 120, 520, 30,
            hwnd,
            controlId(kIdDebugSpecEdit),
            nullptr,
            nullptr
        );

        g_addConditionalBreakpointButton = CreateWindowExA(
            0, "BUTTON", "Add Cond",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            640, 120, 100, 30,
            hwnd,
            controlId(kIdAddConditionalBreakpointButton),
            nullptr, nullptr
        );

        g_clearConditionalBreakpointsButton = CreateWindowExA(
            0, "BUTTON", "Clear Cond",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            750, 120, 105, 30,
            hwnd,
            controlId(kIdClearConditionalBreakpointsButton),
            nullptr, nullptr
        );

        g_addWatchpointButton = CreateWindowExA(
            0, "BUTTON", "Add Watch",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            865, 120, 105, 30,
            hwnd,
            controlId(kIdAddWatchpointButton),
            nullptr, nullptr
        );

        g_clearWatchpointsButton = CreateWindowExA(
            0, "BUTTON", "Clear Watch",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            980, 120, 115, 30,
            hwnd,
            controlId(kIdClearWatchpointsButton),
            nullptr, nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Cond: work R0 == 3   Watch: value 8 write",
            WS_CHILD | WS_VISIBLE,
            1110, 124, 350, 24,
            hwnd, nullptr, nullptr, nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Source Editor (.zasm):",
            WS_CHILD | WS_VISIBLE,
            20,
            160,
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
            186,
            450,
            640,
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
            160,
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
            186,
            460,
            640,
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
            160,
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
            186,
            480,
            640,
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
        applyFont(g_exportSnapshotButton, font);
        applyFont(g_debugSpecEdit, font);
        applyFont(g_addConditionalBreakpointButton, font);
        applyFont(g_clearConditionalBreakpointsButton, font);
        applyFont(g_addWatchpointButton, font);
        applyFont(g_clearWatchpointsButton, font);
        applyFont(g_traceFilterEdit, font);
        applyFont(g_applyTraceFilterButton, font);
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

        if (
            controlIdValue
            == kIdExportSnapshotButton
        ) {
            onExportSnapshotClicked();
            return 0;
        }

        if (controlIdValue == kIdAddConditionalBreakpointButton) {
            onAddConditionalBreakpointClicked();
            return 0;
        }

        if (controlIdValue == kIdClearConditionalBreakpointsButton) {
            onClearConditionalBreakpointsClicked();
            return 0;
        }

        if (controlIdValue == kIdAddWatchpointButton) {
            onAddWatchpointClicked();
            return 0;
        }

        if (controlIdValue == kIdClearWatchpointsButton) {
            onClearWatchpointsClicked();
            return 0;
        }

        if (controlIdValue == kIdApplyTraceFilterButton) {
            onApplyTraceFilterClicked();
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
