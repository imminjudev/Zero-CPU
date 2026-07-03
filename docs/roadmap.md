# Zero-CPU Roadmap

## 1. Project Vision

Zero-CPU는 단순한 명령어 인터프리터가 아니라, 직접 설계한 가상 ISA와 바이너리 포맷을 기반으로 동작하는 CPU 실행 플랫폼을 목표로 한다.

최종 목표는 다음과 같다.

```text
.zasm source
    ↓
Assembler
    ↓
.zbin virtual binary
    ↓
Binary Loader
    ↓
Zero-CPU Engine
    ↓
Debugger / PC Application
    ↓
BIO-OS Runtime Demo
```

Zero-CPU는 다음 요소를 포함하는 시스템 프로그래밍 프로젝트로 발전한다.

```text
Virtual ISA
Assembler
Virtual Binary Format
Byte-addressable Memory
Fetch-Decode-Execute CPU Engine
Trace Logger
CLI Debugger
PC Application
BIO-OS Runtime Demo
```

---

## 2. Core Direction

Zero-CPU의 핵심 방향은 다음과 같다.

1. CPU와 Assembler를 명확히 분리한다.
2. CPU는 `.zasm` 소스를 직접 실행하지 않는다.
3. Assembler는 `.zasm`을 `.zbin` 가상 바이너리로 변환한다.
4. CPU는 `.zbin` 바이트코드를 메모리에 적재한 뒤 실행한다.
5. Program Counter는 명령어 인덱스가 아니라 바이트 주소를 가리킨다.
6. Memory는 byte-addressable 구조를 사용한다.
7. Flags는 bool 필드가 아니라 `uint32_t` bitmask register로 관리한다.
8. 실행 과정은 Trace Logger를 통해 상태 전이로 기록된다.
9. 최종적으로 PC 애플리케이션에서 CPU 상태를 시각화한다.
10. BIO-OS는 Zero-CPU 위에서 실행되는 게스트 OS 데모로 구현한다.

---

## 3. Long-Term Goal

최종적으로 Zero-CPU는 다음과 같은 형태의 PC 애플리케이션이 될 수 있다.

```text
+------------------------------------------------+
| Zero-CPU Studio                                |
+------------------------------------------------+
| Assembly Editor       | Register View          |
|                       | R0 R1 R2 R3 ...         |
| MOV R1, 10            | PC / SP / FLAGS         |
| CALL main             |                         |
|                       | Memory View             |
+-----------------------+-------------------------+
| Trace Log                                       |
| PC=0000 | MOV R1, 10 | R1:0->10                |
+------------------------------------------------+
| [Assemble] [Run] [Step] [Reset] [Export Trace] |
+------------------------------------------------+
```

주요 기능은 다음과 같다.

```text
.zasm 파일 열기
.zasm → .zbin assemble
.zbin 실행
step 실행
register 보기
memory 보기
stack 보기
flags 보기
trace 보기
BIO-OS 실행
```

---

## 4. BIO-OS Direction

BIO-OS는 Zero-CPU 위에서 실행되는 게스트 프로그램이자 미니 운영체제 데모이다.

초기 BIO-OS는 실제 운영체제 전체를 구현하는 것이 아니라, Zero-CPU 실행 환경을 보여주는 데모 프로그램으로 시작한다.

BIO-OS v0.1 목표:

```text
부트 프로그램 형태의 .zasm 작성
초기 메모리 설정
간단한 루틴 호출
상태 메시지 저장
syscall 흉내
메모리/스택/레지스터 상태 시각화
```

향후 확장 가능 기능:

```text
System call
Interrupt
Timer
UART-like output
Process table
Simple scheduler
Virtual file system
Program loader
```

BIO-OS는 Zero-CPU의 목적지이지만, Zero-CPU Engine이 안정화되기 전까지는 본격 구현을 미룬다.

---

## 5. Phase 0: Current Prototype

현재 Zero-CPU는 v0.1 prototype 단계이다.

현재 구현된 요소:

```text
C++ 기반 CPUState
RegisterFile
Memory
Flags
Opcode
Operand
Instruction
CPU execution engine
TraceEvent
TraceLogger
Basic .zasm Assembler
CLI 실행 프로그램
```

현재 구조:

```text
.zasm
    ↓
Assembler
    ↓
vector<Instruction>
    ↓
CPU executes Instruction objects
```

현재 구조의 한계:

```text
CPU가 내부 Instruction 객체에 직접 의존한다
CPU와 Assembler가 완전히 분리되지 않았다
바이너리 파일이 없다
Program Counter가 명령어 인덱스 기반이다
Memory가 byte-addressable 구조가 아니다
```

---

## 6. Phase 1: Documentation Stabilization

목표:

```text
교수님 피드백을 반영한 설계 문서 정리
```

작성할 문서:

```text
docs/virtual-binary-format.md
docs/instruction-encoding.md
docs/memory-model.md
docs/flags-register.md
docs/roadmap.md
```

완료 기준:

```text
Virtual Binary Format 설명 완료
Instruction Encoding 설명 완료
Byte-addressable Memory 설명 완료
Flags Register 설명 완료
전체 개발 Roadmap 정리 완료
```

권장 커밋:

```bash
git commit -m "docs: add virtual binary architecture roadmap"
```

---

## 7. Phase 2: Flags Register Refactoring

목표:

```text
bool 기반 Flags를 uint32_t bitmask 기반 Flags Register로 변경
```

수정 대상:

```text
include/zero_cpu/core/Flags.hpp
src/core/Flags.cpp
```

설계:

```cpp
std::uint32_t bits_;
```

지원 플래그:

```text
CF = 1u << 0
ZF = 1u << 6
SF = 1u << 7
OF = 1u << 11
```

유지할 public API:

```cpp
bool zero() const;
bool sign() const;
bool overflow() const;
bool carry() const;

void setZero(bool value);
void setSign(bool value);
void setOverflow(bool value);
void setCarry(bool value);

void updateZeroAndSign(std::int64_t result);
```

추가할 API:

```cpp
std::uint32_t raw() const;
void setRaw(std::uint32_t value);
```

완료 기준:

```text
기존 CPU 실행 결과가 그대로 유지된다
Trace 출력이 정상적으로 작동한다
내부 Flags 표현이 uint32_t bitmask로 변경된다
```

권장 커밋:

```bash
git commit -m "refactor: implement flags as bitmask register"
```

---

## 8. Phase 3: Byte-Addressable Memory

목표:

```text
Memory를 int64_t cell 기반에서 uint8_t byte 기반으로 변경
```

현재 구조:

```cpp
std::vector<std::int64_t> cells_;
```

목표 구조:

```cpp
std::vector<std::uint8_t> bytes_;
```

추가할 기능:

```cpp
std::uint8_t readU8(std::size_t address) const;
void writeU8(std::size_t address, std::uint8_t value);

std::uint32_t readU32(std::size_t address, Endianness endian) const;
void writeU32(std::size_t address, std::uint32_t value, Endianness endian);

std::uint64_t readU64(std::size_t address, Endianness endian) const;
void writeU64(std::size_t address, std::uint64_t value, Endianness endian);

std::int64_t readI64(std::size_t address, Endianness endian) const;
void writeI64(std::size_t address, std::int64_t value, Endianness endian);

std::vector<std::uint8_t> readBytes(
    std::size_t address,
    std::size_t count
) const;
```

추가할 enum:

```cpp
enum class Endianness {
    Little,
    Big
};
```

초기 메모리 설정:

```text
Default Memory Size = 4096 bytes
Code Base = 0
Stack Base = 2048
Stack Limit = 4096
```

완료 기준:

```text
Memory가 byte-addressable 구조로 동작한다
Little Endian read/write가 가능하다
Big Endian read/write 설계가 반영된다
Bounds checking이 적용된다
기존 LOAD/STORE/PUSH/POP/CALL/RET이 8-byte 단위로 동작한다
```

권장 커밋:

```bash
git commit -m "refactor: make memory byte-addressable"
```

---

## 9. Phase 4: Virtual Binary Format Implementation

목표:

```text
.zbin 가상 바이너리 파일 구조 구현
```

새로 만들 파일:

```text
include/zero_cpu/binary/BinaryFormat.hpp
include/zero_cpu/binary/BinaryProgram.hpp
include/zero_cpu/binary/BinaryWriter.hpp
include/zero_cpu/binary/BinaryReader.hpp

src/binary/BinaryWriter.cpp
src/binary/BinaryReader.cpp
```

Binary Header:

```text
Offset | Size | Field
-------|------|------
0      | 4    | Magic = "ZCPU"
4      | 1    | Major
5      | 1    | Minor
6      | 1    | Endianness
7      | 1    | Reserved
8      | 4    | Entry Point
12     | 4    | Code Size
```

기본값:

```text
Magic = "ZCPU"
Major = 0
Minor = 2
Endianness = Little
Entry Point = 0
Header Size = 16 bytes
Instruction Size = 24 bytes
```

완료 기준:

```text
.zbin header를 쓸 수 있다
.zbin header를 읽을 수 있다
Magic number 검사가 가능하다
Code size 검사가 가능하다
Endianness 정보가 저장된다
```

권장 커밋:

```bash
git commit -m "feat: add virtual binary format"
```

---

## 10. Phase 5: Instruction Encoder and Decoder

목표:

```text
Instruction 객체를 24-byte bytecode로 인코딩하고, bytecode를 다시 DecodedInstruction으로 디코딩한다
```

새로 만들 파일:

```text
include/zero_cpu/isa/EncodedInstruction.hpp
include/zero_cpu/isa/InstructionEncoder.hpp
include/zero_cpu/isa/InstructionDecoder.hpp

src/isa/InstructionEncoder.cpp
src/isa/InstructionDecoder.cpp
```

Instruction Layout:

```text
Offset | Size | Field
-------|------|------
0      | 1    | opcode
1      | 1    | dst_type
2      | 1    | src_type
3      | 1    | reserved
4      | 8    | dst_payload
12     | 8    | src_payload
20     | 4    | reserved
```

완료 기준:

```text
MOV R1, 10을 bytecode로 인코딩할 수 있다
ADD R1, R2를 bytecode로 인코딩할 수 있다
STORE [100], R1을 bytecode로 인코딩할 수 있다
CALL label의 label을 code address로 변환할 수 있다
24-byte bytecode를 DecodedInstruction으로 복원할 수 있다
invalid opcode를 감지할 수 있다
invalid operand type을 감지할 수 있다
```

권장 커밋:

```bash
git commit -m "feat: add instruction encoder and decoder"
```

---

## 11. Phase 6: Assembler to Binary Writer

목표:

```text
Assembler가 .zasm을 직접 CPU에 넘기지 않고 .zbin 파일로 저장하게 한다
```

현재 구조:

```text
.zasm
    ↓
Assembler
    ↓
vector<Instruction>
    ↓
CPU
```

목표 구조:

```text
.zasm
    ↓
Assembler
    ↓
Instruction IR
    ↓
Instruction Encoder
    ↓
Binary Writer
    ↓
.zbin
```

수정 대상:

```text
include/zero_cpu/assembler/Assembler.hpp
src/assembler/Assembler.cpp
tools/zero_cli.cpp
```

새 CLI 동작 예:

```bash
zero_cli assemble examples/function_call.zasm examples/function_call.zbin
```

완료 기준:

```text
.zasm 파일을 읽는다
라벨을 수집한다
라벨을 instruction index에서 byte address로 변환한다
Instruction을 bytecode로 인코딩한다
.zbin 파일을 디스크에 저장한다
```

권장 커밋:

```bash
git commit -m "feat: assemble zasm into zbin"
```

---

## 12. Phase 7: Binary Loader and Fetch-Decode-Execute CPU

목표:

```text
CPU가 vector<Instruction>이 아니라 메모리에 적재된 bytecode를 Fetch-Decode-Execute한다
```

현재 실행 방식:

```cpp
Instruction instruction = program_[pc];
execute(instruction);
pc++;
```

목표 실행 방식:

```cpp
auto bytes = memory.readBytes(pc, 24);
auto instruction = decoder.decode(bytes);
execute(instruction);
pc += 24;
```

분기 명령어:

```text
PC = target_byte_address
```

CALL:

```text
push PC + 24
PC = target_byte_address
```

RET:

```text
PC = pop return_address
```

완료 기준:

```text
CPU가 .zbin을 로드해서 실행한다
PC가 byte address로 동작한다
Fetch 단계가 구현된다
Decode 단계가 구현된다
Execute 단계가 DecodedInstruction 기반으로 동작한다
CALL/RET이 byte address 기반으로 동작한다
Trace가 byte PC를 출력한다
```

권장 커밋:

```bash
git commit -m "refactor: execute bytecode from virtual memory"
```

---

## 13. Phase 8: CLI Debugger

목표:

```text
Zero-CPU를 대화형 CLI 디버거로 실행할 수 있게 한다
```

지원 명령어:

```text
load <file.zbin>
run
step
reset
regs
flags
mem <address> <count>
stack
trace
last
quit
help
```

예:

```text
zero-cpu> load examples/function_call.zbin
zero-cpu> regs
zero-cpu> step
zero-cpu> mem 100 16
zero-cpu> trace
```

완료 기준:

```text
.zbin 파일을 CLI에서 로드할 수 있다
step 실행이 가능하다
run 실행이 가능하다
레지스터 상태를 볼 수 있다
메모리 상태를 볼 수 있다
스택 상태를 볼 수 있다
Trace를 볼 수 있다
```

권장 커밋:

```bash
git commit -m "feat: add interactive cli debugger"
```

---

## 14. Phase 9: Zero-CPU Studio PC Application

목표:

```text
Zero-CPU 실행 엔진을 PC 애플리케이션에서 시각화한다
```

가능한 기술 선택:

```text
Qt + C++
Dear ImGui + C++
Electron + C++ backend
Tauri + C++ backend
```

초기 추천:

```text
Dear ImGui + C++
```

이유:

```text
C++ 프로젝트와 자연스럽게 연결됨
디버거 UI 만들기 좋음
게임/엔진 느낌의 실시간 UI에 적합함
무겁지 않음
포트폴리오 시각화에 좋음
```

초기 UI 구성:

```text
Assembly Editor
Binary Info View
Register View
Flags View
Memory View
Stack View
Trace Log
Control Buttons
```

완료 기준:

```text
.zasm 파일 열기 가능
Assemble 버튼으로 .zbin 생성 가능
Run 버튼으로 실행 가능
Step 버튼으로 한 명령어 실행 가능
레지스터/메모리/스택/플래그/Trace가 UI에 표시됨
```

권장 커밋:

```bash
git commit -m "feat: add zero-cpu studio prototype"
```

---

## 15. Phase 10: BIO-OS Runtime Demo

목표:

```text
Zero-CPU 위에서 실행되는 BIO-OS 데모 프로그램을 만든다
```

초기 BIO-OS는 다음 형태로 시작한다.

```text
bio_os.zasm
    ↓
bio_os.zbin
    ↓
Zero-CPU Engine
    ↓
Zero-CPU Studio에서 실행 상태 시각화
```

BIO-OS v0.1 기능:

```text
boot routine
memory initialization
simple syscall convention
status code writing
basic loop
halt routine
```

예상 파일:

```text
bio-os/
├─ boot.zasm
├─ kernel.zasm
├─ syscall.md
└─ README.md
```

초기 syscall 설계 예:

```text
R0 = syscall number
R1 = argument 1
R2 = argument 2
R3 = return value
```

예:

```asm
MOV R0, 1
MOV R1, 3000
CALL syscall
```

완료 기준:

```text
BIO-OS zasm 프로그램이 assemble된다
BIO-OS zbin이 실행된다
Zero-CPU Studio에서 BIO-OS 상태를 볼 수 있다
간단한 syscall 또는 상태 출력이 가능하다
```

권장 커밋:

```bash
git commit -m "feat: add bio-os runtime demo"
```

---

## 16. Suggested Milestones

## Milestone A: CPU Architecture Refactor

목표:

```text
Zero-CPU가 Instruction 객체 인터프리터에서 bytecode CPU로 전환된다
```

포함 Phase:

```text
Phase 1
Phase 2
Phase 3
Phase 4
Phase 5
Phase 6
Phase 7
```

결과물:

```text
.zasm → .zbin
.zbin → memory
Fetch-Decode-Execute
```

---

## Milestone B: Developer Tooling

목표:

```text
Zero-CPU를 직접 실행하고 디버깅할 수 있는 도구를 만든다
```

포함 Phase:

```text
Phase 8
```

결과물:

```text
Interactive CLI Debugger
```

---

## Milestone C: Visual Platform

목표:

```text
Zero-CPU를 시각적으로 관찰할 수 있는 PC 애플리케이션을 만든다
```

포함 Phase:

```text
Phase 9
```

결과물:

```text
Zero-CPU Studio
```

---

## Milestone D: BIO-OS Demo

목표:

```text
Zero-CPU 위에서 실행되는 게스트 OS 데모를 만든다
```

포함 Phase:

```text
Phase 10
```

결과물:

```text
BIO-OS Runtime Demo
```

---

## 17. Near-Term Task List

가장 가까운 작업 순서는 다음과 같다.

```text
1. docs/roadmap.md 작성
2. Flags bitmask 리팩터링 완료
3. Memory byte-addressable 설계 반영
4. Endianness enum 추가
5. Memory readU8/writeU8 구현
6. Memory readU64/writeU64 구현
7. BinaryFormat.hpp 작성
8. BinaryWriter 작성
9. BinaryReader 작성
10. InstructionEncoder 작성
11. InstructionDecoder 작성
12. Assembler가 .zbin을 만들도록 변경
13. CPU가 .zbin을 로드해서 실행하도록 변경
```

---

## 18. Risk Management

프로젝트가 커지면서 주의해야 할 위험은 다음과 같다.

```text
목표가 너무 커져서 구현이 멈추는 것
BIO-OS를 너무 빨리 시작하는 것
PC 앱을 CPU 엔진 완성 전에 시작하는 것
문서와 코드가 서로 달라지는 것
기능을 한 번에 너무 많이 바꾸는 것
```

대응 전략:

```text
한 번에 하나의 Phase만 진행한다
각 Phase마다 커밋을 남긴다
CPU 엔진이 안정화되기 전에는 BIO-OS를 시작하지 않는다
문서 변경 후 코드 변경 순서를 지킨다
작은 예제 프로그램으로 매번 회귀 테스트한다
```

---

## 19. Current Priority

현재 최우선 순위는 다음과 같다.

```text
Priority 1: Flags bitmask refactoring
Priority 2: Byte-addressable Memory
Priority 3: Virtual Binary Format implementation
Priority 4: Instruction Encoder/Decoder
Priority 5: Fetch-Decode-Execute CPU
```

BIO-OS와 PC Application은 중요하지만, 아직은 후순위이다.

```text
BIO-OS = final demo target
PC Application = visualization layer
Zero-CPU Engine = current core task
```

---

## 20. Summary

Zero-CPU는 다음 방향으로 발전한다.

```text
Prototype Interpreter
    ↓
Virtual Binary CPU
    ↓
CLI Debugger
    ↓
PC Application
    ↓
BIO-OS Runtime Demo
```

현재 가장 중요한 것은 Zero-CPU Engine을 안정화하는 것이다.

특히 다음 세 가지가 핵심이다.

```text
.zasm과 CPU의 분리
.zbin virtual binary format
byte-addressed Fetch-Decode-Execute
```

이 구조가 완성되면 Zero-CPU는 단순한 학습용 인터프리터가 아니라, 직접 설계한 가상 CPU 실행 플랫폼이 된다.
