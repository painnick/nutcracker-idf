# nutcracker-idf 구현 계획

> **에이전트용:** 구현 시 REQUIRED SUB-SKILL: `superpowers:subagent-driven-development`(권장) 또는 `superpowers:executing-plans`로 태스크 단위 실행. 진행 추적은 체크박스(`- [ ]`) 사용.

**Goal:** panzer4-idf 기반으로 ESP32 + Bluepad32 메카넘 RC 카 펌웨어 v1을 빌드 가능한 상태로 만든다.

**Architecture:** panzer4의 Bluepad32/BTstack/Core1 입력 오프로드를 유지하고, 탱크 전용 `rctank`를 `rccar`로 교체한다. 주행은 홀로노믹 믹스 후 DRV8833×3(MCPWM), 레이더는 LEDC 연속 회전 서보, 효과 LED는 74HC595×4, 웜 화이트는 MOSFET GPIO 토글이다.

**Tech Stack:** ESP-IDF v5.5.x, C, FreeRTOS, MCPWM, LEDC, Bluepad32, BTstack, NVS, DFPlayer UART TX

**설계 문서:** `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`

## 전역 제약

- 문서/답변: 한글 (`AGENTS.md`)
- 타깃 칩: ESP32 classic (`idf.py set-target esp32`)
- 환경: 루트 `env.bat` (Espressif IDF `esp-idf-b29c58f93b4ca0f49cdfc4c3ef43b562`)
- DC 모터: 전부 DRV8833 + MCPWM (LEDC 사용 금지)
- 핀 맵: 설계 문서 3.1절 확정값 그대로 (`rccar_pins.h`)
- v1 제외: 주포/기관총 시퀀스, 포신 상하/반동 서보, 웜 화이트 PWM, 595 최종 연출
- ESP32 MCPWM: 그룹당 operator 최대 3 → 모터 5개이므로 **group 0에 3개 + group 1에 2개**
- 커밋 메시지: 변경 요지 명확 (한글/영문 가능)
- 엠 대시(`—`), 가운뎃점(`·`) 출력 금지 (하이픈/쉼표 사용)

---

## 파일 구조 (생성/역할)

| 경로 | 역할 |
|------|------|
| `CMakeLists.txt` | 프로젝트명 `nutcracker_idf` |
| `.gitignore` | panzer4와 동일 계열 |
| `sdkconfig.defaults` | Bluepad32 custom platform 등 |
| `main/main.c` | BTstack + uni 부트 |
| `main/my_platform.c` | 패드 매핑, Core1 입력 처리, 페일세이프 |
| `main/CMakeLists.txt` | main 등록, `rccar` require |
| `components/bluepad32/`, `btstack/`, `cmd_*` | panzer4 복사 (수정 최소) |
| `components/rccar/rccar_pins.h` | 핀 상수 |
| `components/rccar/rccar_drive.c/.h` | 홀로노믹 순수 계산 |
| `components/rccar/rccar_motor.c/.h` | MCPWM 4휠+포탑 |
| `components/rccar/rccar_servo.c/.h` | 레이더 CR 서보 |
| `components/rccar/rccar_shiftreg.c/.h` | 595×4 |
| `components/rccar/rccar_led.c/.h` | 웜 화이트 + 테스트 패턴 |
| `components/rccar/rccar_dfplayer.c/.h` | 사운드 |
| `components/rccar/rccar_storage.c/.h` | 볼륨 NVS |
| `components/rccar/rccar.c/.h` | 통합 init |
| `components/rccar/CMakeLists.txt` | 컴포넌트 등록 |
| `README.md` | 한글 사용법/핀맵/조작 |
| `host_tests/test_drive.c` (선택) | 홀로노믹 단위 검증용 호스트 스모크 |

복사 원본 루트: `C:\Users\painnick\Documents\Projects\panzer4-idf`  
대상: `C:\Users\painnick\Documents\Projects\nutcracker-idf`  
이미 존재: `env.bat`, `AGENTS.md`, `docs/`, `.git/`

---

### Task 1: panzer4 스캐폴드 복사 및 프로젝트 이름 정리

**Files:**
- Create/overwrite: `CMakeLists.txt`, `.gitignore`, `LICENSE`, `sdkconfig.defaults`, `dependencies.lock` (있으면)
- Create: `main/main.c`, `main/CMakeLists.txt` (임시로 panzer4 내용)
- Create: `components/bluepad32/`, `btstack/`, `cmd_nvs/`, `cmd_nvs_4.4/`, `cmd_system/`, `cmd_system_4.4/`
- Do not copy: `build/`, `cmake-build-*/`, `sdkconfig`, `sdkconfig.old`, `components/rctank/` (Task 2에서 새로 작성)
- Keep: `env.bat`, `AGENTS.md`, `docs/`

**Interfaces:**
- Produces: `idf.py set-target esp32` 후 configure 가능한 트리 (main은 아직 rctank 없이 컴파일 실패 가능 → Task 2에서 rccar로 연결)

- [ ] **Step 1: 제외 목록으로 컴포넌트/메인 복사**

PowerShell (프로젝트 루트 `nutcracker-idf`에서):

```powershell
$src = "C:\Users\painnick\Documents\Projects\panzer4-idf"
$dst = "C:\Users\painnick\Documents\Projects\nutcracker-idf"

Copy-Item "$src\.gitignore" $dst -Force
Copy-Item "$src\LICENSE" $dst -Force -ErrorAction SilentlyContinue
Copy-Item "$src\sdkconfig.defaults" $dst -Force
Copy-Item "$src\dependencies.lock" $dst -Force -ErrorAction SilentlyContinue
Copy-Item "$src\CMakeLists.txt" $dst -Force

New-Item -ItemType Directory -Force -Path "$dst\main" | Out-Null
Copy-Item "$src\main\main.c" "$dst\main\" -Force
Copy-Item "$src\main\CMakeLists.txt" "$dst\main\" -Force
# my_platform 은 Task 6에서 카용으로 새로 작성. 임시로 복사해 두되 rccar 미완성이면 이후 교체
Copy-Item "$src\main\my_flatform.c" "$dst\main\my_platform.c" -Force

foreach ($c in @("bluepad32","btstack","cmd_nvs","cmd_nvs_4.4","cmd_system","cmd_system_4.4")) {
  robocopy "$src\components\$c" "$dst\components\$c" /E /NFL /NDL /NJH /NJS /nc /ns /np
}
```

주의: panzer4 파일명은 `my_flatform.c`(오타)이다. nutcracker에서는 `my_platform.c`로 저장한다.

- [ ] **Step 2: 루트 CMake 프로젝트명 변경**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.13)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nutcracker_idf)
```

- [ ] **Step 3: main CMakeLists가 my_platform.c를 가리키게 수정**

`main/CMakeLists.txt`:

```cmake
set(srcs
        "main.c"
        "my_platform.c")

set(requires "bluepad32" "btstack" "rccar")

idf_component_register(SRCS "${srcs}"
        INCLUDE_DIRS "."
        REQUIRES "${requires}")
```

이 시점에는 `rccar`가 없어 빌드 실패가 정상이다. Task 2에서 최소 스텁으로 통과시킨다.

- [ ] **Step 4: 커밋**

```bash
git add -A
git commit -m "chore: panzer4 기반 스캐폴드 복사 및 프로젝트명 정리"
```

---

### Task 2: `rccar` 스텁 + 핀 헤더 + 빌드 통과

**Files:**
- Create: `components/rccar/CMakeLists.txt`
- Create: `components/rccar/rccar_pins.h`
- Create: `components/rccar/rccar.h`, `rccar.c`
- Create: 나머지 헤더/소스 스텁 (빈 init 반환 ESP_OK)
- Modify: `main/my_platform.c` → 탱크 로직 제거한 최소 플랫폼 (연결 로그만, 모터 호출 없음) 또는 컴파일만 되게 include 교체

**Interfaces:**
- Produces:
  - `esp_err_t rccar_init(void);`
  - 핀 매크로 `RCCAR_PIN_*` (설계 3.1과 동일)

- [ ] **Step 1: `rccar_pins.h` 작성**

```c
/**
 * @file rccar_pins.h
 * @brief 핀 정의 (설계 문서 3.1)
 */
#ifndef RCCAR_PINS_H
#define RCCAR_PINS_H

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_PINS_H */
```

- [ ] **Step 2: 최소 모듈 스텁**

각 `*_init`는 `return ESP_OK;`, setter는 no-op.

`rccar.h` / `rccar.c`:

```c
/* rccar.h */
#pragma once
#include "esp_err.h"
esp_err_t rccar_init(void);

/* rccar.c */
#include "rccar.h"
#include "esp_log.h"
static const char *TAG = "rccar";
esp_err_t rccar_init(void) {
    ESP_LOGI(TAG, "rccar_init stub");
    return ESP_OK;
}
```

`CMakeLists.txt`:

```cmake
set(srcs
    rccar.c
    rccar_drive.c
    rccar_motor.c
    rccar_servo.c
    rccar_shiftreg.c
    rccar_led.c
    rccar_dfplayer.c
    rccar_storage.c
)
idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer log freertos)
```

빈 `.c` 파일도 모두 생성 (링커 오류 방지).

- [ ] **Step 3: 최소 `my_platform.c`**

panzer4 플랫폼 골격만 남기고 `rctank_*` 호출을 `rccar_init` 한 번으로 대체. 게임패드 데이터 콜백에서는 로그만:

```c
// on_controller_data: 입력 큐 없이 로그만 (Task 6에서 본구현)
ESP_LOGD("plat", "axis y=%d ry=%d", gp->axis_y, gp->axis_ry);
```

`get_my_platform` / `uni_platform` 테이블은 panzer4 `my_flatform.c` 하단 구조를 그대로 복제하되 include를 `rccar.h`로 바꾼다.  
`main.c`의 `get_my_platform` 선언과 심볼명이 일치해야 한다.

- [ ] **Step 4: 빌드**

```bat
env.bat
idf.py set-target esp32
idf.py build
```

Expected: 빌드 성공 (`nutcracker_idf.bin` 생성).

- [ ] **Step 5: 커밋**

```bash
git add components/rccar main CMakeLists.txt sdkconfig.defaults .gitignore
git commit -m "feat: rccar 스텁과 핀맵으로 최초 빌드 통과"
```

---

### Task 3: 홀로노믹 믹스 (`rccar_drive`)

**Files:**
- Create/Modify: `components/rccar/rccar_drive.h`, `rccar_drive.c`
- Optional host check: `host_tests/test_drive_main.c` (MSVC/gcc로 단독 컴파일 가능하면)

**Interfaces:**
- Produces:

```c
typedef struct {
    int32_t fl, fr, rl, rr; /* -512 .. 511 */
} rccar_wheel_speeds_t;

/** vx,vy,w: 데드존 적용 후 값, 범위 -512..511 */
void rccar_drive_mix(int32_t vx, int32_t vy, int32_t w, rccar_wheel_speeds_t *out);

/** abs가 deadzone 이하면 0, 아니면 그대로 (부호 유지) */
int32_t rccar_drive_apply_deadzone(int32_t v, int32_t deadzone);
```

- [ ] **Step 1: API 및 구현**

알고리즘 (설계 4.2):

```c
void rccar_drive_mix(int32_t vx, int32_t vy, int32_t w, rccar_wheel_speeds_t *out)
{
    int32_t fl = vx + vy + w;
    int32_t fr = vx - vy - w;
    int32_t rl = vx - vy + w;
    int32_t rr = vx + vy - w;

    int32_t m = abs(fl);
    if (abs(fr) > m) m = abs(fr);
    if (abs(rl) > m) m = abs(rl);
    if (abs(rr) > m) m = abs(rr);

    if (m > 512) {
        fl = (fl * 512) / m;
        fr = (fr * 512) / m;
        rl = (rl * 512) / m;
        rr = (rr * 512) / m;
    }
    out->fl = fl;
    out->fr = fr;
    out->rl = rl;
    out->rr = rr;
}
```

`stdlib.h`의 `abs` 사용 또는 로컬 `iabs`.

- [ ] **Step 2: 수동 검증 케이스 (구현자 체크리스트)**

| 입력 (vx,vy,w) | 기대 |
|----------------|------|
| (512,0,0) | 전 휠 +512 (전진) |
| (0,512,0) | FL/RR + , FR/RL - (스트레이프, 부호는 배선에 맞게 문서화) |
| (0,0,512) | 요: FL/RL 한 방향, FR/RR 반대 |
| (512,512,0) 스케일 후 | 어떤 휠도 abs > 512 아님 |

보드 없이 검증: 임시로 `rccar_drive_mix` 호출 후 `ESP_LOGI`로 네 값 출력하는 단위 호출을 `rccar_init`에 넣고 빌드/실행 한 뒤 제거해도 된다.

- [ ] **Step 3: 커밋**

```bash
git add components/rccar/rccar_drive.c components/rccar/rccar_drive.h
git commit -m "feat: 메카넘 홀로노믹 믹스 rccar_drive"
```

---

### Task 4: MCPWM 모터 (`rccar_motor`)

**Files:**
- Modify: `components/rccar/rccar_motor.c`, `rccar_motor.h`

**Interfaces:**
- Consumes: `rccar_pins.h`
- Produces:

```c
esp_err_t rccar_motor_init(void);
void rccar_motor_wheel_set(int fl, int fr, int rl, int rr); /* -512..511, 램프 타깃 */
void rccar_motor_wheel_set_immediate(int fl, int fr, int rl, int rr);
void rccar_motor_turret_set(int speed); /* -512..511 */
void rccar_motor_all_stop(void); /* immediate 0 + turret 0 */
```

- [ ] **Step 1: panzer4 `rctank_motor.c`를 확장해 5채널 구성**

- 타이머/operator:
  - MCPWM group 0: FL, FR, RL (operators 3)
  - MCPWM group 1: RR, TURRET (operators 2)
- 각 모터: comparator A/B + generator A/B, `set_motor_duty` 로직 panzer4와 동일 (양수 IN1 PWM, 음수 IN2 PWM)
- 주파수: 20 kHz, resolution 1 MHz
- 4휠만 10ms 램프 태스크 (가속/감속 스텝은 panzer4 `TRACK_ACCEL_STEP` / `TRACK_DECEL_STEP` 재사용 가능, 휠별 current/target 4쌍)
- 포탑은 즉시 듀티 (램프 없음), 속도 기본 풀스케일 호출은 플랫폼에서 511

핀 연결 예:

```c
/* group0 */
FL: RCCAR_PIN_FL_IN1/IN2
FR: RCCAR_PIN_FR_IN1/IN2
RL: RCCAR_PIN_RL_IN1/IN2
/* group1 */
RR: RCCAR_PIN_RR_IN1/IN2
TURRET: RCCAR_PIN_TURRET_IN1/IN2
```

- [ ] **Step 2: `rccar_init`에서 `rccar_motor_init` 호출**

- [ ] **Step 3: 빌드**

```bat
idf.py build
```

Expected: 성공. 하드웨어 있으면 스틱 연결 전 `rccar_motor_all_stop` 상태 확인.

- [ ] **Step 4: 커밋**

```bash
git add components/rccar/rccar_motor.c components/rccar/rccar_motor.h components/rccar/rccar.c
git commit -m "feat: MCPWM 4휠+포탑 모터 드라이버"
```

---

### Task 5: 레이더 서보 + 웜 화이트 + 595

**Files:**
- Modify: `rccar_servo.c/.h`, `rccar_shiftreg.c/.h`, `rccar_led.c/.h`, `rccar.c`

**Interfaces:**
- Produces:

```c
/* servo */
esp_err_t rccar_servo_init(void);
void rccar_radar_set_armed(bool armed); /* true: 회전 펄스, false: 정지(중립 ~1.5ms) */

/* shiftreg */
esp_err_t rccar_shiftreg_init(void);
void rccar_shiftreg_write32(uint32_t bits); /* bit0 = 체인 끝 LED 등 문서화 */

/* led */
esp_err_t rccar_led_init(void);
void rccar_led_warm_white_set(bool on);
void rccar_led_warm_white_toggle(void);
void rccar_led_test_chase_step(void); /* 테스트용 1스텝 */
```

- [ ] **Step 1: CR 서보 (LEDC)**

- 50 Hz, duty 단위 us 환산 (panzer4 서보 초기화 참고)
- 정지: 1500 us 근처
- 회전: 예) 1700 us 또는 1300 us (실기 보정 상수 `RCCAR_RADAR_SPIN_US`, `RCCAR_RADAR_STOP_US`)
- 핀: `RCCAR_PIN_RADAR_SERVO`

- [ ] **Step 2: 74HC595 비트뱅**

순서 (MSB first, 4칩 = 32비트):

```c
void rccar_shiftreg_write32(uint32_t bits)
{
    for (int i = 31; i >= 0; --i) {
        gpio_set_level(RCCAR_PIN_595_DATA, (bits >> i) & 1);
        gpio_set_level(RCCAR_PIN_595_CLOCK, 1);
        gpio_set_level(RCCAR_PIN_595_CLOCK, 0);
    }
    gpio_set_level(RCCAR_PIN_595_LATCH, 1);
    gpio_set_level(RCCAR_PIN_595_LATCH, 0);
}
```

init 시 DATA/CLOCK/LATCH 출력, 초기 `write32(0)`.

- [ ] **Step 3: 웜 화이트**

```c
gpio_set_direction(RCCAR_PIN_WARM_WHITE, GPIO_MODE_OUTPUT);
gpio_set_level(RCCAR_PIN_WARM_WHITE, 0);
```

토글은 정적 `bool` 상태 유지.

- [ ] **Step 4: `rccar_init` 순서**

```c
storage_init (Task 5.5 전이면 skip)
motor_init
servo_init
shiftreg_init
led_init
dfplayer (다음 태스크)
```

- [ ] **Step 5: 빌드 및 커밋**

```bash
idf.py build
git add components/rccar/
git commit -m "feat: 레이더 서보, 595, 웜 화이트 LED"
```

---

### Task 6: DFPlayer + NVS 볼륨

**Files:**
- Modify: `rccar_dfplayer.c/.h`, `rccar_storage.c/.h`, `rccar.c`

**Interfaces:**
- panzer4 API 이름을 `rccar_` 접두로 복사:

```c
#define RCCAR_DFPLAYER_TRACK_IDLE    1
#define RCCAR_DFPLAYER_TRACK_CONNECT 4
/* GUN/MG 트랙 상수는 남겨도 v1 플랫폼에서 미사용 */

esp_err_t rccar_dfplayer_init(void);
esp_err_t rccar_dfplayer_play(uint8_t track);
esp_err_t rccar_dfplayer_set_volume(uint8_t vol);

esp_err_t rccar_storage_init(void);
uint8_t rccar_storage_volume_get(void);
esp_err_t rccar_storage_volume_set(uint8_t vol);
void rccar_storage_erase_and_restart(void);

#define RCCAR_VOLUME_MIN 10
#define RCCAR_VOLUME_MAX 30
#define RCCAR_VOLUME_DEFAULT 20
```

- [ ] **Step 1: panzer4 `rctank_dfplayer.c` / `rctank_storage.c` 복사 후 리네임**

- UART TX 핀: `RCCAR_PIN_SOUND_TX` (GPIO 5)
- NVS 네임스페이스: `"rccar"` 권장 (panzer4 키와 충돌 방지)

- [ ] **Step 2: `rccar_init`**

```c
ESP_RETURN_ON_ERROR(rccar_storage_init(), TAG, "storage");
/* ... motors leds ... */
ESP_RETURN_ON_ERROR(rccar_dfplayer_init(), TAG, "dfplayer");
ESP_ERROR_CHECK(rccar_dfplayer_set_volume(rccar_storage_volume_get()));
```

- [ ] **Step 3: 빌드 + 커밋**

```bash
idf.py build
git commit -am "feat: DFPlayer 및 볼륨 NVS"
```

---

### Task 7: 플랫폼 매핑 + 페일세이프 (본게임 로직)

**Files:**
- Rewrite: `main/my_platform.c`
- Modify: `rccar.h` if needed

**Interfaces:**
- Consumes: drive/motor/servo/led/dfplayer/storage 위 API
- Produces: Bluepad32 custom platform 완전 동작

- [ ] **Step 1: 입력 이벤트 구조**

```c
typedef struct {
    int32_t axis_x;   /* 좌 스틱 X = vy */
    int32_t axis_y;   /* 좌 스틱 Y = vx */
    int32_t axis_rx;  /* 우 스틱 X = w */
    uint16_t dpad;
    uint16_t buttons;
    uint8_t misc_buttons;
    uni_hid_device_t *device;
    int64_t timestamp_ms;
} input_event_t;
```

- 큐 길이 1, Core1 태스크 (panzer4와 동일 패턴)
- `on_controller_data`에서 최신 샘플 overwrite

- [ ] **Step 2: 처리 루프**

```c
#define AXIS_MAX 512
#define AXIS_DEADZONE 60
#define FAILSAFE_MS 1000
#define TURRET_SPEED 511

// 1) 타임스탬프가 FAILSAFE_MS 초과면 motor_all_stop, radar disarmed, return
// 2) vx = -apply_deadzone(axis_y) 등 스틱 방향은 실차에 맞게 한 곳 상수로
// 3) rccar_drive_mix(vx, vy, w, &wheels)
// 4) rccar_motor_wheel_set(wheels.fl, ...)
// 5) dpad left/right -> turret +/- TURRET_SPEED else 0
// 6) BUTTON_Y edge -> rccar_led_warm_white_toggle
// 7) BUTTON_X edge -> radar armed toggle
// 8) SHOULDER_L/R -> volume -/+ , storage_set + dfplayer_set_volume
// 9) 연결 시 CONNECT 트랙, 대기 idle 타이머는 panzer4 단순화 버전 가능
```

버튼 상수는 Bluepad32 `BUTTON_A` 등이 아니라 해당 헤더의 Y/X/L/R 매크로를 panzer4 `my_flatform.c`에서 확인 후 동일 사용.

- [ ] **Step 3: 연결/해제 콜백**

- connect: seat 할당, rumble 가능 시 짧게, `rccar_dfplayer_play(CONNECT)`, 모터 정지 확인
- disconnect: `rccar_motor_all_stop()`, `rccar_radar_set_armed(false)`

- [ ] **Step 4: 빌드**

```bat
idf.py build
```

실기: `idf.py flash monitor` 후 패드 연결, 로그로 축 값 확인.

- [ ] **Step 5: 커밋**

```bash
git add main/my_platform.c
git commit -m "feat: 메카넘/포탑/레이더/LED 게임패드 매핑과 페일세이프"
```

---

### Task 8: README 및 정리

**Files:**
- Create: `README.md` (한글)
- Update: `AGENTS.md` (빌드 한 줄, 구조 요약 보강)
- Update: 설계 문서 상태를 `승인/구현 중`으로 변경해도 됨

- [ ] **Step 1: README 필수 절**

- 개요 (메카넘 RC 카)
- 기능 표
- 핀 맵 표 (설계 3.1 동일)
- 조작법 표
- 빌드: `env.bat` → `idf.py set-target esp32` → `build` / `flash monitor`
- 하드웨어 주의 (DRV8833 3개, 595 OE/MR 배선)

- [ ] **Step 2: 최종 빌드 스모크**

```bat
idf.py build
```

Expected: 에러 0.

- [ ] **Step 3: 커밋**

```bash
git add README.md AGENTS.md docs/
git commit -m "docs: README 및 프로젝트 가이드 정리"
```

---

## 스펙 커버리지 자가 점검

| 설계 요구 | 태스크 |
|-----------|--------|
| 메카넘 홀로노믹 | Task 3, 7 |
| DRV8833+MCPWM 4휠+포탑 | Task 4 |
| 레이더 CR 서보 무장 회전 | Task 5, 7 |
| 595×4 | Task 5 |
| 웜 화이트 MOSFET Y 토글 | Task 5, 7 |
| DFPlayer + 볼륨 NVS | Task 6, 7 |
| 페일세이프 | Task 7 |
| 핀 맵 3.1 | Task 2 |
| env.bat / ESP32 | Task 1-2, 전역 제약 |
| v1 제외 항목 미구현 | 전 태스크 YAGNI |
| 한글 문서 | Task 8, AGENTS |

## 플레이스홀더 검사

TBD/TODO 단계 없음. 모터 group 분할, API 시그니처, 핀 숫자 명시.

## 타입/이름 일관성

- 접두사 전부 `rccar_`
- 휠 순서 `fl, fr, rl, rr`
- 플랫폼 파일명 `my_platform.c` (panzer4 오타 flatform 폐기)

---

## 실행 인수인계

플랜 저장 위치: `docs/superpowers/plans/2026-07-31-nutcracker-idf.md`

**실행 방식 선택:**

1. **Subagent-Driven (권장)** - 태스크마다 새 서브에이전트, 태스크 사이 리뷰  
2. **Inline Execution** - 이 세션에서 순서대로 실행, 체크포인트 리뷰  

어느 쪽으로 진행할지 알려 주세요.
