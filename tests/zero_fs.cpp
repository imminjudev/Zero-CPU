#include "zero_cpu/system/ZeroFS.hpp"

#include <cstdint>
#include <iostream>
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
