# Zero-CPU Memory Model

## 1. Overview

Zero-CPU v0.2부터 CPU는 `.zasm` 어셈블리 소스나 내부 `Instruction` 객체를 직접 실행하지 않는다.

대신 어셈블러가 생성한 `.zbin` 가상 바이너리를 메모리에 적재하고, CPU가 Program Counter가 가리키는 바이트 주소에서 명령어를 Fetch-Decode-Execute한다.

이를 위해 Zero-CPU의 메모리 모델은 기존의 정수 셀 기반 구조에서 바이트 주소 기반 구조로 확장된다.

기존 v0.1 구조:

```text
Memory cell 0 -> int64_t
Memory cell 1 -> int64_t
Memory cell 2 -> int64_t
```

v0.2 구조:

```text
Memory byte 0 -> uint8_t
Memory byte 1 -> uint8_t
Memory byte 2 -> uint8_t
Memory byte 3 -> uint8_t
...
```

즉, Zero-CPU v0.2의 메모리는 byte-addressable memory를 기본으로 한다.

---

## 2. Design Goals

Zero-CPU Memory Model의 목표는 다음과 같다.

1. 메모리를 바이트 단위로 주소 지정한다.
2. 명령어 바이트코드를 메모리에 적재할 수 있게 한다.
3. 데이터 읽기/쓰기에서 Endianness를 명시적으로 처리한다.
4. 메모리 접근 정렬 규칙을 정의한다.
5. Code 영역, Data 영역, Stack 영역의 개념을 분리할 수 있게 한다.
6. 실제 CPU의 Fetch-Decode-Execute 구조에 가까운 실행 모델을 만든다.

---

## 3. Byte-Addressable Memory

Zero-CPU v0.2의 메모리는 바이트 배열로 표현된다.

개념적으로 다음과 같다.

```cpp
std::vector<std::uint8_t> bytes_;
```

각 주소는 1 byte를 가리킨다.

```text
Address | Value
--------|------
0       | 0x10
1       | 0x01
2       | 0x02
3       | 0x00
4       | 0x01
...
```

이 구조에서는 `Memory[100]`이 더 이상 100번째 정수 셀을 의미하지 않는다.

`Memory[100]`은 100번째 바이트를 의미한다.

---

## 4. Memory Size

초기 구현에서 기본 메모리 크기는 다음과 같이 둔다.

```text
Default Memory Size = 4096 bytes
```

즉, 주소 범위는 다음과 같다.

```text
0 ~ 4095
```

향후 설정을 통해 더 큰 메모리를 사용할 수 있다.

예:

```text
64 KiB
1 MiB
16 MiB
```

초기에는 단순성을 위해 4096 bytes 또는 65536 bytes를 사용할 수 있다.

---

## 5. Memory Regions

Zero-CPU의 메모리는 논리적으로 다음 영역으로 나눌 수 있다.

```text
+--------------------------+
| Code Region              |
+--------------------------+
| Data Region              |
+--------------------------+
| Stack Region             |
+--------------------------+
```

초기 구현에서는 물리적으로 섹션을 엄격히 나누지 않아도 된다.

다만 설계상 각 영역의 역할은 명확히 구분한다.

---

## 6. Code Region

Code Region은 `.zbin` 파일의 Code Section이 적재되는 영역이다.

초기 구현에서는 Code Region의 시작 주소를 0으로 둔다.

```text
Code Base = 0
```

예를 들어 Code Size가 216 bytes라면:

```text
Code Region = [0, 216)
```

CPU의 Program Counter는 Code Region 내부의 바이트 주소를 가리킨다.

```text
PC = 0
PC = 24
PC = 48
PC = 72
```

CPU는 PC가 가리키는 주소에서 24 bytes를 읽어 명령어로 해석한다.

---

## 7. Data Region

Data Region은 프로그램이 사용하는 일반 데이터를 저장하는 영역이다.

예를 들어 다음 명령어는 Data Region에 값을 저장한다.

```asm
STORE [100], R1
```

이때 `[100]`은 메모리의 100번 바이트 주소를 의미한다.

다만 v0.2에서 `STORE [100], R1`은 `R1`의 64-bit 값을 주소 100부터 8 bytes로 저장하는 것으로 정의한다.

```text
Memory[100] ~ Memory[107]
```

즉, 정수 값 하나는 8 bytes를 차지한다.

---

## 8. Stack Region

Stack Region은 `PUSH`, `POP`, `CALL`, `RET` 명령어가 사용하는 영역이다.

초기 구현에서는 Stack Base를 다음과 같이 둘 수 있다.

```text
Stack Base = 2048
```

Stack Pointer는 다음에 쓸 바이트 주소를 가리킨다.

초기 SP:

```text
SP = Stack Base
```

Zero-CPU v0.2의 스택은 낮은 주소에서 높은 주소 방향으로 증가한다.

```text
PUSH value:
    Memory[SP] ~ Memory[SP + 7] = value
    SP = SP + 8
```

```text
POP register:
    SP = SP - 8
    register = Memory[SP] ~ Memory[SP + 7]
```

예:

```text
Initial SP = 2048

PUSH 10
Memory[2048..2055] = 10
SP = 2056

PUSH 20
Memory[2056..2063] = 20
SP = 2064

POP R1
SP = 2056
R1 = 20

POP R2
SP = 2048
R2 = 10
```

---

## 9. Endianness

Endianness는 다중 바이트 값을 메모리에 저장하는 순서를 의미한다.

Zero-CPU는 다음 두 가지 Endianness를 정의한다.

```text
Little Endian
Big Endian
```

초기 기본값은 Little Endian이다.

```text
Default Endianness = Little Endian
```

---

## 10. Little Endian

Little Endian에서는 값의 하위 바이트가 낮은 주소에 저장된다.

예를 들어 32-bit 값 `0x12345678`을 주소 100에 저장하면:

```text
Address | Value
--------|------
100     | 0x78
101     | 0x56
102     | 0x34
103     | 0x12
```

64-bit 값 `0x000000000000000A`를 저장하면:

```text
Address | Value
--------|------
100     | 0x0A
101     | 0x00
102     | 0x00
103     | 0x00
104     | 0x00
105     | 0x00
106     | 0x00
107     | 0x00
```

---

## 11. Big Endian

Big Endian에서는 값의 상위 바이트가 낮은 주소에 저장된다.

예를 들어 32-bit 값 `0x12345678`을 주소 100에 저장하면:

```text
Address | Value
--------|------
100     | 0x12
101     | 0x34
102     | 0x56
103     | 0x78
```

64-bit 값 `0x000000000000000A`를 저장하면:

```text
Address | Value
--------|------
100     | 0x00
101     | 0x00
102     | 0x00
103     | 0x00
104     | 0x00
105     | 0x00
106     | 0x00
107     | 0x0A
```

---

## 12. Memory Read and Write Operations

Memory는 바이트 단위 접근뿐 아니라 다중 바이트 정수 접근을 지원해야 한다.

필요한 기본 연산은 다음과 같다.

```cpp
std::uint8_t readU8(std::size_t address) const;
void writeU8(std::size_t address, std::uint8_t value);

std::uint32_t readU32(std::size_t address, Endianness endian) const;
void writeU32(std::size_t address, std::uint32_t value, Endianness endian);

std::uint64_t readU64(std::size_t address, Endianness endian) const;
void writeU64(std::size_t address, std::uint64_t value, Endianness endian);

std::int64_t readI64(std::size_t address, Endianness endian) const;
void writeI64(std::size_t address, std::int64_t value, Endianness endian);
```

명령어 Fetch에는 `readBytes` 또는 `readU8` 기반 접근을 사용할 수 있다.

```cpp
std::vector<std::uint8_t> readBytes(
    std::size_t address,
    std::size_t count
) const;
```

---

## 13. Signed and Unsigned Values

Zero-CPU의 일반 레지스터는 64-bit signed integer 값을 저장한다.

```text
Register Value = int64_t
```

따라서 `MOV R1, -10` 같은 명령어는 signed integer 값을 사용한다.

```asm
MOV R1, -10
```

메모리에 저장될 때는 해당 signed integer의 64-bit 표현이 그대로 저장된다.

예:

```text
-1 as int64_t = 0xFFFFFFFFFFFFFFFF
```

Little Endian 저장:

```text
FF FF FF FF FF FF FF FF
```

---

## 14. Instruction Fetch

Zero-CPU v0.2의 명령어 크기는 24 bytes이다.

```text
Instruction Size = 24 bytes
```

CPU는 PC가 가리키는 주소에서 24 bytes를 읽는다.

```text
instruction_bytes = Memory[PC..PC+23]
```

Fetch 전 검사 조건:

```text
PC < CodeSize
PC + 24 <= CodeSize
PC % 4 == 0
```

조건을 만족하지 못하면 오류를 발생시킨다.

```text
PCOutOfBounds
InstructionAlignmentFault
InvalidInstructionSize
```

---

## 15. Instruction Alignment

Zero-CPU v0.2의 명령어는 4-byte aligned 주소에서 시작해야 한다.

```text
Instruction Alignment = 4 bytes
```

정상적인 명령어 시작 주소:

```text
0
24
48
72
96
120
```

모두 4의 배수이다.

잘못된 주소:

```text
1
2
3
25
49
```

CPU가 잘못된 주소에서 Fetch를 시도하면 다음 오류를 발생시킨다.

```text
InstructionAlignmentFault
```

---

## 16. Data Alignment

데이터 접근 정렬은 초기에는 유연하게 처리할 수 있다.

다만 설계상 64-bit 데이터 접근은 8-byte alignment를 권장한다.

```text
Recommended I64 Alignment = 8 bytes
```

예:

```text
Address 100 -> not 8-byte aligned
Address 104 -> 8-byte aligned
Address 112 -> 8-byte aligned
```

주소 100은 4로는 나누어지지만 8로는 나누어지지 않는다.

초기 구현에서는 다음 두 가지 모드 중 하나를 선택할 수 있다.

```text
Relaxed Mode:
    정렬되지 않은 데이터 접근도 허용한다.

Strict Mode:
    정렬되지 않은 데이터 접근 시 AlignmentFault를 발생시킨다.
```

Zero-CPU v0.2 초기 구현은 Relaxed Mode를 기본으로 한다.

```text
Default Data Alignment Mode = Relaxed
```

향후 Strict Mode를 옵션으로 추가할 수 있다.

---

## 17. Stack Alignment

Stack은 64-bit 값을 저장하므로 8-byte aligned 상태를 유지하는 것이 좋다.

```text
Stack Alignment = 8 bytes
```

초기 Stack Base는 8의 배수로 설정한다.

예:

```text
Stack Base = 2048
```

`PUSH`와 `POP`은 8 bytes 단위로 SP를 변경하므로, SP는 항상 8-byte aligned 상태를 유지한다.

```text
2048
2056
2064
2072
```

---

## 18. CALL and RET Stack Behavior

`CALL` 명령어는 현재 PC의 다음 명령어 주소를 스택에 저장한 뒤, 호출 대상 주소로 이동한다.

명령어 크기가 24 bytes이므로 return address는 다음과 같다.

```text
Return Address = PC + 24
```

동작:

```text
CALL target:
    Memory[SP..SP+7] = PC + 24
    SP = SP + 8
    PC = target
```

`RET` 명령어는 스택에서 return address를 꺼내 PC로 복원한다.

```text
RET:
    SP = SP - 8
    PC = Memory[SP..SP+7]
```

예:

```text
PC = 24
SP = 2048

CALL 96

Memory[2048..2055] = 48
SP = 2056
PC = 96

RET

SP = 2048
PC = 48
```

---

## 19. Memory Bounds Checking

모든 메모리 접근은 범위 검사를 수행해야 한다.

예:

```text
readU8(address):
    address < memory_size

readU64(address):
    address + 8 <= memory_size

fetchInstruction(PC):
    PC + 24 <= code_size
```

범위를 벗어난 접근은 다음 오류로 처리한다.

```text
MemoryOutOfBounds
PCOutOfBounds
StackOverflow
StackUnderflow
```

---

## 20. Stack Overflow and Underflow

Stack은 메모리의 일정 영역을 사용한다.

초기 설계에서는 Stack Limit을 설정할 수 있다.

예:

```text
Stack Base  = 2048
Stack Limit = 4096
```

`PUSH` 또는 `CALL` 시:

```text
SP + 8 <= Stack Limit
```

조건을 만족하지 못하면:

```text
StackOverflow
```

`POP` 또는 `RET` 시:

```text
SP >= Stack Base + 8
```

조건을 만족하지 못하면:

```text
StackUnderflow
```

---

## 21. Memory Dump

디버깅을 위해 Memory는 특정 주소 범위를 출력할 수 있어야 한다.

예:

```text
dumpBytes(96, 16)
```

출력 예:

```text
[0096] 10 01 02 00 01 00 00 00 00 00 00 00 0A 00 00 00
```

64-bit 값 단위로 출력하는 함수도 유용하다.

```text
dumpI64(100, 4)
```

출력 예:

```text
[0100] 30
[0108] 0
[0116] 0
[0124] 0
```

---

## 22. Example: STORE [100], R1

명령어:

```asm
MOV R1, 30
STORE [100], R1
```

실행 후 R1 값이 30이라면:

```text
R1 = 30
```

Little Endian 기준 메모리 저장 결과:

```text
Address | Value
--------|------
100     | 0x1E
101     | 0x00
102     | 0x00
103     | 0x00
104     | 0x00
105     | 0x00
106     | 0x00
107     | 0x00
```

`LOAD R2, [100]` 실행 후:

```text
R2 = 30
```

---

## 23. Example: Instruction and Data in Same Memory

초기 단순 구현에서는 Code와 Data가 같은 Memory 객체 안에 존재할 수 있다.

예:

```text
Address Range | Purpose
--------------|--------
0 ~ 215       | Code Section
216 ~ 2047    | Data Region
2048 ~ 4095   | Stack Region
```

`STORE [100], R1`처럼 Code Region 내부 주소에 데이터를 쓰는 것은 위험할 수 있다.

초기 구현에서는 이를 허용할 수도 있지만, 향후에는 Code Region을 read-only로 보호할 수 있다.

보호 모드에서는 Code Region 쓰기 시 다음 오류를 발생시킨다.

```text
CodeRegionWriteFault
```

---

## 24. Implementation Plan

Memory 모델 변경은 다음 순서로 진행한다.

1. `Memory` 내부 저장소를 `std::vector<std::uint8_t>`로 변경한다.
2. `readU8`, `writeU8`를 구현한다.
3. `readU32`, `writeU32`를 구현한다.
4. `readU64`, `writeU64`를 구현한다.
5. `readI64`, `writeI64`를 구현한다.
6. Endianness enum을 추가한다.
7. Bounds checking을 모든 접근에 적용한다.
8. Instruction fetch용 `readBytes`를 구현한다.
9. Stack 접근을 8-byte 단위로 변경한다.
10. 기존 `read`, `write` API는 임시 호환용으로 유지하거나 제거한다.

---

## 25. Summary

Zero-CPU v0.2의 메모리 모델은 byte-addressable memory를 기반으로 한다.

핵심 특징은 다음과 같다.

```text
Byte-addressable memory
Little Endian by default
Explicit Big Endian support design
64-bit signed register values
8-byte data load/store
24-byte instruction fetch
4-byte instruction alignment
8-byte stack alignment
Bounds checking
Stack overflow and underflow detection
```

이 메모리 모델을 통해 Zero-CPU는 `.zbin` 바이트코드를 메모리에 적재하고, 실제 CPU처럼 바이트 주소 기반 Fetch-Decode-Execute를 수행할 수 있다.
