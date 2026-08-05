#pragma once

#include "zero_cpu/core/Flags.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstddef>
#include <string>

namespace zero_cpu {

class CPUState {
public:
    static constexpr std::size_t kDefaultStackBase = 2048;

    CPUState();

    RegisterFile& registers();
    const RegisterFile& registers() const;

    Memory& memory();
    const Memory& memory() const;

    Flags& flags();
    const Flags& flags() const;

    PrivilegeLevel privilegeLevel() const;
    void setPrivilegeLevel(PrivilegeLevel value);
    bool isKernelMode() const;
    bool isUserMode() const;

    std::size_t pc() const;
    void setPc(std::size_t value);
    void advancePc();

    std::size_t sp() const;
    void setSp(std::size_t value);

    bool halted() const;
    void halt();
    void setHalted(bool value);

    bool hasError() const;
    const std::string& errorMessage() const;
    void setError(const std::string& message);
    void clearError();

    void reset();

    std::string summary() const;

private:
    RegisterFile registers_;
    Memory memory_;
    Flags flags_;

    PrivilegeLevel privilege_level_ =
        PrivilegeLevel::Kernel;

    std::size_t pc_ = 0;
    std::size_t sp_ = kDefaultStackBase;

    bool halted_ = false;
    bool has_error_ = false;
    std::string error_message_;
};

} // namespace zero_cpu