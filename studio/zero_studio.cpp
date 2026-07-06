#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <windows.h>

#include <exception>
#include <sstream>
#include <string>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 700;

HWND g_inputEdit = nullptr;
HWND g_outputEdit = nullptr;
HWND g_runButton = nullptr;
HWND g_resetButton = nullptr;

std::string getWindowTextString(HWND hwnd) {
    const int length = GetWindowTextLengthA(hwnd);

    if (length <= 0) {
        return {};
    }

    std::string text(static_cast<std::size_t>(length), '\0');
    GetWindowTextA(hwnd, text.data(), length + 1);

    return text;
}

void setOutputText(const std::string& text) {
    SetWindowTextA(g_outputEdit, text.c_str());
}

std::string runAssemblyFile(const std::string& inputPath) {
    using namespace zero_cpu;

    std::ostringstream oss;

    oss << "Zero-CPU Studio v0.1\n";
    oss << "Input file: " << inputPath << "\r\n\r\n";

    try {
        Assembler assembler;
        AssembledProgram assembled = assembler.assembleFile(inputPath);

        CPU cpu;
        cpu.loadProgram(assembled.instructions, assembled.labels);
        cpu.run(1000);

        oss << "=== Program Info ===\r\n";
        oss << "Instruction count: "
            << assembled.instructions.size()
            << "\r\n";

        oss << "Label count: "
            << assembled.labels.size()
            << "\r\n\r\n";

        oss << "=== Final CPU State ===\r\n";
        std::string summary = cpu.state().summary();

        for (char ch : summary) {
            if (ch == '\n') {
                oss << "\r\n";
            } else {
                oss << ch;
            }
        }

        oss << "\r\n";

        if (cpu.state().hasError()) {
            oss << "=== Execution Result ===\r\n";
            oss << "FAILED\r\n";
            oss << "Error: "
                << cpu.state().errorMessage()
                << "\r\n";

            return oss.str();
        }

        const auto finalR1 =
            cpu.state().registers().get(RegisterName::R1);

        const auto finalR2 =
            cpu.state().registers().get(RegisterName::R2);

        oss << "=== Final Check ===\r\n";
        oss << "R1 = " << finalR1 << "\r\n";
        oss << "R2 = " << finalR2 << "\r\n";
        oss << "SP = " << cpu.state().sp() << "\r\n";
        oss << "Memory[100] = "
            << cpu.state().memory().read(100)
            << "\r\n";
        oss << "Memory[2048] = "
            << cpu.state().memory().read(2048)
            << "\r\n\r\n";

        oss << "=== Execution Result ===\r\n";
        oss << "SUCCESS\r\n";
    } catch (const std::exception& ex) {
        oss << "=== Studio Error ===\r\n";
        oss << ex.what() << "\r\n";
    }

    return oss.str();
}

void onRunClicked() {
    const std::string inputPath = getWindowTextString(g_inputEdit);

    if (inputPath.empty()) {
        setOutputText("Input path is empty.\r\n");
        return;
    }

    const std::string result = runAssemblyFile(inputPath);
    setOutputText(result);
}

void onResetClicked() {
    SetWindowTextA(g_inputEdit, "examples\\function_call.zasm");
    setOutputText(
        "Zero-CPU Studio v0.1\r\n"
        "\r\n"
        "Ready.\r\n"
        "Click [Run Assembly] to execute examples\\function_call.zasm.\r\n"
    );
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
            "Assembly file path:",
            WS_CHILD | WS_VISIBLE,
            20,
            20,
            200,
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
            620,
            32,
            hwnd,
            reinterpret_cast<HMENU>(1001),
            nullptr,
            nullptr
        );

        g_runButton = CreateWindowExA(
            0,
            "BUTTON",
            "Run Assembly",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            660,
            50,
            140,
            32,
            hwnd,
            reinterpret_cast<HMENU>(1002),
            nullptr,
            nullptr
        );

        g_resetButton = CreateWindowExA(
            0,
            "BUTTON",
            "Reset",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            820,
            50,
            120,
            32,
            hwnd,
            reinterpret_cast<HMENU>(1003),
            nullptr,
            nullptr
        );

        CreateWindowExA(
            0,
            "STATIC",
            "Execution Output:",
            WS_CHILD | WS_VISIBLE,
            20,
            100,
            200,
            28,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        g_outputEdit = CreateWindowExA(
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
            940,
            500,
            hwnd,
            reinterpret_cast<HMENU>(1004),
            nullptr,
            nullptr
        );

        applyFont(g_inputEdit, font);
        applyFont(g_outputEdit, font);
        applyFont(g_runButton, font);
        applyFont(g_resetButton, font);

        onResetClicked();

        return 0;
    }

    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);

        if (controlId == 1002) {
            onRunClicked();
            return 0;
        }

        if (controlId == 1003) {
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