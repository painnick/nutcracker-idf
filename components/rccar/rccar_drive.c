/**
 * @file rccar_drive.c
 * @brief 메카넘 홀로노믹 믹스 (스텁)
 */
#include "rccar_drive.h"

void rccar_drive_mix(int32_t vx, int32_t vy, int32_t w, rccar_wheel_speeds_t *out)
{
    (void)vx;
    (void)vy;
    (void)w;
    if (out) {
        out->fl = 0;
        out->fr = 0;
        out->rl = 0;
        out->rr = 0;
    }
}

int32_t rccar_drive_apply_deadzone(int32_t v, int32_t deadzone)
{
    (void)deadzone;
    return v;
}
