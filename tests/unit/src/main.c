/*
 * Author: Raguram Kasthurisamy
 */
#include <string.h>
#include <zephyr/ztest.h>

#include <rks/lis2dh12_math.h>

#include "app_state.h"
#include "telemetry.h"

/* ---- Telemetry packet ------------------------------------------------------------------- */

ZTEST_SUITE(telemetry, NULL, NULL, NULL, NULL, NULL);

ZTEST(telemetry, test_layout_matches_documentation)
{
	const struct node_telemetry t = {
		.sequence = 0x1234,
		.env = {.temperature_centi_c = -1250, .pressure_pa = 101325,
			.humidity_centi_pct = 4567, .valid = true},
		.motion = {.accel_mg = {10, -20, 1000}, .peak_motion_mg = 300, .valid = true},
	};
	uint8_t p[TELEMETRY_PACKET_SIZE];

	zassert_equal(telemetry_encode(&t, p), TELEMETRY_PACKET_SIZE);
	zassert_equal(p[0], TELEMETRY_PACKET_VERSION);
	zassert_equal(p[1], TELEMETRY_FLAG_ENV_VALID | TELEMETRY_FLAG_MOTION_VALID);
	zassert_equal(p[2], 0x34);
	zassert_equal(p[3], 0x12);
	zassert_equal((int16_t)(p[4] | (p[5] << 8)), -1250);
	zassert_equal(p[6] | (p[7] << 8), 4567);
	zassert_equal((uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) |
			      ((uint32_t)p[11] << 24), 101325u);
	zassert_equal((int16_t)(p[14] | (p[15] << 8)), -20);
	zassert_equal(p[18] | (p[19] << 8), 300);
}

ZTEST(telemetry, test_invalid_readings_are_zero_and_flagged)
{
	const struct node_telemetry t = {.sequence = 1};
	uint8_t p[TELEMETRY_PACKET_SIZE];
	uint8_t zeros[TELEMETRY_PACKET_SIZE - 4] = {0};

	(void)telemetry_encode(&t, p);
	zassert_equal(p[1], 0);
	zassert_mem_equal(&p[4], zeros, sizeof(zeros));
}

ZTEST(telemetry, test_values_saturate_instead_of_wrapping)
{
	const struct node_telemetry t = {
		.env = {.temperature_centi_c = 100000, .humidity_centi_pct = 70000, .valid = true},
		.motion = {.accel_mg = {-40000, 0, 0}, .peak_motion_mg = 99999, .valid = true},
	};
	uint8_t p[TELEMETRY_PACKET_SIZE];

	(void)telemetry_encode(&t, p);
	zassert_equal((int16_t)(p[4] | (p[5] << 8)), INT16_MAX);
	zassert_equal(p[6] | (p[7] << 8), UINT16_MAX);
	zassert_equal((int16_t)(p[12] | (p[13] << 8)), INT16_MIN);
	zassert_equal(p[18] | (p[19] << 8), UINT16_MAX);
}

ZTEST(telemetry, test_motion_level_is_zero_at_rest_in_any_orientation)
{
	zassert_equal(telemetry_motion_level_mg(0, 0, 1000), 0);
	zassert_equal(telemetry_motion_level_mg(1000, 0, 0), 0);
	zassert_equal(telemetry_motion_level_mg(0, 0, -1000), 0);
	zassert_equal(telemetry_motion_level_mg(0, 0, 1500), 500);
	zassert_equal(telemetry_motion_level_mg(0, 0, 0), 1000); /* free fall */
}

ZTEST(telemetry, test_integer_square_root)
{
	zassert_equal(telemetry_isqrt(0), 0);
	zassert_equal(telemetry_isqrt(1), 1);
	zassert_equal(telemetry_isqrt(15), 3);
	zassert_equal(telemetry_isqrt(16), 4);
	zassert_equal(telemetry_isqrt(1000000), 1000);
	zassert_equal(telemetry_isqrt(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFu);
}

/* ---- Application state machine ------------------------------------------------------------ */

ZTEST_SUITE(app_state, NULL, NULL, NULL, NULL, NULL);

ZTEST(app_state, test_normal_life_cycle)
{
	enum app_state s = APP_STATE_BOOT;

	s = app_state_next(s, APP_EVENT_START);
	zassert_equal(s, APP_STATE_SELF_TEST);
	s = app_state_next(s, APP_EVENT_SELF_TEST_PASSED);
	zassert_equal(s, APP_STATE_ADVERTISING);
	s = app_state_next(s, APP_EVENT_CONNECTED);
	zassert_equal(s, APP_STATE_CONNECTED);
	s = app_state_next(s, APP_EVENT_DISCONNECTED);
	zassert_equal(s, APP_STATE_ADVERTISING);
}

ZTEST(app_state, test_failed_self_test_and_fatal_events_lead_to_error)
{
	zassert_equal(app_state_next(APP_STATE_SELF_TEST, APP_EVENT_SELF_TEST_FAILED),
		      APP_STATE_ERROR);
	zassert_equal(app_state_next(APP_STATE_CONNECTED, APP_EVENT_FATAL), APP_STATE_ERROR);
	zassert_equal(app_state_next(APP_STATE_ERROR, APP_EVENT_START), APP_STATE_ERROR);
}

ZTEST(app_state, test_irrelevant_events_are_ignored)
{
	zassert_equal(app_state_next(APP_STATE_BOOT, APP_EVENT_CONNECTED), APP_STATE_BOOT);
	zassert_equal(app_state_next(APP_STATE_ADVERTISING, APP_EVENT_DISCONNECTED),
		      APP_STATE_ADVERTISING);
	zassert_equal(app_state_next(APP_STATE_CONNECTED, APP_EVENT_IDENTIFY), APP_STATE_CONNECTED);
}

/* ---- LIS2DH12 conversions -------------------------------------------------------------------- */

ZTEST_SUITE(lis2dh12_math, NULL, NULL, NULL, NULL, NULL);

ZTEST(lis2dh12_math, test_raw_to_milli_g)
{
	zassert_equal(lis2dh12_raw_to_mg(16), 1);
	zassert_equal(lis2dh12_raw_to_mg(16384), 1024);
	zassert_equal(lis2dh12_raw_to_mg(-16384), -1024);
	zassert_equal(lis2dh12_raw_to_mg(-16), -1);
	zassert_equal(lis2dh12_raw_to_ug(16), 1000);
}

ZTEST(lis2dh12_math, test_output_data_rate_bits)
{
	zassert_equal(lis2dh12_odr_bits(1), 0x1);
	zassert_equal(lis2dh12_odr_bits(25), 0x3);
	zassert_equal(lis2dh12_odr_bits(400), 0x7);
	zassert_equal(lis2dh12_odr_bits(123), 0x2); /* unsupported: 10 Hz */
}
