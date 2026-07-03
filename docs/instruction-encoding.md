# Zero-CPU Instruction Encoding

## 1. Overview

Zero-CPU v0.2부터 CPU는 `.zasm` 어셈블리 소스 코드를 직접 실행하지 않는다.

대신 어셈블러가 `.zasm` 파일을 읽고, 각 명령어를 고정 길이 바이트코드로 인코딩하여 `.zbin` 가상 바이너리 파일을 생성한다.

CPU는 `.zbin` 파일의 Code Section을 메모리에 적재한 뒤, Program Counter가 가리키는 바이트 주소에서 명령어를 Fetch하고 Decode한 후 Execute한다.

실행 흐름은 다음과 같다.

```text
.zasm source
    ↓
Assembler
    ↓
Instruction IR
    ↓
Instruction Encoder
    ↓
.zbin bytecode
    ↓
Binary Loader
    ↓
Virtual Memory
    ↓
CPU Fetch-Decode-Execute
```

이 문서는 Zero-CPU 명령어가 어떤 바이트 구조로 저장되는지 정의한다.

---

## 2. Fixed-Length Instruction

Zero-CPU v0.2의 명령어는 고정 길이로 인코딩된다.

```text
Instruction Size = 24 bytes
```

고정 길이 명령어를 사용하는 이유는 다음과 같다.

1. Fetch 단계가 단순해진다.
2. 다음 명령어 주소 계산이 쉽다.
3. Program Counter를 바이트 주소로 다루기 좋다.
4. 디코더 구현이 단순하다.
5. Trace와 디버깅이 쉬워진다.

일반 명령어 실행 후 PC는 다음과 같이 증가한다.

```text
PC = PC + 24
```

분기, 호출, 복귀 명령어는 PC를 직접 변경한다.

```text
PC = target_address
```

---

## 3. Instruction Layout

명령어 하나는 24 bytes로 구성된다.

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

구조를 그림으로 표현하면 다음과 같다.

```text
+------------+------------+------------+------------+
| opcode     | dst_type   | src_type   | reserved   |
+------------+------------+------------+------------+
| dst_payload                                      |
| 8 bytes                                          |
+--------------------------------------------------+
| src_payload                                      |
| 8 bytes                                          |
+--------------------------------------------------+
| reserved 4 bytes                                 |
+--------------------------------------------------+
```

모든 reserved 필드는 초기 구현에서 반드시 0이어야 한다.

---

## 4. Endianness

Zero-CPU의 기본 바이트 순서는 Little Endian이다.

```text
Default Endianness = Little Endian
```

8바이트 payload 값은 Little Endian으로 저장된다.

예를 들어 64비트 값 `0x000000000000000A`는 다음과 같이 저장된다.

```text
0A 00 00 00 00 00 00 00
```

값 `0x0000000000000064`는 다음과 같이 저장된다.

```text
64 00 00 00 00 00 00 00
```

향후 Big Endian을 지원할 수 있지만, v0.2의 기본 인코딩은 Little Endian을 기준으로 한다.

---

## 5. Opcode Encoding

Opcode는 1 byte로 저장된다.

```text
Offset = 0
Size   = 1 byte
```

초기 Opcode 값은 다음과 같다.

```text
Opcode | Value | Category
-------|-------|----------------
NOP    | 0x00  | Control
HALT   | 0x01  | Control

MOV    | 0x10  | Data Movement
LOAD   | 0x11  | Data Movement
STORE  | 0x12  | Data Movement

ADD    | 0x20  | Arithmetic
SUB    | 0x21  | Arithmetic
MUL    | 0x22  | Arithmetic
DIV    | 0x23  | Arithmetic

CMP    | 0x30  | Comparison
TEST   | 0x31  | Comparison

JMP    | 0x40  | Branch
JE     | 0x41  | Branch
JNE    | 0x42  | Branch
JG     | 0x43  | Branch
JL     | 0x44  | Branch

PUSH   | 0x50  | Stack
POP    | 0x51  | Stack
CALL   | 0x52  | Function Call
RET    | 0x53  | Function Call
```

아직 인코딩 값만 예약하고 구현을 나중으로 미룰 수 있는 명령어는 다음과 같다.

```text
AND
OR
XOR
NOT
```

---

## 6. Operand Type Encoding

Operand Type은 각각 1 byte로 저장된다.

```text
Dst Operand Type Offset = 1
Src Operand Type Offset = 2
```

지원하는 Operand Type은 다음과 같다.

```text
Type           | Value | Meaning
---------------|-------|------------------------------
None           | 0x00  | Operand 없음
Register       | 0x01  | Register index
Immediate      | 0x02  | Signed integer value
Memory Address | 0x03  | Data memory byte address
Code Address   | 0x04  | Code section byte address
```

기존 `.zasm` 문법의 라벨은 바이너리 파일에 문자열로 저장되지 않는다.

어셈블러는 라벨을 실제 바이트 주소로 변환한 뒤 `Code Address` 타입으로 저장한다.

예:

```asm
JMP loop
```

라벨 해석 후:

```text
loop -> 72
```

바이너리 인코딩:

```text
dst_type    = Code Address
dst_payload = 72
```

---

## 7. Operand Payload

Operand Payload는 8 bytes로 저장된다.

```text
Dst Payload Offset = 4
Src Payload Offset = 12
Payload Size       = 8 bytes
```

Operand Type에 따른 Payload 의미는 다음과 같다.

```text
Operand Type    | Payload Meaning
----------------|--------------------------------
None            | 0
Register        | Register index
Immediate       | Signed 64-bit integer
Memory Address  | Unsigned byte address
Code Address    | Unsigned byte address
```

Register index는 다음과 같다.

```text
Register | Index
---------|------
R0       | 0
R1       | 1
R2       | 2
R3       | 3
R4       | 4
R5       | 5
R6       | 6
R7       | 7
```

---

## 8. No-Operand Instructions

피연산자가 없는 명령어는 `dst_type`과 `src_type`을 모두 `None`으로 저장한다.

예:

```asm
HALT
```

인코딩 개념:

```text
opcode      = 0x01
dst_type    = 0x00
src_type    = 0x00
dst_payload = 0
src_payload = 0
```

예:

```asm
RET
```

인코딩 개념:

```text
opcode      = 0x53
dst_type    = 0x00
src_type    = 0x00
dst_payload = 0
src_payload = 0
```

---

## 9. One-Operand Instructions

피연산자가 하나인 명령어는 첫 번째 피연산자를 `dst` 위치에 저장한다.

예:

```asm
PUSH R1
```

인코딩 개념:

```text
opcode      = 0x50
dst_type    = Register
src_type    = None
dst_payload = 1
src_payload = 0
```

예:

```asm
POP R2
```

인코딩 개념:

```text
opcode      = 0x51
dst_type    = Register
src_type    = None
dst_payload = 2
src_payload = 0
```

예:

```asm
JMP loop
```

라벨 주소가 96이라면:

```text
opcode      = 0x40
dst_type    = Code Address
src_type    = None
dst_payload = 96
src_payload = 0
```

---

## 10. Two-Operand Instructions

피연산자가 두 개인 명령어는 첫 번째 피연산자를 `dst`, 두 번째 피연산자를 `src`로 저장한다.

예:

```asm
MOV R1, 10
```

인코딩 개념:

```text
opcode      = 0x10
dst_type    = Register
src_type    = Immediate
dst_payload = 1
src_payload = 10
```

예:

```asm
ADD R1, R2
```

인코딩 개념:

```text
opcode      = 0x20
dst_type    = Register
src_type    = Register
dst_payload = 1
src_payload = 2
```

예:

```asm
STORE [100], R1
```

인코딩 개념:

```text
opcode      = 0x12
dst_type    = Memory Address
src_type    = Register
dst_payload = 100
src_payload = 1
```

---

## 11. Label Resolution

`.zasm` 소스의 라벨은 어셈블러 단계에서 처리된다.

CPU는 라벨 이름을 알지 못한다.

예:

```asm
MOV R1, 0

loop:
    ADD R1, 1
    CMP R1, 5
    JL loop

HALT
```

명령어 주소는 다음과 같다.

```text
[0] MOV R1, 0  -> byte address 0
[1] ADD R1, 1  -> byte address 24
[2] CMP R1, 5  -> byte address 48
[3] JL loop    -> byte address 72
[4] HALT       -> byte address 96
```

라벨 테이블:

```text
loop -> 24
```

따라서 `JL loop`는 다음과 같이 인코딩된다.

```text
opcode      = JL
dst_type    = Code Address
src_type    = None
dst_payload = 24
src_payload = 0
```

---

## 12. Example Encoding: MOV R1, 10

Source:

```asm
MOV R1, 10
```

Field values:

```text
Field       | Value
------------|------
opcode      | 0x10
dst_type    | 0x01
src_type    | 0x02
reserved    | 0x00
dst_payload | 1
src_payload | 10
reserved    | 0
```

Byte representation:

```text
10 01 02 00
01 00 00 00 00 00 00 00
0A 00 00 00 00 00 00 00
00 00 00 00
```

총 24 bytes이다.

---

## 13. Example Encoding: ADD R1, R2

Source:

```asm
ADD R1, R2
```

Field values:

```text
Field       | Value
------------|------
opcode      | 0x20
dst_type    | 0x01
src_type    | 0x01
reserved    | 0x00
dst_payload | 1
src_payload | 2
reserved    | 0
```

Byte representation:

```text
20 01 01 00
01 00 00 00 00 00 00 00
02 00 00 00 00 00 00 00
00 00 00 00
```

---

## 14. Example Encoding: STORE [100], R1

Source:

```asm
STORE [100], R1
```

Field values:

```text
Field       | Value
------------|------
opcode      | 0x12
dst_type    | 0x03
src_type    | 0x01
reserved    | 0x00
dst_payload | 100
src_payload | 1
reserved    | 0
```

Byte representation:

```text
12 03 01 00
64 00 00 00 00 00 00 00
01 00 00 00 00 00 00 00
00 00 00 00
```

---

## 15. Example Encoding: CALL double_value

Source:

```asm
CALL double_value
```

라벨 주소가 96이라면:

```text
double_value -> 96
```

Field values:

```text
Field       | Value
------------|------
opcode      | 0x52
dst_type    | 0x04
src_type    | 0x00
reserved    | 0x00
dst_payload | 96
src_payload | 0
reserved    | 0
```

Byte representation:

```text
52 04 00 00
60 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
00 00 00 00
```

---

## 16. Fetch Rule

CPU는 PC가 가리키는 주소에서 24 bytes를 읽는다.

```text
instruction_bytes = memory[PC : PC + 24]
```

Fetch 전 조건:

```text
PC < code_size
PC % 4 == 0
PC + 24 <= code_size
```

조건을 만족하지 못하면 실행 오류를 발생시킨다.

```text
PCOutOfBounds
InstructionAlignmentFault
InvalidInstructionSize
```

---

## 17. Decode Rule

CPU Decoder는 24 bytes를 다음 순서로 해석한다.

```text
1. opcode 읽기
2. dst_type 읽기
3. src_type 읽기
4. reserved byte 검사
5. dst_payload 읽기
6. src_payload 읽기
7. trailing reserved 4 bytes 검사
8. opcode와 operand 조합 검증
9. DecodedInstruction 생성
```

디코딩 결과는 내부 실행용 구조체로 변환될 수 있다.

예:

```text
DecodedInstruction
- opcode
- dst_type
- src_type
- dst_payload
- src_payload
```

이 구조체는 기존 `Instruction` 객체와 비슷한 역할을 하지만, CPU는 더 이상 `.zasm` 파서에 의존하지 않는다.

---

## 18. Operand Validation

명령어별로 허용되는 operand 조합은 제한된다.

예:

```text
MOV
- dst: Register
- src: Register 또는 Immediate

LOAD
- dst: Register
- src: Memory Address

STORE
- dst: Memory Address
- src: Register 또는 Immediate

ADD/SUB/MUL/DIV
- dst: Register
- src: Register 또는 Immediate

CMP
- dst: Register
- src: Register 또는 Immediate

JMP/JE/JNE/JG/JL/CALL
- dst: Code Address
- src: None

PUSH
- dst: Register 또는 Immediate
- src: None

POP
- dst: Register
- src: None

RET/HALT/NOP
- dst: None
- src: None
```

잘못된 조합은 `InvalidOperandType`으로 처리한다.

---

## 19. Alignment Rule

명령어는 4-byte aligned 주소에서 시작해야 한다.

```text
PC % 4 == 0
```

Zero-CPU v0.2의 명령어 크기는 24 bytes이고, 24는 4의 배수이므로 정상적인 실행 흐름에서는 항상 정렬 상태가 유지된다.

```text
0
24
48
72
96
120
```

분기 명령어가 잘못된 주소로 이동하면 정렬 오류가 발생할 수 있다.

```text
JMP 25 -> InstructionAlignmentFault
```

---

## 20. Design Summary

Zero-CPU v0.2 instruction encoding은 다음 특징을 가진다.

```text
Fixed-length instruction
24 bytes per instruction
Little Endian payload
1-byte opcode
1-byte operand types
8-byte operand payloads
4-byte instruction alignment
Label names resolved by assembler
CPU executes bytecode, not assembly source
```

이 구조를 통해 Zero-CPU는 단순한 어셈블리 인터프리터가 아니라, 가상 바이너리를 메모리에 적재하고 Fetch-Decode-Execute하는 CPU 에뮬레이터 구조로 발전한다.