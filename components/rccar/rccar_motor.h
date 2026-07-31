/**
 * @file rccar_motor.h
 * @brief MCPWM 4휠 + 포탑 (스텁)
 */
#ifndef RCCAR_MOTOR_H
#define RCCAR_MOTOR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rccar_motor_init(void);
void rccar_motor_wheel_set(int fl, int fr, int rl, int rr);
void rccar_motor_wheel_set_immediate(int fl, int fr, int rl, int rr);
void rccar_motor_turret_set(int speed);
void rccar_motor_all_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_MOTOR_H */
