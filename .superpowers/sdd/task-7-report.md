# Task 7 Report: 플랫폼 매핑 + 페일세이프

**Date:** 2026-07-31  
**Project:** nutcracker-idf  
**Status:** Complete  
**Commit:** `4efdba3` - `feat: 메카넘/포탑/레이더/LED 게임패드 매핑과 페일세이프`

## Summary

`main/my_platform.c` 를 메카넘 RC 카용으로 전면 재작성했다.  
Core0 BT 콜백 → 큐(길이 1, overwrite) → Core1 `input_process_task` 패턴(panzer4)을 유지하고, 탱크 전용(주포/기관총/반동/포신 상하) 로직은 제거했다.  
`idf.py build` 성공.

## Changes

| Path | Change |
|------|--------|
| `main/my_platform.c` | 스텁 → 풀 매핑 + 페일세이프 + 연결/해제 콜백 |

## Gamepad map (v1)

| 입력 | 동작 |
|------|------|
| 좌 스틱 X/Y | `vy` / `vx` (deadzone 60, clamp ±512) → `rccar_drive_mix` → `rccar_motor_wheel_set` |
| 우 스틱 X | `w` (요) |
| D-Pad L/R | 포탑 ±511, 그 외 0 |
| Y (edge, 400ms debounce) | `rccar_led_warm_white_toggle` |
| X (edge) | 레이더 armed 토글 |
| L1 / R1 | 볼륨 -/+ (`rccar_storage` + `rccar_dfplayer`) |
| Select+Start 3s | NVS erase + restart (panzer4 유지) |
| 리포트 없음 1s | failsafe: `rccar_motor_all_stop` + radar disarmed |
| connect (ready) | 모터 정지, CONNECT 트랙, rumble |
| disconnect | all_stop + radar off + IDLE 트랙 |

## Stick conventions

파일 상단 주석 및 `STICK_*_SIGN` 상수:

- `axis_y`: HID 위=음수 → `STICK_VX_SIGN = -1` (위=전진)
- `axis_x`: 오른쪽=양수 → `STICK_VY_SIGN = +1` (우평행)
- `axis_rx`: 오른쪽=양수 → `STICK_W_SIGN = +1` (요 CW)

실차 방향이 반대면 해당 SIGN 만 바꾸면 된다.

## Architecture notes

1. **입력 경로:** `on_controller_data` → `xQueueOverwrite` → Core1 `input_proc` (50ms poll).
2. **페일세이프:** 마지막 샘플 timestamp 기준 `FAILSAFE_MS=1000`. 큐 timeout 폴링으로 무입력 감지. 최초 입력 전에도 failsafe 활성.
3. **레이더 상태:** `s_radar_armed` 를 failsafe/disconnect 시 false 로 리셋해 토글 상태 불일치 방지.
4. **사운드:** init complete / disconnect 시 IDLE + 30s 주기; ready 시 CONNECT.

## Build

```
idf.py build  →  Project build complete (exit 0)
nutcracker_idf.bin size 0xaf290 (~53% free in app partition)
```

## Not in this task

- 실기 flash/monitor 패드 검증
- README (Task 8)
- 주포/MG/반동/elevation (v1 제외)

---

## Review fix: disconnect ↔ input worker 동기화

**Date:** 2026-07-31  
**Finding:** disconnect 시 `failsafe_stop` 만 호출하면 Core1 `input_process_task` 가 큐에 남은 샘플을 적용해 최대 ~1s 모터 재구동 가능.

### Fix

| 항목 | 내용 |
|------|------|
| `volatile bool s_connected` | ready 에서 true, disconnect 에서 false. 워커는 `!s_connected` 이면 drive 스킵 + `last_input_ms=0` |
| disconnect | `s_connected=false` → `failsafe_stop` → `xQueueReset(input_queue)` |
| ready | `s_radar_armed=false` + `rccar_radar_set_armed(false)` + 큐 리셋 후 `s_connected=true` |
| `on_controller_data` | `!s_connected` 이면 enqueue 안 함 |
| 레이스 | receive 후 drive 직전 `s_connected` 재확인 |
| minor | shoulder release 시 no-op `volume_set(get())` 제거 |

### Build (fix-fix)

```
idf.py build  →  Project build complete (exit 0)
nutcracker_idf.bin size 0xaf330 (~53% free in app partition)
```
