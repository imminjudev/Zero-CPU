#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"

#include <windows.h>

#include <cstddef>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1240;
constexpr int kWindowHeight = 780;

constexpr int kIdInputEdit = 1001;
constexpr int kIdLoadAssemblyButton = 1002;
constexpr int kIdLoadBinaryButton = 1003;
constexpr int kIdStepButton = 1004;
constexpr int kIdRunButton = 1005;
constexpr int kIdResetButton = 1006;
constexpr int kIdStateEdit = 1007;
constexpr int kIdTraceEdit = 1008;

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kBinaryMemoryPreviewStart = 512;
constexpr std::size_t kBinaryMemoryPreviewCount = 96;

enum class StudioMode {
    None,
    Assembly,
    Binary
};

HWND g_inputEdit = nullptr;
HWND g_loadAssemblyButton = nullptr;
HWND g_loadBinaryButton = nullptr;
HWND g_stepButton = nullptr;
HWND g_runButton = nullptr;
HWND g_resetButton = nullptr;
HWND g_stateEdit = nullptr;
HWND g_traceEdit = nullptr;

zero_cpu::CPU g_cpu;
StudioMode g_mode = StudioMode::None;
bool g_programLoaded = false;
std::string g_loadedPath;

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

bool endsWithZbin(const std::string& path) {
    if (path.size() < 5) {
        return false;
    }

    const std::string suffix = path.substr(path.size() - 5);

    return suffix == ".zbin" ||
           suffix == ".ZBIN";
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

std::string makeStateView() {
    std::ostringstream oss;

    oss << "Zero-CPU Studio v0.3\n";
    oss << "Mode: " << modeToString(g_mode) << "\n";

    if (g_programLoaded) {
        oss << "Loaded: " << g_loadedPath << "\n";
    } else {
        oss << "Loaded: false\n";
    }

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
    oss << makeRegisterView();

    oss << "\n";
    oss << makeMemoryView();

    return oss.str();
}

void refreshStateView() {
    setEditText(g_stateEdit, makeStateView());
}

bool loadAssemblyProgram(const std::string& inputPath) {
    using namespace zero_cpu;

    try {
        Assembler assembler;
        AssembledProgram assembled = assembler.assembleFile(inputPath);

        g_cpu.loadProgram(assembled.instructions, assembled.labels);
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

    appendTraceText(runLog.str());
    refreshStateView();
}

void onResetClicked() {
    g_cpu.reset();
    g_mode = StudioMode::None;
    g_programLoaded = false;
    g_loadedPath.clear();

    SetWindowTextA(g_inputEdit, "examples\\function_call.zasm");

    setEditText(
        g_traceEdit,
        "Zero-CPU Studio v0.3\n"
        "\n"
        "Ready.\n"
        "Assembly:\n"
        "  examples\\function_call.zasm\n"
        "\n"
        "Binary:\n"
        "  examples\\function_call.zbin\n"
        "  examples\\nop_halt.zbin\n"
        "\n"
        "Use [Load Assembly] or [Load Binary], then [Step] or [Run].\n"
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
            "Input file path (.zasm or .zbin):",
            WS_CHILD | WS_VISIBLE,
            20,
            20,
            320,
            28,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_inputEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "examples\\function_call.zasm",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            20,
            50,
            520,
            32,
            hwnd,
            controlId(kIdInputEdit),
            nullptr,
            nullptr
        );

        g_loadAssemblyButton = CreateWindowExA(
            0,
            "BUTTON",
            "Load Assembly",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            560,
            50,
            130,
            32,
            hwnd,
            controlId(kIdLoadAssemblyButton),
            nullptr,
            nullptr
        );

        g_loadBinaryButton = CreateWindowExA(
            0,
            "BUTTON",
            "Load Binary",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            700,
            50,
            120,
            32,
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
            830,
            50,
            80,
            32,
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
            920,
            50,
            80,
            32,
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
            1010,
            50,
            80,
            32,
            hwnd,
            controlId(kIdResetButton),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "CPU / Register / Memory View:",
            WS_CHILD | WS_VISIBLE,
            20,
            100,
            320,
            28,
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
            20,
            130,
            580,
            580,
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
            630,
            100,
            320,
            28,
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
            630,
            130,
            560,
            580,
            hwnd,
            controlId(kIdTraceEdit),
            nullptr,
            nullptr
        );

        applyFont(g_inputEdit, font);
        applyFont(g_loadAssemblyButton, font);
        applyFont(g_loadBinaryButton, font);
        applyFont(g_stepButton, font);
        applyFont(g_runButton, font);
        applyFont(g_resetButton, font);
        applyFont(g_stateEdit, font);
        applyFont(g_traceEdit, font);

        onResetClicked();

        return 0;
    }

    case WM_COMMAND: {
        const int controlIdValue = LOWORD(wParam);

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