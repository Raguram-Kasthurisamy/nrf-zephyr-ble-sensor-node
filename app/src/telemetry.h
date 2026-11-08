/*
 * Author: Raguram Kasthurisamy
 *
 * Sensor data types and the 20-byte BLE telemetry packet. Pure C with no Zephyr
 * dependencies, so the unit tests exercise it directly.
 */
#ifndef TELEMETRY_H_
#define TELEMETRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct env_reading {
	int32_t temperature_centi_c;
	uint32_t pressure_pa;
	uint32_t humidity_centi_pct;
	bool valid;
};

struct motion_reading {
	int32_t accel_mg[3];
	int32_t peak_motion_mg; /* largest deviation from 1 g since the previous report */
	bool valid;
};

struct node_telemetry {
	uint32_t sequence;
	uint32_t uptime_ms;
	struct env_reading env;
	struct motion_reading motion;
};

/*
 * Packet layout, little-endian, fits a default 23-byte ATT MTU (20-byte payload):
 *   0      version            1      flags (bit 0 env valid, bit 1 motion valid)
 *   2-3    sequence (u16)     4-5    temperature, 0.01 degC (i16)
 *   6-7    humidity, 0.01 %RH (u16)    8-11  pressure, Pa (u32)
 *   12-17  acceleration x, y, z, mg (i16 each)
 *   18-19  peak motion since last report, mg (u16)
 */
#define TELEMETRY_PACKET_VERSION    1
#define TELEMETRY_PACKET_SIZE       20
#define TELEMETRY_FLAG_ENV_VALID    (1u << 0)
#define TELEMETRY_FLAG_MOTION_VALID (1u << 1)

size_t telemetry_encode(const struct node_telemetry *telemetry, uint8_t out[TELEMETRY_PACKET_SIZE]);

/* | |a| - 1 g | in mg: 0 for a node at rest in any orientation. */
int32_t telemetry_motion_level_mg(int32_t x_mg, int32_t y_mg, int32_t z_mg);

uint32_t telemetry_isqrt(uint64_t value);

#endif /* TELEMETRY_H_ */
