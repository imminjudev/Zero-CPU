#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace zero_cpu::system {

enum class ZeroFSNodeType : std::uint8_t {
    File = 0,
    Directory = 1
};

enum class ZeroFSError : std::uint8_t {
    InvalidPath = 0,
    NotFound = 1,
    AlreadyExists = 2,
    NotDirectory = 3,
    IsDirectory = 4,
    InvalidOffset = 5
};

const char* zeroFSErrorToString(
    ZeroFSError error
);

class ZeroFSException final
    : public std::runtime_error {
public:
    ZeroFSException(
        ZeroFSError error,
        const std::string& message
    );

    ZeroFSError error() const noexcept;

private:
    ZeroFSError error_;
};

struct ZeroFSStat {
    ZeroFSNodeType type =
        ZeroFSNodeType::File;

    std::size_t size = 0;

    bool isFile() const;
    bool isDirectory() const;
};

class ZeroFS {
public:
    using Bytes = std::vector<std::uint8_t>;

    inline static constexpr std::size_t
        kReadToEnd =
            std::numeric_limits<std::size_t>::max();

    ZeroFS();

    static std::string normalizePath(
        const std::string& path
    );

    bool exists(
        const std::string& path
    ) const;

    ZeroFSStat stat(
        const std::string& path
    ) const;

    void createDirectory(
        const std::string& path
    );

    void createDirectories(
        const std::string& path
    );

    void putFile(
        const std::string& path,
        Bytes data
    );

    Bytes readFile(
        const std::string& path,
        std::size_t offset = 0,
        std::size_t count = kReadToEnd
    ) const;

    void writeFile(
        const std::string& path,
        std::size_t offset,
        const Bytes& data
    );

    std::vector<std::string> listDirectory(
        const std::string& path
    ) const;

    std::size_t fileCount() const;
    std::size_t directoryCount() const;
    std::size_t totalFileBytes() const;

private:
    struct Node {
        ZeroFSNodeType type =
            ZeroFSNodeType::Directory;

        Bytes data;
    };

    std::map<std::string, Node> nodes_;

    const Node& requireNode(
        const std::string& normalizedPath
    ) const;

    Node& requireNode(
        const std::string& normalizedPath
    );

    static std::string parentPath(
        const std::string& normalizedPath
    );

    static std::string baseName(
        const std::string& normalizedPath
    );
};

} // namespace zero_cpu::system
