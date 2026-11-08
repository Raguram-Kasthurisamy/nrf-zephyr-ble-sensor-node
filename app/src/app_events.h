/*
 * Author: Raguram Kasthurisamy */
#ifndef APP_EVENTS_H_
#define APP_EVENTS_H_

#include <stdint.h>

#include "app_state.h"

/* Queue an event for the main thread. Safe from any thread or callback (never blocks). */
void app_post_event(enum app_event event);
enum app_state app_current_state(void);
uint32_t app_reset_cause(void);

#endif /* APP_EVENTS_H_ */
