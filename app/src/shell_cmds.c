/*
 * Author: Raguram Kasthurisamy
 *
 * "node" shell commands for development and debugging.
 */
#include <app_version.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#include "app_events.h"
#include "ble_service.h"
#include "channels.h"
#include "node_config.h"
#include "sensors.h"

static void print_centi(const struct shell *sh, const char *label, int32_t value, const char *unit)
{
	const char *sign = value < 0 ? "-" : "";
	const int32_t magnitude = value < 0 ? -value : value;

	shell_print(sh, "%s%s%d.%02d %s", label, sign, magnitude / 100, magnitude % 100, unit);
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct node_telemetry t;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "state:       %s%s", app_state_name(app_current_state()),
		    ble_is_connected() ? " (phone connected)" : "");
	shell_print(sh, "uptime:      %u s", k_uptime_get_32() / 1000u);
	shell_print(sh, "interval:    %u ms", node_config_interval_ms());
	shell_print(sh, "sensors:     environment %s, accelerometer %s",
		    sensors_env_ready() ? "ok" : "missing", sensors_accel_ready() ? "ok" : "missing");

	if (zbus_chan_read(&telemetry_chan, &t, K_MSEC(100)) != 0 || t.sequence == 0) {
		shell_print(sh, "no sample yet");
		return 0;
	}
	shell_print(sh, "sample:      #%u at %u ms", t.sequence, t.uptime_ms);
	if (t.env.valid) {
		print_centi(sh, "temperature: ", t.env.temperature_centi_c, "C");
		print_centi(sh, "humidity:    ", (int32_t)t.env.humidity_centi_pct, "%RH");
		shell_print(sh, "pressure:    %u Pa", t.env.pressure_pa);
	}
	if (t.motion.valid) {
		shell_print(sh, "accel:       x %d  y %d  z %d mg   peak motion %d mg",
			    t.motion.accel_mg[0], t.motion.accel_mg[1], t.motion.accel_mg[2],
			    t.motion.peak_motion_mg);
	}
	return 0;
}

static int cmd_interval(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_print(sh, "%u ms", node_config_interval_ms());
		return 0;
	}
	char *end = NULL;
	const unsigned long value = strtoul(argv[1], &end, 10);

	if (end == argv[1] || *end != '\0' || node_config_set_interval_ms((uint32_t)value) != 0) {
		shell_error(sh, "interval must be %u to %u ms", NODE_INTERVAL_MIN_MS,
			    NODE_INTERVAL_MAX_MS);
		return -EINVAL;
	}
	shell_print(sh, "interval set to %lu ms (saved)", value);
	return 0;
}

static int cmd_sample(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	sensors_request_sample();
	shell_print(sh, "sample requested");
	return 0;
}

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "firmware %s", APP_VERSION_STRING);
#ifdef CONFIG_BOOTLOADER_MCUBOOT
	shell_print(sh, "image %s", boot_is_img_confirmed() ? "confirmed" : "NOT confirmed (test boot)");
#endif
	return 0;
}

static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(node_commands,
	SHELL_CMD(status, NULL, "State, sensors and the latest sample", cmd_status),
	SHELL_CMD_ARG(interval, NULL, "Show or set the reporting interval: interval [ms]",
		      cmd_interval, 1, 1),
	SHELL_CMD(sample, NULL, "Sample and report now", cmd_sample),
	SHELL_CMD(version, NULL, "Firmware version and image state", cmd_version),
	SHELL_CMD(reboot, NULL, "Reboot the node", cmd_reboot),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(node, &node_commands, "Sensor node commands", NULL);
