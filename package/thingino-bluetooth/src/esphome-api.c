/*
 * ESPHome Native API Client
 *
 * Manual protobuf implementation (no protobuf-c dependency to save ~2MB)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "esphome-api.h"
#include "esphome-proto-simple.h"

#define PROTO_HEADER_SIZE	3
#define MAX_MESSAGE_SIZE	4096

struct esphome_client {
	int sockfd;
	char host[ESPHOME_ADDR_LEN];
	uint16_t port;
	char device_name[ESPHOME_NAME_LEN];
	int connected;
};

/* Convert MAC address string to uint64 */
static uint64_t mac_to_uint64(const char *mac)
{
	unsigned int bytes[6];
	uint64_t result = 0;

	if (sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
		   &bytes[0], &bytes[1], &bytes[2],
		   &bytes[3], &bytes[4], &bytes[5]) != 6) {
		return 0;
	}

	for (int i = 0; i < 6; i++) {
		result = (result << 8) | bytes[i];
	}

	return result;
}

/* Send message with ESPHome protocol header */
static int send_message(struct esphome_client *client, uint16_t msg_type,
			 const uint8_t *data, size_t data_len)
{
	uint8_t header[PROTO_HEADER_SIZE];
	uint8_t type_buf[5];
	int type_len;
	ssize_t sent;

	/* Encode message type as varint */
	type_len = encode_varint(type_buf, msg_type);

	/* Total length is type_len + data_len */
	size_t total_len = type_len + data_len;

	/* Construct header: 0x00 <len_hi> <len_lo> */
	header[0] = 0x00;
	header[1] = (total_len >> 8) & 0xFF;
	header[2] = total_len & 0xFF;

	/* Send header */
	sent = send(client->sockfd, header, PROTO_HEADER_SIZE, 0);
	if (sent != PROTO_HEADER_SIZE)
		return -1;

	/* Send message type varint */
	sent = send(client->sockfd, type_buf, type_len, 0);
	if (sent != type_len)
		return -1;

	/* Send message data */
	if (data_len > 0) {
		sent = send(client->sockfd, data, data_len, 0);
		if (sent != (ssize_t)data_len)
			return -1;
	}

	return 0;
}

/* Receive message header and type (we don't need to parse responses for BLE proxy) */
static int recv_message_header(struct esphome_client *client, uint16_t *msg_type_out)
{
	uint8_t header[PROTO_HEADER_SIZE];
	uint8_t buf[MAX_MESSAGE_SIZE];
	ssize_t received;
	uint16_t total_len;
	uint16_t msg_type = 0;

	/* Read header */
	received = recv(client->sockfd, header, PROTO_HEADER_SIZE, 0);
	if (received != PROTO_HEADER_SIZE)
		return -1;

	/* Get total message length (type + data) */
	total_len = (header[1] << 8) | header[2];
	if (total_len == 0 || total_len >= MAX_MESSAGE_SIZE)
		return -1;

	/* Read message type + data */
	received = recv(client->sockfd, buf, total_len, 0);
	if (received != total_len)
		return -1;

	/* Decode message type varint */
	uint64_t value = 0;
	int shift = 0;
	for (int i = 0; i < received; i++) {
		value |= ((uint64_t)(buf[i] & 0x7F)) << shift;
		if ((buf[i] & 0x80) == 0) {
			msg_type = (uint16_t)value;
			break;
		}
		shift += 7;
	}

	if (msg_type_out)
		*msg_type_out = msg_type;

	/* We don't parse the response data - just check the message type */
	return 0;
}

/* Connect to TCP socket */
static int tcp_connect(const char *host, uint16_t port)
{
	struct hostent *he;
	struct sockaddr_in addr;
	int sockfd;

	he = gethostbyname(host);
	if (!he) {
		fprintf(stderr, "Failed to resolve %s\n", host);
		return -1;
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		close(sockfd);
		return -1;
	}

	return sockfd;
}

struct esphome_client *esphome_client_create(const char *host, uint16_t port,
					      const char *device_name)
{
	struct esphome_client *client;

	client = calloc(1, sizeof(*client));
	if (!client)
		return NULL;

	strncpy(client->host, host, sizeof(client->host) - 1);
	client->port = port ? port : 6053;

	if (device_name)
		strncpy(client->device_name, device_name,
			sizeof(client->device_name) - 1);
	else
		strcpy(client->device_name, "thingino-camera");

	client->sockfd = -1;

	return client;
}

void esphome_client_destroy(struct esphome_client *client)
{
	if (!client)
		return;

	if (client->connected)
		esphome_client_disconnect(client);

	free(client);
}

int esphome_client_connect(struct esphome_client *client)
{
	uint8_t msg_buf[256];
	size_t msg_len;
	uint16_t msg_type;

	if (!client || client->connected)
		return -1;

	/* Connect TCP socket */
	client->sockfd = tcp_connect(client->host, client->port);
	if (client->sockfd < 0)
		return -1;

	/* Send HelloRequest (message type 1)
	 * Fields: client_info(1), api_version_major(2), api_version_minor(3) */
	msg_len = 0;
	msg_len += encode_string(msg_buf + msg_len, 1, client->device_name);
	msg_len += encode_uint32(msg_buf + msg_len, 2, 1);  /* API version 1 */
	msg_len += encode_uint32(msg_buf + msg_len, 3, 0);  /* API minor 0 */

	if (send_message(client, MSG_HELLO_REQUEST, msg_buf, msg_len) < 0) {
		fprintf(stderr, "[ESPHome] Failed to send HelloRequest\n");
		goto error;
	}

	/* Receive HelloResponse (type 2) - we don't parse it, just check type */
	if (recv_message_header(client, &msg_type) < 0 || msg_type != MSG_HELLO_RESPONSE) {
		fprintf(stderr, "[ESPHome] Failed to receive HelloResponse\n");
		goto error;
	}

	printf("[ESPHome] Connected to Home Assistant\n");

	/* Send ConnectRequest (message type 3)
	 * Fields: password(1) - empty for now */
	msg_len = 0;
	msg_len += encode_string(msg_buf + msg_len, 1, "");  /* empty password */

	if (send_message(client, MSG_CONNECT_REQUEST, msg_buf, msg_len) < 0) {
		fprintf(stderr, "[ESPHome] Failed to send ConnectRequest\n");
		goto error;
	}

	/* Receive ConnectResponse (type 4) */
	if (recv_message_header(client, &msg_type) < 0 || msg_type != MSG_CONNECT_RESPONSE) {
		fprintf(stderr, "[ESPHome] Failed to receive ConnectResponse\n");
		goto error;
	}

	/* Send SubscribeBluetoothLEAdvertisementsRequest (message type 66)
	 * Fields: flags(1) */
	msg_len = 0;
	msg_len += encode_uint32(msg_buf + msg_len, 1, 0);  /* flags = 0 */

	if (send_message(client, MSG_SUBSCRIBE_BLE_ADVERTISEMENTS, msg_buf, msg_len) < 0) {
		fprintf(stderr, "[ESPHome] Failed to subscribe to BLE advertisements\n");
		goto error;
	}

	client->connected = 1;
	printf("[ESPHome] Connected to %s:%d\n", client->host, client->port);
	printf("[ESPHome] Subscribed to BLE advertisements\n");

	return 0;

error:
	close(client->sockfd);
	client->sockfd = -1;
	return -1;
}

void esphome_client_disconnect(struct esphome_client *client)
{
	if (!client || !client->connected)
		return;

	/* Send DisconnectRequest (message type 5) - empty message */
	send_message(client, MSG_DISCONNECT_REQUEST, NULL, 0);

	close(client->sockfd);
	client->sockfd = -1;
	client->connected = 0;

	printf("[ESPHome] Disconnected\n");
}

int esphome_client_send_ble_adv(struct esphome_client *client,
				const struct esphome_ble_adv *adv)
{
	uint8_t msg_buf[512];
	uint8_t svc_data_buf[300];
	size_t msg_len = 0;
	size_t svc_data_len;
	uint64_t address;

	if (!client || !client->connected || !adv)
		return -1;

	/* Convert MAC address to uint64 */
	address = adv->address;
	if (address == 0 && adv->address_str[0]) {
		address = mac_to_uint64(adv->address_str);
	}

	/* BluetoothLEAdvertisementResponse message (type 67)
	 * Fields: address(1), name(2), rssi(3), service_uuids(4),
	 *         service_data(5), manufacturer_data(6), address_type(7) */

	/* Field 1: address (uint64) */
	msg_len += encode_uint64(msg_buf + msg_len, 1, address);

	/* Field 2: name (string) - optional */
	if (adv->name && adv->name[0]) {
		msg_len += encode_string(msg_buf + msg_len, 2, adv->name);
	}

	/* Field 3: rssi (sint32) */
	msg_len += encode_sint32(msg_buf + msg_len, 3, adv->rssi);

	/* Field 6: manufacturer_data (repeated BluetoothServiceData) */
	if (adv->adv_data && adv->adv_data_len > 0) {
		/* Build BluetoothServiceData submessage
		 * Fields: uuid(1), legacy_data(2), data(3) */
		svc_data_len = 0;
		svc_data_len += encode_string(svc_data_buf + svc_data_len, 1, "");  /* empty uuid */
		svc_data_len += encode_bytes(svc_data_buf + svc_data_len, 3, adv->adv_data, adv->adv_data_len);

		/* Encode the submessage as a length-delimited field */
		msg_len += encode_tag(msg_buf + msg_len, 6, WIRE_LEN_DELIM);
		msg_len += encode_varint(msg_buf + msg_len, svc_data_len);
		memcpy(msg_buf + msg_len, svc_data_buf, svc_data_len);
		msg_len += svc_data_len;
	}

	/* Field 7: address_type (uint32) - 0 = public, 1 = random */
	msg_len += encode_uint32(msg_buf + msg_len, 7, 0);

	/* Send the message */
	return send_message(client, MSG_BLE_ADVERTISEMENT, msg_buf, msg_len);
}

int esphome_client_ping(struct esphome_client *client)
{
	if (!client || !client->connected)
		return -1;

	/* PingRequest (message type 7) - empty message */
	return send_message(client, MSG_PING_REQUEST, NULL, 0);
}

int esphome_client_is_connected(struct esphome_client *client)
{
	return client && client->connected;
}
