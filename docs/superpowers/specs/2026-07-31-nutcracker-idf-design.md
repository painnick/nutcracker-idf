# nutcracker-idf 설계 문서

날짜: 2026-07-31  
상태: 구현 완료 (v1 펌웨어)  
기반: panzer4-idf (ESP-IDF + Bluepad32)

문서 및 이 프로젝트에 대한 답변은 한글로 작성한다.

## 1. 목표

**ESP32 classic** 위에서 Bluepad32 게임패드로 조작하는 **메카넘 휠 RC 카**(탱크 아님) 펌웨어를 만든다.

v1 포함:

- 홀로노믹 주행 (DC 모터 4개)
- 포탑 회전 (DC 모터 1개)
- 연속 회전 레이더 서보 (무장 시 회전)
- 표시/효과용 LED 다수 (74HC595 4개 데이지체인)
- 대전류 **웜 화이트** LED 1채널 (MOSFET, 온/오프)
- DFPlayer 사운드, 볼륨 NVS 저장, 패드 진동
- 연결 해제 / 리포트 타임아웃 시 페일세이프

v1 제외:

- panzer4의 주포/기관총 발사 시퀀스
- 포신 상하/반동 서보
- 웜 화이트 PWM 디밍
- 595 LED 최종 연출 패턴 (드라이버 + 테스트 패턴만)
- PCB 실크 최종 다듬기

## 2. 접근 방식

**panzer4-idf를 포크한 뒤 모듈을 재작성** (방식 A).

Bluepad32, BTstack, 콘솔 헬퍼, env 패턴, 플랫폼 구조를 복사한다. 탱크 전용 주행/LED 처리는 `components/rccar/` 아래 카 전용 모듈로 교체한다.

## 3. 하드웨어

| 하위 시스템 | 하드웨어 | 제어 |
|-------------|----------|------|
| 주행 | DRV8833 2개 (하프브리지 4) | MCPWM |
| 포탑 | DRV8833 3번째 칩 | MCPWM |
| 레이더 | 연속 회전 서보 | LEDC |
| 효과 LED | 74HC595 4개 데이지체인 (32채널) | 비트뱅 또는 SPI 스타일 |
| 웜 화이트 | MOSFET 로우사이드(또는 하이사이드) 1채널 | GPIO 온/오프 |
| 사운드 | DFPlayer Mini, TX 전용 | UART TX |
| 입력 | BLE/Classic HID 게임패드 | Bluepad32 + BTstack |

모든 **DC 모터**는 **DRV8833 + MCPWM** (panzer4와 동일 패턴). 서보는 LEDC만 사용. 웜 화이트는 595 체인에 올리지 않는다.

### 3.1 핀 맵 (확정, 물리 배치 우선)

모터 출력에 스트래핑 위험 핀(0, 2, 12, 15) 사용 안 함. 플래시(6-11), UART0 콘솔(1, 3), 입력 전용(34-39)은 출력에 사용하지 않음.

| 기능 | GPIO | 블록 |
|------|------|------|
| FL IN1 | 27 | DRV8833 #1 전륜 |
| FL IN2 | 26 | DRV8833 #1 전륜 |
| FR IN1 | 25 | DRV8833 #1 전륜 |
| FR IN2 | 33 | DRV8833 #1 전륜 |
| RL IN1 | 32 | DRV8833 #2 후륜 |
| RL IN2 | 14 | DRV8833 #2 후륜 |
| RR IN1 | 13 | DRV8833 #2 후륜 |
| RR IN2 | 16 | DRV8833 #2 후륜 |
| 포탑 IN1 | 22 | DRV8833 #3 |
| 포탑 IN2 | 21 | DRV8833 #3 |
| 레이더 서보 | 17 | LEDC |
| 웜 화이트 MOSFET 게이트 | 4 | Y 토글 |
| DFPlayer TX | 5 | UART TX |
| 595 DATA (SER) | 23 | 시프트 LED |
| 595 CLOCK (SRCLK) | 18 | 시프트 LED |
| 595 LATCH (RCLK) | 19 | 시프트 LED |

595 v1: OE는 GND(항상 enable), MR은 VCC(하드웨어 클리어 없음). OE/SRCLR용 GPIO는 이후 선택.

### 3.2 배치 의도

- 전륜 모터 4선, 후륜 모터 4선을 각각 커넥터 그룹으로 묶기 쉽게 함
- 포탑 2선은 인접 핀(21/22)
- 595 3선 버스는 18/19/23
- 저전류 제어(웜 화이트, DFPlayer, 레이더)는 4/5/17로 모터 묶음과 분리

## 4. 소프트웨어 구조

```
nutcracker-idf/
  env.bat                    # ESP-IDF 환경 (추가됨)
  main/
    main.c                   # BTstack + Bluepad32 부트
    my_platform.c            # 패드 이벤트, Core1 워커, 매핑
  components/
    bluepad32/ btstack/ ...  # panzer4에서 복사
    rccar/
      rccar.c / rccar.h      # 초기화 오케스트레이션
      rccar_pins.h           # 위 핀 표
      rccar_drive.c          # 홀로노믹 믹스
      rccar_motor.c          # MCPWM: 휠 4 + 포탑
      rccar_servo.c          # 연속 회전 레이더
      rccar_shiftreg.c       # 595 체인
      rccar_led.c            # 웜 화이트 + LED 모드
      rccar_dfplayer.c
      rccar_storage.c        # 볼륨 NVS
```

### 4.1 입력 경로 (panzer4 계승)

- `my_platform_on_controller_data` (BT 경로, Core 0)가 샘플을 큐에 넣음
- Core 1의 `input_process_task`가 주행, 포탑, 버튼, 페일세이프 적용
- 모터 목표는 volatile/공유 타깃; 램프는 panzer4와 같이 모터 태스크 또는 처리 루프에서 수행

### 4.2 홀로노믹 믹스

데드존 적용 후 공통 범위(예: +/-512)로 스케일:

- `vx` = 좌 스틱 Y (전후)
- `vy` = 좌 스틱 X (좌우 평행이동)
- `w`  = 우 스틱 X (요 회전)

휠 명령:

- `FL = vx + vy + w`
- `FR = vx - vy - w`
- `RL = vx - vy + w`
- `RR = vx + vy - w`

이후 `max(|wheel|)`이 풀 듀티를 넘지 않게 스케일하고, 필요 시 소프트 램프 / 최소 기동 속도 적용 (panzer4 트랙 아이디어를 휠 단위로 적용).

### 4.3 게임패드 맵 (v1)

| 입력 | 기능 |
|------|------|
| 좌 스틱 X/Y | 평행이동 / 전후 |
| 우 스틱 X | 요 회전 |
| D-Pad 좌/우 | 포탑 DC |
| Y | 웜 화이트 MOSFET 토글 |
| X | 레이더 무장/해제 (고정 속도 회전) |
| L1 / R1 | 볼륨 감소 / 증가 (NVS) |
| 연결 해제 또는 약 1초 리포트 없음 | 페일세이프: 전 모터 0, 레이더 정지 |

### 4.4 모듈 책임

| 모듈 | 책임 |
|------|------|
| `rccar_motor` | MCPWM 초기화 및 FL/FR/RL/RR + 포탑 듀티 설정 |
| `rccar_drive` | 스틱 값 -> 4휠 속도 |
| `rccar_servo` | LEDC 연속 회전 레이더: 무장 시 회전 / 해제 시 정지(중립 펄스) |
| `rccar_shiftreg` | 32비트 패턴 시프트 및 래치 |
| `rccar_led` | 웜 화이트 GPIO; 시프트레지스터를 쓰는 상위 LED 모드 |
| `rccar_dfplayer` | UART TX 명령, 트랙 ID는 추후 확정 |
| `rccar_storage` | NVS 볼륨 |
| `rccar` | 순서 있는 초기화: NVS -> 핀 드라이버 -> 사운드 |

## 5. 도구 / 환경

- **ESP-IDF:** v5.5.x (panzer4와 동일 설치)
- **env.bat:** 프로젝트 루트. 다음을 실행:  
  `C:\Espressif\idf_cmd_init.bat esp-idf-b29c58f93b4ca0f49cdfc4c3ef43b562`
- 빌드: env 실행 후 `idf.py set-target esp32`, `idf.py build`

## 6. 성공 기준

1. 프로젝트 `env.bat` IDF 환경에서 타깃 `esp32`로 빌드된다.
2. Bluepad32로 게임패드가 연결되고, 페일세이프가 주행/포탑/레이더를 정지시킨다.
3. 홀로노믹 매핑이 MCPWM 4채널을 구동하고, 포탑은 D-Pad, 레이더는 X, 웜 화이트는 Y, 볼륨은 L1/R1 + NVS로 동작한다.
4. 595 드라이버가 32비트 테스트 패턴(예: 체이싱 또는 전체 점등)을 래치할 수 있다.

## 7. 구현 순서 (미리보기)

1. panzer4에서 스캐폴드 (gitignore, CMake, bluepad32/btstack, sdkconfig.defaults, main 스텁)
2. 이 문서 핀 표와 맞는 `rccar` 도입
3. MCPWM 4휠 + 포탑, 홀로노믹 믹스
4. 레이더 CR 서보 + 웜 화이트 GPIO + 595 드라이버
5. 플랫폼 매핑, 페일세이프, DFPlayer/볼륨/NVS
6. 카용 README 및 Agent 메모 (탱크 문서가 아님)

## 8. 결정 로그

| 항목 | 결정 |
|------|------|
| MCU | ESP32 classic (panzer4) |
| 기반 프로젝트 | panzer4-idf |
| 주행 | 메카넘, 홀로노믹 좌 스틱 XY + 우 스틱 X 요 |
| 모터 드라이버 | DRV8833 3개, DC 전부 MCPWM |
| 레이더 | 연속 회전 서보, X로 무장 시 회전, 고정 속도 |
| LED | 74HC595 4개 데이지체인 |
| 웜 화이트 | MOSFET 단일 채널, Y 온/오프 |
| 사운드 v1 | DFPlayer + 볼륨 NVS + 진동 + 페일세이프 |
| 핀 | 3.1절 표로 확정 |
| env.bat | 사용자가 프로젝트 루트에 추가 |
| 문서/답변 언어 | 한글 |
