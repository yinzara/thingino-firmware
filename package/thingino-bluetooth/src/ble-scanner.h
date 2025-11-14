/*
 * BLE Scanner API
 */

#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <stdint.h>

#define BLE_ADDR_LEN	18
#define BLE_NAME_LEN	64
#define BLE_PATH_LEN	128

struct ble_device {
	char address[BLE_ADDR_LEN];
	char name[BLE_NAME_LEN];
	char path[BLE_PATH_LEN];
	int16_t rssi;
	uint8_t has_rssi;
	uint8_t has_mfg_data;
	uint8_t mfg_data[256];
	size_t mfg_data_len;
};

struct ble_scanner;

typedef void (*ble_device_callback)(const struct ble_device *device, void *user_data);

/* Create scanner instance */
struct ble_scanner *ble_scanner_create(const char *adapter);

/* Destroy scanner instance */
void ble_scanner_destroy(struct ble_scanner *scanner);

/* Start scanning */
int ble_scanner_start(struct ble_scanner *scanner,
		      ble_device_callback callback,
		      void *user_data);

/* Stop scanning */
void ble_scanner_stop(struct ble_scanner *scanner);

/* Run event loop */
void ble_scanner_run(struct ble_scanner *scanner);

/* Quit event loop */
void ble_scanner_quit(struct ble_scanner *scanner);

#endif /* BLE_SCANNER_H */
