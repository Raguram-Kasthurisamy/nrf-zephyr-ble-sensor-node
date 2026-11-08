/*
 * Author: Raguram Kasthurisamy
 *
 * Per-thread watchdog. Each critical thread gets its own channel with its own timeout; the
 * hardware watchdog is only fed while every channel is fed, so one stuck thread is enough to
 * reset the chip.
 */
#ifndef NODE_WATCHDOG_H_
#define NODE_WATCHDOG_H_

#include <stdint.h>

int node_watchdog_init(void);
/* Returns a channel id, or a negative error. A missed timeout resets the system. */
int node_watchdog_add(uint32_t timeout_ms);
void node_watchdog_feed(int channel);

#endif /* NODE_WATCHDOG_H_ */
