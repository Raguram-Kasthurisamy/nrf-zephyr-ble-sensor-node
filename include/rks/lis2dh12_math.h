/*
 * Author: Raguram Kasthurisamy
 *
 * Pure conversion helpers for the LIS2DH12 driver. No Zephyr dependencies, so the unit tests
 * check them directly.
 */
#ifndef RKS_LIS2DH12_MATH_H_
#define RKS_LIS2DH12_MATH_H_

#include <stdint.h>

/*
 * High-resolution mode at +/-2 g: 12-bit samples, left-justified in a 16-bit register pair,
 * 1 mg per least-significant bit. The low 4 bits are always zero, so dividing by 16 is exact
 * (unlike a right shift of a negative number, whose result C leaves implementation-defined).
 */
static inline int32_t lis2dh12_raw_to_mg(int16_t raw)
{
	return (int32_t)raw / 16;
}

static inline int32_t lis2dh12_raw_to_ug(int16_t raw)
{
	return lis2dh12_raw_to_mg(raw) * 1000;
}

/* CTRL_REG1 ODR[3:0] field for a rate in Hz; unsupported rates fall back to 10 Hz. */
static inline uint8_t lis2dh12_odr_bits(uint16_t odr_hz)
{
	switch (odr_hz) {
	case 1: return 0x1;
	case 10: return 0x2;
	case 25: return 0x3;
	case 50: return 0x4;
	case 100: return 0x5;
	case 200: return 0x6;
	case 400: return 0x7;
	default: return 0x2;
	}
}

#endif /* RKS_LIS2DH12_MATH_H_ */
