#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/system/ZeroFS.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using zero_cpu::system::ZeroFSError;
using zero_cpu::system::ZeroFS;
using zero_cpu::system::ZeroFSException;
using zero_cpu::system::ZeroFSNodeType;

ZeroFS::Bytes bytes(
    const std::string& text
) {
    return ZeroFS::Bytes(
        text.begin(),
        text.end()
    );
}

std::string text(
    const ZeroFS::Bytes& data
) {
    return std::string(
        data.begin(),
        data.end()
    );
}

using ProtectedABI =
    zero_cpu::kernel::ProtectedSyscallABI;

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;
    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembler.assembleString(source)
            .toBinaryProgram(),
        name
    );
}

void writePathRequest(
    zero_cpu::CPUState& state,
    std::size_t requestAddress,
    std::size_t pathAddress,
    const std::string& path
) {
    state.memory().writeBytes(
        pathAddress,
        std::vector<std::uint8_t>(
            path.begin(),
            path.end()
        )
    );
    state.memory().writeI64(
        requestAddress,
        static_cast<std::int64_t>(pathAddress)
    );
    state.memory().writeI64(
        requestAddress + 8,
        static_cast<std::int64_t>(path.size())
    );
}

zero_cpu::SoftwareInterruptResult invokeFilesystem(
    zero_cpu::kernel::ProtectedSyscallDispatcher& dispatcher,
    zero_cpu::CPUState& state,
    std::int64_t service,
    std::int64_t requestAddress
) {
    state.registers().set(
        ProtectedABI::kServiceRegister,
        service
    );
    state.registers().set(
        ProtectedABI::kArgument0ResultRegister,
        requestAddress
    );
    return dispatcher.handle(
        ProtectedABI::kSyscallVector,
        state,
        nullptr
    );
}

template <typename Fn>
bool expectError(
    ZeroFSError expected,
    Fn&& operation
) {
    try {
        operation();
    } catch (const ZeroFSException& error) {
        return error.error() == expected;
    } catch (...) {
        return false;
    }

    return false;
}

bool defaultLayout(
    std::string& detail
) {
    ZeroFS fs;

    if (
        !fs.stat("/").isDirectory()
        || !fs.stat("/bin").isDirectory()
        || !fs.stat("/www").isDirectory()
        || !fs.stat("/data").isDirectory()
    ) {
        detail =
            "default root/bin/www/data layout is missing";
        return false;
    }

    if (
        fs.directoryCount() != 4
        || fs.fileCount() != 0
        || fs.totalFileBytes() != 0
    ) {
        detail =
            "default ZeroFS counters are incorrect";
        return false;
    }

    return true;
}

bool pathNormalizationAndDirectories(
    std::string& detail
) {
    ZeroFS fs;

    if (
        ZeroFS::normalizePath(
            "/www//site/./assets/"
        ) != "/www/site/assets"
    ) {
        detail =
            "absolute path normalization failed";
        return false;
    }

    fs.createDirectories(
        "/www/site/assets/icons"
    );

    if (
        !fs.stat("/www/site").isDirectory()
        || !fs.stat("/www/site/assets").isDirectory()
        || !fs.stat(
            "/www/site/assets/icons"
        ).isDirectory()
    ) {
        detail =
            "recursive directory creation failed";
        return false;
    }

    if (
        !expectError(
            ZeroFSError::InvalidPath,
            []() {
                ZeroFS::normalizePath(
                    "relative/path"
                );
            }
        )
        || !expectError(
            ZeroFSError::InvalidPath,
            []() {
                ZeroFS::normalizePath(
                    "/www/../escape"
                );
            }
        )
    ) {
        detail =
            "invalid or traversal paths were accepted";
        return false;
    }

    return true;
}

bool fileStorageAndRanges(
    std::string& detail
) {
    ZeroFS fs;

    fs.putFile(
        "/www/index.html",
        bytes("hello")
    );

    const auto initialStat =
        fs.stat("/www/index.html");

    if (
        initialStat.type != ZeroFSNodeType::File
        || initialStat.size != 5
        || text(
            fs.readFile(
                "/www/index.html",
                1,
                3
            )
        ) != "ell"
        || !fs.readFile(
            "/www/index.html",
            100,
            5
        ).empty()
    ) {
        detail =
            "file stat or ranged read failed";
        return false;
    }

    fs.putFile(
        "/www/index.html",
        bytes("hello world")
    );

    if (
        fs.stat("/www/index.html").size != 11
        || text(
            fs.readFile("/www/index.html")
        ) != "hello world"
        || fs.fileCount() != 1
        || fs.totalFileBytes() != 11
    ) {
        detail =
            "file replacement or byte accounting failed";
        return false;
    }

    return true;
}

bool sparseWriteGrowth(
    std::string& detail
) {
    ZeroFS fs;

    fs.putFile(
        "/data/blob.bin",
        ZeroFS::Bytes{1, 2}
    );

    fs.writeFile(
        "/data/blob.bin",
        4,
        ZeroFS::Bytes{9, 8}
    );

    const ZeroFS::Bytes expected{
        1, 2, 0, 0, 9, 8
    };

    if (
        fs.readFile("/data/blob.bin")
            != expected
        || fs.stat("/data/blob.bin").size
            != expected.size()
    ) {
        detail =
            "sparse write did not zero-fill/grow file";
        return false;
    }

    return true;
}

bool deterministicDirectoryListing(
    std::string& detail
) {
    ZeroFS fs;

    fs.putFile(
        "/www/b.txt",
        bytes("b")
    );

    fs.createDirectory("/www/z-dir");

    fs.putFile(
        "/www/a.txt",
        bytes("a")
    );

    fs.createDirectories(
        "/www/z-dir/nested"
    );

    const std::vector<std::string> expected{
        "a.txt",
        "b.txt",
        "z-dir"
    };

    if (
        fs.listDirectory("/www")
        != expected
    ) {
        detail =
            "directory listing is not deterministic/immediate-only";
        return false;
    }

    return true;
}

bool protectedFilesystemServiceContract(
    std::string& detail
) {
    using namespace zero_cpu;

    auto fs = std::make_shared<ZeroFS>();
    fs->putFile("/www/index.txt", bytes("hello"));

    kernel::ProtectedSyscallDispatcher dispatcher(fs);
    if (dispatcher.serviceCount() != 3) {
        detail = "protected filesystem service was not registered";
        return false;
    }

    CPUState state;
    state.setPrivilegeLevel(PrivilegeLevel::Kernel);

    constexpr std::size_t pathAddress = 0;
    constexpr std::size_t requestAddress = 64;
    constexpr std::size_t bufferAddress = 160;

    writePathRequest(
        state, requestAddress, pathAddress,
        "/www/index.txt"
    );

    auto statResult = invokeFilesystem(
        dispatcher, state,
        ProtectedABI::kFilesystemStatSyscall,
        requestAddress
    );

    if (!statResult.has_status
        || statResult.status != ProtectedABI::kStatusOk
        || !statResult.has_result
        || statResult.result_value != 5
        || state.memory().readI64(requestAddress + 16)
            != ProtectedABI::kFilesystemNodeFile
        || state.memory().readI64(requestAddress + 24) != 5) {
        detail = "protected FS_STAT contract failed";
        return false;
    }

    writePathRequest(
        state, requestAddress, pathAddress,
        "/www/index.txt"
    );
    state.memory().writeI64(requestAddress + 16, 1);
    state.memory().writeI64(requestAddress + 24, bufferAddress);
    state.memory().writeI64(requestAddress + 32, 3);

    auto readResult = invokeFilesystem(
        dispatcher, state,
        ProtectedABI::kFilesystemReadSyscall,
        requestAddress
    );
    const auto readData =
        state.memory().readBytes(bufferAddress, 3);

    if (readResult.status != ProtectedABI::kStatusOk
        || !readResult.has_result
        || readResult.result_value != 3
        || std::string(readData.begin(), readData.end()) != "ell") {
        detail = "protected FS_READ contract failed";
        return false;
    }

    state.memory().writeBytes(
        bufferAddress,
        std::vector<std::uint8_t>{
            static_cast<std::uint8_t>('X'),
            static_cast<std::uint8_t>('Y')
        }
    );
    writePathRequest(
        state, requestAddress, pathAddress,
        "/www/index.txt"
    );
    state.memory().writeI64(requestAddress + 16, 2);
    state.memory().writeI64(requestAddress + 24, bufferAddress);
    state.memory().writeI64(requestAddress + 32, 2);

    auto writeResult = invokeFilesystem(
        dispatcher, state,
        ProtectedABI::kFilesystemWriteSyscall,
        requestAddress
    );

    if (writeResult.status != ProtectedABI::kStatusOk
        || !writeResult.has_result
        || writeResult.result_value != 2
        || text(fs->readFile("/www/index.txt")) != "heXYo") {
        detail = "protected FS_WRITE contract failed";
        return false;
    }

    writePathRequest(
        state, requestAddress, pathAddress,
        "/www/missing.txt"
    );
    auto missingResult = invokeFilesystem(
        dispatcher, state,
        ProtectedABI::kFilesystemStatSyscall,
        requestAddress
    );
    if (missingResult.status
            != ProtectedABI::kStatusFilesystemNotFound) {
        detail = "ZeroFS NotFound did not map to protected ABI status";
        return false;
    }

    auto badResult = invokeFilesystem(
        dispatcher, state,
        ProtectedABI::kFilesystemReadSyscall,
        504
    );
    if (badResult.status
            != ProtectedABI::kStatusInvalidGuestMemory
        || state.registers().get(
            ProtectedABI::kStatusRegister
        ) != ProtectedABI::kStatusInvalidGuestMemory) {
        detail = "invalid guest request memory was not rejected";
        return false;
    }

    return true;
}

bool protectedFilesystemUserKernelIntegration(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::system;

    auto fs = std::make_shared<ZeroFS>();
    fs->putFile("/www/index.txt", bytes("OK"));
    fs->putFile("/data/out.txt", ZeroFS::Bytes{});

    auto handler =
        std::make_shared<
            kernel::ProtectedSyscallDispatcher
        >(fs);

    const char* source = R"ASM(
.entry start
.text
start:
    MOV R5, 7236837303819663151
    STORE [0], R5
    MOV R5, 128060694100069
    STORE [8], R5

    MOV R5, 8462034320463324207
    STORE [16], R5
    MOV R5, 500237086324
    STORE [24], R5

    MOV R5, 0
    STORE [64], R5
    MOV R5, 14
    STORE [72], R5
    MOV R5, 0
    STORE [80], R5
    MOV R5, 128
    STORE [88], R5
    MOV R5, 2
    STORE [96], R5

    MOV R1, 31
    MOV R2, 64
    INT 80
    STORE [104], R2
    STORE [112], R4

    MOV R5, 16
    STORE [64], R5
    MOV R5, 13
    STORE [72], R5
    MOV R5, 0
    STORE [80], R5
    MOV R5, 128
    STORE [88], R5
    MOV R5, 2
    STORE [96], R5

    MOV R1, 32
    MOV R2, 64
    INT 80
    STORE [120], R2
    STORE [136], R4

    MOV R1, 3
    MOV R2, 0
    INT 80
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 150;
    options.software_interrupt_handler = handler;

    MultiProcessRunner runner;
    const MultiProcessRunResult result =
        runner.runImages(
            {makeImage(
                source,
                "protected-filesystem-read-write.zbin"
            )},
            options
        );

    if (!result.success()
        || result.fault_count != 0
        || result.software_interrupts.size() != 3) {
        detail = "protected filesystem INT 80 read/write workload failed";
        return false;
    }

    const ProcessRunSummary& process = result.process(1);
    const auto data =
        process.final_memory.readBytes(128, 2);

    if (std::string(data.begin(), data.end()) != "OK"
        || process.final_memory.readI64(104) != 2
        || process.final_memory.readI64(112)
            != ProtectedABI::kStatusOk
        || process.final_memory.readI64(120) != 2
        || process.final_memory.readI64(136)
            != ProtectedABI::kStatusOk
        || text(fs->readFile("/data/out.txt")) != "OK"
        || !process.has_exit_code
        || process.exit_code != 0) {
        detail = "filesystem read/write results did not survive process execution";
        return false;
    }

    const SoftwareInterruptResult& readCall =
        result.software_interrupts[0]
            .observation.result;
    const SoftwareInterruptResult& writeCall =
        result.software_interrupts[1]
            .observation.result;

    if (!readCall.has_service_number
        || readCall.service_number
            != ProtectedABI::kFilesystemReadSyscall
        || readCall.status != ProtectedABI::kStatusOk
        || !readCall.has_result
        || readCall.result_value != 2
        || !writeCall.has_service_number
        || writeCall.service_number
            != ProtectedABI::kFilesystemWriteSyscall
        || writeCall.status != ProtectedABI::kStatusOk
        || !writeCall.has_result
        || writeCall.result_value != 2) {
        detail = "filesystem read/write semantic observations were not preserved";
        return false;
    }

    return true;
}

bool typedErrors(
    std::string& detail
) {
    ZeroFS fs;

    fs.putFile(
        "/www/index.html",
        bytes("ok")
    );

    if (
        !expectError(
            ZeroFSError::NotFound,
            [&]() {
                fs.putFile(
                    "/missing/file.txt",
                    bytes("x")
                );
            }
        )
        || !expectError(
            ZeroFSError::AlreadyExists,
            [&]() {
                fs.createDirectory("/www");
            }
        )
        || !expectError(
            ZeroFSError::IsDirectory,
            [&]() {
                fs.readFile("/www");
            }
        )
        || !expectError(
            ZeroFSError::NotDirectory,
            [&]() {
                fs.createDirectory(
                    "/www/index.html/child"
                );
            }
        )
    ) {
        detail =
            "ZeroFS typed error contract failed";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU ZeroFS Core Test ===\n\n";

    int failures = 0;

    auto report = [&failures](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";
            ++failures;
        }
    };

    {
        std::string detail;
        report(
            "Default filesystem layout",
            defaultLayout(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Path normalization and directories",
            pathNormalizationAndDirectories(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "File storage and ranged reads",
            fileStorageAndRanges(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Sparse write growth",
            sparseWriteGrowth(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Deterministic directory listing",
            deterministicDirectoryListing(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Protected filesystem service contract",
            protectedFilesystemServiceContract(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Protected filesystem User/Kernel integration",
            protectedFilesystemUserKernelIntegration(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Typed filesystem errors",
            typedErrors(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "ZeroFS core test finished successfully.\n";
        return 0;
    }

    std::cout
        << "ZeroFS core test failed. Failure count: "
        << failures
        << "\n";

    return 1;
}

// Patch: v1.8-zero-fs-core-r1

// Patch: v1.8-protected-filesystem-syscalls-r2
