/**
 * @file rccar_drive.h
 * @brief 메카넘 홀로노믹 믹스 (스텁)
 */
#ifndef RCCAR_DRIVE_H
#define RCCAR_DRIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t fl, fr, rl, rr; /* -512 .. 511 */
} rccar_wheel_speeds_t;

/** vx,vy,w: 데드존 적용 후 값, 범위 -512..511 */
void rccar_drive_mix(int32_t vx, int32_t vy, int32_t w, rccar_wheel_speeds_t *out);

/** abs가 deadzone 이하면 0, 아니면 그대로 (부호 유지) */
int32_t rccar_drive_apply_deadzone(int32_t v, int32_t deadzone);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_DRIVE_H */
