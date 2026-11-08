/*
 * Author: Raguram Kasthurisamy
 *
 * Sampling. The accelerometer streams at its own data rate through the data-ready trigger;
 * each sample updates the latest reading and the peak motion level. Once per reporting
 * interval the sensor thread reads the environment sensor and publishes one telemetry
 * message on zbus. Fast event-driven sampling, slow reporting: the phone sees a short jolt
 * even when it falls between two reports.
 */
#include "sensors.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "channels.h"
#include "node_config.h"
#include "node_watchdog.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

#define ENV_NODE   DT_ALIAS(env_sensor)
#define ACCEL_NODE DT_ALIAS(accel_sensor)

#if DT_NODE_HAS_STATUS(ENV_NODE, okay)
static const struct device *const env_dev = DEVICE_DT_GET(ENV_NODE);
#else
static const struct device *const env_dev;
#endif

#if DT_NODE_HAS_STATUS(ACCEL_NODE, okay)
static const struct device *const accel_dev = DEVICE_DT_GET(ACCEL_NODE);
#else
static const struct device *const accel_dev;
#endif

#define SENSOR_THREAD_STACK    2048
#define SENSOR_THREAD_PRIORITY 7

static K_SEM_DEFINE(sample_now, 0, 1);
static struct k_spinlock motion_lock;
static struct motion_reading latest_motion; /* guarded by motion_lock */
static int32_t peak_motion_mg;              /* guarded by motion_lock */
static bool env_ok;
static bool accel_ok;
static bool trigger_active;

static int32_t ms2_to_mg(const struct sensor_value *value)
{
	const int64_t micro_ms2 = (int64_t)value->val1 * 1000000 + value->val2;

	return (int32_t)(micro_ms2 * 1000 / 9806650); /* 1 g = 9.80665 m/s^2 */
}

static void read_accel(void)
{
	struct sensor_value xyz[3];
	struct motion_reading reading = {.valid = true};

	if (sensor_sample_fetch(accel_dev) < 0 ||
	    sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_XYZ, xyz) < 0) {
		return;
	}
	for (int axis = 0; axis < 3; axis++) {
		reading.accel_mg[axis] = ms2_to_mg(&xyz[axis]);
	}
	const int32_t level = telemetry_motion_level_mg(reading.accel_mg[0], reading.accel_mg[1],
							reading.accel_mg[2]);
	k_spinlock_key_t key = k_spin_lock(&motion_lock);

	latest_motion = reading;
	if (level > peak_motion_mg) {
		peak_motion_mg = level;
	}
	k_spin_unlock(&motion_lock, key);
}

/* Runs in the system work queue (see the driver), at the accelerometer's data rate. */
static void accel_data_ready(const struct device *dev, const struct sensor_trigger *trigger)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trigger);
	read_accel();
}

static int32_t centi(const struct sensor_value *value)
{
	return value->val1 * 100 + value->val2 / 10000;
}

static struct env_reading read_env(void)
{
	struct env_reading reading = {0};
	struct sensor_value temperature, pressure, humidity;

	if (sensor_sample_fetch(env_dev) < 0 ||
	    sensor_channel_get(env_dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature) < 0 ||
	    sensor_channel_get(env_dev, SENSOR_CHAN_PRESS, &pressure) < 0 ||
	    sensor_channel_get(env_dev, SENSOR_CHAN_HUMIDITY, &humidity) < 0) {
		return reading;
	}
	reading.temperature_centi_c = centi(&temperature);
	reading.humidity_centi_pct = (uint32_t)centi(&humidity);
	/* Zephyr reports pressure in kPa. */
	reading.pressure_pa = (uint32_t)(pressure.val1 * 1000 + pressure.val2 / 1000);
	reading.valid = true;
	return reading;
}

static void sensor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Longer than the longest reporting interval, so a slow interval is never a "hang". */
	const int wdt_channel = node_watchdog_add(2u * NODE_INTERVAL_MAX_MS + 1000u);
	uint32_t sequence = 0;

	while (true) {
		(void)k_sem_take(&sample_now, K_MSEC(node_config_interval_ms()));

		struct node_telemetry telemetry = {0};

		telemetry.sequence = ++sequence;
		telemetry.uptime_ms = k_uptime_get_32();
		if (env_ok) {
			telemetry.env = read_env();
		}
		if (accel_ok) {
			if (!trigger_active) {
				read_accel(); /* no INT1 wired: poll instead */
			}
			k_spinlock_key_t key = k_spin_lock(&motion_lock);

			telemetry.motion = latest_motion;
			telemetry.motion.peak_motion_mg = peak_motion_mg;
			peak_motion_mg = 0;
			k_spin_unlock(&motion_lock, key);
		}
		if (zbus_chan_pub(&telemetry_chan, &telemetry, K_MSEC(100)) != 0) {
			LOG_WRN("telemetry publish timed out");
		}
		node_watchdog_feed(wdt_channel);
	}
}

K_THREAD_DEFINE(sensor_tid, SENSOR_THREAD_STACK, sensor_thread, NULL, NULL, NULL,
		SENSOR_THREAD_PRIORITY, 0, SYS_FOREVER_MS);

int sensors_init(void)
{
	static const struct sensor_trigger data_ready = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ACCEL_XYZ,
	};

	env_ok = env_dev != NULL && device_is_ready(env_dev);
	accel_ok = accel_dev != NULL && device_is_ready(accel_dev);
	if (accel_ok) {
		trigger_active = sensor_trigger_set(accel_dev, &data_ready, accel_data_ready) == 0;
	}
	LOG_INF("environment sensor: %s, accelerometer: %s%s", env_ok ? "ok" : "missing",
		accel_ok ? "ok" : "missing", trigger_active ? " (data-ready trigger)" : "");
	return 0;
}

void sensors_start(void)
{
	static bool started;

	if (!started) {
		started = true;
		k_thread_start(sensor_tid);
	}
}

void sensors_request_sample(void)
{
	k_sem_give(&sample_now);
}

bool sensors_env_ready(void)
{
	return env_ok;
}

bool sensors_accel_ready(void)
{
	return accel_ok;
}
