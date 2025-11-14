/*
 * BLE Scanner for Thingino
 *
 * Scans for BLE advertisements using BlueZ D-Bus API
 * Provides callbacks for discovered devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <glib.h>

#include "ble-scanner.h"

#define BLUEZ_SERVICE			"org.bluez"
#define ADAPTER_IFACE			"org.bluez.Adapter1"
#define DEVICE_IFACE			"org.bluez.Device1"
#define PROPERTIES_IFACE		"org.freedesktop.DBus.Properties"

struct ble_scanner {
	DBusConnection *conn;
	GMainLoop *loop;
	char *adapter_path;
	ble_device_callback callback;
	void *user_data;
	int running;
};

static DBusHandlerResult properties_changed_handler(DBusConnection *conn,
						     DBusMessage *msg,
						     void *user_data)
{
	struct ble_scanner *scanner = user_data;
	DBusMessageIter iter, variant, dict;
	const char *iface;
	const char *path = dbus_message_get_path(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return DBUS_HANDLER_RESULT_HANDLED;

	/* Get interface name */
	dbus_message_iter_get_basic(&iter, &iface);

	/* We only care about Device1 interface */
	if (strcmp(iface, DEVICE_IFACE) != 0)
		return DBUS_HANDLER_RESULT_HANDLED;

	dbus_message_iter_next(&iter);
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		return DBUS_HANDLER_RESULT_HANDLED;

	dbus_message_iter_recurse(&iter, &dict);

	struct ble_device device = {0};
	strncpy(device.path, path, sizeof(device.path) - 1);

	/* Parse changed properties */
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &variant);

		if (strcmp(key, "Address") == 0) {
			const char *addr;
			dbus_message_iter_get_basic(&variant, &addr);
			strncpy(device.address, addr, sizeof(device.address) - 1);
		} else if (strcmp(key, "Name") == 0) {
			const char *name;
			dbus_message_iter_get_basic(&variant, &name);
			strncpy(device.name, name, sizeof(device.name) - 1);
		} else if (strcmp(key, "RSSI") == 0) {
			dbus_int16_t rssi;
			dbus_message_iter_get_basic(&variant, &rssi);
			device.rssi = rssi;
			device.has_rssi = 1;
		} else if (strcmp(key, "ManufacturerData") == 0) {
			/* Parse manufacturer data if needed */
			device.has_mfg_data = 1;
		}

		dbus_message_iter_next(&dict);
	}

	/* Only report if we have an address and RSSI */
	if (device.address[0] && device.has_rssi && scanner->callback) {
		scanner->callback(&device, scanner->user_data);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static int start_discovery(struct ble_scanner *scanner)
{
	DBusMessage *msg, *reply;
	DBusError error;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    scanner->adapter_path,
					    ADAPTER_IFACE,
					    "StartDiscovery");
	if (!msg)
		return -1;

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(scanner->conn, msg,
							   -1, &error);
	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "Failed to start discovery: %s\n", error.message);
		dbus_error_free(&error);
		return -1;
	}

	dbus_message_unref(reply);
	printf("[Scanner] Discovery started\n");
	return 0;
}

static int stop_discovery(struct ble_scanner *scanner)
{
	DBusMessage *msg, *reply;
	DBusError error;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    scanner->adapter_path,
					    ADAPTER_IFACE,
					    "StopDiscovery");
	if (!msg)
		return -1;

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(scanner->conn, msg,
							   -1, &error);
	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "Failed to stop discovery: %s\n", error.message);
		dbus_error_free(&error);
		return -1;
	}

	dbus_message_unref(reply);
	printf("[Scanner] Discovery stopped\n");
	return 0;
}

struct ble_scanner *ble_scanner_create(const char *adapter)
{
	struct ble_scanner *scanner;
	DBusError error;

	scanner = calloc(1, sizeof(*scanner));
	if (!scanner)
		return NULL;

	/* Default adapter path */
	if (!adapter)
		adapter = "/org/bluez/hci0";

	scanner->adapter_path = strdup(adapter);

	dbus_error_init(&error);
	scanner->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (!scanner->conn) {
		fprintf(stderr, "Failed to connect to D-Bus: %s\n", error.message);
		dbus_error_free(&error);
		free(scanner->adapter_path);
		free(scanner);
		return NULL;
	}

	/* Add signal match for PropertiesChanged */
	dbus_bus_add_match(scanner->conn,
			   "type='signal',"
			   "interface='" PROPERTIES_IFACE "',"
			   "member='PropertiesChanged'",
			   &error);
	if (dbus_error_is_set(&error)) {
		fprintf(stderr, "Failed to add match: %s\n", error.message);
		dbus_error_free(&error);
		dbus_connection_unref(scanner->conn);
		free(scanner->adapter_path);
		free(scanner);
		return NULL;
	}

	/* Add message filter */
	dbus_connection_add_filter(scanner->conn, properties_changed_handler,
				    scanner, NULL);

	scanner->loop = g_main_loop_new(NULL, FALSE);

	return scanner;
}

void ble_scanner_destroy(struct ble_scanner *scanner)
{
	if (!scanner)
		return;

	if (scanner->running)
		ble_scanner_stop(scanner);

	if (scanner->loop)
		g_main_loop_unref(scanner->loop);

	if (scanner->conn)
		dbus_connection_unref(scanner->conn);

	free(scanner->adapter_path);
	free(scanner);
}

int ble_scanner_start(struct ble_scanner *scanner,
		      ble_device_callback callback,
		      void *user_data)
{
	if (!scanner || !callback)
		return -1;

	scanner->callback = callback;
	scanner->user_data = user_data;

	if (start_discovery(scanner) < 0)
		return -1;

	scanner->running = 1;
	return 0;
}

void ble_scanner_stop(struct ble_scanner *scanner)
{
	if (!scanner || !scanner->running)
		return;

	stop_discovery(scanner);
	scanner->running = 0;
}

void ble_scanner_run(struct ble_scanner *scanner)
{
	if (!scanner || !scanner->loop)
		return;

	/* D-Bus connection from dbus_bus_get is already integrated with GLib main loop */
	g_main_loop_run(scanner->loop);
}

void ble_scanner_quit(struct ble_scanner *scanner)
{
	if (!scanner || !scanner->loop)
		return;

	g_main_loop_quit(scanner->loop);
}
