# Low-power BLE sensor node with secure OTA: nRF7002 DK (nRF5340) + Zephyr

Author: Raguram Kasthurisamy

A Bluetooth LE sensor node on the nRF Connect SDK (Zephyr RTOS). It reads an environmental
sensor through Zephyr's upstream driver, and an accelerometer through a driver written for
this project. It streams telemetry to a phone over a custom GATT service protected by LE
Secure Connections pairing, and accepts signed firmware updates over the air, which roll back
on their own if the new firmware fails its self-test.

## What it does

- **Own sensor driver.** A LIS2DH12 / LIS3DH SPI driver as an out-of-tree Zephyr module, with
  a devicetree binding, Kconfig, the sensor API, and a data-ready interrupt trigger.
- **Event-driven sampling, slow reporting.**
  - The accelerometer streams at 25 Hz through its interrupt, and the node keeps the peak
    motion level.
  - Once per interval (100 ms to 10 s, default 1 s) it reads the BME280 and publishes one
    telemetry message.
- **Decoupled modules.** Sensors publish on a zbus channel. The BLE module observes it, and
  neither knows the other exists.
- **Secure BLE.** One custom service:
  - Telemetry is a read + notify characteristic; reading or subscribing needs an encrypted
    link.
  - Commands and settings need an authenticated (passkey) pairing.
  - Only LE Secure Connections is allowed, and bonds are kept in flash.
- **Signed OTA with rollback.**
  - MCUboot verifies each image's signature (ECDSA P-256, your own key).
  - Updates arrive over BLE through MCUmgr.
  - A new image confirms itself only after its self-test passes; otherwise the next reset
    restores the previous firmware.
- **Robustness.**
  - A per-thread task watchdog, backed by the hardware watchdog.
  - Reset-cause reporting.
  - An explicit, unit-tested application state machine.
- **Low power.** A dedicated build variant without UART, shell or logging, with slow
  advertising, for current measurements.

## Threads and execution contexts

| Context | Priority | Runs | Job |
|---|---|---|---|
| main | default (0) | event queue, 250 ms tick | state machine, self-test, image confirm, LEDs |
| sensor thread | 7 | every reporting interval, or on request | read BME280, collect motion, publish on zbus |
| system work queue | -1 (cooperative) | on demand | accelerometer data-ready handler, BLE notifications, advertising restart |
| Bluetooth host threads | stack-defined | on radio events | GATT, pairing, MCUmgr SMP |
| shell | stack-defined | on console input | `node` commands |
| GPIO interrupt | ISR | accelerometer INT1 edge | submit work only (SPI is not used in ISR context) |

## Hardware

- nRF7002 DK (nRF5340 host; Wi-Fi chip unused). One devicetree overlay serves both.
- BME280 breakout on the Arduino I2C pins (address 0x76), 3.3 V.
- LIS2DH12 or LIS3DH breakout on the Arduino SPI pins: SCK D13, MISO D12, MOSI D11, CS D10,
  INT1 to D2, 3.3 V.
- Optional: a Power Profiler Kit II or a multimeter on the DK's current measurement header.
- Without sensors the node still boots, advertises, and accepts updates. The self-test treats
  missing sensors as degraded, not failed.

## Getting started

Install the nRF Connect SDK toolchain (nRF Connect for VS Code, or `nrfutil toolchain-manager`), then:

```bash
west init -m https://github.com/<you>/ble-sensor-node --mr main ncs-workspace
cd ncs-workspace && west update
cd ble-sensor-node

west build -b -b nrf7002dk/nrf5340/cpuapp --sysbuild          
west flash
```

Open the DK's serial port at 115200 baud: logs and the `node` shell appear there.

**Your own signing key:** run `scripts/gen_signing_key.sh`, then add
`-- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$PWD/keys/signing-key.pem\"` to the build command.
Never commit the key.

**Low-power build:** `west build -b nrf7002dk/nRF5340 app --sysbuild -- -DEXTRA_CONF_FILE=overlay-low-power.conf`

**Unit tests:** `west twister -T tests -p native_sim/native/64`

## Using it

1. Connect with nRF Connect for Mobile and find "RKS Sensor Node".
2. Subscribe to the telemetry characteristic. The phone asks to pair, and the node prints a
   six-digit passkey on the serial log. Type it on the phone.
3. Telemetry arrives once per interval as a 20-byte packet (layout in `app/src/telemetry.h`).
   Write `0x01` to the command characteristic to sample now, or `0x02` to blink the LEDs.
4. **Firmware update:**
   - Bump `app/VERSION` and rebuild.
   - Send `build/app/zephyr/zephyr.signed.bin` with nRF Connect Device Manager in
     "test and confirm" mode.
   - The node reboots into the new image, which runs its self-test and confirms itself.

| GATT characteristic | UUID (base 9a3bxxxx-6c0e-4b6f-8e8a-2f5d1c7b4e10) | Access |
|---|---|---|
| Service | 9a3b0001 | |
| Telemetry | 9a3b0002 | read, notify (encrypted) |
| Command | 9a3b0003 | write (authenticated): 0x01 sample now, 0x02 identify |
| Interval | 9a3b0004 | read (encrypted), write (authenticated): uint16 ms, 100 to 10000 |

Shell: `node status`, `node interval [ms]`, `node sample`, `node version`, `node reboot`.

## Repository layout

```
west.yml                 workspace manifest, pins the nRF Connect SDK
zephyr/module.yml        this repo is also a Zephyr module (driver + binding)
drivers/sensor/lis2dh12  the accelerometer driver        dts/bindings  its devicetree binding
include/rks/             pure conversion helpers shared by the driver and the tests
app/                     the application: src/, prj.conf, sysbuild.conf, app.overlay, VERSION
tests/unit/              ztest suites for the packet format, state machine and conversions
scripts/                 signing-key helper            
```

## Dependencies

nRF Connect SDK v3.3.0 (Zephyr, MCUboot, the SoftDevice Controller), pulled by `west.yml`.
