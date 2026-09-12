/**
 * @file rccar_drive.c
 * @brief 메카넘 홀로노믹 믹스
 */
#include "rccar_drive.h"

#include <stddef.h>

static int32_t iabs(int32_t x)
{
    return (x < 0) ? -x : x;
}

void rccar_drive_mix(int32_t vx, int32_t vy, int32_t w, rccar_wheel_speeds_t *out)
{
    if (!out) {
        return;
    }

    int32_t fl = vx + vy + w;
    int32_t fr = vx - vy - w;
    int32_t rl = vx - vy + w;
    int32_t rr = vx + vy - w;

    int32_t m = iabs(fl);
    if (iabs(fr) > m) {
        m = iabs(fr);
    }
    if (iabs(rl) > m) {
        m = iabs(rl);
    }
    if (iabs(rr) > m) {
        m = iabs(rr);
    }

    if (m > 512) {
        fl = (fl * 512) / m;
        fr = (fr * 512) / m;
        rl = (rl * 512) / m;
        rr = (rr * 512) / m;
    }

    out->fl = fl;
    out->fr = fr;
    out->rl = rl;
    out->rr = rr;
}

int32_t rccar_drive_apply_deadzone(int32_t v, int32_t deadzone)
{
    if (iabs(v) <= deadzone) {
        return 0;
    }
    return v;
}

/* tan(20°)≈0.364, tan(70°)≈2.747. 축 정렬에 가까운 입력은 그대로 둔다. */
#define DIAG_RATIO_NUM  1000
#define DIAG_TAN_LO     364
#define DIAG_TAN_HI     2747
#define DIAG_AXIS_GATE  48

static bool in_diagonal_cone(int32_t ax, int32_t ay)
{
    return (ay * DIAG_RATIO_NUM >= ax * DIAG_TAN_LO) &&
           (ay * DIAG_RATIO_NUM <= ax * DIAG_TAN_HI);
}

void rccar_drive_snap_diagonal(int32_t *vx, int32_t *vy, int32_t deadzone)
{
    if (vx == NULL || vy == NULL) {
        return;
    }

    int32_t ax = iabs(*vx);
    int32_t ay = iabs(*vy);
    int32_t mag = (ax > ay) ? ax : ay;
    int32_t gate = DIAG_AXIS_GATE;
    if (deadzone > 0 && deadzone < gate) {
        gate = deadzone;
    }

    if (ax >= gate && ay >= gate && mag > deadzone && in_diagonal_cone(ax, ay)) {
        *vx = (*vx < 0) ? -mag : mag;
        *vy = (*vy < 0) ? -mag : mag;
        return;
    }

    if (ax <= deadzone) {
        *vx = 0;
    }
    if (ay <= deadzone) {
        *vy = 0;
    }
}

bool rccar_drive_idle_to_move(bool *was_moving, int64_t *idle_since_ms,
                              bool moving, int64_t now_ms, int64_t idle_need_ms)
{
    if (was_moving == NULL || idle_since_ms == NULL) {
        return false;
    }

    bool pulse = false;
    if (moving) {
        if (!*was_moving && *idle_since_ms >= 0 &&
            (now_ms - *idle_since_ms) >= idle_need_ms) {
            pulse = true;
        }
        *was_moving = true;
    } else {
        if (*was_moving || *idle_since_ms < 0) {
            *idle_since_ms = now_ms;
        }
        *was_moving = false;
    }
    return pulse;
}
