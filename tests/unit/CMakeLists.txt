# Author: Raguram Kasthurisamy
# Unit tests for the hardware-independent modules, run on native_sim with Twister:
#   west twister -T tests -p native_sim/native/64
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(node_unit_tests)

target_sources(app PRIVATE
  src/main.c
  ../../app/src/telemetry.c
  ../../app/src/app_state.c
)
target_include_directories(app PRIVATE ../../app/src ../../include)
