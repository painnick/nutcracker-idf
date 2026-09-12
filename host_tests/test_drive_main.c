/**
 * Host-side checklist for rccar_drive (no ESP-IDF).
 * Compile: gcc -std=c99 -Wall -Wextra -o test_drive host_tests/test_drive_main.c components/rccar/rccar_drive.c -I components/rccar
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "rccar_drive.h"

static int g_fail;

static int iabs32(int32_t x) { return x < 0 ? -x : x; }

static void expect_eq(const char *name, int32_t got, int32_t want)
{
    if (got != want) {
        printf("FAIL %s: got %ld want %ld\n", name, (long)got, (long)want);
        g_fail++;
    } else {
        printf("OK   %s = %ld\n", name, (long)got);
    }
}

static void expect_bool(const char *name, bool got, bool want)
{
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", name, (int)got, (int)want);
        g_fail++;
    } else {
        printf("OK   %s = %d\n", name, (int)got);
    }
}

static void expect_sign(const char *name, int32_t v, int want_sign)
{
    /* want_sign: +1, -1, or 0 */
    int s = (v > 0) - (v < 0);
    if (s != want_sign) {
        printf("FAIL %s: value %ld sign %d want %d\n", name, (long)v, s, want_sign);
        g_fail++;
    } else {
        printf("OK   %s = %ld (sign %d)\n", name, (long)v, want_sign);
    }
}

int main(void)
{
    rccar_wheel_speeds_t w;

    /* (512,0,0) -> all +512 */
    rccar_drive_mix(512, 0, 0, &w);
    printf("--- case (512,0,0) forward ---\n");
    expect_eq("fl", w.fl, 512);
    expect_eq("fr", w.fr, 512);
    expect_eq("rl", w.rl, 512);
    expect_eq("rr", w.rr, 512);

    /* (0,512,0) strafe: FL/RR +, FR/RL - */
    rccar_drive_mix(0, 512, 0, &w);
    printf("--- case (0,512,0) strafe ---\n");
    expect_sign("fl", w.fl, +1);
    expect_sign("fr", w.fr, -1);
    expect_sign("rl", w.rl, -1);
    expect_sign("rr", w.rr, +1);
    expect_eq("fl", w.fl, 512);
    expect_eq("fr", w.fr, -512);
    expect_eq("rl", w.rl, -512);
    expect_eq("rr", w.rr, 512);

    /* (0,0,512) yaw: FL/RL one way, FR/RR opposite */
    rccar_drive_mix(0, 0, 512, &w);
    printf("--- case (0,0,512) yaw ---\n");
    expect_sign("fl", w.fl, +1);
    expect_sign("fr", w.fr, -1);
    expect_sign("rl", w.rl, +1);
    expect_sign("rr", w.rr, -1);
    expect_eq("fl", w.fl, 512);
    expect_eq("fr", w.fr, -512);
    expect_eq("rl", w.rl, 512);
    expect_eq("rr", w.rr, -512);

    /* (512,512,0) after scale: no |wheel| > 512 */
    rccar_drive_mix(512, 512, 0, &w);
    printf("--- case (512,512,0) scaled ---\n");
    printf("  fl=%ld fr=%ld rl=%ld rr=%ld\n", (long)w.fl, (long)w.fr, (long)w.rl, (long)w.rr);
    if (iabs32(w.fl) > 512 || iabs32(w.fr) > 512 || iabs32(w.rl) > 512 || iabs32(w.rr) > 512) {
        printf("FAIL scale: abs>512\n");
        g_fail++;
    } else {
        printf("OK   all |wheel| <= 512\n");
    }
    /* raw before scale would be fl=1024, fr=0, rl=0, rr=1024 -> scale by 512/1024 */
    expect_eq("fl", w.fl, 512);
    expect_eq("fr", w.fr, 0);
    expect_eq("rl", w.rl, 0);
    expect_eq("rr", w.rr, 512);

    /* deadzone */
    printf("--- deadzone ---\n");
    expect_eq("dz 0", rccar_drive_apply_deadzone(0, 60), 0);
    expect_eq("dz 60", rccar_drive_apply_deadzone(60, 60), 0);
    expect_eq("dz -60", rccar_drive_apply_deadzone(-60, 60), 0);
    expect_eq("dz 61", rccar_drive_apply_deadzone(61, 60), 61);
    expect_eq("dz -61", rccar_drive_apply_deadzone(-61, 60), -61);
    expect_eq("dz 100", rccar_drive_apply_deadzone(100, 60), 100);

    printf("--- snap_diagonal ---\n");
    {
        int32_t x, y;
        x = 400; y = 250;
        rccar_drive_snap_diagonal(&x, &y, 60);
        expect_eq("diag 400/250 x", x, 400);
        expect_eq("diag 400/250 y", y, 400);

        x = -300; y = 220;
        rccar_drive_snap_diagonal(&x, &y, 60);
        expect_eq("diag -300/220 x", x, -300);
        expect_eq("diag -300/220 y", y, 300);

        x = 400; y = 50;
        rccar_drive_snap_diagonal(&x, &y, 60);
        expect_eq("axis 400/50 x", x, 400);
        expect_eq("axis 400/50 y", y, 0);

        x = 0; y = 400;
        rccar_drive_snap_diagonal(&x, &y, 60);
        expect_eq("axis 0/400 x", x, 0);
        expect_eq("axis 0/400 y", y, 400);

        x = 40; y = 40;
        rccar_drive_snap_diagonal(&x, &y, 60);
        expect_eq("small 40/40 x", x, 0);
        expect_eq("small 40/40 y", y, 0);
    }

    printf("--- idle_to_move ---\n");
    {
        bool was = false;
        int64_t idle = -1;
        const int64_t need = 3000;

        expect_bool("stop t=0", rccar_drive_idle_to_move(&was, &idle, false, 0, need), false);
        expect_bool("still stop 2999", rccar_drive_idle_to_move(&was, &idle, true, 2999, need), false);

        was = false;
        idle = -1;
        rccar_drive_idle_to_move(&was, &idle, false, 0, need);
        expect_bool("move after 3000", rccar_drive_idle_to_move(&was, &idle, true, 3000, need), true);
        expect_bool("keep moving", rccar_drive_idle_to_move(&was, &idle, true, 3500, need), false);

        rccar_drive_idle_to_move(&was, &idle, false, 4000, need);
        expect_bool("move after 1s idle", rccar_drive_idle_to_move(&was, &idle, true, 5000, need), false);

        rccar_drive_idle_to_move(&was, &idle, false, 6000, need);
        rccar_drive_idle_to_move(&was, &idle, false, 8000, need);
        expect_bool("move after 3s idle", rccar_drive_idle_to_move(&was, &idle, true, 9000, need), true);

        was = false;
        idle = -1;
        expect_bool("first sample already moving",
                    rccar_drive_idle_to_move(&was, &idle, true, 100, need), false);
    }

    if (g_fail) {
        printf("\n%d FAILURE(S)\n", g_fail);
        return 1;
    }
    printf("\nALL PASS\n");
    return 0;
}
