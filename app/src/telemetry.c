/*
 * Author: Raguram Kasthurisamy
 */
#include "telemetry.h"

static void put_le16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xFFu);
	out[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *out, uint32_t value)
{
	put_le16(out, (uint16_t)(value & 0xFFFFu));
	put_le16(out + 2, (uint16_t)(value >> 16));
}

static int16_t clamp_i16(int32_t value)
{
	if (value > INT16_MAX) {
		return INT16_MAX;
	}
	if (value < INT16_MIN) {
		return INT16_MIN;
	}
	return (int16_t)value;
}

static uint16_t clamp_u16(int64_t value)
{
	if (value < 0) {
		return 0;
	}
	if (value > UINT16_MAX) {
		return UINT16_MAX;
	}
	return (uint16_t)value;
}

size_t telemetry_encode(const struct node_telemetry *t, uint8_t out[TELEMETRY_PACKET_SIZE])
{
	uint8_t flags = 0;

	for (size_t i = 0; i < TELEMETRY_PACKET_SIZE; i++) {
		out[i] = 0;
	}
	if (t->env.valid) {
		flags |= TELEMETRY_FLAG_ENV_VALID;
	}
	if (t->motion.valid) {
		flags |= TELEMETRY_FLAG_MOTION_VALID;
	}

	out[0] = TELEMETRY_PACKET_VERSION;
	out[1] = flags;
	put_le16(&out[2], (uint16_t)(t->sequence & 0xFFFFu));
	if (t->env.valid) {
		put_le16(&out[4], (uint16_t)clamp_i16(t->env.temperature_centi_c));
		put_le16(&out[6], clamp_u16(t->env.humidity_centi_pct));
		put_le32(&out[8], t->env.pressure_pa);
	}
	if (t->motion.valid) {
		for (size_t axis = 0; axis < 3; axis++) {
			put_le16(&out[12 + 2 * axis], (uint16_t)clamp_i16(t->motion.accel_mg[axis]));
		}
		put_le16(&out[18], clamp_u16(t->motion.peak_motion_mg));
	}
	return TELEMETRY_PACKET_SIZE;
}

uint32_t telemetry_isqrt(uint64_t value)
{
	uint64_t result = 0;
	uint64_t bit = (uint64_t)1 << 62;

	while (bit > value) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)result;
}

int32_t telemetry_motion_level_mg(int32_t x_mg, int32_t y_mg, int32_t z_mg)
{
	const uint64_t sum = (uint64_t)((int64_t)x_mg * x_mg) + (uint64_t)((int64_t)y_mg * y_mg) +
			     (uint64_t)((int64_t)z_mg * z_mg);
	const int32_t magnitude = (int32_t)telemetry_isqrt(sum);
	const int32_t deviation = magnitude - 1000;

	return deviation < 0 ? -deviation : deviation;
}
