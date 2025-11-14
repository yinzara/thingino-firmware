/*
 * ESPHome Native API Client
 *
 * Implementation using protobuf-c for BLE Proxy
 */

#ifndef ESPHOME_API_H
#define ESPHOME_API_H

#include <stdint.h>
#include <stddef.h>

#define ESPHOME_ADDR_LEN	64
#define ESPHOME_NAME_LEN	64

/* Message types */
#define MSG_HELLO_REQUEST			1
#define MSG_HELLO_RESPONSE			2
#define MSG_CONNECT_REQUEST			3
#define MSG_CONNECT_RESPONSE			4
#define MSG_DISCONNECT_REQUEST			5
#define MSG_PING_REQUEST			7
#define MSG_PING_RESPONSE			8
#define MSG_SUBSCRIBE_BLE_ADVERTISEMENTS	66
#define MSG_BLE_ADVERTISEMENT			67

struct esphome_client;

/* BLE advertisement structure */
struct esphome_ble_adv {
	uint64_t address;		/* BLE address as uint64 */
	char address_str[18];		/* BLE address as string */
	int8_t rssi;
	const uint8_t *adv_data;
	size_t adv_data_len;
	const char *name;
};

/* Create client instance */
struct esphome_client *esphome_client_create(const char *host, uint16_t port,
					      const char *device_name);

/* Destroy client instance */
void esphome_client_destroy(struct esphome_client *client);

/* Connect to Home Assistant */
int esphome_client_connect(struct esphome_client *client);

/* Disconnect from Home Assistant */
void esphome_client_disconnect(struct esphome_client *client);

/* Send BLE advertisement */
int esphome_client_send_ble_adv(struct esphome_client *client,
				const struct esphome_ble_adv *adv);

/* Send ping (keepalive) */
int esphome_client_ping(struct esphome_client *client);

/* Check if connected */
int esphome_client_is_connected(struct esphome_client *client);

#endif /* ESPHOME_API_H */
