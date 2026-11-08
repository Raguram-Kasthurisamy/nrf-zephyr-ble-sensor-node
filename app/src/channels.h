/*
 * Author: Raguram Kasthurisamy
 *
 * zbus channels: the only way modules exchange data. The sensor module publishes, BLE (and
 * the shell) observe; neither knows the other exists.
 */
#ifndef CHANNELS_H_
#define CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include "telemetry.h"

ZBUS_CHAN_DECLARE(telemetry_chan);

#endif /* CHANNELS_H_ */
