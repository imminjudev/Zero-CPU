# Zero-CPU Virtual Binary Format

## 1. Overview

Zero-CPU는 초기 구현 단계에서 `.zasm` 어셈블리 소스 코드를 직접 파싱하여 내부 `Instruction` 객체로 변환한 뒤 실행하는 구조를 사용했다.

그러나 이 방식은 어셈블러와 CPU 실행 엔진이 강하게 연결되어 있으며, 실제 CPU가 기계어를 메모리에서 Fetch-Decode-Execute하는 구조와는 차이가 있다.

따라서 Zero-CPU는 v0.2부터 어셈블러와 CPU를 명확히 분리하기 위해 Virtual Binary Format을 도입한다.

새로운 실행 흐름은 다음과 같다.

```text
.zasm source file
    ↓
Assembler
    ↓
.zbin virtual binary file
    ↓
Binary Loader
    ↓
Virtual Memory
    ↓
CPU Fetch-Decode-Execute
    ↓
Trace Logger
```

이 구조에서 CPU는 더 이상 `.zasm` 소스 코드나 라벨 이름을 직접 알지 않는다.

CPU는 오직 메모리에 적재된 바이트코드만 읽고 실행한다.

---

## 2. Design Goals

Zero-CPU Virtual Binary Format의 목표는 다음과 같다.

1. 어셈블러와 CPU 실행 엔진을 분리한다.
2. `.zasm` 소스 코드를 `.zbin` 가상 바이너리 파일로 변환한다.
3. CPU는 `.zbin` 파일의 바이트코드를 메모리에 적재한 뒤 실행한다.
4. Program Counter는 명령어 인덱스가 아니라 바이트 주소를 가리킨다.
5. 명령어 인코딩, 엔디안, 정렬 규칙을 명시한다.
6. 향후 디스어셈블러, 로더, 링커, 섹션 구조로 확장할 수 있게 한다.

---

## 3. File Extension

Zero-CPU의 가상 바이너리 파일 확장자는 다음과 같다.

```text
.zbin
```

예시:

```text
function_call.zasm  →  function_call.zbin
simple_add.zasm     →  simple_add.zbin
branch_loop.zasm    →  branch_loop.zbin
```

선택적으로 실행 파일 의미를 강조하고 싶을 경우 `.zexe` 확장자를 사용할 수 있지만, 초기 구현에서는 `.zbin`을 기본으로 사용한다.

---

## 4. Binary File Layout

Zero-CPU `.zbin` 파일은 다음 구조를 가진다.

```text
+--------------------------+
| Header                   |
+--------------------------+
| Code Section             |
+--------------------------+
```

초기 버전에서는 단순성을 위해 Header와 Code Section만 사용한다.

향후 확장에서는 다음 섹션을 추가할 수 있다.

```text
+--------------------------+
| Header                   |
+--------------------------+
| Code Section             |
+--------------------------+
| Data Section             |
+--------------------------+
| Symbol Table             |
+--------------------------+
| Debug Information        |
+--------------------------+
```

---

## 5. Header Layout

Zero-CPU Virtual Binary Header는 파일의 메타데이터를 저장한다.

초기 Header 크기는 16 bytes로 고정한다.

```text
Offset | Size | Field        | Description
-------|------|--------------|------------------------------
0      | 4    | Magic        | "ZCPU"
4      | 1    | Major        | Major version
5      | 1    | Minor        | Minor version
6      | 1    | Endianness   | 1 = Little, 2 = Big
7      | 1    | Reserved     | Reserved, must be 0
8      | 4    | Entry Point  | First instruction byte address
12     | 4    | Code Size    | Code section size in bytes
```

전체 Header는 다음과 같이 표현할 수 있다.

```text
[0x00 - 0x03] Magic Number
[0x04]        Major Version
[0x05]        Minor Version
[0x06]        Endianness
[0x07]        Reserved
[0x08 - 0x0B] Entry Point
[0x0C - 0x0F] Code Size
```

---

## 6. Magic Number

Zero-CPU 바이너리 파일은 Magic Number로 시작한다.

```text
Magic = "ZCPU"
```

바이트 표현:

```text
0x5A 0x43 0x50 0x55
```

문자 기준:

```text
Z C P U
```

Loader는 `.zbin` 파일을 읽을 때 가장 먼저 Magic Number를 검사한다.

Magic Number가 일치하지 않으면 유효하지 않은 Zero-CPU 바이너리로 처리한다.

---

## 7. Version

초기 Virtual Binary Format 버전은 다음과 같다.

```text
Major = 0
Minor = 2
```

즉:

```text
Zero-CPU Virtual Binary Format v0.2
```

v0.1은 기존의 내부 `Instruction` 직접 실행 구조로 보고, v0.2부터 바이너리 포맷을 도입한다.

---

## 8. Endianness

Zero-CPU는 바이너리 파일 내부의 다중 바이트 정수 저장 방식을 명시한다.

지원하는 Endianness 값은 다음과 같다.

```text
1 = Little Endian
2 = Big Endian
```

초기 기본값은 Little Endian이다.

```text
Default Endianness = Little Endian
```

예를 들어 `0x12345678` 값을 4바이트로 저장할 때:

Little Endian:

```text
78 56 34 12
```

Big Endian:

```text
12 34 56 78
```

초기 구현에서는 Little Endian을 기본으로 사용하되, Binary Reader/Writer가 Endianness를 명시적으로 처리할 수 있도록 설계한다.

---

## 9. Entry Point

Entry Point는 CPU가 처음 실행을 시작할 Code Section 내부의 바이트 주소이다.

초기 구현에서는 항상 0으로 둔다.

```text
Entry Point = 0
```

즉, CPU는 Code Section의 첫 번째 명령어부터 실행한다.

향후에는 `_start` 라벨이나 사용자 지정 entry symbol을 지원할 수 있다.

---

## 10. Code Size

Code Size는 Code Section의 전체 크기를 바이트 단위로 저장한다.

예를 들어 명령어 크기가 24 bytes이고 명령어가 9개라면:

```text
Code Size = 24 * 9 = 216 bytes
```

Loader는 Header의 Code Size를 읽고 그만큼의 바이트를 Code Section으로 해석한다.

---

## 11. Code Section

Code Section에는 Zero-CPU 명령어들이 바이트코드 형태로 저장된다.

초기 구현에서는 모든 명령어를 고정 길이로 인코딩한다.

```text
Instruction Size = 24 bytes
```

따라서 N번째 명령어의 바이트 주소는 다음과 같이 계산된다.

```text
address = N * 24
```

예시:

```text
Instruction 0 → byte address 0
Instruction 1 → byte address 24
Instruction 2 → byte address 48
Instruction 3 → byte address 72
```

---

## 12. Program Counter Rule

기존 v0.1 구조에서는 PC가 명령어 인덱스를 의미했다.

```text
PC = 0
PC = 1
PC = 2
```

v0.2 바이너리 구조부터 PC는 바이트 주소를 의미한다.

```text
PC = 0
PC = 24
PC = 48
PC = 72
```

일반 명령어 실행 후 PC는 다음과 같이 증가한다.

```text
PC = PC + INSTRUCTION_SIZE
```

초기 고정값:

```text
INSTRUCTION_SIZE = 24
```

분기 명령어는 PC를 라벨이 가리키는 바이트 주소로 변경한다.

```text
PC = target_byte_address
```

---

## 13. Instruction Encoding Overview

Zero-CPU v0.2의 명령어는 24 bytes 고정 길이로 인코딩된다.

```text
Offset | Size | Field            | Description
-------|------|------------------|------------------------------
0      | 1    | Opcode           | Operation code
1      | 1    | Dst Operand Type | Destination operand type
2      | 1    | Src Operand Type | Source operand type
3      | 1    | Reserved         | Reserved, must be 0
4      | 8    | Dst Payload      | Destination operand payload
12     | 8    | Src Payload      | Source operand payload
20     | 4    | Reserved         | Reserved, must be 0
```

전체 구조:

```text
+0   opcode
+1   dst_type
+2   src_type
+3   reserved
+4   dst_payload
+12  src_payload
+20  reserved
```

---

## 14. Opcode Encoding

Opcode는 1 byte로 저장한다.

초기 Opcode 값은 다음과 같이 정의한다.

```text
Opcode | Value
-------|------
NOP    | 0x00
HALT   | 0x01

MOV    | 0x10
LOAD   | 0x11
STORE  | 0x12

ADD    | 0x20
SUB    | 0x21
MUL    | 0x22
DIV    | 0x23

CMP    | 0x30
TEST   | 0x31

JMP    | 0x40
JE     | 0x41
JNE    | 0x42
JG     | 0x43
JL     | 0x44

PUSH   | 0x50
POP    | 0x51
CALL   | 0x52
RET    | 0x53
```

아직 구현하지 않은 논리 연산 명령어는 이후 확장한다.

```text
AND
OR
XOR
NOT
```

---

## 15. Operand Type Encoding

Operand Type은 1 byte로 저장한다.

```text
Type           | Value
---------------|------
None           | 0x00
Register       | 0x01
Immediate      | 0x02
Memory Address | 0x03
Code Address   | 0x04
```

기존의 `Label` operand는 바이너리 파일에서는 문자열로 저장하지 않는다.

Assembler가 라벨을 실제 코드 주소로 변환한 뒤 `Code Address`로 저장한다.

예:

```asm
JMP loop
```

Assembler 내부 변환:

```text
loop -> byte address 48
```

Binary operand:

```text
type    = Code Address
payload = 48
```

---

## 16. Operand Payload

Operand Payload는 8 bytes로 저장한다.

Operand Type에 따라 의미가 달라진다.

```text
Operand Type    | Payload Meaning
----------------|----------------
None            | 0
Register        | Register index
Immediate       | Signed integer value
Memory Address  | Memory byte address
Code Address    | Code byte address
```

예시:

```asm
MOV R1, 10
```

인코딩 개념:

```text
opcode      = MOV
dst_type    = Register
src_type    = Immediate
dst_payload = 1
src_payload = 10
```

예시:

```asm
STORE [100], R1
```

인코딩 개념:

```text
opcode      = STORE
dst_type    = Memory Address
src_type    = Register
dst_payload = 100
src_payload = 1
```

예시:

```asm
CALL double_value
```

라벨 주소가 168이라고 가정하면:

```text
opcode      = CALL
dst_type    = Code Address
src_type    = None
dst_payload = 168
src_payload = 0
```

---

## 17. Alignment

Zero-CPU v0.2는 명령어 정렬 규칙을 가진다.

```text
Instruction Alignment = 4 bytes
Instruction Size      = 24 bytes
```

24는 4의 배수이므로 모든 명령어 시작 주소는 4-byte aligned 상태를 유지한다.

```text
0, 24, 48, 72, 96, ...
```

CPU가 명령어를 Fetch할 때 PC가 4-byte aligned가 아니면 Alignment Fault를 발생시킨다.

```text
PC % 4 != 0 → AlignmentFault
```

향후 strict mode에서는 메모리 데이터 접근도 정렬 검사를 적용할 수 있다.

예:

```text
8-byte value access requires address % 8 == 0
```

---

## 18. Example: simple_add.zasm

Source:

```asm
MOV R1, 10
MOV R2, 20
ADD R1, R2
STORE [100], R1
HALT
```

Instruction addresses:

```text
[0] MOV R1, 10       → byte address 0
[1] MOV R2, 20       → byte address 24
[2] ADD R1, R2       → byte address 48
[3] STORE [100], R1  → byte address 72
[4] HALT             → byte address 96
```

Expected result:

```text
R1 = 30
R2 = 20
Memory[100] = 30
halted = true
```

---

## 19. Example: function_call.zasm

Source:

```asm
MOV R1, 10
CALL double_value
STORE [100], R1
HALT

double_value:
    ADD R1, R1
    RET
```

Instruction addresses:

```text
[0] MOV R1, 10        → byte address 0
[1] CALL double_value → byte address 24
[2] STORE [100], R1   → byte address 48
[3] HALT              → byte address 72
[4] ADD R1, R1        → byte address 96
[5] RET               → byte address 120
```

Label table before binary encoding:

```text
double_value -> instruction index 4
```

Label table after binary encoding:

```text
double_value -> byte address 96
```

CALL instruction encoding concept:

```text
opcode      = CALL
dst_type    = Code Address
src_type    = None
dst_payload = 96
src_payload = 0
```

---

## 20. Separation Between Assembler and CPU

The assembler is responsible for:

```text
.zasm parsing
label resolution
instruction encoding
binary header generation
.zbin file writing
```

The CPU emulator is responsible for:

```text
.zbin loading
bytecode fetch
bytecode decode
instruction execution
state transition tracing
```

The CPU must not depend on:

```text
.zasm source text
assembly syntax
label names
parser logic
```

This separation makes Zero-CPU closer to a real CPU execution model.

---

## 21. Error Conditions

The binary loader and CPU should handle the following errors.

```text
InvalidMagicNumber
UnsupportedBinaryVersion
UnsupportedEndianness
InvalidCodeSize
PCOutOfBounds
InstructionAlignmentFault
InvalidOpcode
InvalidOperandType
InvalidCodeAddress
MemoryOutOfBounds
StackOverflow
StackUnderflow
DivisionByZero
```

---

## 22. Future Extensions

Future versions of Zero-CPU Virtual Binary Format may include:

```text
Data section
Symbol table
Debug information
String table
Relocation entries
Section headers
Executable metadata
Disassembler support
Linker support
```

Potential future layout:

```text
+--------------------------+
| Header                   |
+--------------------------+
| Section Table            |
+--------------------------+
| Code Section             |
+--------------------------+
| Data Section             |
+--------------------------+
| Symbol Table             |
+--------------------------+
| Debug Info               |
+--------------------------+
```

---

## 23. Summary

Zero-CPU Virtual Binary Format separates the assembler from the CPU execution engine.

In v0.2, `.zasm` source files are assembled into `.zbin` virtual binary files.

The CPU loads `.zbin` bytecode into virtual memory and executes it using a byte-addressed Program Counter.

This design makes Zero-CPU closer to a real CPU model by introducing:

```text
virtual binary files
byte-addressable execution
explicit instruction encoding
endianness control
alignment rules
assembler/CPU separation
```