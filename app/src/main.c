/*
 * Author: Raguram Kasthurisamy
 *
 * BLE sensor node: life cycle and self-test.
 *
 * The main thread owns the application state machine (app_state.c). Callbacks from other
 * threads never change state themselves; they post events to a message queue, and only this
 * thread acts on them. On every boot of a not-yet-confirmed firmware image, the self-test
 * decides whether MCUboot keeps the new image or reverts to the previous one.
 */
#include <app_version.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include <dk_buttons_and_leds.h>

#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#include "app_events.h"
#include "app_state.h"
#include "ble_service.h"
#include "node_watchdog.h"
#include "sensors.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define STATUS_LED          DK_LED1
#define ERROR_LED           DK_LED2
#define LOOP_PERIOD_MS      250
#define IDENTIFY_MS         3000
#define MAIN_WDT_TIMEOUT_MS 3000

K_MSGQ_DEFINE(event_queue, sizeof(enum app_event), 8, 4);

static atomic_t current_state = ATOMIC_INIT(APP_STATE_BOOT);
static uint32_t boot_reset_cause;

void app_post_event(enum app_event event)
{
	if (k_msgq_put(&event_queue, &event, K_NO_WAIT) != 0) {
		LOG_WRN("event queue full, dropped event %d", event);
	}
}

enum app_state app_current_state(void)
{
	return (enum app_state)atomic_get(&current_state);
}

uint32_t app_reset_cause(void)
{
	return boot_reset_cause;
}

static void report_reset_cause(void)
{
	if (hwinfo_get_reset_cause(&boot_reset_cause) != 0) {
		return;
	}
	(void)hwinfo_clear_reset_cause();
	if (boot_reset_cause & RESET_WATCHDOG) {
		LOG_WRN("reset by the watchdog: a thread stopped running");
	} else if (boot_reset_cause & RESET_SOFTWARE) {
		LOG_INF("reset by software");
	} else if (boot_reset_cause & RESET_PIN) {
		LOG_INF("reset by the reset pin");
	} else if (boot_reset_cause & (RESET_POR | RESET_BROWNOUT)) {
		LOG_INF("power-on reset");
	}
}

/* Bluetooth must work for the node to be useful (and to receive the next update). Missing
 * sensors are reported but do not fail the test: a bare DK still boots and can be updated. */
static bool run_self_test(void)
{
	bool passed = true;
	const int err = ble_init();

	if (err != 0) {
		LOG_ERR("Bluetooth initialisation failed (%d)", err);
		passed = false;
	}
	(void)sensors_init();
	if (!sensors_env_ready()) {
		LOG_WRN("environment sensor not found: continuing without it");
	}
	if (!sensors_accel_ready()) {
		LOG_WRN("accelerometer not found: continuing without it");
	}
	return passed;
}

static void confirm_or_revert(bool passed)
{
#ifdef CONFIG_BOOTLOADER_MCUBOOT
	if (boot_is_img_confirmed()) {
		return;
	}
	if (passed) {
		const int err = boot_write_img_confirmed();

		if (err != 0) {
			LOG_ERR("image confirmation failed (%d)", err);
		} else {
			LOG_INF("new firmware passed its self-test and is now confirmed");
		}
		return;
	}
	LOG_ERR("self-test failed on an unconfirmed image: rebooting into the previous one");
	k_sleep(K_MSEC(200));
	sys_reboot(SYS_REBOOT_COLD);
#else
	ARG_UNUSED(passed);
#endif
}

static void enter_state(enum app_state next, enum app_state previous)
{
	atomic_set(&current_state, next);
	LOG_INF("state: %s -> %s", app_state_name(previous), app_state_name(next));

	switch (next) {
	case APP_STATE_SELF_TEST: {
		const bool passed = run_self_test();

		confirm_or_revert(passed);
		app_post_event(passed ? APP_EVENT_SELF_TEST_PASSED : APP_EVENT_SELF_TEST_FAILED);
		break;
	}
	case APP_STATE_ADVERTISING:
		if (previous == APP_STATE_SELF_TEST) {
			sensors_start();
			const int err = ble_start_advertising();

			if (err != 0) {
				LOG_ERR("advertising failed (%d)", err);
				app_post_event(APP_EVENT_FATAL);
			}
		}
		/* After a disconnect, the BLE module restarts advertising once the connection
		 * object is released (see recycled() in ble_service.c). */
		break;
	case APP_STATE_ERROR:
		(void)dk_set_led_on(ERROR_LED);
		break;
	default:
		break;
	}
}

static void update_leds(uint32_t tick, int64_t identify_until)
{
	const enum app_state state = app_current_state();
	const bool identifying = k_uptime_get() < identify_until;

	switch (state) {
	case APP_STATE_ADVERTISING:
		(void)dk_set_led(STATUS_LED, (tick % 4u) == 0u); /* short blink every second */
		break;
	case APP_STATE_CONNECTED:
		(void)dk_set_led_on(STATUS_LED);
		break;
	default:
		(void)dk_set_led_off(STATUS_LED);
		break;
	}
	if (state != APP_STATE_ERROR) {
		(void)dk_set_led(ERROR_LED, identifying && (tick % 2u) == 0u);
	}
}

int main(void)
{
	int64_t identify_until = 0;
	uint32_t tick = 0;

	LOG_INF("BLE sensor node %s", APP_VERSION_STRING);
	report_reset_cause();
	if (dk_leds_init() != 0) {
		LOG_WRN("LEDs unavailable");
	}
	if (node_watchdog_init() != 0) {
		LOG_ERR("watchdog initialisation failed");
	}
	const int wdt_channel = node_watchdog_add(MAIN_WDT_TIMEOUT_MS);

	app_post_event(APP_EVENT_START);

	while (true) {
		enum app_event event;

		if (k_msgq_get(&event_queue, &event, K_MSEC(LOOP_PERIOD_MS)) == 0) {
			if (event == APP_EVENT_IDENTIFY) {
				identify_until = k_uptime_get() + IDENTIFY_MS;
			} else {
				const enum app_state current = app_current_state();
				const enum app_state next = app_state_next(current, event);

				if (next != current) {
					enter_state(next, current);
				}
			}
		}
		update_leds(tick++, identify_until);
		node_watchdog_feed(wdt_channel);
	}
	return 0;
}
