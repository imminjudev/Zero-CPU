#include "zero_cpu/hardware/WindowsSerialTransport.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace zero_cpu::hardware {

namespace {

#ifdef _WIN32

HANDLE asHandle(void* value) {
    return static_cast<HANDLE>(value);
}

std::string windowsErrorMessage(
    const std::string& action,
    DWORD errorCode = GetLastError()
) {
    LPSTR buffer = nullptr;

    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr
    );

    std::string message = action + " failed";

    if (length != 0 && buffer != nullptr) {
        std::string detail(buffer, length);
        while (!detail.empty() &&
               (detail.back() == '\r' || detail.back() == '\n')) {
            detail.pop_back();
        }
        message += ": " + detail;
    }

    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    message += " (Windows error " + std::to_string(errorCode) + ")";
    return message;
}

#endif

} // namespace

WindowsSerialTransport::WindowsSerialTransport(
    std::string portName,
    std::uint32_t baudRate
)
    : port_name_(std::move(portName)),
      baud_rate_(baudRate) {
    if (port_name_.empty()) {
        throw std::runtime_error("Serial port name must not be empty");
    }

    if (baud_rate_ == 0) {
        throw std::runtime_error("Serial baud rate must be greater than zero");
    }
}

WindowsSerialTransport::~WindowsSerialTransport() {
    disconnect();
}

std::string WindowsSerialTransport::name() const {
    return "windows-serial:" + port_name_;
}

bool WindowsSerialTransport::connected() const {
    return handle_ != nullptr;
}

void WindowsSerialTransport::connect() {
    if (connected()) {
        return;
    }

#ifndef _WIN32
    throw std::runtime_error(
        "WindowsSerialTransport is available only on Windows"
    );
#else
    const std::string path = normalizedPortPath(port_name_);

    HANDLE handle = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            windowsErrorMessage("Opening serial port " + port_name_)
        );
    }

    handle_ = handle;

    try {
        configurePort();
        configureTimeout(500);

        if (!SetupComm(handle, 4096, 4096)) {
            throw std::runtime_error(
                windowsErrorMessage("Configuring serial buffers")
            );
        }

        if (!PurgeComm(
                handle,
                PURGE_RXABORT | PURGE_RXCLEAR |
                    PURGE_TXABORT | PURGE_TXCLEAR
            )) {
            throw std::runtime_error(
                windowsErrorMessage("Clearing serial buffers")
            );
        }

        // ESP32-S3 USB CDC may briefly reset or re-enumerate when opened.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR);
    } catch (...) {
        disconnect();
        throw;
    }
#endif
}

void WindowsSerialTransport::disconnect() {
#ifdef _WIN32
    if (handle_ != nullptr) {
        CloseHandle(asHandle(handle_));
        handle_ = nullptr;
    }
#else
    handle_ = nullptr;
#endif
}

std::string WindowsSerialTransport::transact(
    const std::string& request,
    std::uint32_t timeoutMilliseconds
) {
    requireConnected();

    if (request.empty()) {
        throw std::runtime_error("Serial request must not be empty");
    }

    if (timeoutMilliseconds == 0) {
        throw std::runtime_error("Serial timeout must be greater than zero");
    }

#ifndef _WIN32
    (void)request;
    (void)timeoutMilliseconds;
    throw std::runtime_error(
        "WindowsSerialTransport is available only on Windows"
    );
#else
    HANDLE handle = asHandle(handle_);
    configureTimeout(timeoutMilliseconds);

    std::size_t writtenTotal = 0;

    while (writtenTotal < request.size()) {
        const std::size_t remaining = request.size() - writtenTotal;
        const DWORD chunk = static_cast<DWORD>(
            remaining > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
                ? std::numeric_limits<DWORD>::max()
                : remaining
        );

        DWORD written = 0;

        if (!WriteFile(
                handle,
                request.data() + writtenTotal,
                chunk,
                &written,
                nullptr
            )) {
            throw std::runtime_error(
                windowsErrorMessage("Writing serial request")
            );
        }

        if (written == 0) {
            throw std::runtime_error("Serial write made no progress");
        }

        writtenTotal += written;
    }

    if (!FlushFileBuffers(handle)) {
        throw std::runtime_error(
            windowsErrorMessage("Flushing serial request")
        );
    }

    std::string response;
    response.reserve(128);

    while (response.size() < 4096) {
        char character = 0;
        DWORD readCount = 0;

        if (!ReadFile(handle, &character, 1, &readCount, nullptr)) {
            throw std::runtime_error(
                windowsErrorMessage("Reading serial response")
            );
        }

        if (readCount == 0) {
            throw std::runtime_error(
                "Serial transaction timed out waiting for a response"
            );
        }

        response.push_back(character);

        if (character == '\n') {
            return response;
        }
    }

    throw std::runtime_error("Serial response exceeded 4096 bytes");
#endif
}

const std::string& WindowsSerialTransport::portName() const {
    return port_name_;
}

std::uint32_t WindowsSerialTransport::baudRate() const {
    return baud_rate_;
}

void WindowsSerialTransport::requireConnected() const {
    if (!connected()) {
        throw std::runtime_error("Windows serial transport is not connected");
    }
}

void WindowsSerialTransport::configurePort() {
#ifndef _WIN32
    throw std::runtime_error(
        "WindowsSerialTransport is available only on Windows"
    );
#else
    HANDLE handle = asHandle(handle_);

    DCB state{};
    state.DCBlength = sizeof(state);

    if (!GetCommState(handle, &state)) {
        throw std::runtime_error(
            windowsErrorMessage("Reading serial configuration")
        );
    }

    state.BaudRate = baud_rate_;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    state.fDtrControl = DTR_CONTROL_DISABLE;
    state.fDsrSensitivity = FALSE;
    state.fTXContinueOnXoff = TRUE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fErrorChar = FALSE;
    state.fNull = FALSE;
    state.fRtsControl = RTS_CONTROL_DISABLE;
    state.fAbortOnError = FALSE;

    if (!SetCommState(handle, &state)) {
        throw std::runtime_error(
            windowsErrorMessage("Applying serial configuration")
        );
    }
#endif
}

void WindowsSerialTransport::configureTimeout(
    std::uint32_t timeoutMilliseconds
) {
#ifndef _WIN32
    (void)timeoutMilliseconds;
    throw std::runtime_error(
        "WindowsSerialTransport is available only on Windows"
    );
#else
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = timeoutMilliseconds;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = timeoutMilliseconds;

    if (!SetCommTimeouts(asHandle(handle_), &timeouts)) {
        throw std::runtime_error(
            windowsErrorMessage("Applying serial timeouts")
        );
    }
#endif
}

std::string WindowsSerialTransport::normalizedPortPath(
    const std::string& portName
) {
    if (portName.rfind("\\\\.\\", 0) == 0) {
        return portName;
    }

    return "\\\\.\\" + portName;
}

} // namespace zero_cpu::hardware
