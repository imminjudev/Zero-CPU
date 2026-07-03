#pragma once

#include "zero_cpu/core/Flags.hpp"
#include "zero_cpu/core/Memory.hpp"
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

    std::size_t pc() const;
    void setPc(std::size_t value);
    void advancePc();

    std::size_t sp() const;
    void setSp(std::size_t value);

    bool halted() const;
    void halt();

    bool hasError() const;
    const std::string& errorMessage() const;
    void setError(std::string message);

    void reset();

    std::string summary() const;

private:
    RegisterFile registers_;
    Memory memory_;
    Flags flags_;

    std::size_t pc_;
    std::size_t sp_;

    bool halted_;
    bool error_;
    std::string error_message_;
};

} // namespace zero_cpu