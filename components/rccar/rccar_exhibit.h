/**
 * @file rccar_exhibit.h
 * @brief 미연결 전시 연출. 호출은 Core1 input_process_task 만 한다.
 */
#ifndef RCCAR_EXHIBIT_H
#define RCCAR_EXHIBIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*rccar_exhibit_is_connected_fn)(void);

/** 스크립트를 1번부터 다시 시작한다. 첫 포탑은 오른쪽이고, 이후 회전은 매번 반대이다. */
void rccar_exhibit_begin(int64_t now_ms);

/**
 * 미연결 50ms 주기에서 한 스텝.
 * 출력을 바꾸기 직전에 is_connected 를 본다. 연결됐으면 남은 단계를 내지 않는다.
 */
void rccar_exhibit_tick(int64_t now_ms, rccar_exhibit_is_connected_fn is_connected);

/**
 * 연결로 넘어갈 때 한 번. 이미 비활성이면 출력을 건드리지 않는다.
 * 포탑, 헤드라이트, 레이더, 가습기, 레이저를 끄고 휠을 0으로 둔다.
 */
void rccar_exhibit_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* RCCAR_EXHIBIT_H */
