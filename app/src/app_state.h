/*
 * Author: Raguram Kasthurisamy
 *
 * Application life cycle as an explicit state machine. The transition function is pure, so
 * every path is unit tested; main.c performs the actions on entering each state.
 */
#ifndef APP_STATE_H_
#define APP_STATE_H_

enum app_state {
	APP_STATE_BOOT,
	APP_STATE_SELF_TEST,
	APP_STATE_ADVERTISING,
	APP_STATE_CONNECTED,
	APP_STATE_ERROR,
};

enum app_event {
	APP_EVENT_START,
	APP_EVENT_SELF_TEST_PASSED,
	APP_EVENT_SELF_TEST_FAILED,
	APP_EVENT_CONNECTED,
	APP_EVENT_DISCONNECTED,
	APP_EVENT_FATAL,
	APP_EVENT_IDENTIFY, /* blink the LEDs; no state change */
};

enum app_state app_state_next(enum app_state current, enum app_event event);
const char *app_state_name(enum app_state state);

#endif /* APP_STATE_H_ */
