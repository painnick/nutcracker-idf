/**
 * Host-side checklist for DFPlayer TX-only IDLE resume delays.
 * Compile: gcc -std=c99 -Wall -Wextra -o test_dfplayer_resume host_tests/test_dfplayer_resume.c -I components/rccar
 */
#include <stdio.h>
#include <stdint.h>

#include "rccar_dfplayer.h"

static int g_fail;

static void expect_eq(const char *name, uint32_t got, uint32_t want)
{
    if (got != want) {
        printf("FAIL %s: got %u want %u\n", name, (unsigned)got, (unsigned)want);
        g_fail++;
    } else {
        printf("OK   %s = %u\n", name, (unsigned)got);
    }
}

int main(void)
{
    expect_eq("idle", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_IDLE), 0);
    expect_eq("gun", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_GUN), 2500);
    expect_eq("mg", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_MG), 1500);
    expect_eq("connect", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_CONNECT), 4000);
    expect_eq("nitro", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_NITRO), 3000);
    expect_eq("bgm min", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_BGM_MIN), 0);
    expect_eq("bgm max", rccar_dfplayer_resume_idle_ms(RCCAR_DFPLAYER_TRACK_BGM_MAX), 0);
    expect_eq("unknown", rccar_dfplayer_resume_idle_ms(99), 0);

    if (g_fail) {
        printf("\n%d FAILURE(S)\n", g_fail);
        return 1;
    }
    printf("\nALL PASS\n");
    return 0;
}
