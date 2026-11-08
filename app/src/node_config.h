/*
 * Author: Raguram Kasthurisamy
 *
 * Run-time configuration kept in flash through Zephyr's settings subsystem.
 */
#ifndef NODE_CONFIG_H_
#define NODE_CONFIG_H_

#include <stdint.h>

#define NODE_INTERVAL_MIN_MS 100u
#define NODE_INTERVAL_MAX_MS 10000u

uint32_t node_config_interval_ms(void);
/* Validates, applies and persists. Returns -EINVAL outside the allowed range. */
int node_config_set_interval_ms(uint32_t interval_ms);

#endif /* NODE_CONFIG_H_ */
