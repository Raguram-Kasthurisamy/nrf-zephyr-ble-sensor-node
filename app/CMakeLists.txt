# Author: Raguram Kasthurisamy
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_sensor_node)

target_sources(app PRIVATE
  src/main.c
  src/app_state.c
  src/ble_service.c
  src/channels.c
  src/node_config.c
  src/node_watchdog.c
  src/sensors.c
  src/telemetry.c
)
# The low-power build drops the shell entirely (overlay-low-power.conf).
target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell_cmds.c)
