# rccar 보드별 핀 매핑 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) 또는 superpowers:executing-plans 로 태스크 단위 실행. 진행 추적은 체크박스(`- [ ]`) 사용.

**Goal:** Kconfig `choice`로 rccar 핀 매핑을 PCB 변형(nutcracker1.0 / kingtiger1.1)별로 전환하고, 기본 빌드를 kingtiger1.1로 만든다.

**Architecture:** `components/rccar/Kconfig`에 보드 `choice`를 추가하고, `rccar_pins.h`를 `#if/#elif/#error` 블록으로 재편한다. 매크로 이름은 그대로라 소비 모듈은 변경하지 않는다. 두 변형 GPIO 값은 현재 동일하다.

**Tech Stack:** ESP-IDF v5.5.x, Kconfig, C 전처리기

**설계 문서:** `docs/superpowers/specs/2026-08-13-rccar-board-pin-variants-design.md`

## Global Constraints

- 문서/답변: 한글 (`AGENTS.md`)
- 타깃 칩: ESP32 classic (`idf.py set-target esp32`)
- 환경: 루트 `env.bat` (Espressif IDF `esp-idf-b29c58f93b4ca0f49cdfc4c3ef43b562`)
- 엠 대시(`—`), 가운뎃점(`·`) 출력 금지 (하이픈/쉼표 사용)
- Kconfig 옵션 이름: `RCCAR_BOARD_NUTCRACKER_1_0`, `RCCAR_BOARD_KINGTIGER_1_1`
- 기본 변형: kingtiger1.1
- 초기 kingtiger1.1 GPIO 값 = nutcracker1.0과 동일
- 소비 모듈(motor/servo/shiftreg/led/dfplayer)은 변경하지 않는다
- 커밋 메시지: 변경 요지 명확 (한글/영문 가능)

---

## 파일 구조 (생성/수정)

| 경로 | 역할 |
|------|------|
| `components/rccar/Kconfig` | (생성) 보드 변형 choice |
| `components/rccar/rccar_pins.h` | (수정) 변형별 조건부 핀 블록 |
| `sdkconfig.defaults` | (수정) 기본 변형 kingtiger1.1 명시 |
| `README.md` | (수정) 핀 맵 두 변형 컬럼 + 선택 방법 |
| `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md` | (수정) 3.1절에 변형 분기 참조 |
| `AGENTS.md` | (수정) 두 변형 동기화 규칙 |

기존 GPIO 값(설계 3.1 및 현재 `rccar_pins.h`):

| 매크로 | GPIO |
|--------|------|
| RCCAR_PIN_FL_IN1 | 27 |
| RCCAR_PIN_FL_IN2 | 26 |
| RCCAR_PIN_FR_IN1 | 25 |
| RCCAR_PIN_FR_IN2 | 33 |
| RCCAR_PIN_RL_IN1 | 32 |
| RCCAR_PIN_RL_IN2 | 14 |
| RCCAR_PIN_RR_IN1 | 13 |
| RCCAR_PIN_RR_IN2 | 16 |
| RCCAR_PIN_TURRET_IN1 | 22 |
| RCCAR_PIN_TURRET_IN2 | 21 |
| RCCAR_PIN_RADAR_SERVO | 17 |
| RCCAR_PIN_WARM_WHITE | 4 |
| RCCAR_PIN_SOUND_TX | 5 |
| RCCAR_PIN_SOUND_RX | NC |
| RCCAR_PIN_595_DATA | 23 |
| RCCAR_PIN_595_CLOCK | 18 |
| RCCAR_PIN_595_LATCH | 19 |

---

### Task 1: Kconfig 보드 choice + 조건부 핀 헤더 + 기본값

**Files:**
- Create: `components/rccar/Kconfig`
- Modify: `components/rccar/rccar_pins.h`
- Modify: `sdkconfig.defaults`

**Interfaces:**
- Consumes: 없음 (전처리기 심볼 `CONFIG_RCCAR_BOARD_*` 는 Kconfig가 sdkconfig.h로 생성)
- Produces: `RCCAR_PIN_*` 매크로 (이름/의미 기존과 동일, 값은 선택된 변형 기준). 소비 모듈이 그대로 사용.

- [ ] **Step 1: `components/rccar/Kconfig` 생성**

`components/rccar/Kconfig`:

```
menu "RCCAR board"

choice RCCAR_BOARD
    prompt "PCB variant"
    default RCCAR_BOARD_KINGTIGER_1_1
    help
        rccar 핀 매핑을 결정하는 PCB 변형을 선택한다.

    config RCCAR_BOARD_NUTCRACKER_1_0
        bool "nutcracker 1.0"

    config RCCAR_BOARD_KINGTIGER_1_1
        bool "kingtiger 1.1"
endchoice

endmenu
```

- [ ] **Step 2: `rccar_pins.h` 를 변형별 블록으로 재편**

`components/rccar/rccar_pins.h` 전체를 아래로 교체:

```c
/**
 * @file rccar_pins.h
 * @brief 핀 정의 (보드 변형별). 변형 선택은 Kconfig RCCAR_BOARD.
 *        설계: docs/superpowers/specs/2026-08-13-rccar-board-pin-variants-design.md
 */
#ifndef RCCAR_PINS_H
#define RCCAR_PINS_H

#include "sdkconfig.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_RCCAR_BOARD_KINGTIGER_1_1)

/* ===== kingtiger1.1 (현재 nutcracker1.0과 동일 값) ===== */

/* DRV8833 #1 전륜 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
#define RCCAR_PIN_FL_IN2 (GPIO_NUM_26)
#define RCCAR_PIN_FR_IN1 (GPIO_NUM_25)
#define RCCAR_PIN_FR_IN2 (GPIO_NUM_33)

/* DRV8833 #2 후륜 */
#define RCCAR_PIN_RL_IN1 (GPIO_NUM_32)
#define RCCAR_PIN_RL_IN2 (GPIO_NUM_14)
#define RCCAR_PIN_RR_IN1 (GPIO_NUM_13)
#define RCCAR_PIN_RR_IN2 (GPIO_NUM_16)

/* DRV8833 #3 포탑 */
#define RCCAR_PIN_TURRET_IN1 (GPIO_NUM_22)
#define RCCAR_PIN_TURRET_IN2 (GPIO_NUM_21)

/* 레이더 CR 서보 */
#define RCCAR_PIN_RADAR_SERVO (GPIO_NUM_17)

/* 웜 화이트 MOSFET */
#define RCCAR_PIN_WARM_WHITE (GPIO_NUM_4)

/* DFPlayer TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

/* 74HC595 x4 */
#define RCCAR_PIN_595_DATA  (GPIO_NUM_23)
#define RCCAR_PIN_595_CLOCK (GPIO_NUM_18)
#define RCCAR_PIN_595_LATCH (GPIO_NUM_19)

#elif defined(CONFIG_RCCAR_BOARD_NUTCRACKER_1_0)

/* ===== nutcracker1.0 ===== */

/* DRV8833 #1 전륜 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
#define RCCAR_PIN_FL_IN2 (GPIO_NUM_26)
#define RCCAR_PIN_FR_IN1 (GPIO_NUM_25)
#define RCCAR_PIN_FR_IN2 (GPIO_NUM_33)

/* DRV8833 #2 후륜 */
#define RCCAR_PIN_RL_IN1 (GPIO_NUM_32)
#define RCCAR_PIN_RL_IN2 (GPIO_NUM_14)
#define RCCAR_PIN_RR_IN1 (GPIO_NUM_13)
#define RCCAR_PIN_RR_IN2 (GPIO_NUM_16)

/* DRV8833 #3 포탑 */
#define RCCAR_PIN_TURRET_IN1 (GPIO_NUM_22)
#define RCCAR_PIN_TURRET_IN2 (GPIO_NUM_21)

/* 레이더 CR 서보 */
#define RCCAR_PIN_RADAR_SERVO (GPIO_NUM_17)

/* 웜 화이트 MOSFET */
#define RCCAR_PIN_WARM_WHITE (GPIO_NUM_4)

/* DFPlayer TX */
#define RCCAR_PIN_SOUND_TX (GPIO_NUM_5)
#define RCCAR_PIN_SOUND_RX (GPIO_NUM_NC)

/* 74HC595 x4 */
#define RCCAR_PIN_595_DATA  (GPIO_NUM_23)
#define RCCAR_PIN_595_CLOCK (GPIO_NUM_18)
#define RCCAR_PIN_595_LATCH (GPIO_NUM_19)

#else
#error "RCCAR board variant not selected (CONFIG_RCCAR_BOARD_*)"
#endif

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_PINS_H */
```

- [ ] **Step 3: `sdkconfig.defaults` 에 기본 변형 명시**

`sdkconfig.defaults` 끝에 한 줄 추가:

```
CONFIG_RCCAR_BOARD_KINGTIGER_1_1=y
```

- [ ] **Step 4: 기존 sdkconfig 재생성 후 빌드 (기본 = kingtiger1.1)**

기존 `sdkconfig`는 새 Kconfig 심볼을 모르므로 삭제 후 재생성한다.

```bat
env.bat
del sdkconfig
idf.py set-target esp32
idf.py build
```

Expected: 빌드 성공, `nutcracker_idf.bin` 생성. 경고/에러 0.

확인: 생성된 `sdkconfig`에 `CONFIG_RCCAR_BOARD_KINGTIGER_1_1=y` 존재.

```bat
findstr RCCAR_BOARD sdkconfig
```

Expected 출력에 `CONFIG_RCCAR_BOARD_KINGTIGER_1_1=y` 포함.

- [ ] **Step 5: nutcracker1.0 전환 빌드로 양쪽 검증**

nutcracker1.0 변형도 빌드되는지 확인한다. ESP-IDF는 `SDKCONFIG_DEFAULTS`에 세미콜론으로 여러 파일을 넘기면 뒤 파일이 앞을 덮어쓴다. 이를 이용해 소스/기본값을 건드리지 않고 override 파일 하나로 nutcracker를 강제한다.

override 파일 생성 (`sdkconfig.nutcracker`, 한 줄):

```
CONFIG_RCCAR_BOARD_NUTCRACKER_1_0=y
```

기존 sdkconfig 삭제 후 override로 재구성/빌드:

```bat
del sdkconfig
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.nutcracker" set-target esp32
idf.py build
findstr RCCAR_BOARD sdkconfig
```

Expected: 빌드 성공, 출력에 `CONFIG_RCCAR_BOARD_NUTCRACKER_1_0=y` 포함.

검증 후 기본 kingtiger1.1로 원복 (override 파일 및 sdkconfig 제거):

```bat
del sdkconfig sdkconfig.nutcracker
idf.py set-target esp32
idf.py build
findstr RCCAR_BOARD sdkconfig
```

Expected: 빌드 성공, 출력에 `CONFIG_RCCAR_BOARD_KINGTIGER_1_1=y` 포함. `sdkconfig.nutcracker`는 커밋하지 않는다.

- [ ] **Step 6: 커밋**

```bash
git add components/rccar/Kconfig components/rccar/rccar_pins.h sdkconfig.defaults
git commit -m "feat: rccar 보드 변형 Kconfig 선택 (기본 kingtiger1.1)"
```

---

### Task 2: 문서 갱신 (README / 설계 3.1 / AGENTS)

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: Task 1의 Kconfig 옵션 이름과 기본값
- Produces: 없음 (문서)

- [ ] **Step 1: README 핀 맵 절 갱신**

`README.md`의 "## 핀 맵" 절에서, 현재 단일 GPIO 표를 두 변형 컬럼으로 바꾸고 선택 방법을 추가한다.

기존 표 헤더/행을 아래 형태로 교체 (값은 두 변형 동일):

```markdown
## 핀 맵

정의: `components/rccar/rccar_pins.h`. PCB 변형은 Kconfig `RCCAR_BOARD` 로 선택하며 기본값은 **kingtiger1.1** 입니다.

변형 전환: `idf.py menuconfig` → "RCCAR board" → "PCB variant", 또는 `sdkconfig.defaults` 의 `CONFIG_RCCAR_BOARD_*` 를 수정.

모터 출력에 스트래핑 위험 핀(0, 2, 12, 15)을 쓰지 않습니다.
플래시(6-11), UART0 콘솔(1, 3), 입력 전용(34-39)은 출력에 사용하지 않습니다.

| 기능 | nutcracker1.0 | kingtiger1.1 | 블록 |
| :--- | ---: | ---: | :--- |
| FL IN1 | 27 | 27 | DRV8833 #1 전륜 |
| FL IN2 | 26 | 26 | DRV8833 #1 전륜 |
| FR IN1 | 25 | 25 | DRV8833 #1 전륜 |
| FR IN2 | 33 | 33 | DRV8833 #1 전륜 |
| RL IN1 | 32 | 32 | DRV8833 #2 후륜 |
| RL IN2 | 14 | 14 | DRV8833 #2 후륜 |
| RR IN1 | 13 | 13 | DRV8833 #2 후륜 |
| RR IN2 | 16 | 16 | DRV8833 #2 후륜 |
| 포탑 IN1 | 22 | 22 | DRV8833 #3 |
| 포탑 IN2 | 21 | 21 | DRV8833 #3 |
| 레이더 서보 | 17 | 17 | LEDC |
| 웜 화이트 MOSFET 게이트 | 4 | 4 | Y 토글 |
| DFPlayer TX | 5 | 5 | UART TX |
| 595 DATA (SER) | 23 | 23 | 시프트 LED |
| 595 CLOCK (SRCLK) | 18 | 18 | 시프트 LED |
| 595 LATCH (RCLK) | 19 | 19 | 시프트 LED |
```

주의: 기존 "설계 문서 3.1과 동일" 문구는 위 안내 문장으로 대체된다.

- [ ] **Step 2: 설계 3.1절에 변형 분기 참조 추가**

`docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`의 3.1절 표 바로 위(제목 `### 3.1 핀 맵 (확정, 물리 배치 우선)` 다음 줄)에 한 줄 추가:

```markdown
> 핀 매핑은 이제 PCB 변형별로 갈린다 (nutcracker1.0 / kingtiger1.1, 기본 kingtiger1.1). 아래 표는 nutcracker1.0 기준이며 상세는 `docs/superpowers/specs/2026-08-13-rccar-board-pin-variants-design.md` 참조.
```

기존 표는 그대로 둔다 (nutcracker1.0 기준으로 유효).

- [ ] **Step 3: AGENTS.md 동기화 규칙 갱신**

`AGENTS.md`의 "## 하드웨어 제약 (코드 변경 시)" 절에서 핀 관련 항목을 찾아 아래로 교체/보강한다.

기존:

```markdown
- 핀 변경 시 `rccar_pins.h` 와 README / 설계 3.1을 함께 맞출 것.
```

교체:

```markdown
- 핀 변경 시 `rccar_pins.h` 의 해당 보드 변형 블록과 README 핀 맵 두 컬럼을 함께 맞출 것. 보드 변형은 Kconfig `RCCAR_BOARD` (nutcracker1.0 / kingtiger1.1, 기본 kingtiger1.1).
```

- [ ] **Step 4: 문서 정합성 확인**

세 문서에서 변형 이름과 기본값 표기가 일치하는지 확인.

```bash
grep -rn "kingtiger1.1\|nutcracker1.0\|RCCAR_BOARD" README.md AGENTS.md docs/superpowers/specs/
```

Expected: README/AGENTS/양쪽 설계 문서에서 변형 이름이 일관되게 등장, 기본값 kingtiger1.1 표기 일치.

- [ ] **Step 5: 커밋**

```bash
git add README.md AGENTS.md docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md
git commit -m "docs: 보드 변형(nutcracker1.0/kingtiger1.1) 핀맵과 선택 방법 반영"
```

---

## 스펙 커버리지 자가 점검

| 설계 요구 (스펙 절) | 태스크 |
|---------------------|--------|
| Kconfig choice, 기본 kingtiger1.1 (3, 5) | Task 1 |
| rccar_pins.h #if/#elif/#error 블록 (4) | Task 1 |
| 두 변형 16핀 + SOUND_RX 정의, 초기값 동일 (4.1) | Task 1 |
| sdkconfig.defaults 기본값 (5) | Task 1 |
| 소비 모듈 무변경 (2, 성공기준 4) | Task 1 (구조상 보장) |
| README 두 변형 + 선택법 (6) | Task 2 |
| 설계 3.1 참조 (6) | Task 2 |
| AGENTS 동기화 규칙 (6) | Task 2 |
| 미선택 시 #error (성공기준 3) | Task 1 Step 2 |
| 양쪽 변형 빌드 (성공기준 1, 2) | Task 1 Step 4-5 |

## 플레이스홀더 검사

TBD/TODO 없음. Kconfig 옵션 이름, 핀 값, 빌드/검증 명령 모두 명시.

## 타입/이름 일관성

- Kconfig 심볼: `RCCAR_BOARD`, `RCCAR_BOARD_NUTCRACKER_1_0`, `RCCAR_BOARD_KINGTIGER_1_1` (전 문서 동일)
- 매크로 이름: 기존 `RCCAR_PIN_*` 유지
- 변형 표기: `nutcracker1.0`, `kingtiger1.1` (문서 통일)
