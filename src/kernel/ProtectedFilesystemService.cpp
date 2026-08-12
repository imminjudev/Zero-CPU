#include "zero_cpu/kernel/ProtectedFilesystemService.hpp"

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zero_cpu::kernel {

namespace {

using ABI = ProtectedSyscallABI;

SoftwareInterruptResult requestResult(
    std::int64_t requestAddress,
    std::int64_t status
) {
    SoftwareInterruptResult result;
    result.has_argument0 = true;
    result.argument0 = requestAddress;
    result.has_status = true;
    result.status = status;
    return result;
}

} // namespace

ProtectedFilesystemService::ProtectedFilesystemService(
    std::shared_ptr<system::ZeroFS> filesystem
)
    : filesystem_(std::move(filesystem)) {
    if (!filesystem_) {
        throw std::invalid_argument(
            "ProtectedFilesystemService requires ZeroFS"
        );
    }
}

bool ProtectedFilesystemService::handles(
    std::int64_t serviceNumber
) const {
    return serviceNumber == ABI::kFilesystemStatSyscall
        || serviceNumber == ABI::kFilesystemReadSyscall
        || serviceNumber == ABI::kFilesystemWriteSyscall;
}

SoftwareInterruptResult ProtectedFilesystemService::handle(
    std::int64_t serviceNumber,
    CPUState& state,
    MMIOBus*
) {
    if (!handles(serviceNumber)) {
        SoftwareInterruptResult unsupported;
        unsupported.has_status = true;
        unsupported.status = ABI::kStatusUnsupported;
        return unsupported;
    }

    const std::int64_t requestAddress =
        state.registers().get(
            ABI::kArgument0ResultRegister
        );

    SoftwareInterruptResult result =
        requestResult(
            requestAddress,
            ABI::kStatusOk
        );

    std::string path;
    std::int64_t pathStatus = ABI::kStatusOk;

    if (!readPath(
            state,
            requestAddress,
            path,
            pathStatus
        )) {
        result.status = pathStatus;
        return result;
    }

    try {
        if (serviceNumber == ABI::kFilesystemStatSyscall) {
            const system::ZeroFSStat stat = filesystem_->stat(path);

            if (stat.size > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max()
                )) {
                result.status = ABI::kStatusFilesystemError;
                return result;
            }

            const std::int64_t type =
                stat.isFile()
                    ? ABI::kFilesystemNodeFile
                    : ABI::kFilesystemNodeDirectory;
            const std::int64_t size =
                static_cast<std::int64_t>(stat.size);

            if (!writeRequestI64(
                    state,
                    requestAddress,
                    static_cast<std::size_t>(
                        ABI::kFilesystemStatTypeOffset
                    ),
                    type
                )
                || !writeRequestI64(
                    state,
                    requestAddress,
                    static_cast<std::size_t>(
                        ABI::kFilesystemStatSizeOffset
                    ),
                    size
                )) {
                result.status = ABI::kStatusInvalidGuestMemory;
                return result;
            }

            state.registers().set(
                ABI::kArgument0ResultRegister,
                size
            );
            state.registers().set(
                ABI::kArgument1Register,
                type
            );
            result.has_result = true;
            result.result_value = size;
            return result;
        }

        std::int64_t fileOffset = 0;
        std::int64_t bufferAddress = 0;
        std::int64_t transferCount = 0;

        if (!readRequestI64(
                state,
                requestAddress,
                static_cast<std::size_t>(
                    ABI::kFilesystemFileOffsetOffset
                ),
                fileOffset
            )
            || !readRequestI64(
                state,
                requestAddress,
                static_cast<std::size_t>(
                    ABI::kFilesystemBufferPointerOffset
                ),
                bufferAddress
            )
            || !readRequestI64(
                state,
                requestAddress,
                static_cast<std::size_t>(
                    ABI::kFilesystemTransferCountOffset
                ),
                transferCount
            )) {
            result.status = ABI::kStatusInvalidGuestMemory;
            return result;
        }

        if (fileOffset < 0 || transferCount < 0) {
            result.status = ABI::kStatusFilesystemInvalidOffset;
            return result;
        }

        if (!validGuestRange(bufferAddress, transferCount)) {
            result.status = ABI::kStatusInvalidGuestMemory;
            return result;
        }

        if (serviceNumber == ABI::kFilesystemReadSyscall) {
            const system::ZeroFS::Bytes data =
                filesystem_->readFile(
                    path,
                    static_cast<std::size_t>(fileOffset),
                    static_cast<std::size_t>(transferCount)
                );

            state.memory().writeBytes(
                static_cast<std::size_t>(bufferAddress),
                data
            );

            const std::int64_t transferred =
                static_cast<std::int64_t>(data.size());

            state.registers().set(
                ABI::kArgument0ResultRegister,
                transferred
            );
            result.has_result = true;
            result.result_value = transferred;
            return result;
        }

        const std::vector<std::uint8_t> data =
            state.memory().readBytes(
                static_cast<std::size_t>(bufferAddress),
                static_cast<std::size_t>(transferCount)
            );

        filesystem_->writeFile(
            path,
            static_cast<std::size_t>(fileOffset),
            data
        );

        state.registers().set(
            ABI::kArgument0ResultRegister,
            transferCount
        );
        result.has_result = true;
        result.result_value = transferCount;
        return result;
    } catch (const system::ZeroFSException& error) {
        result.status = mapFilesystemError(error.error());
        return result;
    } catch (const std::exception&) {
        result.status = ABI::kStatusFilesystemError;
        return result;
    }
}

std::shared_ptr<system::ZeroFS>
ProtectedFilesystemService::filesystem() const {
    return filesystem_;
}

bool ProtectedFilesystemService::readRequestI64(
    CPUState& state,
    std::int64_t requestAddress,
    std::size_t fieldOffset,
    std::int64_t& value
) {
    if (requestAddress < 0) {
        return false;
    }

    const std::size_t base =
        static_cast<std::size_t>(requestAddress);

    if (base > memory_map::kUserDataEndExclusive
        || fieldOffset
            > memory_map::kUserDataEndExclusive - base) {
        return false;
    }

    const std::size_t address = base + fieldOffset;

    if (address % 8 != 0
        || !memory_map::isUserDataRange(address, 8)) {
        return false;
    }

    value = state.memory().readI64(address);
    return true;
}

bool ProtectedFilesystemService::writeRequestI64(
    CPUState& state,
    std::int64_t requestAddress,
    std::size_t fieldOffset,
    std::int64_t value
) {
    if (requestAddress < 0) {
        return false;
    }

    const std::size_t base =
        static_cast<std::size_t>(requestAddress);

    if (base > memory_map::kUserDataEndExclusive
        || fieldOffset
            > memory_map::kUserDataEndExclusive - base) {
        return false;
    }

    const std::size_t address = base + fieldOffset;

    if (address % 8 != 0
        || !memory_map::isUserDataRange(address, 8)) {
        return false;
    }

    state.memory().writeI64(address, value);
    return true;
}

bool ProtectedFilesystemService::validGuestRange(
    std::int64_t address,
    std::int64_t count
) {
    if (address < 0 || count < 0) {
        return false;
    }

    return memory_map::isUserDataRange(
        static_cast<std::size_t>(address),
        static_cast<std::size_t>(count)
    );
}

bool ProtectedFilesystemService::readPath(
    CPUState& state,
    std::int64_t requestAddress,
    std::string& path,
    std::int64_t& status
) {
    std::int64_t pathAddress = 0;
    std::int64_t pathLength = 0;

    if (!readRequestI64(
            state,
            requestAddress,
            static_cast<std::size_t>(
                ABI::kFilesystemPathPointerOffset
            ),
            pathAddress
        )
        || !readRequestI64(
            state,
            requestAddress,
            static_cast<std::size_t>(
                ABI::kFilesystemPathLengthOffset
            ),
            pathLength
        )) {
        status = ABI::kStatusInvalidGuestMemory;
        return false;
    }

    if (pathLength <= 0
        || pathLength > ABI::kFilesystemMaxPathLength) {
        status = ABI::kStatusFilesystemInvalidPath;
        return false;
    }

    if (!validGuestRange(pathAddress, pathLength)) {
        status = ABI::kStatusInvalidGuestMemory;
        return false;
    }

    const std::vector<std::uint8_t> bytes =
        state.memory().readBytes(
            static_cast<std::size_t>(pathAddress),
            static_cast<std::size_t>(pathLength)
        );

    path.assign(bytes.begin(), bytes.end());
    status = ABI::kStatusOk;
    return true;
}

std::int64_t ProtectedFilesystemService::mapFilesystemError(
    system::ZeroFSError error
) {
    switch (error) {
    case system::ZeroFSError::InvalidPath:
        return ABI::kStatusFilesystemInvalidPath;
    case system::ZeroFSError::NotFound:
        return ABI::kStatusFilesystemNotFound;
    case system::ZeroFSError::NotDirectory:
    case system::ZeroFSError::IsDirectory:
        return ABI::kStatusFilesystemTypeError;
    case system::ZeroFSError::InvalidOffset:
        return ABI::kStatusFilesystemInvalidOffset;
    case system::ZeroFSError::AlreadyExists:
        return ABI::kStatusFilesystemError;
    }

    return ABI::kStatusFilesystemError;
}

} // namespace zero_cpu::kernel

// Patch: v1.8-protected-filesystem-syscalls-r2
