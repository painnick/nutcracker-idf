# nutcracker-idf

ESP32 classic + Bluepad32 기반 **메카넘 휠 RC 카** 펌웨어입니다.  
panzer4-idf(RC 탱크)를 포크한 뒤 탱크 전용 모듈을 카용 `components/rccar/` 로 교체했습니다.

- Framework: ESP-IDF v5.5.x
- 입력: Bluepad32 + BTstack (BLE/Classic HID 게임패드)
- 주행: 홀로노믹 믹스 (전후 + 좌우 평행이동 + 요 회전)

## 주요 기능

| 기능 | 설명 |
| :--- | :--- |
| 홀로노믹 주행 | 좌 스틱 전후+Yaw, 우 스틱 평행이동(정면 유지) → 4휠 DRV8833 |
| 포탑 회전 | D-Pad 좌/우 → DRV8833 3번째 칩 (MCPWM) |
| 가습기 | GPIO4, X 버튼 시 3초 ON (HIGH=ON), 부팅 시 OFF |
| 네오픽셀 | WS2812 4개 (GPIO13), Y 버튼 엔진 idle 효과 토글 |
| 사운드 | DFPlayer Mini (UART TX), L1/R1 볼륨, NVS 저장 |
| 페일세이프 | 연결 해제 또는 약 1초 리포트 없음 → 전 모터 정지 |
| 패드 피드백 | 연결 시 진동, 상태별 효과음 (IDLE / CONNECT) |

## 핀 맵

정의: `components/rccar/rccar_pins.h`.

모터 출력에 스트래핑 위험 핀(0, 2, 12, 15)을 쓰지 않습니다.
플래시(6-11), UART0 콘솔(1, 3), 입력 전용(34-39)은 출력에 사용하지 않습니다.

| 기능 | GPIO | 블록 |
| :--- | ---: | :--- |
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
| 네오픽셀 DATA | 13 | WS2812 x4 |
| DFPlayer TX | 5 | UART TX |

## 게임패드 조작

| 입력 | 기능 |
| :--- | :--- |
| 좌 스틱 Y | 전후 |
| 좌 스틱 X | Yaw 회전 (일반 차량 조향) |
| 우 스틱 X/Y | 차체 정면 유지 평행이동 (좌우, 대각선, w=0) |
| D-Pad 좌/우 | 포탑 회전 |
| X | 패드 럼블 (500ms) + 가습기 3초 ON |
| Y | 네오픽셀 엔진 idle 효과 ON/OFF 토글 |
| L1 / R1 | 볼륨 감소 / 증가 (NVS 저장) |
| Select + Start (3초) | NVS 설정 초기화 후 재시작 |
| 연결 해제 또는 약 1초 리포트 없음 | 페일세이프: 전 모터 0 |
| 연결 성공 | 패드 럼블 (400ms) |

### Wii Balance Board

| 입력 | 기능 |
| :--- | :--- |
| 무게 중심 (좌/우) | 좌우 평행이동 |
| 무게 중심 (앞/뒤) | 전후 |
| 보드 위 무게 없음 | 정지 (연결은 유지) |

TV 쪽이 보드 **앞(top)** 센서입니다. 앞으로 기울이면 전진, 오른쪽으로 기울이면 우측 이동입니다. 우측 스틱 평행이동과 동일하게 w=0 으로 믹스합니다.

스틱 부호(실차 방향이 반대일 때): `main/my_platform.c` 의 `STICK_VX_SIGN` / `STICK_VY_SIGN` / `STICK_W_SIGN` / `STICK_RY_VX_SIGN`.

### 홀로노믹 믹스 (참고)

데드존 적용 후 좌/우 스틱 입력을 합산:

- `vx` = 좌 Y (전후) + 우 Y (정면 유지 전후 평행)
- `vy` = 우 X (좌우 평행)
- `w`  = 좌 X (Yaw만, 우 스틱은 w에 기여하지 않음)

- `FL = vx + vy + w`
- `FR = vx - vy - w`
- `RL = vx - vy + w`
- `RR = vx + vy - w`

이후 `max(|wheel|)` 이 풀 듀티를 넘지 않게 스케일합니다.

## 하드웨어 주의

- **DRV8833 3개**: #1 전륜(FL/FR), #2 후륜(RL/RR), #3 포탑. DC 모터는 전부 MCPWM (LEDC 사용 안 함).
- **ESP32 MCPWM**: 그룹당 operator 최대 3. 모터 5채널은 group 0에 3개 + group 1에 2개로 배치.
- **DFPlayer**: TX 전용 (GPIO 5 → 모듈 RX). 수신 핀 미사용.
- 모터 전원과 로직 전원을 분리하고, 공통 GND를 확실히 연결할 것.

## 소프트웨어 구조

```
nutcracker-idf/
  env.bat                 # ESP-IDF 환경
  main/
    main.c                # BTstack + Bluepad32 부트
    my_platform.c         # 패드 이벤트, Core1 워커, 매핑, 페일세이프
  components/
    bluepad32/            # git submodule: painnick/bluepad32
                          # IDF 컴포넌트는 src/components/bluepad32
    btstack/              # panzer4에서 복사한 BTstack
    rccar/
      rccar.c / .h        # 초기화 오케스트레이션
      rccar_pins.h        # 핀 상수
      rccar_drive.c       # 홀로노믹 믹스
      rccar_motor.c       # MCPWM: 휠 4 + 포탑
      rccar_humidifier.c  # 가습기 GPIO
      rccar_neopixel.c   # WS2812 엔진 효과
      rccar_dfplayer.c    # 사운드
      rccar_storage.c     # 볼륨 NVS
```

입력 경로: Core0 `on_controller_data` → 큐 → Core1 `input_process_task` 에서 주행/포탑/버튼/페일세이프 적용.

## 빌드

클론 후 서브모듈을 먼저 받습니다.

```bat
git submodule update --init
```

Windows (프로젝트 루트):

```bat
env.bat
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

- `env.bat` 는 Espressif IDF 설치의 `idf_cmd_init.bat` 를 호출합니다. 경로가 다르면 `env.bat` 를 수정하세요.
- 타깃은 **ESP32 classic** (`esp32`) 입니다. S2/S3/C3 등에서는 핀/MCPWM 배치를 그대로 쓰지 마세요.

## 설계 문서

- 설계: [`docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`](docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md)
- 구현 계획: [`docs/superpowers/plans/2026-07-31-nutcracker-idf.md`](docs/superpowers/plans/2026-07-31-nutcracker-idf.md)
- 에이전트 가이드: [`AGENTS.md`](AGENTS.md)
- 기반 프로젝트: `../panzer4-idf`

## 라이선스

`LICENSE` 참고. Bluepad32 / BTstack 등 서드파티 라이선스가 포함됩니다.
