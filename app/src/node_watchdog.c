/*
 * Author: Raguram Kasthurisamy
 */
#include "node_watchdog.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/task_wdt/task_wdt.h>

int node_watchdog_init(void)
{
	const struct device *hw_wdt = NULL;

#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	hw_wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	if (!device_is_ready(hw_wdt)) {
		hw_wdt = NULL; /* the task watchdog still works on a kernel timer alone */
	}
#endif
	return task_wdt_init(hw_wdt);
}

int node_watchdog_add(uint32_t timeout_ms)
{
	return task_wdt_add(timeout_ms, NULL, NULL); /* no callback: timeout resets the system */
}

void node_watchdog_feed(int channel)
{
	if (channel >= 0) {
		(void)task_wdt_feed(channel);
	}
}
