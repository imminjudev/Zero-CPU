#include "zero_cpu/core/CPUState.hpp"

#include <sstream>

namespace zero_cpu {

CPUState::CPUState() {
    reset();
}

RegisterFile& CPUState::registers() {
    return registers_;
}

const RegisterFile& CPUState::registers() const {
    return registers_;
}

Memory& CPUState::memory() {
    return memory_;
}

const Memory& CPUState::memory() const {
    return memory_;
}

Flags& CPUState::flags() {
    return flags_;
}

const Flags& CPUState::flags() const {
    return flags_;
}

PrivilegeLevel CPUState::privilegeLevel() const {
    return privilege_level_;
}

void CPUState::setPrivilegeLevel(PrivilegeLevel value) {
    privilege_level_ = value;
}

bool CPUState::isKernelMode() const {
    return privilege_level_ == PrivilegeLevel::Kernel;
}

bool CPUState::isUserMode() const {
    return privilege_level_ == PrivilegeLevel::User;
}

std::size_t CPUState::pc() const {
    return pc_;
}

void CPUState::setPc(std::size_t value) {
    pc_ = value;
}

void CPUState::advancePc() {
    ++pc_;
}

std::size_t CPUState::sp() const {
    return sp_;
}

void CPUState::setSp(std::size_t value) {
    sp_ = value;
}

bool CPUState::halted() const {
    return halted_;
}

void CPUState::halt() {
    halted_ = true;
}

void CPUState::setHalted(bool value) {
    halted_ = value;
}

bool CPUState::hasError() const {
    return has_error_;
}

const std::string& CPUState::errorMessage() const {
    return error_message_;
}

void CPUState::setError(const std::string& message) {
    has_error_ = true;
    error_message_ = message;
    halted_ = true;
}

void CPUState::clearError() {
    has_error_ = false;
    error_message_.clear();
}

void CPUState::reset() {
    registers_.reset();
    memory_.reset();
    flags_.reset();

    privilege_level_ = PrivilegeLevel::Kernel;

    pc_ = 0;
    sp_ = kDefaultStackBase;

    halted_ = false;
    has_error_ = false;
    error_message_.clear();
}

std::string CPUState::summary() const {
    std::ostringstream oss;

    oss << "PC = " << pc_ << "\n";
    oss << "SP = " << sp_ << "\n";
    oss << "Privilege = "
        << privilegeLevelToString(privilege_level_)
        << "\n";
    oss << "Halted = " << (halted_ ? "true" : "false") << "\n";

    if (has_error_) {
        oss << "Error = " << error_message_ << "\n";
    }

    oss << "Registers: " << registers_.toString() << "\n";
    oss << "Flags: " << flags_.toString() << "\n";

    return oss.str();
}

} // namespace zero_cpu