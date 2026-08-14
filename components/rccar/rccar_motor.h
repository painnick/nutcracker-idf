/**
 * @file rccar_motor.h
 * @brief MCPWM 4휠 + 포탑 (DRV8833 x3)
 */
#ifndef RCCAR_MOTOR_H
#define RCCAR_MOTOR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MCPWM 모터 드라이버 초기화 (group0: FL/FR/RL, group1: RR/TURRET)
 * @return ESP_OK on success
 */
esp_err_t rccar_motor_init(void);

/**
 * @brief 4휠 속도 즉시 설정 (램프 없음)
 * @param fl,fr,rl,rr -512 .. 511 (양수: IN1 PWM, 음수: IN2 PWM)
 * @note 0이 아닌 값은 최소 듀티(448) 이상으로 올려서 적용한다.
 */
void rccar_motor_wheel_set(int fl, int fr, int rl, int rr);

/**
 * @brief 포탑 속도 즉시 설정 (램프 없음)
 * @param speed -512 .. 511
 */
void rccar_motor_turret_set(int speed);

/**
 * @brief 전 모터 즉시 정지 (휠 + 포탑)
 */
void rccar_motor_all_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_MOTOR_H */
