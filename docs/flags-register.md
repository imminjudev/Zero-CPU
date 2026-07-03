# Zero-CPU Flags Register

## 1. Overview

Zero-CPU v0.1에서는 CPU 상태 플래그를 개별 bool 값으로 표현했다.

예:

```cpp
bool zero_flag;
bool sign_flag;
bool overflow_flag;
bool carry_flag;
```

이 방식은 구현이 단순하지만, 실제 CPU의 플래그 레지스터 구조와는 거리가 있다.

Zero-CPU v0.2부터는 플래그를 하나의 32-bit 정수 레지스터로 표현한다.

```cpp
std::uint32_t bits_;
```

각 플래그는 이 정수 안의 특정 비트로 표현된다.

이 구조는 실제 x86의 EFLAGS/RFLAGS처럼 하나의 플래그 레지스터 안에서 여러 상태 비트를 관리하는 방식에 가깝다.

---

## 2. Design Goals

Zero-CPU Flags Register의 목표는 다음과 같다.

1. 여러 플래그를 하나의 32-bit 레지스터로 관리한다.
2. 각 플래그는 비트 마스크를 통해 설정하고 검사한다.
3. 실제 CPU의 EFLAGS 구조를 참고한 플래그 배치를 사용한다.
4. 산술, 비교, 분기 명령어가 플래그 레지스터를 기반으로 동작하게 한다.
5. Trace 시스템에서 플래그 변경을 명확히 기록할 수 있게 한다.

---

## 3. Flags Register Layout

Zero-CPU의 Flags Register는 32-bit unsigned integer로 표현한다.

```text
Flags Register = uint32_t
```

초기 플래그 배치는 다음과 같다.

```text
Bit | Mask       | Name | Meaning
----|------------|------|----------------
0   | 0x00000001 | CF   | Carry Flag
6   | 0x00000040 | ZF   | Zero Flag
7   | 0x00000080 | SF   | Sign Flag
11  | 0x00000800 | OF   | Overflow Flag
```

이 배치는 x86 EFLAGS의 주요 플래그 위치를 참고한다.

Zero-CPU가 x86과 완전히 호환되는 것은 아니지만, 실제 CPU 구조와 유사한 형태로 설계하기 위해 해당 위치를 사용한다.

---

## 4. Bit Mask Constants

Flags 클래스는 다음 비트 마스크 상수를 가진다.

```cpp
enum Bit : std::uint32_t {
    CF = 1u << 0,
    ZF = 1u << 6,
    SF = 1u << 7,
    OF = 1u << 11
};
```

각 값은 다음과 같다.

```text
CF = 0x00000001
ZF = 0x00000040
SF = 0x00000080
OF = 0x00000800
```

---

## 5. Flag Meanings

## 5.1 Carry Flag

Carry Flag는 주로 unsigned arithmetic에서 자리올림 또는 빌림이 발생했는지 나타낸다.

```text
CF = 1 → carry or borrow occurred
CF = 0 → no carry or borrow
```

초기 Zero-CPU 구현에서는 CF를 단순 산술 연산 확장 단계에서 사용한다.

---

## 5.2 Zero Flag

Zero Flag는 연산 결과가 0인지 나타낸다.

```text
ZF = 1 → result == 0
ZF = 0 → result != 0
```

예:

```asm
MOV R1, 10
CMP R1, 10
```

`R1 - 10 = 0` 이므로:

```text
ZF = 1
```

`JE` 명령어는 ZF를 검사한다.

```text
JE target:
    if ZF == 1:
        PC = target
```

---

## 5.3 Sign Flag

Sign Flag는 signed integer 기준으로 결과가 음수인지 나타낸다.

```text
SF = 1 → result < 0
SF = 0 → result >= 0
```

예:

```asm
MOV R1, 3
CMP R1, 5
```

`R1 - 5 = -2` 이므로:

```text
SF = 1
```

`JL` 명령어는 초기 구현에서 SF를 기반으로 동작한다.

```text
JL target:
    if SF == 1:
        PC = target
```

---

## 5.4 Overflow Flag

Overflow Flag는 signed arithmetic에서 표현 가능한 범위를 넘어서는 결과가 발생했는지 나타낸다.

```text
OF = 1 → signed overflow occurred
OF = 0 → no signed overflow
```

예:

```text
INT64_MAX + 1 → signed overflow
```

초기 구현에서는 OF를 산술 연산 확장 단계에서 적용할 수 있다.

---

## 6. Class Design

Zero-CPU v0.2의 Flags 클래스는 다음 형태를 가진다.

```cpp
class Flags {
public:
    enum Bit : std::uint32_t {
        CF = 1u << 0,
        ZF = 1u << 6,
        SF = 1u << 7,
        OF = 1u << 11
    };

    Flags();

    void reset();

    std::uint32_t raw() const;
    void setRaw(std::uint32_t value);

    bool carry() const;
    bool zero() const;
    bool sign() const;
    bool overflow() const;

    void setCarry(bool value);
    void setZero(bool value);
    void setSign(bool value);
    void setOverflow(bool value);

    void updateZeroAndSign(std::int64_t result);

    std::string toString() const;

private:
    std::uint32_t bits_;

    bool test(std::uint32_t mask) const;
    void set(std::uint32_t mask, bool value);
};
```

핵심은 `bits_` 하나로 모든 플래그를 관리한다는 점이다.

---

## 7. Internal Operations

플래그 검사는 bitwise AND를 사용한다.

```cpp
bool Flags::test(std::uint32_t mask) const {
    return (bits_ & mask) != 0;
}
```

플래그 설정은 bitwise OR와 bitwise AND NOT을 사용한다.

```cpp
void Flags::set(std::uint32_t mask, bool value) {
    if (value) {
        bits_ |= mask;
    } else {
        bits_ &= ~mask;
    }
}
```

예를 들어 ZF를 켜려면:

```cpp
bits_ |= ZF;
```

ZF를 끄려면:

```cpp
bits_ &= ~ZF;
```

---

## 8. Public Accessors

외부에서는 직접 비트 연산을 하지 않고, 의미 있는 함수로 플래그를 접근한다.

```cpp
bool Flags::zero() const {
    return test(ZF);
}

void Flags::setZero(bool value) {
    set(ZF, value);
}
```

다른 플래그도 같은 방식으로 구현한다.

```cpp
bool carry() const;
bool sign() const;
bool overflow() const;

void setCarry(bool value);
void setSign(bool value);
void setOverflow(bool value);
```

이렇게 하면 내부 구현은 bitmask 기반이지만, CPU 실행 코드에서는 읽기 쉬운 API를 사용할 수 있다.

---

## 9. Updating Zero and Sign Flags

대부분의 산술/비교 명령어는 결과값을 기준으로 ZF와 SF를 갱신한다.

```cpp
void Flags::updateZeroAndSign(std::int64_t result) {
    setZero(result == 0);
    setSign(result < 0);
}
```

예:

```text
result = 0
ZF = 1
SF = 0
```

```text
result = -3
ZF = 0
SF = 1
```

```text
result = 7
ZF = 0
SF = 0
```

---

## 10. Flags and Arithmetic Instructions

산술 명령어는 결과를 레지스터에 저장한 뒤 플래그를 갱신한다.

예:

```asm
ADD R1, R2
```

실행 흐름:

```text
lhs = R1
rhs = R2
result = lhs + rhs
R1 = result
update ZF and SF
optionally update CF and OF
```

초기 구현에서는 ZF와 SF를 우선 적용한다.

```text
ADD/SUB/MUL/DIV:
    updateZeroAndSign(result)
```

CF와 OF는 이후 산술 정확도 개선 단계에서 추가한다.

---

## 11. Flags and Comparison Instructions

`CMP` 명령어는 두 값을 비교하지만 결과를 레지스터에 저장하지 않는다.

예:

```asm
CMP R1, R2
```

실행 흐름:

```text
lhs = R1
rhs = R2
result = lhs - rhs
update ZF and SF
```

레지스터 값은 변경되지 않는다.

```text
R1 unchanged
R2 unchanged
```

플래그만 변경된다.

예:

```asm
MOV R1, 5
CMP R1, 5
```

결과:

```text
ZF = 1
SF = 0
```

예:

```asm
MOV R1, 3
CMP R1, 5
```

결과:

```text
ZF = 0
SF = 1
```

---

## 12. Flags and Branch Instructions

분기 명령어는 Flags Register를 읽어서 PC를 변경한다.

초기 조건 분기 규칙은 다음과 같다.

```text
JE:
    jump if ZF == 1

JNE:
    jump if ZF == 0

JG:
    jump if ZF == 0 and SF == 0

JL:
    jump if SF == 1
```

예:

```asm
CMP R1, R2
JE equal_label
```

`CMP` 결과가 0이면 ZF가 1이 되고, `JE`는 점프한다.

---

## 13. Raw Flags Value

디버깅과 Trace를 위해 Flags Register의 raw 값을 확인할 수 있어야 한다.

```cpp
std::uint32_t Flags::raw() const {
    return bits_;
}
```

예:

```text
bits_ = 0x00000040
```

이는 ZF만 켜진 상태를 의미한다.

```text
ZF = 1
SF = 0
OF = 0
CF = 0
```

ZF와 SF가 동시에 켜진 경우:

```text
bits_ = 0x000000C0
```

```text
0x00000040 | 0x00000080 = 0x000000C0
```

---

## 14. String Representation

Flags는 사람이 읽기 쉬운 문자열로 출력될 수 있어야 한다.

예:

```text
ZF=1 SF=0 OF=0 CF=0 RAW=0x00000040
```

또는:

```text
FLAGS=0x00000040 [ZF]
```

초기 구현에서는 기존 출력 형식과 호환성을 위해 다음 형식을 유지할 수 있다.

```text
ZF=1 SF=0 OF=0 CF=0
```

다만 내부적으로는 bool 값이 아니라 bitmask를 검사해서 출력한다.

---

## 15. Trace Integration

Trace 시스템은 명령어 실행 전후의 Flags Register 값을 비교해야 한다.

기존 방식:

```text
before.ZF != after.ZF
before.SF != after.SF
```

v0.2 방식:

```text
before.raw() != after.raw()
```

또는 개별 플래그 비교:

```text
before.zero() != after.zero()
before.sign() != after.sign()
before.overflow() != after.overflow()
before.carry() != after.carry()
```

Trace 출력 예:

```text
PC=0048 | CMP R1, R2 | ZF:0->1 | FLAGS:0x00000000->0x00000040
```

혹은 간단히:

```text
PC=0048 | CMP R1, R2 | ZF:0->1
```

---

## 16. Advantages Over Boolean Fields

기존 bool 기반 구조:

```cpp
bool zf_;
bool sf_;
bool of_;
bool cf_;
```

장점:

```text
구현이 쉽다
읽기 쉽다
```

단점:

```text
실제 CPU 구조와 거리가 있다
플래그 전체 값을 하나의 레지스터로 다루기 어렵다
Trace에서 raw flags 값을 보여주기 어렵다
비트 연산 학습 효과가 적다
```

bitmask 기반 구조:

```cpp
std::uint32_t bits_;
```

장점:

```text
실제 CPU의 Flags Register와 유사하다
비트 연산 기반 설계를 보여줄 수 있다
raw flags 값을 저장하고 출력할 수 있다
추가 플래그 확장이 쉽다
바이너리 상태 저장에 유리하다
```

---

## 17. Future Flags

향후 Zero-CPU는 다음 플래그를 추가할 수 있다.

```text
PF  Parity Flag
AF  Auxiliary Carry Flag
IF  Interrupt Enable Flag
DF  Direction Flag
```

예상 배치:

```text
Bit | Name
----|------
2   | PF
4   | AF
9   | IF
10  | DF
```

다만 v0.2 초기 구현에서는 다음 네 개만 사용한다.

```text
CF
ZF
SF
OF
```

---

## 18. Example State

초기 상태:

```text
FLAGS = 0x00000000
ZF=0 SF=0 OF=0 CF=0
```

`CMP R1, R1` 실행 후:

```text
result = 0
ZF = 1
SF = 0
FLAGS = 0x00000040
```

`CMP R1, 10` 실행 후 결과가 음수라면:

```text
result < 0
ZF = 0
SF = 1
FLAGS = 0x00000080
```

---

## 19. Implementation Plan

Flags 리팩터링은 다음 순서로 진행한다.

1. `Flags.hpp`에서 bool 멤버를 제거한다.
2. `std::uint32_t bits_`를 추가한다.
3. `enum Bit`으로 CF, ZF, SF, OF 마스크를 정의한다.
4. `test(mask)`와 `set(mask, value)` private 함수를 구현한다.
5. 기존 `zero()`, `sign()`, `overflow()`, `carry()` API를 유지한다.
6. 기존 `setZero()`, `setSign()`, `setOverflow()`, `setCarry()` API를 유지한다.
7. `raw()`와 `setRaw()`를 추가한다.
8. `updateZeroAndSign()`이 bitmask 기반으로 동작하게 한다.
9. `toString()`을 bitmask 기반으로 수정한다.
10. TraceEvent가 기존 API를 그대로 사용하므로 최소 변경으로 동작하게 한다.

---

## 20. Compatibility

외부 CPU 실행 코드는 가능하면 기존 API를 그대로 사용한다.

예:

```cpp
state_.flags().setZero(true);
state_.flags().zero();
state_.flags().updateZeroAndSign(result);
```

따라서 내부 구현을 bool에서 bitmask로 바꾸더라도 CPU 코드 대부분은 그대로 유지할 수 있다.

이 방식은 리팩터링 위험을 줄인다.

---

## 21. Summary

Zero-CPU v0.2의 Flags Register는 하나의 32-bit 정수로 표현된다.

핵심 특징은 다음과 같다.

```text
uint32_t 기반 Flags Register
CF/ZF/SF/OF 비트 마스크
x86 EFLAGS와 유사한 주요 비트 위치
bitwise AND/OR 기반 플래그 설정
raw flags 값 출력 가능
Trace 시스템과 연동 가능
기존 public API 유지
```

이 구조를 통해 Zero-CPU는 단순한 bool 플래그 집합이 아니라, 실제 CPU에 가까운 플래그 레지스터 모델을 갖게 된다.
