# rccar 보드별 핀 매핑 설계 문서

날짜: 2026-08-13
상태: 승인 (구현 대기)
기반: nutcracker-idf v1 (components/rccar)

문서 및 이 프로젝트에 대한 답변은 한글로 작성한다.

## 1. 목표

PCB 종류에 따라 `components/rccar`의 핀 매핑을 전환할 수 있게 한다.

- 현재 핀 매핑을 **nutcracker1.0** 변형으로 명명한다.
- 새로운 **kingtiger1.1** 변형을 추가한다.
- 기본 빌드 변형은 **kingtiger1.1**로 둔다.

두 변형은 서브시스템과 신호 종류가 동일하고 GPIO 번호만 다르다. 초기에는 kingtiger1.1의 GPIO 값을 nutcracker1.0과 동일하게 채워두고, 실측이 확정되면 kingtiger1.1 블록만 수정한다.

## 2. 접근 방식

Kconfig `choice`로 보드 변형을 선택하고, `rccar_pins.h`에서 선택된 매크로에 따라 GPIO를 정의한다.

- 신호 집합과 소비 코드 로직은 바뀌지 않는다.
- `RCCAR_PIN_*` 매크로 이름은 그대로 유지되므로 소비 모듈(motor/servo/shiftreg/led/dfplayer)은 변경하지 않는다.

## 3. 선택 메커니즘 (Kconfig)

`components/rccar/Kconfig` 신설.

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

- ESP-IDF는 컴포넌트 루트의 `Kconfig` 파일을 자동으로 읽어 "Component config" 메뉴에 노출한다.
- `idf.py menuconfig`로 전환하거나, `sdkconfig.defaults`에 값을 명시해 소스 수정 없이 전환한다.
- 기본값은 `RCCAR_BOARD_KINGTIGER_1_1`.

## 4. 핀 헤더 구조

`rccar_pins.h`를 보드별 조건부 블록으로 재편한다.

```c
#if defined(CONFIG_RCCAR_BOARD_KINGTIGER_1_1)

/* kingtiger1.1 (현재 nutcracker1.0과 동일 값) */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
/* ... 나머지 핀 ... */

#elif defined(CONFIG_RCCAR_BOARD_NUTCRACKER_1_0)

/* nutcracker1.0 */
#define RCCAR_PIN_FL_IN1 (GPIO_NUM_27)
/* ... 나머지 핀 ... */

#else
#error "RCCAR board variant not selected (CONFIG_RCCAR_BOARD_*)"
#endif
```

- 두 블록 모두 16개 `RCCAR_PIN_*` 와 `RCCAR_PIN_SOUND_RX = GPIO_NUM_NC` 를 전부 정의한다.
- 어느 변형도 선택되지 않으면 컴파일 오류로 조기 차단한다.

### 4.1 핀 표 (초기값, 두 변형 동일)

| 신호 | 매크로 | nutcracker1.0 | kingtiger1.1 |
|------|--------|--------------:|-------------:|
| FL IN1 | RCCAR_PIN_FL_IN1 | 27 | 27 |
| FL IN2 | RCCAR_PIN_FL_IN2 | 26 | 26 |
| FR IN1 | RCCAR_PIN_FR_IN1 | 25 | 25 |
| FR IN2 | RCCAR_PIN_FR_IN2 | 33 | 33 |
| RL IN1 | RCCAR_PIN_RL_IN1 | 32 | 32 |
| RL IN2 | RCCAR_PIN_RL_IN2 | 14 | 14 |
| RR IN1 | RCCAR_PIN_RR_IN1 | 13 | 13 |
| RR IN2 | RCCAR_PIN_RR_IN2 | 16 | 16 |
| 포탑 IN1 | RCCAR_PIN_TURRET_IN1 | 22 | 22 |
| 포탑 IN2 | RCCAR_PIN_TURRET_IN2 | 21 | 21 |
| 레이더 서보 | RCCAR_PIN_RADAR_SERVO | 17 | 17 |
| 웜 화이트 | RCCAR_PIN_WARM_WHITE | 4 | 4 |
| DFPlayer TX | RCCAR_PIN_SOUND_TX | 5 | 5 |
| DFPlayer RX | RCCAR_PIN_SOUND_RX | NC | NC |
| 595 DATA | RCCAR_PIN_595_DATA | 23 | 23 |
| 595 CLOCK | RCCAR_PIN_595_CLOCK | 18 | 18 |
| 595 LATCH | RCCAR_PIN_595_LATCH | 19 | 19 |

kingtiger1.1 실측이 확정되면 이 표의 kingtiger 컬럼과 헤더의 kingtiger 블록만 갱신한다.

## 5. 기본 빌드 설정

`sdkconfig.defaults`에 다음을 추가한다.

```
CONFIG_RCCAR_BOARD_KINGTIGER_1_1=y
```

Kconfig `default`만으로도 kingtiger1.1이 선택되지만, 기본 빌드 의도를 명시적으로 남긴다.

## 6. 문서 갱신

- `README.md`: 핀 맵 절을 두 변형(nutcracker1.0 / kingtiger1.1) 컬럼으로 갱신하고, `menuconfig` 또는 `sdkconfig.defaults`로 변형을 선택하는 방법을 안내한다. 기본값이 kingtiger1.1임을 표기한다.
- `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`: 3.1 핀 맵 절이 이제 보드 변형별로 갈린다는 점을 짧게 참조로 남긴다 (본 문서로 연결).
- `AGENTS.md`: "핀 변경 시 rccar_pins.h 와 README/설계를 함께 맞출 것" 지침에, 두 변형 컬럼을 함께 유지하라는 규칙을 추가한다.

## 7. 범위 밖 (YAGNI)

- 신호 집합/서브시스템 변경 없음.
- 두 변형의 GPIO가 현재 동일하므로 핀 값 외 조건부 코드 로직 없음.
- 세 번째 이후 변형은 필요 시 같은 패턴으로 추가한다 (지금 만들지 않음).

## 8. 성공 기준

1. `idf.py set-target esp32` 후 기본 설정에서 kingtiger1.1 변형으로 빌드된다.
2. `menuconfig`에서 nutcracker1.0으로 전환 후에도 빌드된다.
3. 어느 변형도 선택되지 않으면 컴파일 단계에서 명확한 `#error`로 실패한다.
4. 소비 모듈(motor/servo/shiftreg/led/dfplayer)은 코드 변경 없이 그대로 빌드된다.
5. README/설계/AGENTS 문서가 두 변형과 선택 방법을 반영한다.

## 9. 결정 로그

| 항목 | 결정 |
|------|------|
| 선택 방식 | Kconfig choice (menuconfig / sdkconfig.defaults) |
| 변형 이름 | nutcracker1.0, kingtiger1.1 |
| 기본 변형 | kingtiger1.1 |
| 초기 kingtiger 값 | nutcracker1.0과 동일 (실측 확정 시 갱신) |
| 헤더 구조 | rccar_pins.h 내 #if/#elif/#error 블록 |
| 소비 코드 | 변경 없음 (매크로 이름 유지) |
