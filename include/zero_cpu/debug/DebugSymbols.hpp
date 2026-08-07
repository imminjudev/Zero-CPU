#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

struct AssembledProgram;

namespace debug {

enum class DebugSymbolKind {
    Code,
    Data
};

const char* debugSymbolKindToString(
    DebugSymbolKind kind
);

struct DebugSymbol {
    DebugSymbolKind kind =
        DebugSymbolKind::Code;

    std::string name;
    std::size_t address = 0;

    bool operator==(
        const DebugSymbol& other
    ) const;
};

class DebugSymbols {
public:
    static DebugSymbols fromAssembledProgram(
        const AssembledProgram& program,
        std::size_t codeBase
    );

    static DebugSymbols readFile(
        const std::string& path
    );

    void writeFile(
        const std::string& path
    ) const;

    bool empty() const;
    std::size_t size() const;

    bool has(
        const std::string& name
    ) const;

    bool hasCode(
        const std::string& name
    ) const;

    bool hasData(
        const std::string& name
    ) const;

    std::size_t resolveCode(
        const std::string& name
    ) const;

    std::size_t resolveData(
        const std::string& name
    ) const;

    const std::vector<DebugSymbol>&
    entries() const;

    bool operator==(
        const DebugSymbols& other
    ) const;

private:
    std::vector<DebugSymbol> entries_;

    std::unordered_map<
        std::string,
        std::size_t
    > indices_;

    void add(
        DebugSymbolKind kind,
        const std::string& name,
        std::size_t address
    );
};

std::string debugSymbolsPathForExecutable(
    const std::string& executablePath
);

} // namespace debug
} // namespace zero_cpu
