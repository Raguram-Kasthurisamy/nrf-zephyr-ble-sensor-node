/*
 * Author: Raguram Kasthurisamy
 */
#ifndef BLE_SERVICE_H_
#define BLE_SERVICE_H_

#include <stdbool.h>

/* Registers pairing callbacks, enables Bluetooth and loads bonds and settings from flash. */
int ble_init(void);
int ble_start_advertising(void);
bool ble_is_connected(void);

#endif /* BLE_SERVICE_H_ */
