/*
 * Author: Raguram Kasthurisamy
 */
#include "app_state.h"

enum app_state app_state_next(enum app_state current, enum app_event event)
{
	if (event == APP_EVENT_FATAL) {
		return APP_STATE_ERROR;
	}
	switch (current) {
	case APP_STATE_BOOT:
		return event == APP_EVENT_START ? APP_STATE_SELF_TEST : current;
	case APP_STATE_SELF_TEST:
		if (event == APP_EVENT_SELF_TEST_PASSED) {
			return APP_STATE_ADVERTISING;
		}
		return event == APP_EVENT_SELF_TEST_FAILED ? APP_STATE_ERROR : current;
	case APP_STATE_ADVERTISING:
		return event == APP_EVENT_CONNECTED ? APP_STATE_CONNECTED : current;
	case APP_STATE_CONNECTED:
		return event == APP_EVENT_DISCONNECTED ? APP_STATE_ADVERTISING : current;
	case APP_STATE_ERROR:
		return current; /* only a reset leaves the error state */
	}
	return current;
}

const char *app_state_name(enum app_state state)
{
	switch (state) {
	case APP_STATE_BOOT: return "boot";
	case APP_STATE_SELF_TEST: return "self-test";
	case APP_STATE_ADVERTISING: return "advertising";
	case APP_STATE_CONNECTED: return "connected";
	case APP_STATE_ERROR: return "error";
	}
	return "unknown";
}
