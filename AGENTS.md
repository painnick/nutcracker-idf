# nutcracker-idf 에이전트 가이드

## 언어
- 이 프로젝트의 **문서**(README, 설계/계획 스펙, Agent 메모 등)와 **사용자 답변**은 **한글**로 작성한다.
- 코드 식별자, 파일명, 프로토콜/라이브러리 고유명, 로그 태그 등은 기존 ESP-IDF / panzer4 관례(영문)를 따른다.
- 커밋 메시지는 한글 또는 영문 모두 가능하나, 변경 요지를 분명히 쓴다.
- 문서/답변에 엠 대시(`—`), 가운뎃점(`·`)을 쓰지 않는다. 하이픈 또는 쉼표를 사용한다.

## 기술 스택
- Framework: ESP-IDF v5.5.x
- 언어: C
- 빌드: CMake / idf.py. 루트 `env.bat`로 IDF 환경을 먼저 잡는다.
- 타깃: ESP32 classic (`idf.py set-target esp32`)
- 입력: Bluepad32 + BTstack

## 빌드
```bat
env.bat
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## 구조 요약
| 경로 | 역할 |
|------|------|
| `main/main.c` | BTstack + Bluepad32 부트 |
| `main/my_platform.c` | 패드 매핑, Core1 입력 처리, 페일세이프 |
| `components/rccar/` | 메카넘 카 전용 (drive/motor/servo/led/595/dfplayer/storage) |
| `components/rccar/rccar_pins.h` | 핀 맵 (설계 3.1) |
| `components/bluepad32/`, `btstack/` | panzer4 계열 입력 스택 |
| `README.md` | 사용자용 개요, 핀 맵, 조작, 빌드 |
| `docs/superpowers/specs/` | 설계 문서 |
| `docs/superpowers/plans/` | 구현 계획 |

## 하드웨어 제약 (코드 변경 시)
- DC 모터 전부 DRV8833 + MCPWM (LEDC 금지). 그룹당 operator 3 → 휠/포탑 5채널은 group 0+1 분할.
- 핀 변경 시 `rccar_pins.h` 와 README / 설계 3.1을 함께 맞출 것.
- v1 제외: 주포/기관총, 포신 상하/반동, 웜 화이트 PWM, 595 최종 연출.

## 참고
- 설계: `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`
- 계획: `docs/superpowers/plans/2026-07-31-nutcracker-idf.md`
- 기반 프로젝트: `../panzer4-idf`
