# Zero-CPU ISA Specification

## 1. Overview

이 문서는 Zero-CPU의 초기 명령어 집합 구조, 즉 ISA(Instruction Set Architecture)를 정의한다.

Zero-CPU ISA는 실제 x86 또는 ARM 아키텍처를 그대로 모방하지 않고, CPU의 핵심 실행 원리를 학습하고 분석하기 위해 단순화된 가상 명령어 집합으로 설계한다.

Zero-CPU의 ISA는 다음 목표를 가진다.

* 명령어 실행 흐름을 명확하게 추적할 수 있어야 한다.
* 각 명령어는 CPU 상태를 어떻게 변화시키는지 문서로 설명 가능해야 한다.
* 레지스터, 메모리, 플래그, 스택, 분기 구조를 모두 다룰 수 있어야 한다.
* 어셈블러와 디버거 구현이 가능할 정도로 단순해야 한다.
* 이후 운영체제 시뮬레이션, 인터럽트, 가상 메모리, 파이프라인 실험 등으로 확장 가능해야 한다.

## 2. ISA Version

```text
ISA Version: Zero-CPU ISA v0.1
Status: Experimental
Target: Educational / Research-Oriented Virtual CPU
```

v0.1 단계에서는 CPU의 기본 동작을 검증하기 위해 단순한 명령어 집합을 우선 정의한다.

이후 버전에서 다음 기능을 확장할 수 있다.

* 64-bit register mode
* 320-bit experimental register mode
* interrupt instruction
* system call instruction
* process context instruction
* virtual memory instruction
* pipeline simulation metadata

## 3. Register Model

Zero-CPU는 초기 버전에서 8개의 범용 레지스터를 가진다.

```text
R0
R1
R2
R3
R4
R5
R6
R7
```

각 레지스터는 정수 값을 저장한다.

초기 구현에서는 단순성을 위해 `int64_t` 기반 정수 레지스터를 사용한다.
이후 확장 단계에서는 `UInt320` 또는 custom integer type을 도입하여 실험적 레지스터 모델을 구현할 수 있다.

## 4. Special Registers

Zero-CPU는 다음 특수 레지스터를 가진다.

| 이름      | 의미                              |
| ------- | ------------------------------- |
| `PC`    | Program Counter. 다음에 실행할 명령어 위치 |
| `SP`    | Stack Pointer. 현재 스택 위치         |
| `FLAGS` | 연산 결과 상태를 저장하는 플래그 레지스터         |

## 5. Flags

Zero-CPU는 초기 버전에서 다음 플래그를 사용한다.

| 플래그  | 이름            | 의미                           |
| ---- | ------------- | ---------------------------- |
| `ZF` | Zero Flag     | 연산 결과가 0이면 1                 |
| `SF` | Sign Flag     | 연산 결과가 음수이면 1                |
| `OF` | Overflow Flag | 산술 오버플로우 발생 시 1              |
| `CF` | Carry Flag    | 부호 없는 연산에서 자리올림 또는 빌림 발생 시 1 |

초기 구현에서는 `ZF`, `SF`를 우선 구현하고, `OF`, `CF`는 이후 확장 구현으로 둘 수 있다.

## 6. Memory Model

Zero-CPU는 단순한 선형 메모리 구조를 사용한다.

```text
Memory[0]
Memory[1]
Memory[2]
...
Memory[N-1]
```

초기 메모리 크기는 다음과 같이 설정한다.

```text
Memory Size: 1024 cells
Cell Type: int64_t
```

메모리 접근은 대괄호 문법을 사용한다.

```asm
LOAD R1, [100]
STORE [100], R1
```

## 7. Operand Types

Zero-CPU 명령어는 다음 피연산자 타입을 사용한다.

| 타입          | 예시            | 설명       |
| ----------- | ------------- | -------- |
| `REGISTER`  | `R1`          | 범용 레지스터  |
| `IMMEDIATE` | `10`, `-5`    | 즉시값      |
| `MEMORY`    | `[100]`       | 메모리 주소   |
| `LABEL`     | `loop`, `end` | 분기 대상 라벨 |
| `NONE`      | 없음            | 피연산자 없음  |

## 8. Assembly Syntax

Zero-CPU 어셈블리 파일의 확장자는 `.zasm`이다.

기본 문법은 다음과 같다.

```asm
OPCODE DST, SRC
```

예시:

```asm
MOV R1, 10
MOV R2, 20
ADD R1, R2
HALT
```

라벨은 다음과 같이 정의한다.

```asm
loop:
    ADD R1, R2
    JMP loop
```

주석은 `;` 문자를 사용한다.

```asm
MOV R1, 10   ; R1에 10 저장
```

## 9. Instruction Categories

Zero-CPU ISA v0.1의 명령어는 다음 범주로 나뉜다.

| 범주            | 명령어                            |
| ------------- | ------------------------------ |
| Data Movement | `MOV`, `LOAD`, `STORE`         |
| Arithmetic    | `ADD`, `SUB`, `MUL`, `DIV`     |
| Logical       | `AND`, `OR`, `XOR`, `NOT`      |
| Comparison    | `CMP`, `TEST`                  |
| Branch        | `JMP`, `JE`, `JNE`, `JG`, `JL` |
| Stack         | `PUSH`, `POP`                  |
| Function Call | `CALL`, `RET`                  |
| Control       | `NOP`, `HALT`                  |

## 10. Data Movement Instructions

### 10.1 MOV

값을 레지스터에 복사한다.

```asm
MOV dst, src
```

지원 형식:

```asm
MOV R1, 10
MOV R1, R2
```

의미:

```text
dst = src
PC = PC + 1
```

플래그 변경:

```text
None
```

예시:

```asm
MOV R1, 10
```

실행 결과:

```text
R1 = 10
```

---

### 10.2 LOAD

메모리 값을 레지스터로 가져온다.

```asm
LOAD dst, [address]
```

지원 형식:

```asm
LOAD R1, [100]
```

의미:

```text
dst = Memory[address]
PC = PC + 1
```

플래그 변경:

```text
None
```

예시:

```asm
LOAD R1, [100]
```

실행 결과:

```text
R1 = Memory[100]
```

---

### 10.3 STORE

레지스터 값을 메모리에 저장한다.

```asm
STORE [address], src
```

지원 형식:

```asm
STORE [100], R1
STORE [100], 10
```

의미:

```text
Memory[address] = src
PC = PC + 1
```

플래그 변경:

```text
None
```

예시:

```asm
STORE [100], R1
```

실행 결과:

```text
Memory[100] = R1
```

## 11. Arithmetic Instructions

### 11.1 ADD

두 값을 더한다.

```asm
ADD dst, src
```

지원 형식:

```asm
ADD R1, R2
ADD R1, 10
```

의미:

```text
dst = dst + src
Update ZF
Update SF
PC = PC + 1
```

플래그 변경:

| 플래그  | 조건           |
| ---- | ------------ |
| `ZF` | 결과가 0이면 1    |
| `SF` | 결과가 음수이면 1   |
| `OF` | 오버플로우 발생 시 1 |
| `CF` | 자리올림 발생 시 1  |

예시:

```asm
MOV R1, 10
MOV R2, 20
ADD R1, R2
```

실행 결과:

```text
R1 = 30
```

---

### 11.2 SUB

두 값을 뺀다.

```asm
SUB dst, src
```

지원 형식:

```asm
SUB R1, R2
SUB R1, 10
```

의미:

```text
dst = dst - src
Update ZF
Update SF
PC = PC + 1
```

예시:

```asm
MOV R1, 20
SUB R1, 5
```

실행 결과:

```text
R1 = 15
```

---

### 11.3 MUL

두 값을 곱한다.

```asm
MUL dst, src
```

지원 형식:

```asm
MUL R1, R2
MUL R1, 10
```

의미:

```text
dst = dst * src
Update ZF
Update SF
PC = PC + 1
```

예시:

```asm
MOV R1, 3
MUL R1, 4
```

실행 결과:

```text
R1 = 12
```

---

### 11.4 DIV

두 값을 나눈다.

```asm
DIV dst, src
```

지원 형식:

```asm
DIV R1, R2
DIV R1, 10
```

의미:

```text
dst = dst / src
Update ZF
Update SF
PC = PC + 1
```

예외 조건:

```text
src == 0이면 division by zero error 발생
```

예시:

```asm
MOV R1, 20
DIV R1, 4
```

실행 결과:

```text
R1 = 5
```

## 12. Logical Instructions

### 12.1 AND

비트 AND 연산을 수행한다.

```asm
AND dst, src
```

의미:

```text
dst = dst & src
Update ZF
Update SF
PC = PC + 1
```

예시:

```asm
MOV R1, 6
MOV R2, 3
AND R1, R2
```

실행 결과:

```text
R1 = 2
```

---

### 12.2 OR

비트 OR 연산을 수행한다.

```asm
OR dst, src
```

의미:

```text
dst = dst | src
Update ZF
Update SF
PC = PC + 1
```

---

### 12.3 XOR

비트 XOR 연산을 수행한다.

```asm
XOR dst, src
```

의미:

```text
dst = dst ^ src
Update ZF
Update SF
PC = PC + 1
```

---

### 12.4 NOT

비트 NOT 연산을 수행한다.

```asm
NOT dst
```

의미:

```text
dst = ~dst
Update ZF
Update SF
PC = PC + 1
```

## 13. Comparison Instructions

### 13.1 CMP

두 값을 비교한다.

```asm
CMP lhs, rhs
```

지원 형식:

```asm
CMP R1, R2
CMP R1, 10
```

의미:

```text
temp = lhs - rhs
Update ZF
Update SF
PC = PC + 1
```

중요한 점은 `CMP`는 실제 레지스터 값을 변경하지 않는다는 것이다.
오직 플래그만 변경한다.

예시:

```asm
MOV R1, 10
CMP R1, 10
JE equal
```

실행 결과:

```text
ZF = 1
```

---

### 13.2 TEST

두 값을 AND 연산하여 결과에 따라 플래그를 설정한다.

```asm
TEST lhs, rhs
```

의미:

```text
temp = lhs & rhs
Update ZF
Update SF
PC = PC + 1
```

`TEST` 역시 실제 레지스터 값을 변경하지 않는다.

## 14. Branch Instructions

### 14.1 JMP

무조건 분기한다.

```asm
JMP label
```

의미:

```text
PC = label_address
```

예시:

```asm
JMP end
```

---

### 14.2 JE

Zero Flag가 1이면 분기한다.

```asm
JE label
```

의미:

```text
if ZF == 1:
    PC = label_address
else:
    PC = PC + 1
```

예시:

```asm
CMP R1, R2
JE equal
```

---

### 14.3 JNE

Zero Flag가 0이면 분기한다.

```asm
JNE label
```

의미:

```text
if ZF == 0:
    PC = label_address
else:
    PC = PC + 1
```

---

### 14.4 JG

왼쪽 값이 오른쪽 값보다 크다고 판단되면 분기한다.

```asm
JG label
```

초기 설계에서는 `CMP lhs, rhs` 이후 다음 조건을 사용한다.

```text
if ZF == 0 and SF == 0:
    PC = label_address
else:
    PC = PC + 1
```

단, 이 조건은 단순화된 비교 규칙이며, 실제 CPU의 signed comparison과는 다를 수 있다.

---

### 14.5 JL

왼쪽 값이 오른쪽 값보다 작다고 판단되면 분기한다.

```asm
JL label
```

초기 설계에서는 `CMP lhs, rhs` 이후 다음 조건을 사용한다.

```text
if SF == 1:
    PC = label_address
else:
    PC = PC + 1
```

단, 이 조건은 단순화된 비교 규칙이며, 실제 CPU의 signed comparison과는 다를 수 있다.

## 15. Stack Instructions

### 15.1 PUSH

값을 스택에 저장한다.

```asm
PUSH src
```

지원 형식:

```asm
PUSH R1
PUSH 10
```

초기 스택은 낮은 주소에서 높은 주소로 증가한다고 가정한다.

의미:

```text
Memory[SP] = src
SP = SP + 1
PC = PC + 1
```

예시:

```asm
MOV R1, 10
PUSH R1
```

---

### 15.2 POP

스택에서 값을 꺼내 레지스터에 저장한다.

```asm
POP dst
```

지원 형식:

```asm
POP R1
```

의미:

```text
SP = SP - 1
dst = Memory[SP]
PC = PC + 1
```

예시:

```asm
PUSH 10
POP R1
```

실행 결과:

```text
R1 = 10
```

## 16. Function Call Instructions

### 16.1 CALL

함수를 호출한다.

```asm
CALL label
```

의미:

```text
Memory[SP] = PC + 1
SP = SP + 1
PC = label_address
```

`CALL`은 다음 명령어의 주소를 스택에 저장한 뒤, 지정된 라벨로 이동한다.

예시:

```asm
CALL function
HALT

function:
    MOV R1, 10
    RET
```

---

### 16.2 RET

함수 호출 이전 위치로 복귀한다.

```asm
RET
```

의미:

```text
SP = SP - 1
PC = Memory[SP]
```

예시:

```asm
CALL function
HALT

function:
    MOV R1, 10
    RET
```

실행 흐름:

```text
CALL function
MOV R1, 10
RET
HALT
```

## 17. Control Instructions

### 17.1 NOP

아무 동작도 하지 않는다.

```asm
NOP
```

의미:

```text
PC = PC + 1
```

---

### 17.2 HALT

CPU 실행을 중지한다.

```asm
HALT
```

의미:

```text
halted = true
```

`HALT` 명령어가 실행되면 CPU는 더 이상 다음 명령어를 실행하지 않는다.

## 18. Initial Instruction Table

| Opcode  | Operand Format    | Description     | Flags      |
| ------- | ----------------- | --------------- | ---------- |
| `MOV`   | `reg, reg/imm`    | 값 복사            | 변경 없음      |
| `LOAD`  | `reg, [addr]`     | 메모리에서 로드        | 변경 없음      |
| `STORE` | `[addr], reg/imm` | 메모리에 저장         | 변경 없음      |
| `ADD`   | `reg, reg/imm`    | 덧셈              | `ZF`, `SF` |
| `SUB`   | `reg, reg/imm`    | 뺄셈              | `ZF`, `SF` |
| `MUL`   | `reg, reg/imm`    | 곱셈              | `ZF`, `SF` |
| `DIV`   | `reg, reg/imm`    | 나눗셈             | `ZF`, `SF` |
| `AND`   | `reg, reg/imm`    | 비트 AND          | `ZF`, `SF` |
| `OR`    | `reg, reg/imm`    | 비트 OR           | `ZF`, `SF` |
| `XOR`   | `reg, reg/imm`    | 비트 XOR          | `ZF`, `SF` |
| `NOT`   | `reg`             | 비트 NOT          | `ZF`, `SF` |
| `CMP`   | `reg, reg/imm`    | 비교              | `ZF`, `SF` |
| `TEST`  | `reg, reg/imm`    | 비트 테스트          | `ZF`, `SF` |
| `JMP`   | `label`           | 무조건 분기          | 변경 없음      |
| `JE`    | `label`           | `ZF == 1`이면 분기  | 변경 없음      |
| `JNE`   | `label`           | `ZF == 0`이면 분기  | 변경 없음      |
| `JG`    | `label`           | greater 조건이면 분기 | 변경 없음      |
| `JL`    | `label`           | less 조건이면 분기    | 변경 없음      |
| `PUSH`  | `reg/imm`         | 스택에 저장          | 변경 없음      |
| `POP`   | `reg`             | 스택에서 꺼냄         | 변경 없음      |
| `CALL`  | `label`           | 함수 호출           | 변경 없음      |
| `RET`   | 없음                | 함수 복귀           | 변경 없음      |
| `NOP`   | 없음                | 동작 없음           | 변경 없음      |
| `HALT`  | 없음                | 실행 중지           | 변경 없음      |

## 19. Example Program: Simple Addition

```asm
; simple_add.zasm

MOV R1, 10
MOV R2, 20
ADD R1, R2
HALT
```

예상 최종 상태:

```text
R1 = 30
R2 = 20
halted = true
```

## 20. Example Program: Branch

```asm
; branch_test.zasm

MOV R1, 10
MOV R2, 10
CMP R1, R2
JE equal

MOV R3, 0
JMP end

equal:
    MOV R3, 1

end:
    HALT
```

예상 최종 상태:

```text
R3 = 1
halted = true
```

## 21. Example Program: Function Call

```asm
; function_call.zasm

CALL set_value
HALT

set_value:
    MOV R1, 100
    RET
```

예상 최종 상태:

```text
R1 = 100
halted = true
```

## 22. Error Conditions

Zero-CPU는 다음 상황을 오류로 처리한다.

| 오류                   | 설명                   |
| -------------------- | -------------------- |
| Invalid Opcode       | 정의되지 않은 명령어 사용       |
| Invalid Register     | 존재하지 않는 레지스터 사용      |
| Invalid Operand      | 명령어에 맞지 않는 피연산자 사용   |
| Memory Out of Bounds | 메모리 범위를 벗어난 접근       |
| Stack Overflow       | 스택 범위를 초과한 PUSH/CALL |
| Stack Underflow      | 빈 스택에서 POP/RET       |
| Division By Zero     | 0으로 나눔               |
| Unknown Label        | 존재하지 않는 라벨 참조        |

## 23. Design Notes

Zero-CPU ISA v0.1은 완전한 실제 CPU 구현이 아니라, CPU의 핵심 구조를 이해하기 위한 실험적 ISA이다.

따라서 다음과 같은 의도적인 단순화를 포함한다.

* 명령어 길이를 고정적으로 다루지 않는다.
* 실제 바이너리 인코딩은 초기 버전에서 구현하지 않는다.
* 메모리 주소는 단순 정수 인덱스로 처리한다.
* 조건 분기는 단순화된 플래그 규칙을 사용한다.
* 인터럽트, 캐시, 파이프라인, 가상 메모리는 초기 버전에서 제외한다.

이러한 단순화를 통해 초기 구현에서는 CPU 실행 구조, 어셈블러, 디버거, 트레이스 시스템에 집중한다.

## 24. Summary

Zero-CPU ISA v0.1은 직접 설계한 가상 CPU에서 실행되는 최소 명령어 집합이다.

이 ISA는 데이터 이동, 산술 연산, 논리 연산, 비교, 분기, 스택, 함수 호출, 실행 제어 명령어를 포함한다.

Zero-CPU의 핵심은 명령어 실행 결과뿐 아니라 실행 전후의 CPU 상태 변화를 추적하는 것이다. 따라서 모든 명령어는 이후 `execution-semantics.md` 문서에서 상태 전이 규칙으로 더 자세히 정의한다.
