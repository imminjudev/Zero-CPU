#include "zero_cpu/system/ZeroFS.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace zero_cpu::system {

namespace {

[[noreturn]] void throwZeroFSError(
    ZeroFSError error,
    const std::string& message
) {
    throw ZeroFSException(error, message);
}

} // namespace

const char* zeroFSErrorToString(
    ZeroFSError error
) {
    switch (error) {
    case ZeroFSError::InvalidPath:
        return "InvalidPath";
    case ZeroFSError::NotFound:
        return "NotFound";
    case ZeroFSError::AlreadyExists:
        return "AlreadyExists";
    case ZeroFSError::NotDirectory:
        return "NotDirectory";
    case ZeroFSError::IsDirectory:
        return "IsDirectory";
    case ZeroFSError::InvalidOffset:
        return "InvalidOffset";
    }

    return "Unknown";
}

ZeroFSException::ZeroFSException(
    ZeroFSError error,
    const std::string& message
)
    : std::runtime_error(message),
      error_(error) {
}

ZeroFSError ZeroFSException::error() const noexcept {
    return error_;
}

bool ZeroFSStat::isFile() const {
    return type == ZeroFSNodeType::File;
}

bool ZeroFSStat::isDirectory() const {
    return type == ZeroFSNodeType::Directory;
}

ZeroFS::ZeroFS() {
    nodes_.emplace(
        "/",
        Node{ZeroFSNodeType::Directory, {}}
    );

    createDirectory("/bin");
    createDirectory("/www");
    createDirectory("/data");
}

std::string ZeroFS::normalizePath(
    const std::string& path
) {
    if (
        path.empty()
        || path.front() != '/'
        || path.find('\\') != std::string::npos
        || path.find('\0') != std::string::npos
    ) {
        throwZeroFSError(
            ZeroFSError::InvalidPath,
            "ZeroFS path must be an absolute '/' path"
        );
    }

    std::vector<std::string> components;
    std::size_t cursor = 1;

    while (cursor <= path.size()) {
        std::size_t slash = path.find('/', cursor);

        if (slash == std::string::npos) {
            slash = path.size();
        }

        const std::string component =
            path.substr(
                cursor,
                slash - cursor
            );

        if (
            component.empty()
            || component == "."
        ) {
            // Repeated separators and '.' are canonicalized away.
        } else if (component == "..") {
            throwZeroFSError(
                ZeroFSError::InvalidPath,
                "ZeroFS path traversal '..' is not allowed"
            );
        } else {
            components.push_back(component);
        }

        if (slash == path.size()) {
            break;
        }

        cursor = slash + 1;
    }

    if (components.empty()) {
        return "/";
    }

    std::string normalized;

    for (const std::string& component : components) {
        normalized += '/';
        normalized += component;
    }

    return normalized;
}

bool ZeroFS::exists(
    const std::string& path
) const {
    const std::string normalized =
        normalizePath(path);

    return nodes_.find(normalized)
        != nodes_.end();
}

ZeroFSStat ZeroFS::stat(
    const std::string& path
) const {
    const std::string normalized =
        normalizePath(path);

    const Node& node =
        requireNode(normalized);

    ZeroFSStat result;
    result.type = node.type;

    if (node.type == ZeroFSNodeType::File) {
        result.size = node.data.size();
    }

    return result;
}

void ZeroFS::createDirectory(
    const std::string& path
) {
    const std::string normalized =
        normalizePath(path);

    if (nodes_.find(normalized) != nodes_.end()) {
        throwZeroFSError(
            ZeroFSError::AlreadyExists,
            "ZeroFS path already exists: " + normalized
        );
    }

    const std::string parent =
        parentPath(normalized);

    const Node& parentNode =
        requireNode(parent);

    if (
        parentNode.type
        != ZeroFSNodeType::Directory
    ) {
        throwZeroFSError(
            ZeroFSError::NotDirectory,
            "ZeroFS parent is not a directory: " + parent
        );
    }

    nodes_.emplace(
        normalized,
        Node{ZeroFSNodeType::Directory, {}}
    );
}

void ZeroFS::createDirectories(
    const std::string& path
) {
    const std::string normalized =
        normalizePath(path);

    if (normalized == "/") {
        return;
    }

    std::size_t cursor = 1;

    while (cursor < normalized.size()) {
        std::size_t slash =
            normalized.find('/', cursor);

        const std::string current =
            slash == std::string::npos
                ? normalized
                : normalized.substr(0, slash);

        auto existing = nodes_.find(current);

        if (existing == nodes_.end()) {
            nodes_.emplace(
                current,
                Node{
                    ZeroFSNodeType::Directory,
                    {}
                }
            );
        } else if (
            existing->second.type
            != ZeroFSNodeType::Directory
        ) {
            throwZeroFSError(
                ZeroFSError::NotDirectory,
                "ZeroFS path component is a file: "
                    + current
            );
        }

        if (slash == std::string::npos) {
            break;
        }

        cursor = slash + 1;
    }
}

void ZeroFS::putFile(
    const std::string& path,
    Bytes data
) {
    const std::string normalized =
        normalizePath(path);

    if (normalized == "/") {
        throwZeroFSError(
            ZeroFSError::IsDirectory,
            "ZeroFS root is a directory"
        );
    }

    const std::string parent =
        parentPath(normalized);

    const Node& parentNode =
        requireNode(parent);

    if (
        parentNode.type
        != ZeroFSNodeType::Directory
    ) {
        throwZeroFSError(
            ZeroFSError::NotDirectory,
            "ZeroFS parent is not a directory: " + parent
        );
    }

    auto existing = nodes_.find(normalized);

    if (existing != nodes_.end()) {
        if (
            existing->second.type
            == ZeroFSNodeType::Directory
        ) {
            throwZeroFSError(
                ZeroFSError::IsDirectory,
                "ZeroFS path is a directory: " + normalized
            );
        }

        existing->second.data =
            std::move(data);
        return;
    }

    nodes_.emplace(
        normalized,
        Node{
            ZeroFSNodeType::File,
            std::move(data)
        }
    );
}

ZeroFS::Bytes ZeroFS::readFile(
    const std::string& path,
    std::size_t offset,
    std::size_t count
) const {
    const std::string normalized =
        normalizePath(path);

    const Node& node =
        requireNode(normalized);

    if (node.type == ZeroFSNodeType::Directory) {
        throwZeroFSError(
            ZeroFSError::IsDirectory,
            "ZeroFS path is a directory: " + normalized
        );
    }

    if (offset >= node.data.size()) {
        return {};
    }

    const std::size_t available =
        node.data.size() - offset;

    const std::size_t readCount =
        count == kReadToEnd
            ? available
            : std::min(count, available);

    return Bytes(
        node.data.begin()
            + static_cast<std::ptrdiff_t>(offset),
        node.data.begin()
            + static_cast<std::ptrdiff_t>(
                offset + readCount
            )
    );
}

void ZeroFS::writeFile(
    const std::string& path,
    std::size_t offset,
    const Bytes& data
) {
    const std::string normalized =
        normalizePath(path);

    Node& node =
        requireNode(normalized);

    if (node.type == ZeroFSNodeType::Directory) {
        throwZeroFSError(
            ZeroFSError::IsDirectory,
            "ZeroFS path is a directory: " + normalized
        );
    }

    if (
        data.size()
        > std::numeric_limits<std::size_t>::max()
            - offset
    ) {
        throwZeroFSError(
            ZeroFSError::InvalidOffset,
            "ZeroFS write offset overflows size_t"
        );
    }

    const std::size_t end =
        offset + data.size();

    if (offset > node.data.size()) {
        node.data.resize(offset, 0);
    }

    if (end > node.data.size()) {
        node.data.resize(end, 0);
    }

    std::copy(
        data.begin(),
        data.end(),
        node.data.begin()
            + static_cast<std::ptrdiff_t>(offset)
    );
}

std::vector<std::string>
ZeroFS::listDirectory(
    const std::string& path
) const {
    const std::string normalized =
        normalizePath(path);

    const Node& node =
        requireNode(normalized);

    if (node.type != ZeroFSNodeType::Directory) {
        throwZeroFSError(
            ZeroFSError::NotDirectory,
            "ZeroFS path is not a directory: " + normalized
        );
    }

    std::vector<std::string> names;

    for (const auto& entry : nodes_) {
        if (entry.first == "/") {
            continue;
        }

        if (parentPath(entry.first) == normalized) {
            names.push_back(
                baseName(entry.first)
            );
        }
    }

    std::sort(names.begin(), names.end());
    return names;
}

std::size_t ZeroFS::fileCount() const {
    std::size_t count = 0;

    for (const auto& entry : nodes_) {
        if (
            entry.second.type
            == ZeroFSNodeType::File
        ) {
            ++count;
        }
    }

    return count;
}

std::size_t ZeroFS::directoryCount() const {
    std::size_t count = 0;

    for (const auto& entry : nodes_) {
        if (
            entry.second.type
            == ZeroFSNodeType::Directory
        ) {
            ++count;
        }
    }

    return count;
}

std::size_t ZeroFS::totalFileBytes() const {
    std::size_t total = 0;

    for (const auto& entry : nodes_) {
        if (
            entry.second.type
            == ZeroFSNodeType::File
        ) {
            total += entry.second.data.size();
        }
    }

    return total;
}

const ZeroFS::Node& ZeroFS::requireNode(
    const std::string& normalizedPath
) const {
    const auto it =
        nodes_.find(normalizedPath);

    if (it == nodes_.end()) {
        throwZeroFSError(
            ZeroFSError::NotFound,
            "ZeroFS path not found: " + normalizedPath
        );
    }

    return it->second;
}

ZeroFS::Node& ZeroFS::requireNode(
    const std::string& normalizedPath
) {
    auto it = nodes_.find(normalizedPath);

    if (it == nodes_.end()) {
        throwZeroFSError(
            ZeroFSError::NotFound,
            "ZeroFS path not found: " + normalizedPath
        );
    }

    return it->second;
}

std::string ZeroFS::parentPath(
    const std::string& normalizedPath
) {
    if (normalizedPath == "/") {
        return "/";
    }

    const std::size_t slash =
        normalizedPath.find_last_of('/');

    if (slash == 0) {
        return "/";
    }

    return normalizedPath.substr(0, slash);
}

std::string ZeroFS::baseName(
    const std::string& normalizedPath
) {
    if (normalizedPath == "/") {
        return "/";
    }

    const std::size_t slash =
        normalizedPath.find_last_of('/');

    return normalizedPath.substr(slash + 1);
}

} // namespace zero_cpu::system

// Patch: v1.8-zero-fs-core-r1
