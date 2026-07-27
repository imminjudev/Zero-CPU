#include "zero_cpu/trace/TraceEvent.hpp"

#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/core/MemoryMap.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>

namespace zero_cpu {

namespace {

bool isAluTraceOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::MUL:
    case Opcode::DIV:
    case Opcode::CMP:
    case Opcode::TEST:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::NOT:
        return true;

    default:
        return false;
    }
}

bool aluOpcodeWritesDestination(Opcode opcode) {
    switch (opcode) {
    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::MUL:
    case Opcode::DIV:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::NOT:
        return true;

    default:
        return false;
    }
}

std::string aluOperationName(Opcode opcode) {
    switch (opcode) {
    case Opcode::ADD:
        return "ALU_ADD";
    case Opcode::SUB:
        return "ALU_SUB";
    case Opcode::MUL:
        return "ALU_MUL";
    case Opcode::DIV:
        return "ALU_DIV";
    case Opcode::CMP:
        return "ALU_COMPARE";
    case Opcode::TEST:
        return "ALU_TEST";
    case Opcode::AND:
        return "ALU_AND";
    case Opcode::OR:
        return "ALU_OR";
    case Opcode::XOR:
        return "ALU_XOR";
    case Opcode::NOT:
        return "ALU_NOT";
    default:
        return "ALU_UNKNOWN";
    }
}

void appendAluNote(ALUTraceDetail& detail, const std::string& note) {
    if (note.empty()) {
        return;
    }

    if (!detail.note.empty()) {
        detail.note += "; ";
    }

    detail.note += note;
}

bool tryReadTraceOperandValue(
    const CPUState& state,
    const Operand& operand,
    std::int64_t& value,
    std::string& note
) {
    try {
        switch (operand.type()) {
        case OperandType::Register:
            value = state.registers().get(operand.asRegister());
            return true;

        case OperandType::Immediate:
            value = operand.asImmediate();
            return true;

        case OperandType::MemoryAddress:
            value = state.memory().read(operand.asMemoryAddress());
            return true;

        case OperandType::RegisterIndirectAddress: {
            const std::int64_t rawAddress =
                state.registers().get(operand.asRegisterIndirectBase());

            if (rawAddress < 0) {
                note = "negative register-indirect address";
                return false;
            }

            value = state.memory().read(static_cast<std::size_t>(rawAddress));
            return true;
        }

        case OperandType::Label:
            note = "label operand has no runtime value snapshot";
            return false;

        case OperandType::None:
        default:
            note = "operand is empty";
            return false;
        }
    } catch (const std::exception& ex) {
        note = ex.what();
        return false;
    }
}

std::string registerNameText(RegisterName reg) {
    return RegisterFile::registerNameToString(reg);
}


bool isMemoryTraceOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::LOAD:
    case Opcode::STORE:
        return true;

    default:
        return false;
    }
}

std::string memoryOperationName(Opcode opcode) {
    switch (opcode) {
    case Opcode::LOAD:
        return "MEMORY_READ";
    case Opcode::STORE:
        return "MEMORY_WRITE";
    default:
        return "MEMORY_UNKNOWN";
    }
}

void appendMemoryNote(MemoryTraceDetail& detail, const std::string& note) {
    if (note.empty()) {
        return;
    }

    if (!detail.note.empty()) {
        detail.note += "; ";
    }

    detail.note += note;
}

std::string memoryRouteText(std::size_t address) {
    using namespace zero_cpu::memory_map;

    if (isDebugOutputAddress(address)) {
        return "DebugOutput MMIO";
    }

    if (isTimerAddress(address)) {
        return "Timer MMIO";
    }

    if (isMmioAddress(address)) {
        return "MMIO";
    }

    if (address < kDefaultMemorySize) {
        return "RAM";
    }

    return "Outside default RAM";
}


bool isStackTraceOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::PUSH:
    case Opcode::POP:
    case Opcode::CALL:
    case Opcode::RET:
        return true;

    default:
        return false;
    }
}

std::string stackOperationName(Opcode opcode) {
    switch (opcode) {
    case Opcode::PUSH:
        return "STACK_PUSH";
    case Opcode::POP:
        return "STACK_POP";
    case Opcode::CALL:
        return "CALL";
    case Opcode::RET:
        return "RETURN";
    default:
        return "STACK_UNKNOWN";
    }
}

void appendStackNote(StackTraceDetail& detail, const std::string& note) {
    if (note.empty()) {
        return;
    }

    if (!detail.note.empty()) {
        detail.note += "; ";
    }

    detail.note += note;
}

bool findMemoryChangeAt(
    const std::vector<MemoryChange>& changes,
    std::size_t address,
    MemoryChange& found
) {
    for (const MemoryChange& change : changes) {
        if (change.address == address) {
            found = change;
            return true;
        }
    }

    return false;
}


bool tryResolveTraceMemoryAddress(
    const CPUState& state,
    const Operand& operand,
    std::size_t& address,
    std::string& note
) {
    try {
        switch (operand.type()) {
        case OperandType::MemoryAddress:
            address = operand.asMemoryAddress();
            return true;

        case OperandType::RegisterIndirectAddress: {
            const std::int64_t rawAddress =
                state.registers().get(operand.asRegisterIndirectBase());

            if (rawAddress < 0) {
                note = "negative register-indirect address";
                return false;
            }

            address = static_cast<std::size_t>(rawAddress);
            return true;
        }

        default:
            note = "operand is not a memory address";
            return false;
        }
    } catch (const std::exception& ex) {
        note = ex.what();
        return false;
    }
}

} // namespace

TraceEvent::TraceEvent(
    CPUState before,
    Instruction instruction,
    CPUState after,
    std::string error_message
)
    : before_(std::move(before)),
      instruction_(std::move(instruction)),
      after_(std::move(after)),
      changed_registers_(),
      changed_flags_(),
      changed_memory_(),
      stage_(),
      action_(),
      datapath_nodes_(),
      alu_detail_(),
      memory_detail_(),
      stack_detail_(),
      error_message_(std::move(error_message)) {
    analyzeChanges();
    analyzeVisualMetadata();
    analyzeAluDetail();
    analyzeMemoryDetail();
    analyzeStackDetail();
}

const CPUState& TraceEvent::before() const {
    return before_;
}

const CPUState& TraceEvent::after() const {
    return after_;
}

const Instruction& TraceEvent::instruction() const {
    return instruction_;
}

std::size_t TraceEvent::pcBefore() const {
    return before_.pc();
}

std::size_t TraceEvent::pcAfter() const {
    return after_.pc();
}

const std::vector<RegisterChange>& TraceEvent::changedRegisters() const {
    return changed_registers_;
}

const std::vector<FlagChange>& TraceEvent::changedFlags() const {
    return changed_flags_;
}

const std::vector<MemoryChange>& TraceEvent::changedMemory() const {
    return changed_memory_;
}

const std::string& TraceEvent::stage() const {
    return stage_;
}

const std::string& TraceEvent::action() const {
    return action_;
}

const std::vector<std::string>& TraceEvent::datapathNodes() const {
    return datapath_nodes_;
}

std::string TraceEvent::datapathString() const {
    return joinDatapathNodes(datapath_nodes_);
}

const ALUTraceDetail& TraceEvent::aluDetail() const {
    return alu_detail_;
}

std::string TraceEvent::aluDetailString() const {
    if (!alu_detail_.active) {
        return "ALU Detail = inactive";
    }

    std::ostringstream oss;

    oss << alu_detail_.operation;

    if (alu_detail_.has_lhs) {
        oss << " | lhs("
            << alu_detail_.lhs_text
            << ")="
            << alu_detail_.lhs;
    }

    if (alu_detail_.has_rhs) {
        oss << " | rhs("
            << alu_detail_.rhs_text
            << ")="
            << alu_detail_.rhs;
    }

    if (alu_detail_.has_result) {
        oss << " | result";

        if (!alu_detail_.destination.empty()) {
            oss << "("
                << alu_detail_.destination
                << ")";
        }

        oss << "="
            << alu_detail_.result;
    }

    if (!alu_detail_.note.empty()) {
        oss << " | note="
            << alu_detail_.note;
    }

    return oss.str();
}

const MemoryTraceDetail& TraceEvent::memoryDetail() const {
    return memory_detail_;
}

std::string TraceEvent::memoryDetailString() const {
    if (!memory_detail_.active) {
        return "Memory Detail = inactive";
    }

    std::ostringstream oss;

    oss << memory_detail_.operation;

    if (memory_detail_.has_address) {
        oss << " | address("
            << memory_detail_.address_text
            << ")="
            << memory_detail_.address;
    }

    if (!memory_detail_.route.empty()) {
        oss << " | route="
            << memory_detail_.route;
    }

    if (memory_detail_.has_value) {
        oss << " | value";

        if (!memory_detail_.value_text.empty()) {
            oss << "("
                << memory_detail_.value_text
                << ")";
        }

        oss << "="
            << memory_detail_.value;
    }

    if (!memory_detail_.destination.empty()) {
        oss << " | destination="
            << memory_detail_.destination;
    }

    if (!memory_detail_.note.empty()) {
        oss << " | note="
            << memory_detail_.note;
    }

    return oss.str();
}

const StackTraceDetail& TraceEvent::stackDetail() const {
    return stack_detail_;
}

std::string TraceEvent::stackDetailString() const {
    if (!stack_detail_.active) {
        return "Stack Detail = inactive";
    }

    std::ostringstream oss;

    oss << stack_detail_.operation;

    if (stack_detail_.has_stack_address) {
        oss << " | stack_address="
            << stack_detail_.stack_address;
    }

    if (stack_detail_.has_sp_change) {
        oss << " | SP="
            << stack_detail_.sp_before
            << "->"
            << stack_detail_.sp_after;
    }

    if (stack_detail_.has_value) {
        oss << " | value="
            << stack_detail_.value;
    }

    if (stack_detail_.has_return_address) {
        oss << " | return_address="
            << stack_detail_.return_address;
    }

    if (!stack_detail_.destination.empty()) {
        oss << " | destination="
            << stack_detail_.destination;
    }

    if (stack_detail_.has_target) {
        oss << " | target="
            << stack_detail_.target;
    }

    if (!stack_detail_.note.empty()) {
        oss << " | note="
            << stack_detail_.note;
    }

    return oss.str();
}


bool TraceEvent::hasError() const {
    return !error_message_.empty();
}

const std::string& TraceEvent::errorMessage() const {
    return error_message_;
}

std::string TraceEvent::toCompactString() const {
    std::ostringstream oss;

    oss << "PC=" << formatPc(pcBefore())
        << " | " << instruction_.toString();

    if (!stage_.empty()) {
        oss << " | stage=" << stage_;
    }

    if (!action_.empty()) {
        oss << " | action=" << action_;
    }

    if (!datapath_nodes_.empty()) {
        oss << " | path=" << datapathString();
    }

    if (alu_detail_.active) {
        oss << " | " << aluDetailString();
    }

    if (memory_detail_.active) {
        oss << " | " << memoryDetailString();
    }

    if (stack_detail_.active) {
        oss << " | " << stackDetailString();
    }

    for (const auto& change : changed_registers_) {
        oss << " | "
            << change.name
            << ":"
            << change.before
            << "->"
            << change.after;
    }

    for (const auto& change : changed_flags_) {
        oss << " | "
            << change.name
            << ":"
            << (change.before ? 1 : 0)
            << "->"
            << (change.after ? 1 : 0);
    }

    if (before_.sp() != after_.sp()) {
        oss << " | SP:"
            << before_.sp()
            << "->"
            << after_.sp();
    }

    if (before_.halted() != after_.halted()) {
        oss << " | halted:"
            << boolToString(before_.halted())
            << "->"
            << boolToString(after_.halted());
    }

    if (hasError()) {
        oss << " | ERROR: " << error_message_;
    }

    oss << " | NextPC=" << formatPc(pcAfter());

    return oss.str();
}

std::string TraceEvent::toFullString() const {
    std::ostringstream oss;

    oss << "[PC=" << formatPc(pcBefore()) << "] "
        << instruction_.toString()
        << "\n\n";

    oss << "Visual Metadata:\n";
    oss << "  Stage: "
        << (stage_.empty() ? "Unknown" : stage_)
        << "\n";
    oss << "  Action: "
        << (action_.empty() ? "Unknown" : action_)
        << "\n";
    oss << "  Datapath: "
        << (datapath_nodes_.empty() ? "None" : datapathString())
        << "\n\n";

    oss << "ALU Trace Detail:\n";
    if (!alu_detail_.active) {
        oss << "  None\n\n";
    } else {
        oss << "  Operation: "
            << alu_detail_.operation
            << "\n";

        if (!alu_detail_.destination.empty()) {
            oss << "  Destination: "
                << alu_detail_.destination
                << "\n";
        }

        if (alu_detail_.has_lhs) {
            oss << "  LHS "
                << alu_detail_.lhs_text
                << " = "
                << alu_detail_.lhs
                << "\n";
        } else if (!alu_detail_.lhs_text.empty()) {
            oss << "  LHS "
                << alu_detail_.lhs_text
                << " = <unavailable>\n";
        }

        if (alu_detail_.has_rhs) {
            oss << "  RHS "
                << alu_detail_.rhs_text
                << " = "
                << alu_detail_.rhs
                << "\n";
        } else if (!alu_detail_.rhs_text.empty()) {
            oss << "  RHS "
                << alu_detail_.rhs_text
                << " = <unavailable>\n";
        }

        if (alu_detail_.has_result) {
            oss << "  Result: "
                << alu_detail_.result
                << "\n";
        }

        if (!alu_detail_.note.empty()) {
            oss << "  Note: "
                << alu_detail_.note
                << "\n";
        }

        oss << "\n";
    }

    oss << "Memory Trace Detail:\n";
    if (!memory_detail_.active) {
        oss << "  None\n\n";
    } else {
        oss << "  Operation: "
            << memory_detail_.operation
            << "\n";

        if (memory_detail_.has_address) {
            oss << "  Address "
                << memory_detail_.address_text
                << " = "
                << memory_detail_.address
                << "\n";
        } else if (!memory_detail_.address_text.empty()) {
            oss << "  Address "
                << memory_detail_.address_text
                << " = <unavailable>\n";
        }

        if (!memory_detail_.route.empty()) {
            oss << "  Route: "
                << memory_detail_.route
                << "\n";
        }

        if (memory_detail_.has_value) {
            oss << "  Value";

            if (!memory_detail_.value_text.empty()) {
                oss << " "
                    << memory_detail_.value_text;
            }

            oss << " = "
                << memory_detail_.value
                << "\n";
        }

        if (!memory_detail_.destination.empty()) {
            oss << "  Destination: "
                << memory_detail_.destination
                << "\n";
        }

        if (!memory_detail_.note.empty()) {
            oss << "  Note: "
                << memory_detail_.note
                << "\n";
        }

        oss << "\n";
    }

    oss << "Stack Trace Detail:\n";
    if (!stack_detail_.active) {
        oss << "  None\n\n";
    } else {
        oss << "  Operation: "
            << stack_detail_.operation
            << "\n";

        if (!stack_detail_.operand_text.empty()) {
            oss << "  Operand: "
                << stack_detail_.operand_text
                << "\n";
        }

        if (stack_detail_.has_stack_address) {
            oss << "  Stack Address: "
                << stack_detail_.stack_address
                << "\n";
        }

        if (stack_detail_.has_sp_change) {
            oss << "  SP: "
                << stack_detail_.sp_before
                << " -> "
                << stack_detail_.sp_after
                << "\n";
        }

        if (stack_detail_.has_value) {
            oss << "  Value: "
                << stack_detail_.value
                << "\n";
        }

        if (stack_detail_.has_return_address) {
            oss << "  Return Address: "
                << stack_detail_.return_address
                << "\n";
        }

        if (!stack_detail_.destination.empty()) {
            oss << "  Destination: "
                << stack_detail_.destination
                << "\n";
        }

        if (stack_detail_.has_target) {
            oss << "  Target: "
                << stack_detail_.target
                << "\n";
        }

        if (!stack_detail_.note.empty()) {
            oss << "  Note: "
                << stack_detail_.note
                << "\n";
        }

        oss << "\n";
    }

    oss << "Before:\n";
    oss << before_.summary();

    oss << "\nAfter:\n";
    oss << after_.summary();

    oss << "\nChanged Registers:\n";
    if (changed_registers_.empty()) {
        oss << "  None\n";
    } else {
        for (const auto& change : changed_registers_) {
            oss << "  "
                << change.name
                << ": "
                << change.before
                << " -> "
                << change.after
                << "\n";
        }
    }

    oss << "\nChanged Flags:\n";
    if (changed_flags_.empty()) {
        oss << "  None\n";
    } else {
        for (const auto& change : changed_flags_) {
            oss << "  "
                << change.name
                << ": "
                << (change.before ? 1 : 0)
                << " -> "
                << (change.after ? 1 : 0)
                << "\n";
        }
    }

    oss << "\nChanged Memory:\n";
    if (changed_memory_.empty()) {
        oss << "  None\n";
    } else {
        for (const auto& change : changed_memory_) {
            oss << "  Memory["
                << change.address
                << "]: "
                << change.before
                << " -> "
                << change.after
                << "\n";
        }
    }

    if (before_.sp() != after_.sp()) {
        oss << "\nChanged Stack Pointer:\n";
        oss << "  SP: "
            << before_.sp()
            << " -> "
            << after_.sp()
            << "\n";
    }

    oss << "\nNextPC:\n";
    oss << "  " << formatPc(pcAfter()) << "\n";

    oss << "\nError:\n";
    if (hasError()) {
        oss << "  " << error_message_ << "\n";
    } else {
        oss << "  None\n";
    }

    return oss.str();
}

void TraceEvent::analyzeAluDetail() {
    alu_detail_ = ALUTraceDetail{};

    const Opcode opcode = instruction_.opcode();

    if (!isAluTraceOpcode(opcode)) {
        return;
    }

    alu_detail_.active = true;
    alu_detail_.operation =
        action_.empty() || action_ == "UNKNOWN"
            ? aluOperationName(opcode)
            : action_;

    const Operand& dst = instruction_.dst();
    const Operand& src = instruction_.src();

    if (!dst.isNone()) {
        alu_detail_.lhs_text = dst.toString();

        std::string note;
        std::int64_t value = 0;

        if (tryReadTraceOperandValue(before_, dst, value, note)) {
            alu_detail_.lhs = value;
            alu_detail_.has_lhs = true;
        } else {
            appendAluNote(
                alu_detail_,
                "lhs unavailable: " + note
            );
        }

        if (dst.isRegister()) {
            alu_detail_.destination =
                registerNameText(dst.asRegister());
        } else {
            alu_detail_.destination = dst.toString();
        }
    }

    if (opcode != Opcode::NOT && !src.isNone()) {
        alu_detail_.rhs_text = src.toString();

        std::string note;
        std::int64_t value = 0;

        if (tryReadTraceOperandValue(before_, src, value, note)) {
            alu_detail_.rhs = value;
            alu_detail_.has_rhs = true;
        } else {
            appendAluNote(
                alu_detail_,
                "rhs unavailable: " + note
            );
        }
    }

    if (aluOpcodeWritesDestination(opcode)) {
        if (dst.isRegister()) {
            alu_detail_.result =
                after_.registers().get(dst.asRegister());
            alu_detail_.has_result = true;

            if (alu_detail_.destination.empty()) {
                alu_detail_.destination =
                    registerNameText(dst.asRegister());
            }
        } else if (!changed_registers_.empty()) {
            alu_detail_.result = changed_registers_.front().after;
            alu_detail_.destination = changed_registers_.front().name;
            alu_detail_.has_result = true;
        }
    } else if (opcode == Opcode::CMP) {
        appendAluNote(
            alu_detail_,
            "comparison result updates flags and is not written to a register"
        );
    } else if (opcode == Opcode::TEST) {
        appendAluNote(
            alu_detail_,
            "bit-test result updates flags and is not written to a register"
        );
    }
}


void TraceEvent::analyzeMemoryDetail() {
    memory_detail_ = MemoryTraceDetail{};

    const Opcode opcode = instruction_.opcode();

    if (!isMemoryTraceOpcode(opcode)) {
        return;
    }

    memory_detail_.active = true;
    memory_detail_.operation =
        action_.empty() || action_ == "UNKNOWN"
            ? memoryOperationName(opcode)
            : action_;

    const Operand& dst = instruction_.dst();
    const Operand& src = instruction_.src();

    if (opcode == Opcode::LOAD) {
        memory_detail_.is_read = true;
        memory_detail_.address_text = src.toString();

        std::string note;
        std::size_t address = 0;

        if (tryResolveTraceMemoryAddress(before_, src, address, note)) {
            memory_detail_.address = address;
            memory_detail_.has_address = true;
            memory_detail_.route = memoryRouteText(address);
        } else {
            appendMemoryNote(
                memory_detail_,
                "address unavailable: " + note
            );
        }

        if (dst.isRegister()) {
            memory_detail_.destination = registerNameText(dst.asRegister());
            memory_detail_.value = after_.registers().get(dst.asRegister());
            memory_detail_.has_value = true;
            memory_detail_.value_text = memory_detail_.destination;
        } else if (!changed_registers_.empty()) {
            memory_detail_.destination = changed_registers_.front().name;
            memory_detail_.value = changed_registers_.front().after;
            memory_detail_.has_value = true;
            memory_detail_.value_text = memory_detail_.destination;
        } else {
            appendMemoryNote(
                memory_detail_,
                "load value unavailable because no destination register changed"
            );
        }

        return;
    }

    if (opcode == Opcode::STORE) {
        memory_detail_.is_write = true;
        memory_detail_.address_text = dst.toString();
        memory_detail_.value_text = src.toString();

        std::string addressNote;
        std::size_t address = 0;

        if (tryResolveTraceMemoryAddress(before_, dst, address, addressNote)) {
            memory_detail_.address = address;
            memory_detail_.has_address = true;
            memory_detail_.route = memoryRouteText(address);
        } else {
            appendMemoryNote(
                memory_detail_,
                "address unavailable: " + addressNote
            );
        }

        std::string valueNote;
        std::int64_t value = 0;

        if (tryReadTraceOperandValue(before_, src, value, valueNote)) {
            memory_detail_.value = value;
            memory_detail_.has_value = true;
        } else {
            appendMemoryNote(
                memory_detail_,
                "value unavailable: " + valueNote
            );
        }

        if (memory_detail_.has_address &&
            memory_detail_.route.find("MMIO") != std::string::npos &&
            changed_memory_.empty()) {
            appendMemoryNote(
                memory_detail_,
                "MMIO write is visible through the device, not normal RAM snapshot"
            );
        }
    }
}

void TraceEvent::analyzeStackDetail() {
    stack_detail_ = StackTraceDetail{};

    const Opcode opcode = instruction_.opcode();

    if (!isStackTraceOpcode(opcode)) {
        return;
    }

    stack_detail_.active = true;
    stack_detail_.operation =
        action_.empty() || action_ == "UNKNOWN"
            ? stackOperationName(opcode)
            : action_;

    stack_detail_.sp_before = before_.sp();
    stack_detail_.sp_after = after_.sp();
    stack_detail_.has_sp_change = before_.sp() != after_.sp();

    const Operand& dst = instruction_.dst();

    if (!dst.isNone()) {
        stack_detail_.operand_text = dst.toString();
    }

    if (opcode == Opcode::PUSH) {
        stack_detail_.is_push = true;
        stack_detail_.stack_address = before_.sp();
        stack_detail_.has_stack_address = true;

        std::string note;
        std::int64_t value = 0;

        if (tryReadTraceOperandValue(before_, dst, value, note)) {
            stack_detail_.value = value;
            stack_detail_.has_value = true;
        } else {
            appendStackNote(
                stack_detail_,
                "push value unavailable: " + note
            );
        }

        MemoryChange pushed{};
        if (findMemoryChangeAt(changed_memory_, stack_detail_.stack_address, pushed)) {
            stack_detail_.value = pushed.after;
            stack_detail_.has_value = true;
        }

        return;
    }

    if (opcode == Opcode::POP) {
        stack_detail_.is_pop = true;
        stack_detail_.stack_address = after_.sp();
        stack_detail_.has_stack_address = true;

        if (dst.isRegister()) {
            stack_detail_.destination = registerNameText(dst.asRegister());
            stack_detail_.value = after_.registers().get(dst.asRegister());
            stack_detail_.has_value = true;
        } else if (!changed_registers_.empty()) {
            stack_detail_.destination = changed_registers_.front().name;
            stack_detail_.value = changed_registers_.front().after;
            stack_detail_.has_value = true;
        } else {
            try {
                stack_detail_.value =
                    before_.memory().read(stack_detail_.stack_address);
                stack_detail_.has_value = true;
            } catch (const std::exception& ex) {
                appendStackNote(
                    stack_detail_,
                    std::string("pop value unavailable: ") + ex.what()
                );
            }
        }

        return;
    }

    if (opcode == Opcode::CALL) {
        stack_detail_.is_call = true;
        stack_detail_.stack_address = before_.sp();
        stack_detail_.has_stack_address = true;
        stack_detail_.target = after_.pc();
        stack_detail_.has_target = true;

        MemoryChange pushed{};
        if (findMemoryChangeAt(changed_memory_, stack_detail_.stack_address, pushed)) {
            stack_detail_.return_address = pushed.after;
            stack_detail_.has_return_address = true;
            stack_detail_.value = pushed.after;
            stack_detail_.has_value = true;
        } else {
            appendStackNote(
                stack_detail_,
                "return address write was not found in changed memory"
            );
        }

        return;
    }

    if (opcode == Opcode::RET) {
        stack_detail_.is_ret = true;
        stack_detail_.stack_address = after_.sp();
        stack_detail_.has_stack_address = true;
        stack_detail_.return_address = static_cast<std::int64_t>(after_.pc());
        stack_detail_.has_return_address = true;
        stack_detail_.target = after_.pc();
        stack_detail_.has_target = true;

        try {
            stack_detail_.value =
                before_.memory().read(stack_detail_.stack_address);
            stack_detail_.has_value = true;
        } catch (const std::exception& ex) {
            appendStackNote(
                stack_detail_,
                std::string("return value unavailable: ") + ex.what()
            );
        }
    }
}



void TraceEvent::analyzeChanges() {
    analyzeRegisterChanges();
    analyzeFlagChanges();
    analyzeMemoryChanges();
}

void TraceEvent::analyzeRegisterChanges() {
    const auto before_registers = before_.registers().snapshot();
    const auto after_registers = after_.registers().snapshot();

    for (std::size_t i = 0; i < RegisterFile::kRegisterCount; ++i) {
        const auto before_value = before_registers[i];
        const auto after_value = after_registers[i];

        if (before_value != after_value) {
            auto reg = static_cast<RegisterName>(i);

            changed_registers_.push_back(
                RegisterChange{
                    RegisterFile::registerNameToString(reg),
                    before_value,
                    after_value
                }
            );
        }
    }
}

void TraceEvent::analyzeFlagChanges() {
    const auto& before_flags = before_.flags();
    const auto& after_flags = after_.flags();

    if (before_flags.zero() != after_flags.zero()) {
        changed_flags_.push_back(
            FlagChange{"ZF", before_flags.zero(), after_flags.zero()}
        );
    }

    if (before_flags.sign() != after_flags.sign()) {
        changed_flags_.push_back(
            FlagChange{"SF", before_flags.sign(), after_flags.sign()}
        );
    }

    if (before_flags.overflow() != after_flags.overflow()) {
        changed_flags_.push_back(
            FlagChange{"OF", before_flags.overflow(), after_flags.overflow()}
        );
    }

    if (before_flags.carry() != after_flags.carry()) {
        changed_flags_.push_back(
            FlagChange{"CF", before_flags.carry(), after_flags.carry()}
        );
    }
}

void TraceEvent::analyzeMemoryChanges() {
    const auto before_memory = before_.memory().snapshot();
    const auto after_memory = after_.memory().snapshot();

    const std::size_t count =
        before_memory.size() < after_memory.size()
            ? before_memory.size()
            : after_memory.size();

    for (std::size_t address = 0; address < count; ++address) {
        const auto before_value = before_memory[address];
        const auto after_value = after_memory[address];

        if (before_value != after_value) {
            changed_memory_.push_back(
                MemoryChange{
                    address,
                    before_value,
                    after_value
                }
            );
        }
    }
}

void TraceEvent::analyzeVisualMetadata() {
    stage_ = "EXECUTE";
    action_ = "UNKNOWN";
    datapath_nodes_.clear();

    addDatapathNode("PC");
    addDatapathNode("InstructionMemory");
    addDatapathNode("Decoder");

    switch (instruction_.opcode()) {
    case Opcode::NOP:
        action_ = "NOP";
        addDatapathNode("ControlUnit");
        break;

    case Opcode::HALT:
        action_ = "HALT";
        addDatapathNode("ControlUnit");
        break;

    case Opcode::MOV:
        action_ = "REGISTER_TRANSFER";
        addDatapathNode("RegisterFile");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::LOAD:
        action_ = "MEMORY_READ";
        addDatapathNode("AddressUnit");
        addDatapathNode("Memory/MMIO");
        addDatapathNode("RegisterFile");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::STORE:
        action_ = "MEMORY_WRITE";
        addDatapathNode("AddressUnit");
        addDatapathNode("RegisterFile");
        addDatapathNode("Memory/MMIO");
        addDatapathNode("Flags");
        break;

    case Opcode::ADD:
        action_ = "ALU_ADD";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::SUB:
        action_ = "ALU_SUB";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::MUL:
        action_ = "ALU_MUL";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::DIV:
        action_ = "ALU_DIV";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::CMP:
        action_ = "ALU_COMPARE";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        break;

    case Opcode::TEST:
        action_ = "ALU_TEST";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        break;

    case Opcode::JMP:
        action_ = "JUMP";
        addDatapathNode("ControlUnit");
        addDatapathNode("PC");
        break;

    case Opcode::JE:
    case Opcode::JNE:
    case Opcode::JG:
    case Opcode::JL:
        action_ = "CONDITIONAL_BRANCH";
        addDatapathNode("Flags");
        addDatapathNode("ControlUnit");
        addDatapathNode("PC");
        break;

    case Opcode::PUSH:
        action_ = "STACK_PUSH";
        addDatapathNode("RegisterFile");
        addDatapathNode("Stack");
        addDatapathNode("SP");
        break;

    case Opcode::POP:
        action_ = "STACK_POP";
        addDatapathNode("Stack");
        addDatapathNode("SP");
        addDatapathNode("RegisterFile");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::CALL:
        action_ = "CALL";
        addDatapathNode("ControlUnit");
        addDatapathNode("Stack");
        addDatapathNode("PC");
        break;

    case Opcode::RET:
        action_ = "RETURN";
        addDatapathNode("Stack");
        addDatapathNode("PC");
        break;

    case Opcode::INT:
        action_ = "SOFTWARE_INTERRUPT";
        addDatapathNode("InterruptController");
        addDatapathNode("Stack");
        addDatapathNode("Flags");
        addDatapathNode("PC");
        break;

    case Opcode::IRET:
        action_ = "INTERRUPT_RETURN";
        addDatapathNode("Stack");
        addDatapathNode("Flags");
        addDatapathNode("PC");
        break;

    case Opcode::EI:
        action_ = "INTERRUPT_ENABLE";
        addDatapathNode("InterruptController");
        addDatapathNode("ControlUnit");
        break;

    case Opcode::DI:
        action_ = "INTERRUPT_DISABLE";
        addDatapathNode("InterruptController");
        addDatapathNode("ControlUnit");
        break;

    case Opcode::AND:
        action_ = "ALU_AND";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::OR:
        action_ = "ALU_OR";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::XOR:
        action_ = "ALU_XOR";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::NOT:
        action_ = "ALU_NOT";
        addDatapathNode("RegisterFile");
        addDatapathNode("ALU");
        addDatapathNode("Flags");
        addDatapathNode("Writeback");
        break;

    case Opcode::Invalid:
    default:
        action_ = "INVALID";
        addDatapathNode("ControlUnit");
        break;
    }

    if (!changed_registers_.empty()) {
        addDatapathNode("RegisterFile");
    }

    if (!changed_flags_.empty()) {
        addDatapathNode("Flags");
    }

    if (!changed_memory_.empty()) {
        addDatapathNode("Memory");
    }

    if (before_.sp() != after_.sp()) {
        addDatapathNode("Stack");
        addDatapathNode("SP");
    }

    if (before_.pc() != after_.pc()) {
        addDatapathNode("PC");
    }
}

void TraceEvent::addDatapathNode(std::string node) {
    if (
        std::find(
            datapath_nodes_.begin(),
            datapath_nodes_.end(),
            node
        ) == datapath_nodes_.end()
    ) {
        datapath_nodes_.push_back(std::move(node));
    }
}

std::string TraceEvent::boolToString(bool value) {
    return value ? "true" : "false";
}

std::string TraceEvent::formatPc(std::size_t pc) {
    std::ostringstream oss;

    oss << std::setw(4)
        << std::setfill('0')
        << pc;

    return oss.str();
}

std::string TraceEvent::joinDatapathNodes(
    const std::vector<std::string>& nodes
) {
    std::ostringstream oss;

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) {
            oss << " -> ";
        }

        oss << nodes[i];
    }

    return oss.str();
}

} // namespace zero_cpu
