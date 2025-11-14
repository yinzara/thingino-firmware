/* Simple manual protobuf encoder for ESPHome Native API
 * Replaces protobuf-c to save ~2MB
 * Only implements the minimal messages needed for BLE proxy
 */

#ifndef ESPHOME_PROTO_SIMPLE_H
#define ESPHOME_PROTO_SIMPLE_H

#include <stdint.h>
#include <stddef.h>

/* ESPHome Native API message types */
#define MSG_HELLO_REQUEST 1
#define MSG_HELLO_RESPONSE 2
#define MSG_CONNECT_REQUEST 3
#define MSG_CONNECT_RESPONSE 4
#define MSG_DISCONNECT_REQUEST 5
#define MSG_PING_REQUEST 7
#define MSG_PING_RESPONSE 8
#define MSG_SUBSCRIBE_BLE_ADS 66
#define MSG_BLE_ADVERTISEMENT 67

/* Protobuf wire types */
#define WIRE_VARINT 0
#define WIRE_64BIT 1
#define WIRE_LEN_DELIM 2
#define WIRE_32BIT 5

/* Helper to encode varint */
static inline size_t encode_varint(uint8_t *buf, uint64_t value)
{
	size_t len = 0;
	while (value >= 0x80) {
		buf[len++] = (value & 0x7F) | 0x80;
		value >>= 7;
	}
	buf[len++] = value & 0x7F;
	return len;
}

/* Helper to encode tag */
static inline size_t encode_tag(uint8_t *buf, uint32_t field, uint32_t wire_type)
{
	return encode_varint(buf, (field << 3) | wire_type);
}

/* Helper to encode string field */
static inline size_t encode_string(uint8_t *buf, uint32_t field, const char *str)
{
	size_t len = 0;
	size_t str_len = str ? strlen(str) : 0;

	if (str_len == 0)
		return 0;

	len += encode_tag(buf + len, field, WIRE_LEN_DELIM);
	len += encode_varint(buf + len, str_len);
	memcpy(buf + len, str, str_len);
	len += str_len;

	return len;
}

/* Helper to encode uint32 field */
static inline size_t encode_uint32(uint8_t *buf, uint32_t field, uint32_t value)
{
	size_t len = 0;
	len += encode_tag(buf + len, field, WIRE_VARINT);
	len += encode_varint(buf + len, value);
	return len;
}

/* Helper to encode uint64 field */
static inline size_t encode_uint64(uint8_t *buf, uint32_t field, uint64_t value)
{
	size_t len = 0;
	len += encode_tag(buf + len, field, WIRE_VARINT);
	len += encode_varint(buf + len, value);
	return len;
}

/* Helper to encode sint32 field (zigzag encoding) */
static inline size_t encode_sint32(uint8_t *buf, uint32_t field, int32_t value)
{
	size_t len = 0;
	uint32_t zigzag = (value << 1) ^ (value >> 31);
	len += encode_tag(buf + len, field, WIRE_VARINT);
	len += encode_varint(buf + len, zigzag);
	return len;
}

/* Helper to encode bytes field */
static inline size_t encode_bytes(uint8_t *buf, uint32_t field, const uint8_t *data, size_t data_len)
{
	size_t len = 0;

	if (data_len == 0)
		return 0;

	len += encode_tag(buf + len, field, WIRE_LEN_DELIM);
	len += encode_varint(buf + len, data_len);
	memcpy(buf + len, data, data_len);
	len += data_len;

	return len;
}

#endif /* ESPHOME_PROTO_SIMPLE_H */
