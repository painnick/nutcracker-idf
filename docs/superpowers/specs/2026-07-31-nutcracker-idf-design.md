# nutcracker-idf Design

Date: 2026-07-31  
Status: Draft for review  
Base: panzer4-idf (ESP-IDF + Bluepad32)

## 1. Goals

Build a Bluepad32-controlled **mecanum-wheel RC car** (not a tank) on **ESP32 classic**.

v1 includes:

- Holonomic drive (4 DC motors)
- Turret rotation (1 DC motor)
- Continuous-rotation radar servo (spin when armed)
- Many indicator LEDs via 4x 74HC595 daisy-chain
- One high-current **warm white** LED via MOSFET (on/off)
- DFPlayer sound, volume in NVS, pad rumble
- Failsafe on disconnect / report timeout

Out of v1:

- Cannon / MG fire sequences from panzer4
- Elevation / recoil servos
- Warm-white PWM dimming
- Final decorative 595 LED art (driver + test patterns only)
- Final PCB silkscreen polish

## 2. Approach

**Fork panzer4-idf and rewrite modules** (approach A).

Copy Bluepad32, BTstack, console helpers, env pattern, and platform architecture. Replace tank-specific drive/LED usage with car modules under `components/rccar/`.

## 3. Hardware

| Subsystem | Hardware | Control |
|-----------|----------|---------|
| Drive | 2x DRV8833 (4 half-bridges) | MCPWM |
| Turret | 3rd DRV8833 | MCPWM |
| Radar | Continuous-rotation servo | LEDC |
| Effect LEDs | 4x 74HC595 daisy-chain (32 ch) | SPI-style bit-bang or SPI |
| Warm white | MOSFET low-side (or high-side), 1 ch | GPIO on/off |
| Sound | DFPlayer Mini, TX only | UART TX |
| Input | BLE/Classic HID gamepad | Bluepad32 + BTstack |

All **DC motors** use **DRV8833 + MCPWM** (same pattern as panzer4). Servo is LEDC only. Warm white is not on the 595 chain.

### 3.1 Pin map (locked, placement-oriented)

Strapping-risk pins for motors avoided (0, 2, 12, 15). Flash pins (6-11), UART0 console (1, 3), input-only (34-39) unused for outputs.

| Function | GPIO | Block |
|----------|------|--------|
| FL IN1 | 27 | DRV8833 #1 front |
| FL IN2 | 26 | DRV8833 #1 front |
| FR IN1 | 25 | DRV8833 #1 front |
| FR IN2 | 33 | DRV8833 #1 front |
| RL IN1 | 32 | DRV8833 #2 rear |
| RL IN2 | 14 | DRV8833 #2 rear |
| RR IN1 | 13 | DRV8833 #2 rear |
| RR IN2 | 16 | DRV8833 #2 rear |
| Turret IN1 | 22 | DRV8833 #3 |
| Turret IN2 | 21 | DRV8833 #3 |
| Radar servo | 17 | LEDC |
| Warm white MOSFET gate | 4 | Y toggle |
| DFPlayer TX | 5 | UART TX |
| 595 DATA (SER) | 23 | shift LEDs |
| 595 CLOCK (SRCLK) | 18 | shift LEDs |
| 595 LATCH (RCLK) | 19 | shift LEDs |

595 in v1: OE tied to GND (always enable), MR tied to VCC (no hardware clear). Optional OE/SRCLR GPIO later.

### 3.2 Placement rationale

- Front motor 4 wires and rear motor 4 wires form two connector groups.
- Turret 2 wires adjacent (21/22).
- 595 three-wire bus on 18/19/23.
- Low-current controls (warm white, DFPlayer, radar) on 4/5/17, separated from motor clusters.

## 4. Software architecture

```
nutcracker-idf/
  env.bat                    # ESP-IDF environment (present)
  main/
    main.c                   # BTstack + Bluepad32 boot
    my_platform.c            # pad events, Core1 worker, mapping
  components/
    bluepad32/ btstack/ ...  # from panzer4
    rccar/
      rccar.c / rccar.h      # init orchestration
      rccar_pins.h           # pin table above
      rccar_drive.c          # holonomic mix
      rccar_motor.c          # MCPWM: 4 wheels + turret
      rccar_servo.c          # CR radar
      rccar_shiftreg.c       # 595 chain
      rccar_led.c            # warm white + LED modes
      rccar_dfplayer.c
      rccar_storage.c        # volume NVS
```

### 4.1 Input path (from panzer4)

- `my_platform_on_controller_data` (BT path, Core 0) enqueues samples.
- `input_process_task` on Core 1 applies drive, turret, buttons, failsafe.
- Motor targets use volatile/shared targets; ramps run in motor task or process loop as in panzer4.

### 4.2 Holonomic mix

Inputs after deadzone, scaled to a common range (e.g. +/-512):

- `vx` = left stick Y (forward/back)
- `vy` = left stick X (strafe)
- `w`  = right stick X (yaw)

Wheel commands:

- `FL = vx + vy + w`
- `FR = vx - vy - w`
- `RL = vx - vy + w`
- `RR = vx + vy - w`

Then scale so max(|wheel|) does not exceed full duty, apply soft ramp / min start speed as needed (panzer4 track ideas adapted per wheel).

### 4.3 Gamepad map (v1)

| Input | Function |
|-------|----------|
| L-stick X/Y | Strafe / forward-back |
| R-stick X | Yaw |
| D-Pad L/R | Turret DC |
| Y | Warm white MOSFET toggle |
| X | Radar arm/disarm (spin at fixed speed) |
| L1 / R1 | Volume down / up (NVS) |
| Disconnect or ~1s no report | Failsafe: all motors 0, radar stop |

### 4.4 Module responsibilities

| Module | Responsibility |
|--------|----------------|
| `rccar_motor` | MCPWM init and set duty for FL/FR/RL/RR + turret |
| `rccar_drive` | Stick to four wheel speeds |
| `rccar_servo` | LEDC CR radar: armed spin / disarmed stop (neutral pulse) |
| `rccar_shiftreg` | Shift and latch 32-bit patterns |
| `rccar_led` | Warm white GPIO; high-level patterns calling shiftreg |
| `rccar_dfplayer` | UART TX commands, track IDs TBD |
| `rccar_storage` | NVS volume |
| `rccar` | Ordered init: NVS -> pins drivers -> sound |

## 5. Tooling / environment

- **ESP-IDF:** v5.5.x (same install as panzer4)
- **env.bat:** project root; runs  
  `C:\Espressif\idf_cmd_init.bat esp-idf-b29c58f93b4ca0f49cdfc4c3ef43b562`
- Build: open env, then `idf.py set-target esp32`, `idf.py build`

## 6. Success criteria

1. Project builds under the project `env.bat` IDF environment for target `esp32`.
2. Gamepad connects via Bluepad32; failsafe stops drive, turret, and radar.
3. Holonomic mapping drives four MCPWM channels; turret on D-Pad; radar on X; warm white on Y; volume L1/R1 with NVS.
4. 595 driver can latch a full 32-bit test pattern (e.g. chase or all-on).

## 7. Implementation order (preview)

1. Scaffold from panzer4 (gitignore, CMake, components bluepad32/btstack, sdkconfig.defaults, main stub).
2. Introduce `rccar` with pins header matching this doc.
3. MCPWM four-wheel + turret; holonomic mix.
4. Radar CR servo + warm white GPIO + 595 driver.
5. Platform mapping, failsafe, DFPlayer/volume/NVS.
6. README and Agent notes for the car (not tank).

## 8. Decisions log

| Topic | Decision |
|-------|----------|
| MCU | ESP32 classic (panzer4) |
| Base project | panzer4-idf |
| Drive | Mecanum, holonomic L-stick XY + R-stick X yaw |
| Motor drivers | 3x DRV8833, all DC via MCPWM |
| Radar | CR servo, spin when armed (X), fixed speed |
| LEDs | 4x 74HC595 daisy-chain |
| Warm white | MOSFET, single channel, Y on/off |
| Sound v1 | DFPlayer + volume NVS + rumble + failsafe |
| Pins | Locked table in section 3.1 |
| env.bat | Added by user at project root |
