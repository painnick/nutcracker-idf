/**
 * @file rccar_motor.c
 * @brief MCPWM 4휠 + 포탑 (스텁)
 */
#include "rccar_motor.h"

esp_err_t rccar_motor_init(void)
{
    return ESP_OK;
}

void rccar_motor_wheel_set(int fl, int fr, int rl, int rr)
{
    (void)fl;
    (void)fr;
    (void)rl;
    (void)rr;
}

void rccar_motor_wheel_set_immediate(int fl, int fr, int rl, int rr)
{
    (void)fl;
    (void)fr;
    (void)rl;
    (void)rr;
}

void rccar_motor_turret_set(int speed)
{
    (void)speed;
}

void rccar_motor_all_stop(void)
{
}
