# nutcracker-idf

ESP32 classic + Bluepad32 기반 **메카넘 휠 RC 카** 펌웨어입니다.  
panzer4-idf(RC 탱크)를 포크한 뒤 탱크 전용 모듈을 카용 `components/rccar/` 로 교체했습니다.

- Framework: ESP-IDF v5.5.x
- 입력: Bluepad32 + BTstack (BLE/Classic HID 게임패드)
- 주행: 홀로노믹 믹스 (전후 + 좌우 평행이동 + 요 회전)

## 주요 기능

| 기능 | 설명 |
| :--- | :--- |
| 홀로노믹 주행 | 좌 스틱 XY + 우 스틱 X → 4휠 DRV8833 (MCPWM, 소프트 램프) |
| 포탑 회전 | D-Pad 좌/우 → DRV8833 3번째 칩 (MCPWM) |
| 레이더 | 연속 회전 서보 (LEDC). X 버튼으로 무장/해제 |
| 효과 LED | 74HC595 ×4 데이지체인 (32채널, v1은 드라이버 + 테스트 패턴) |
| 웜 화이트 | MOSFET 1채널, Y 버튼 온/오프 토글 |
| 사운드 | DFPlayer Mini (UART TX), L1/R1 볼륨, NVS 저장 |
| 페일세이프 | 연결 해제 또는 약 1초 리포트 없음 → 전 모터 정지, 레이더 해제 |
| 패드 피드백 | 연결 시 진동, 상태별 효과음 (IDLE / CONNECT) |

v1에서 제외: 주포/기관총 발사 시퀀스, 포신 상하/반동 서보, 웜 화이트 PWM 디밍, 595 최종 연출 패턴.

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

## 게임패드 조작

| 입력 | 기능 |
| :--- | :--- |
| 좌 스틱 X/Y | 좌우 평행이동 / 전후 |
| 우 스틱 X | 요 회전 |
| D-Pad 좌/우 | 포탑 회전 |
| Y | 웜 화이트 MOSFET 토글 |
| X | 레이더 무장/해제 (고정 속도 회전) |
| L1 / R1 | 볼륨 감소 / 증가 (NVS 저장) |
| Select + Start (3초) | NVS 설정 초기화 후 재시작 |
| 연결 해제 또는 약 1초 리포트 없음 | 페일세이프: 전 모터 0, 레이더 정지 |

스틱 부호(실차 방향이 반대일 때): `main/my_platform.c` 의 `STICK_VX_SIGN` / `STICK_VY_SIGN` / `STICK_W_SIGN`.

### 홀로노믹 믹스 (참고)

데드존 적용 후 공통 범위(±512)로 스케일:

- `vx` = 좌 스틱 Y (전후)
- `vy` = 좌 스틱 X (좌우 평행이동)
- `w`  = 우 스틱 X (요)

- `FL = vx + vy + w`
- `FR = vx - vy - w`
- `RL = vx - vy + w`
- `RR = vx + vy - w`

이후 `max(|wheel|)` 이 풀 듀티를 넘지 않게 스케일합니다.

## 하드웨어 주의

- **DRV8833 3개**: #1 전륜(FL/FR), #2 후륜(RL/RR), #3 포탑. DC 모터는 전부 MCPWM (LEDC 사용 안 함).
- **ESP32 MCPWM**: 그룹당 operator 최대 3. 모터 5채널은 group 0에 3개 + group 1에 2개로 배치.
- **74HC595 ×4**: DATA/CLOCK/LATCH 만 GPIO. v1에서 **OE는 GND**(항상 enable), **MR(SRCLR)은 VCC**(하드웨어 클리어 없음). OE/MR용 GPIO는 이후 확장 가능.
- **웜 화이트**: 595 체인에 올리지 않음. MOSFET 로우사이드(또는 하이사이드) 게이트를 GPIO 4로 온/오프.
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
    bluepad32/ btstack/   # panzer4에서 복사
    rccar/
      rccar.c / .h        # 초기화 오케스트레이션
      rccar_pins.h        # 핀 상수
      rccar_drive.c       # 홀로노믹 믹스
      rccar_motor.c       # MCPWM: 휠 4 + 포탑
      rccar_servo.c       # 연속 회전 레이더
      rccar_shiftreg.c    # 595 체인
      rccar_led.c         # 웜 화이트 + LED 모드
      rccar_dfplayer.c    # 사운드
      rccar_storage.c     # 볼륨 NVS
```

입력 경로: Core0 `on_controller_data` → 큐 → Core1 `input_process_task` 에서 주행/포탑/버튼/페일세이프 적용.

## 빌드

Windows (프로젝트 루트):

```bat
env.bat
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

- `env.bat` 는 Espressif IDF 설치의 `idf_cmd_init.bat` 를 호출합니다. 경로가 다르면 `env.bat` 를 수정하세요.
- 타깃은 **ESP32 classic** (`esp32`) 입니다. S2/S3/C3 는 이 핀/MCPWM 배치를 그대로 쓰지 마세요.

## 설계 문서

- 설계: [`docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`](docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md)
- 구현 계획: [`docs/superpowers/plans/2026-07-31-nutcracker-idf.md`](docs/superpowers/plans/2026-07-31-nutcracker-idf.md)
- 에이전트 가이드: [`AGENTS.md`](AGENTS.md)
- 기반 프로젝트: `../panzer4-idf`

## 라이선스

`LICENSE` 참고. Bluepad32 / BTstack 등 서드파티 라이선스가 포함됩니다.
