# nutcracker-idf 설계 문서

날짜: 2026-07-31  
갱신: 2026-09-19  
상태: 현행 펌웨어와 일치 (`rccar_pins.h`, `README.md`)  
기반: panzer4-idf (ESP-IDF + Bluepad32)

문서 및 이 프로젝트에 대한 답변은 한글로 작성한다.

## 1. 목표

**ESP32 classic (WROOM)** 위에서 Bluepad32 게임패드로 조작하는 **메카넘 휠 RC 카** 펌웨어.

포함:

- 홀로노믹 주행 (DC 모터 4개)
- 포탑 회전 (DC 모터 1개, 최대의 75%)
- 위치형 레이더 서보 (Y 토글, 0°↔180° 왕복)
- 가습기 GPIO, 네오픽셀 8개, 레이저/개틀링/헤드라이트 LED
- DFPlayer 사운드, 볼륨 NVS 저장, 패드 진동
- 연결 해제 / 리포트 타임아웃 시 페일세이프

제외:

- panzer4의 주포/기관총 발사 시퀀스
- 포신 상하/반동 서보
- 74HC595 시프트 LED (채택하지 않음)
- 연속 회전 레이더 서보 (채택하지 않음, 위치형으로 구현)
- 보드 변형 Kconfig (`2026-08-13` 스펙은 미구현, 단일 핀 맵)

## 2. 접근 방식

**panzer4-idf를 포크한 뒤 모듈을 재작성**.

Bluepad32, BTstack, 콘솔 헬퍼, env 패턴, 플랫폼 구조를 유지한다. 탱크 전용 주행/LED는 `components/rccar/` 아래 카 전용 모듈로 교체한다.

## 3. 하드웨어

| 하위 시스템 | 하드웨어 | 제어 |
|-------------|----------|------|
| 주행 | DRV8833 2개 (하프브리지 4) | MCPWM |
| 포탑 | DRV8833 3번째 칩 | MCPWM |
| 레이더 | 위치형 서보 (0°~180°) | LEDC 50 Hz 14-bit |
| 가습기 | MOSFET/릴레이 (HIGH=ON) | GPIO |
| 네오픽셀 | WS2812 x8 | RMT |
| 레이저 | MOSFET (LOW=ON) | GPIO |
| 개틀링 LED | GPIO12 HIGH=ON | GPIO |
| 헤드라이트 | GPIO14 HIGH=ON | GPIO |
| 사운드 | DFPlayer Mini, TX 전용 | UART TX |
| 입력 | BLE/Classic HID 게임패드 | Bluepad32 + BTstack |

모든 **DC 모터**는 **DRV8833 + MCPWM**. 서보만 LEDC. ESP32 MCPWM은 그룹당 operator 3이므로 휠 4 + 포탑 1은 group 0(FL/FR/RL) + group 1(RR/TURRET)로 나눈다.

### 3.1 핀 맵

정의: `components/rccar/rccar_pins.h`. README 핀 표와 동일하다.

모터 출력에 스트래핑 위험 핀(0, 2)을 쓰지 않는다. GPIO12는 개틀링 LED만 쓰고, 리셋 시 LOW를 유지한다 (MTDI). 플래시(6-11), UART0 콘솔(1, 3), 입력 전용(34-39)은 출력에 사용하지 않는다. GPIO16/17 포탑은 WROOM 전용이며 WROVER PSRAM과 겹친다.

| 기능 | GPIO | 블록 |
|------|------|------|
| FL IN1 | 22 | DRV8833 #1 전륜 좌 |
| FL IN2 | 21 | DRV8833 #1 전륜 좌 |
| FR IN1 | 18 | DRV8833 #1 전륜 우 |
| FR IN2 | 19 | DRV8833 #1 전륜 우 |
| RL IN1 | 25 | DRV8833 #2 후륜 좌 |
| RL IN2 | 33 | DRV8833 #2 후륜 좌 |
| RR IN1 | 26 | DRV8833 #2 후륜 우 |
| RR IN2 | 27 | DRV8833 #2 후륜 우 |
| 포탑 IN1 | 16 | DRV8833 #3 |
| 포탑 IN2 | 17 | DRV8833 #3 |
| 가습기 MOSFET/릴레이 | 4 | HIGH=ON |
| 네오픽셀 DATA | 13 | WS2812 x8 |
| 레이저 LED | 15 | LOW=ON |
| 개틀링 LED | 12 | HIGH=ON |
| 헤드라이트 LED | 14 | HIGH=ON |
| 레이더 서보 | 32 | LEDC 50 Hz 14-bit |
| DFPlayer TX | 23 | UART TX |
| DFPlayer RX | NC | 미사용 |

### 3.2 배치 의도

- 전륜/후륜 모터 4선을 커넥터 그룹으로 묶기 쉽게 함
- 포탑 2선은 인접 핀(16/17)
- DFPlayer는 TX만 배선. 트랙 종료(0x3D)는 받지 않음

## 4. 소프트웨어 구조

```
nutcracker-idf/
  env.bat                    # ESP-IDF 환경
  main/
    main.c                   # BTstack + Bluepad32 부트
    my_platform.c            # 패드 이벤트, Core1 워커, 매핑, 페일세이프
  components/
    bluepad32/ btstack/ ...
    rccar/
      rccar.c / rccar.h      # 초기화: storage → motor → humidifier → neopixel → laser → headlight → radar
      rccar_pins.h           # 3.1 핀 표
      rccar_drive.c          # 홀로노믹 믹스, 대각선 스냅, idle→move
      rccar_motor.c          # MCPWM 휠 4 + 포탑, 휠 테스트
      rccar_humidifier.c     # 가습기 GPIO
      rccar_neopixel.c       # WS2812 엔진/미스트
      rccar_laser.c          # 레이저/개틀링 LED
      rccar_headlight.c      # 헤드라이트
      rccar_radar.c          # 레이더 서보
      rccar_dfplayer.c       # UART TX 사운드
      rccar_storage.c        # NVS 볼륨
```

DFPlayer는 `uni_init()` 밖(`on_init_complete`)에서 따로 초기화한다.

### 4.1 입력 경로

- `my_platform_on_controller_data` (Core 0, btstack)가 샘플을 큐에 넣음
- Core 1의 `input_process_task`가 주행, 포탑, 버튼, 페일세이프 적용
- Core1에서 btstack/Bluepad32 API(`d->report_parser.*` 포함)를 직접 호출하지 않음. 럼블은 `btstack_run_loop_execute_on_main_thread()`로 위임

### 4.2 홀로노믹 믹스

데드존 적용 후 범위 ±512.

- `vx` = 좌 스틱 Y (전후) + 우 스틱 Y (정면 유지 전후 평행)
- `vy` = 우 스틱 X (좌우 평행). 대각선 각도면 45°로 맞춤
- `w`  = 좌 스틱 X (Yaw). 우 스틱은 w에 기여하지 않음

휠 명령:

- `FL = vx + vy + w`
- `FR = vx - vy - w`
- `RL = vx - vy + w`
- `RR = vx + vy - w`

이후 `max(|wheel|)`이 512를 넘지 않게 스케일한다. 0이 아닌 휠 명령은 최소 듀티 448/512(약 87.5%)로 올려 정지 마찰을 이긴다. 램프 없이 즉시 반영한다.

### 4.3 게임패드 맵

| 입력 | 기능 |
|------|------|
| 좌 스틱 Y | 전후 |
| 좌 스틱 X | Yaw |
| 우 스틱 X/Y | 차체 정면 유지 평행이동 |
| D-Pad 좌/우 | 포탑 (속도 384, 최대의 75%) |
| X | 럼블 500ms + 가습기 2초 ON (0005.mp3) |
| 1초 이상 정지 후 출발 | 가습기 + 네오픽셀 1초 ON |
| Y | 레이더 ON/OFF (기본 OFF). ON 때 PWM 연결 |
| A | 개틀링 (0003.mp3 + LED 점멸) |
| B | 레이저 (LED 즉시 1.2초, 0.2초 뒤 0002.mp3) |
| Select | 헤드라이트 토글 |
| L1 / R1 | 볼륨 감소 / 증가 (NVS) |
| L1 + R1 (3초) | 개별 휠 테스트 |
| Select + Start (3초) | NVS 초기화 후 재시작 |
| 연결 해제 또는 약 1초 리포트 없음 | 페일세이프: 모터/휠 테스트 정지, 레이더 PWM 해제, 가습기/레이저 OFF |
| 연결 중 30초 무조작 | 30초마다 BGM 0006~0009 중 하나 |

### 4.4 레이더

- 기본 OFF. Y로 켤 때만 LEDC를 GPIO32에 붙이고, Y OFF 또는 페일세이프 때 뗀다 (`gpio_reset_pin` 금지, 풀업 방지)
- ON이고 정지 5초 후면 0°↔180° 편도 3초, 끝에서 3초 휴식
- 주행이 시작돼도 진행 중인 편도는 끝까지 간다
- LEDC: 50 Hz, 14-bit, 펄스 500~2500 µs

### 4.5 사운드

- TX 전용 (GPIO23 → 모듈 RX). RX 태스크 없음
- 대기음은 `play_loop(0001)`
- 짧은 효과음(0002/0003/0004/0005)은 1회 재생 후 타이머로 IDLE 루프 복귀
- BGM 0006~0009는 길이 미지이므로 자동 IDLE 복귀 없음

### 4.6 모듈 책임

| 모듈 | 책임 |
|------|------|
| `rccar_motor` | MCPWM 초기화, FL/FR/RL/RR + 포탑, 휠 테스트 취소 |
| `rccar_drive` | 스틱 값 → 4휠 속도, 대각선 스냅, idle→move |
| `rccar_radar` | LEDC 위치형 서보 attach/detach, 왕복 태스크 |
| `rccar_humidifier` | GPIO4, 펄스 ON |
| `rccar_neopixel` | WS2812 엔진/미스트 |
| `rccar_laser` | GPIO15 레이저, GPIO12 개틀링 |
| `rccar_headlight` | GPIO14 토글 |
| `rccar_dfplayer` | UART TX, IDLE 루프/효과음 재개 타이머 |
| `rccar_storage` | NVS 볼륨 |
| `rccar` | 순서 있는 초기화. DFPlayer는 `on_init_complete` |

## 5. 도구 / 환경

- **ESP-IDF:** v5.5.x
- **env.bat:** 프로젝트 루트. `C:\Espressif\idf_cmd_init.bat esp-idf-b29c58f93b4ca0f49cdfc4c3ef43b562`
- 빌드: env 실행 후 `idf.py set-target esp32`, `idf.py build`

## 6. 성공 기준

1. 프로젝트 `env.bat` IDF 환경에서 타깃 `esp32`로 빌드된다.
2. Bluepad32로 게임패드가 연결되고, 페일세이프가 주행/포탑/레이더/가습기/레이저를 정지시킨다.
3. 홀로노믹 매핑이 MCPWM 4채널을 구동하고, 포탑은 D-Pad, 레이더는 Y, 가습기는 X, 볼륨은 L1/R1 + NVS로 동작한다.
4. 레이더 PWM은 14-bit 50 Hz이며, Y OFF 또는 페일세이프 때 GPIO에서 떨어진다.

## 7. 구현 순서 (완료)

1. panzer4에서 스캐폴드
2. `rccar` 도입, 현행 핀 맵
3. MCPWM 4휠 + 포탑, 홀로노믹 믹스
4. 레이더 위치형 서보, 가습기, 네오픽셀, 레이저/개틀링/헤드라이트
5. 플랫폼 매핑, 페일세이프, DFPlayer/볼륨/NVS
6. README 및 Agent 메모

초기 v1 초안의 595 체인과 연속 회전 서보는 구현하지 않았다.

## 8. 결정 로그

| 항목 | 결정 |
|------|------|
| MCU | ESP32 classic WROOM |
| 기반 프로젝트 | panzer4-idf |
| 주행 | 메카넘. 좌 Y 전후 + 좌 X 요, 우 스틱 평행이동 |
| 모터 드라이버 | DRV8833 3개, DC 전부 MCPWM |
| 최소 듀티 | 448/512 (정지 마찰) |
| 포탑 속도 | 384 (최대의 75%) |
| 레이더 | 위치형 서보, Y 토글, LEDC 50 Hz 14-bit, GPIO32 |
| LED | 네오픽셀 8 + 레이저/개틀링/헤드라이트. 595 없음 |
| 가습기 | GPIO4, X 및 정지 후 출발 펄스 |
| 사운드 | DFPlayer TX 전용, IDLE 루프, 볼륨 NVS |
| 핀 | `rccar_pins.h` 단일 맵. 보드 변형 Kconfig 미사용 |
| 문서/답변 언어 | 한글 |
