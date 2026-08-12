#pragma once

#include "zero_cpu/kernel/ProtectedRuntimeService.hpp"
#include "zero_cpu/system/ZeroFS.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace zero_cpu::kernel {

// Minimal path-based protected filesystem service.
// File descriptors are intentionally excluded because the current runtime
// service boundary does not carry process identity.
class ProtectedFilesystemService final
    : public ProtectedRuntimeService {
public:
    explicit ProtectedFilesystemService(
        std::shared_ptr<system::ZeroFS> filesystem
    );

    bool handles(
        std::int64_t serviceNumber
    ) const override;

    SoftwareInterruptResult handle(
        std::int64_t serviceNumber,
        CPUState& state,
        MMIOBus* mmioBus
    ) override;

    std::shared_ptr<system::ZeroFS> filesystem() const;

private:
    std::shared_ptr<system::ZeroFS> filesystem_;

    static bool readRequestI64(
        CPUState& state,
        std::int64_t requestAddress,
        std::size_t fieldOffset,
        std::int64_t& value
    );

    static bool writeRequestI64(
        CPUState& state,
        std::int64_t requestAddress,
        std::size_t fieldOffset,
        std::int64_t value
    );

    static bool validGuestRange(
        std::int64_t address,
        std::int64_t count
    );

    static bool readPath(
        CPUState& state,
        std::int64_t requestAddress,
        std::string& path,
        std::int64_t& status
    );

    static std::int64_t mapFilesystemError(
        system::ZeroFSError error
    );
};

} // namespace zero_cpu::kernel
