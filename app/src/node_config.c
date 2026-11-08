/*
 * Author: Raguram Kasthurisamy
 */
#include "node_config.h"

#include <errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

static atomic_t interval_ms = ATOMIC_INIT(CONFIG_APP_SAMPLE_INTERVAL_MS);

static bool interval_valid(uint32_t value)
{
	return value >= NODE_INTERVAL_MIN_MS && value <= NODE_INTERVAL_MAX_MS;
}

/* Called by settings_load() for every stored key under "node/". */
static int node_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	uint32_t value;

	if (settings_name_steq(key, "interval", &next) && next == NULL) {
		if (len != sizeof(value)) {
			return -EINVAL;
		}
		const ssize_t rc = read_cb(cb_arg, &value, sizeof(value));

		if (rc < 0) {
			return (int)rc;
		}
		if (interval_valid(value)) {
			atomic_set(&interval_ms, (atomic_val_t)value);
		}
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(node, "node", NULL, node_settings_set, NULL, NULL);

uint32_t node_config_interval_ms(void)
{
	return (uint32_t)atomic_get(&interval_ms);
}

int node_config_set_interval_ms(uint32_t value)
{
	if (!interval_valid(value)) {
		return -EINVAL;
	}
	atomic_set(&interval_ms, (atomic_val_t)value);
	return settings_save_one("node/interval", &value, sizeof(value));
}
