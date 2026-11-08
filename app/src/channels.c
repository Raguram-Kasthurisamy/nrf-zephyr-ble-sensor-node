/*
 * Author: Raguram Kasthurisamy
 */
#include "channels.h"

ZBUS_OBS_DECLARE(ble_telemetry_listener);

ZBUS_CHAN_DEFINE(telemetry_chan, struct node_telemetry, NULL, NULL,
		 ZBUS_OBSERVERS(ble_telemetry_listener), ZBUS_MSG_INIT(0));
