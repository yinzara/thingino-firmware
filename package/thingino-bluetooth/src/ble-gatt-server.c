/*
 * BLE GATT Server for Thingino Complete Provisioning
 *
 * Fully functional BlueZ D-Bus GATT implementation
 * Implements complete GATT service with advertising for device provisioning
 *
 * Service UUID: 00000001-0000-1000-8000-00805f9b34fb
 * Characteristics:
 *   - WiFi Scan (0x0002) - Read: List available networks (JSON)
 *   - WiFi SSID (0x0003) - Write: Set target SSID
 *   - WiFi Password (0x0004) - Write: Set WiFi password
 *   - Provision Status (0x0005) - Read/Notify: Provisioning status
 *   - Admin Username (0x0006) - Write: Set admin username
 *   - Admin Password (0x0007) - Write: Set admin password
 *   - Hostname (0x0008) - Write: Set device hostname
 *   - BLE Proxy Enabled (0x000C) - Write: Enable/disable proxy
 *   - BLE Proxy HA Host (0x000D) - Write: Home Assistant hostname
 *   - BLE Proxy HA Port (0x000E) - Write: Home Assistant port
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <dbus/dbus.h>
#include <glib.h>

/* HCI protocol definitions */
#define AF_BLUETOOTH	31
#define BTPROTO_HCI	1
#define HCI_COMMAND_PKT	0x01
#define HCI_DEV_NONE	0xffff

#define HCIDEVUP	_IOW('H', 201, int)
#define HCIDEVDOWN	_IOW('H', 202, int)
#define HCIGETDEVINFO	_IOR('H', 211, int)

struct sockaddr_hci {
	sa_family_t	hci_family;
	unsigned short	hci_dev;
	unsigned short  hci_channel;
};

struct hci_dev_info {
	uint16_t dev_id;
	char     name[8];
	uint8_t  bdaddr[6];
	uint32_t flags;
	uint8_t  type;
	uint8_t  features[8];
	uint32_t pkt_type;
	uint32_t link_policy;
	uint32_t link_mode;
	uint16_t acl_mtu;
	uint16_t acl_pkts;
	uint16_t sco_mtu;
	uint16_t sco_pkts;
	/* stats */
	uint32_t stat_pad[10];
};

/* HCI commands */
#define HCI_OP_WRITE_LE_HOST_SUPPORTED	0x0C6D
#define HCI_OP_WRITE_SCAN_ENABLE	0x0C1A

/* HCI LE Advertising commands */
#define HCI_OP_LE_SET_ADV_PARAMS	0x2006
#define HCI_OP_LE_SET_ADV_DATA		0x2008
#define HCI_OP_LE_SET_SCAN_RSP_DATA	0x2009
#define HCI_OP_LE_SET_ADV_ENABLE	0x200A

/* D-Bus constants */
#define BLUEZ_SERVICE			"org.bluez"
#define ADAPTER_IFACE			"org.bluez.Adapter1"
#define GATT_MANAGER_IFACE		"org.bluez.GattManager1"
#define GATT_SERVICE_IFACE		"org.bluez.GattService1"
#define GATT_CHRC_IFACE			"org.bluez.GattCharacteristic1"
#define LE_ADVERTISING_MANAGER_IFACE	"org.bluez.LEAdvertisingManager1"
#define LE_ADVERTISEMENT_IFACE		"org.bluez.LEAdvertisement1"
#define DBUS_OM_IFACE			"org.freedesktop.DBus.ObjectManager"
#define DBUS_PROP_IFACE			"org.freedesktop.DBus.Properties"

/* Application paths */
#define APP_PATH			"/org/bluez/thingino"
#define SERVICE_PATH			"/org/bluez/thingino/service0"
#define ADV_PATH			"/org/bluez/thingino/advertisement0"

/* UUIDs */
#define SERVICE_UUID			"00000001-0000-1000-8000-00805f9b34fb"
#define WIFI_SCAN_UUID			"00000002-0000-1000-8000-00805f9b34fb"
#define WIFI_SSID_UUID			"00000003-0000-1000-8000-00805f9b34fb"
#define WIFI_PASS_UUID			"00000004-0000-1000-8000-00805f9b34fb"
#define STATUS_UUID			"00000005-0000-1000-8000-00805f9b34fb"
#define ADMIN_USER_UUID			"00000006-0000-1000-8000-00805f9b34fb"
#define ADMIN_PASS_UUID			"00000007-0000-1000-8000-00805f9b34fb"
#define HOSTNAME_UUID			"00000008-0000-1000-8000-00805f9b34fb"
#define PROXY_ENABLED_UUID		"0000000c-0000-1000-8000-00805f9b34fb"
#define PROXY_HOST_UUID			"0000000d-0000-1000-8000-00805f9b34fb"
#define PROXY_PORT_UUID			"0000000e-0000-1000-8000-00805f9b34fb"

/* Paths */
#define PROVISION_DIR			"/tmp/ble_provision"
#define BLE_PROVISION_CMD		"/usr/sbin/ble-provision"

/* Characteristic flags */
#define FLAG_READ			(1 << 0)
#define FLAG_WRITE			(1 << 1)
#define FLAG_NOTIFY			(1 << 2)
#define FLAG_ENCRYPT_READ		(1 << 3)
#define FLAG_ENCRYPT_WRITE		(1 << 4)

typedef struct {
	char *uuid;
	char *path;
	int flags;
	char *(*read_handler)(size_t *len);
	int (*write_handler)(const char *value, size_t len);
} Characteristic;

typedef struct {
	DBusConnection *conn;
	GMainLoop *loop;
	char *adapter_path;
	Characteristic *chrcs[10];
	int num_chrcs;
	char ssid[128];
	char password[128];
	char admin_username[64];
} AppContext;

static AppContext *g_ctx = NULL;

/* Forward declarations */
static DBusHandlerResult handle_properties_get_all(DBusConnection *conn, DBusMessage *msg, const char *path);
static DBusHandlerResult handle_properties_get(DBusConnection *conn, DBusMessage *msg, const char *path);

/* Utility functions */
static void ensure_provision_dir(void)
{
	mkdir(PROVISION_DIR, 0755);
}

static int write_file(const char *path, const char *data)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	fputs(data, f);
	fclose(f);
	return 0;
}

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *buf = malloc(size + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	size_t read_size = fread(buf, 1, size, f);
	buf[read_size] = '\0';
	fclose(f);

	if (len)
		*len = read_size;
	return buf;
}

static int run_command(const char *cmd)
{
	return system(cmd);
}

/* Get device name from hostname */
static char *get_device_name(void)
{
	char hostname[64];
	char *device_name;

	if (gethostname(hostname, sizeof(hostname)) == 0) {
		device_name = malloc(strlen(hostname) + 20);
		sprintf(device_name, "%s-Setup", hostname);
	} else {
		device_name = strdup("Thingino-Setup");
	}

	return device_name;
}

/* Characteristic handlers */
static char *wifi_scan_read(size_t *len)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "%s scan > %s/scan.json 2>&1",
		 BLE_PROVISION_CMD, PROVISION_DIR);
	run_command(cmd);

	return read_file(PROVISION_DIR "/scan.json", len);
}

static int wifi_ssid_write(const char *value, size_t len)
{
	if (len >= sizeof(g_ctx->ssid))
		return -1;

	memcpy(g_ctx->ssid, value, len);
	g_ctx->ssid[len] = '\0';

	write_file(PROVISION_DIR "/ssid", g_ctx->ssid);
	printf("[WiFiSSID] Received: %s\n", g_ctx->ssid);
	return 0;
}

static int wifi_password_write(const char *value, size_t len)
{
	if (len >= sizeof(g_ctx->password))
		return -1;

	memcpy(g_ctx->password, value, len);
	g_ctx->password[len] = '\0';

	write_file(PROVISION_DIR "/password", g_ctx->password);
	printf("[WiFiPassword] Received (length: %zu)\n", len);

	/* Trigger provisioning if both SSID and password are set */
	if (g_ctx->ssid[0] && g_ctx->password[0]) {
		char cmd[512];
		printf("[Provisioning] Both credentials received, triggering provision\n");
		snprintf(cmd, sizeof(cmd), "%s apply '%s' '%s' &",
			 BLE_PROVISION_CMD, g_ctx->ssid, g_ctx->password);
		run_command(cmd);
	}

	return 0;
}

static char *status_read(size_t *len)
{
	char *status = read_file(PROVISION_DIR "/status", len);
	if (!status) {
		status = strdup("waiting");
		*len = strlen(status);
	}
	printf("[Status] Current status: %s\n", status);
	return status;
}

static int admin_username_write(const char *value, size_t len)
{
	if (len >= sizeof(g_ctx->admin_username))
		return -1;

	memcpy(g_ctx->admin_username, value, len);
	g_ctx->admin_username[len] = '\0';

	write_file(PROVISION_DIR "/admin_username", g_ctx->admin_username);
	printf("[AdminUsername] Received: %s\n", g_ctx->admin_username);
	return 0;
}

static int admin_password_write(const char *value, size_t len)
{
	char password[128];
	if (len >= sizeof(password))
		return -1;

	memcpy(password, value, len);
	password[len] = '\0';

	write_file(PROVISION_DIR "/admin_password", password);
	printf("[AdminPassword] Received (length: %zu)\n", len);

	/* Trigger admin setup */
	const char *username = g_ctx->admin_username[0] ?
		g_ctx->admin_username : "root";

	char cmd[512];
	snprintf(cmd, sizeof(cmd), "%s set-admin '%s' '%s' &",
		 BLE_PROVISION_CMD, username, password);
	run_command(cmd);

	return 0;
}

static int hostname_write(const char *value, size_t len)
{
	char hostname[128];
	if (len >= sizeof(hostname))
		return -1;

	memcpy(hostname, value, len);
	hostname[len] = '\0';

	write_file(PROVISION_DIR "/hostname", hostname);
	printf("[Hostname] Received: %s\n", hostname);

	/* Trigger hostname setup */
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "%s set-hostname '%s' &",
		 BLE_PROVISION_CMD, hostname);
	run_command(cmd);

	return 0;
}

static int proxy_enabled_write(const char *value, size_t len)
{
	char enabled[16];
	if (len >= sizeof(enabled))
		return -1;

	memcpy(enabled, value, len);
	enabled[len] = '\0';

	/* Normalize to true/false */
	const char *normalized = "false";
	if (strcasecmp(enabled, "true") == 0 ||
	    strcmp(enabled, "1") == 0 ||
	    strcasecmp(enabled, "yes") == 0 ||
	    strcasecmp(enabled, "on") == 0 ||
	    strcasecmp(enabled, "enabled") == 0) {
		normalized = "true";
	}

	printf("[BLEProxyEnabled] Received: %s (normalized: %s)\n", enabled, normalized);

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "fw_setenv ble_proxy_enabled %s", normalized);
	run_command(cmd);

	return 0;
}

static int proxy_host_write(const char *value, size_t len)
{
	char host[256];
	if (len >= sizeof(host) || len < 3)
		return -1;

	memcpy(host, value, len);
	host[len] = '\0';

	printf("[BLEProxyHAHost] Received: %s\n", host);

	char cmd[512];
	snprintf(cmd, sizeof(cmd), "fw_setenv ble_proxy_ha_host '%s'", host);
	run_command(cmd);

	return 0;
}

static int proxy_port_write(const char *value, size_t len)
{
	char port_str[16];
	if (len >= sizeof(port_str))
		return -1;

	memcpy(port_str, value, len);
	port_str[len] = '\0';

	int port = atoi(port_str);
	if (port < 1 || port > 65535)
		return -1;

	printf("[BLEProxyHAPort] Received: %d\n", port);

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "fw_setenv ble_proxy_ha_port %d", port);
	run_command(cmd);

	return 0;
}

/* Find characteristic by path */
static Characteristic *find_chrc(const char *path)
{
	for (int i = 0; i < g_ctx->num_chrcs; i++) {
		if (strcmp(g_ctx->chrcs[i]->path, path) == 0)
			return g_ctx->chrcs[i];
	}
	return NULL;
}

/* D-Bus message handlers - Characteristic ReadValue/WriteValue */
static DBusHandlerResult handle_read_value(DBusConnection *conn, DBusMessage *msg)
{
	const char *path = dbus_message_get_path(msg);
	Characteristic *chrc = find_chrc(path);
	DBusMessage *reply;
	DBusMessageIter iter, array;

	if (!chrc || !chrc->read_handler) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.NotSupported",
					       "Read not supported");
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	size_t len;
	char *value = chrc->read_handler(&len);
	if (!value) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.Failed",
					       "Read failed");
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	reply = dbus_message_new_method_return(msg);
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "y", &array);

	for (size_t i = 0; i < len; i++) {
		uint8_t byte = (uint8_t)value[i];
		dbus_message_iter_append_basic(&array, DBUS_TYPE_BYTE, &byte);
	}

	dbus_message_iter_close_container(&iter, &array);
	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);
	free(value);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_write_value(DBusConnection *conn, DBusMessage *msg)
{
	const char *path = dbus_message_get_path(msg);
	Characteristic *chrc = find_chrc(path);
	DBusMessage *reply;
	DBusMessageIter iter, array;
	unsigned char buf[1024];
	size_t len = 0;

	if (!chrc || !chrc->write_handler) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.NotSupported",
					       "Write not supported");
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	dbus_message_iter_init(msg, &iter);
	dbus_message_iter_recurse(&iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_BYTE && len < sizeof(buf)) {
		uint8_t byte;
		dbus_message_iter_get_basic(&array, &byte);
		buf[len++] = byte;
		dbus_message_iter_next(&array);
	}

	if (chrc->write_handler((char *)buf, len) < 0) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.Failed",
					       "Write failed");
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	reply = dbus_message_new_method_return(msg);
	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Properties.Get for characteristic */
static DBusHandlerResult handle_chrc_properties_get(DBusConnection *conn, DBusMessage *msg, Characteristic *chrc)
{
	const char *iface, *prop;
	DBusMessage *reply;
	DBusMessageIter iter, variant;

	dbus_message_get_args(msg, NULL,
			      DBUS_TYPE_STRING, &iface,
			      DBUS_TYPE_STRING, &prop,
			      DBUS_TYPE_INVALID);

	if (strcmp(iface, GATT_CHRC_IFACE) != 0) {
		reply = dbus_message_new_error(msg, "org.bluez.Error.InvalidArguments",
					       "Invalid interface");
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	reply = dbus_message_new_method_return(msg);
	dbus_message_iter_init_append(reply, &iter);

	if (strcmp(prop, "UUID") == 0) {
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &chrc->uuid);
		dbus_message_iter_close_container(&iter, &variant);
	} else if (strcmp(prop, "Service") == 0) {
		const char *service_path = SERVICE_PATH;
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "o", &variant);
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &service_path);
		dbus_message_iter_close_container(&iter, &variant);
	} else if (strcmp(prop, "Flags") == 0) {
		DBusMessageIter variant, array;
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "as", &variant);
		dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);

		if (chrc->flags & FLAG_READ) {
			const char *flag = "read";
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
		}
		if (chrc->flags & FLAG_WRITE) {
			const char *flag = "write";
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
		}
		if (chrc->flags & FLAG_NOTIFY) {
			const char *flag = "notify";
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
		}
		if (chrc->flags & FLAG_ENCRYPT_READ) {
			const char *flag = "encrypt-read";
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
		}
		if (chrc->flags & FLAG_ENCRYPT_WRITE) {
			const char *flag = "encrypt-write";
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
		}

		dbus_message_iter_close_container(&variant, &array);
		dbus_message_iter_close_container(&iter, &variant);
	} else {
		dbus_message_unref(reply);
		reply = dbus_message_new_error(msg, "org.bluez.Error.InvalidArguments",
					       "Invalid property");
	}

	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Properties.GetAll for characteristic */
static void append_chrc_properties(DBusMessageIter *dict, Characteristic *chrc)
{
	DBusMessageIter entry, variant, array;
	const char *service_path = SERVICE_PATH;

	/* UUID property */
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *uuid_key = "UUID";
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &uuid_key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &chrc->uuid);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);

	/* Service property */
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *service_key = "Service";
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &service_key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &service_path);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);

	/* Flags property */
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *flags_key = "Flags";
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &flags_key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);

	if (chrc->flags & FLAG_READ) {
		const char *flag = "read";
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
	}
	if (chrc->flags & FLAG_WRITE) {
		const char *flag = "write";
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
	}
	if (chrc->flags & FLAG_NOTIFY) {
		const char *flag = "notify";
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
	}
	if (chrc->flags & FLAG_ENCRYPT_READ) {
		const char *flag = "encrypt-read";
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
	}
	if (chrc->flags & FLAG_ENCRYPT_WRITE) {
		const char *flag = "encrypt-write";
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &flag);
	}

	dbus_message_iter_close_container(&variant, &array);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

static DBusHandlerResult handle_chrc_properties_get_all(DBusConnection *conn, DBusMessage *msg, Characteristic *chrc)
{
	DBusMessage *reply;
	DBusMessageIter iter, dict;

	reply = dbus_message_new_method_return(msg);
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	append_chrc_properties(&dict, chrc);

	dbus_message_iter_close_container(&iter, &dict);
	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Properties.GetAll for service */
static DBusHandlerResult handle_service_properties_get_all(DBusConnection *conn, DBusMessage *msg)
{
	DBusMessage *reply;
	DBusMessageIter iter, dict, entry, variant;

	reply = dbus_message_new_method_return(msg);
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	/* UUID property */
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *uuid_key = "UUID";
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &uuid_key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
	const char *uuid_val = SERVICE_UUID;
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &uuid_val);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(&dict, &entry);

	/* Primary property */
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *primary_key = "Primary";
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &primary_key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
	dbus_bool_t primary_val = TRUE;
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &primary_val);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(&dict, &entry);

	dbus_message_iter_close_container(&iter, &dict);
	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* ObjectManager.GetManagedObjects */
static DBusHandlerResult handle_get_managed_objects(DBusConnection *conn, DBusMessage *msg)
{
	DBusMessage *reply;
	DBusMessageIter iter, dict, entry, ifaces, iface_entry, props;

	printf("[App] GetManagedObjects called - building object tree...\n");

	reply = dbus_message_new_method_return(msg);
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &dict);

	/* Service object */
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	const char *service_path = SERVICE_PATH;
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &service_path);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

	/* Service interface */
	dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);
	const char *service_iface = GATT_SERVICE_IFACE;
	dbus_message_iter_append_basic(&iface_entry, DBUS_TYPE_STRING, &service_iface);
	dbus_message_iter_open_container(&iface_entry, DBUS_TYPE_ARRAY, "{sv}", &props);

	/* UUID */
	DBusMessageIter prop_entry, variant;
	dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop_entry);
	const char *uuid_key = "UUID";
	dbus_message_iter_append_basic(&prop_entry, DBUS_TYPE_STRING, &uuid_key);
	dbus_message_iter_open_container(&prop_entry, DBUS_TYPE_VARIANT, "s", &variant);
	const char *uuid_val = SERVICE_UUID;
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &uuid_val);
	dbus_message_iter_close_container(&prop_entry, &variant);
	dbus_message_iter_close_container(&props, &prop_entry);

	/* Primary */
	dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop_entry);
	const char *primary_key = "Primary";
	dbus_message_iter_append_basic(&prop_entry, DBUS_TYPE_STRING, &primary_key);
	dbus_message_iter_open_container(&prop_entry, DBUS_TYPE_VARIANT, "b", &variant);
	dbus_bool_t primary_val = TRUE;
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &primary_val);
	dbus_message_iter_close_container(&prop_entry, &variant);
	dbus_message_iter_close_container(&props, &prop_entry);

	dbus_message_iter_close_container(&iface_entry, &props);
	dbus_message_iter_close_container(&ifaces, &iface_entry);
	dbus_message_iter_close_container(&entry, &ifaces);
	dbus_message_iter_close_container(&dict, &entry);

	/* Characteristic objects */
	for (int i = 0; i < g_ctx->num_chrcs; i++) {
		Characteristic *chrc = g_ctx->chrcs[i];

		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &chrc->path);
		dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

		/* Characteristic interface */
		dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);
		const char *chrc_iface = GATT_CHRC_IFACE;
		dbus_message_iter_append_basic(&iface_entry, DBUS_TYPE_STRING, &chrc_iface);
		dbus_message_iter_open_container(&iface_entry, DBUS_TYPE_ARRAY, "{sv}", &props);

		append_chrc_properties(&props, chrc);

		dbus_message_iter_close_container(&iface_entry, &props);
		dbus_message_iter_close_container(&ifaces, &iface_entry);
		dbus_message_iter_close_container(&entry, &ifaces);
		dbus_message_iter_close_container(&dict, &entry);
	}

	dbus_message_iter_close_container(&iter, &dict);
	printf("[App] Sending GetManagedObjects reply with %d characteristics...\n", g_ctx->num_chrcs);
	dbus_connection_send(conn, reply, NULL);
	dbus_connection_flush(conn);
	dbus_message_unref(reply);
	printf("[App] GetManagedObjects reply sent and flushed\n");

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Main message handler */
static DBusHandlerResult message_handler(DBusConnection *conn, DBusMessage *msg, void *data)
{
	const char *path = dbus_message_get_path(msg);
	const char *iface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);

	printf("[DBus] %s.%s on %s\n", iface ? iface : "?", member ? member : "?", path ? path : "?");

	/* ObjectManager */
	if (strcmp(path, APP_PATH) == 0 && strcmp(iface, DBUS_OM_IFACE) == 0) {
		if (strcmp(member, "GetManagedObjects") == 0) {
			return handle_get_managed_objects(conn, msg);
		}
	}

	/* Service properties */
	if (strcmp(path, SERVICE_PATH) == 0) {
		if (strcmp(iface, DBUS_PROP_IFACE) == 0) {
			if (strcmp(member, "GetAll") == 0) {
				return handle_service_properties_get_all(conn, msg);
			}
		}
	}

	/* Advertisement properties */
	if (strcmp(path, ADV_PATH) == 0) {
		if (strcmp(iface, DBUS_PROP_IFACE) == 0 && strcmp(member, "GetAll") == 0) {
			/* Return advertisement properties */
			DBusMessage *reply = dbus_message_new_method_return(msg);
			DBusMessageIter iter, dict, entry, variant, array;

			dbus_message_iter_init_append(reply, &iter);
			dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

			/* Type property */
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
			const char *type_key = "Type";
			dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &type_key);
			dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
			const char *type_val = "peripheral";
			dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &type_val);
			dbus_message_iter_close_container(&entry, &variant);
			dbus_message_iter_close_container(&dict, &entry);

			/* ServiceUUIDs property */
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
			const char *svc_key = "ServiceUUIDs";
			dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &svc_key);
			dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
			dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);
			const char *svc_uuid = SERVICE_UUID;
			dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &svc_uuid);
			dbus_message_iter_close_container(&variant, &array);
			dbus_message_iter_close_container(&entry, &variant);
			dbus_message_iter_close_container(&dict, &entry);

			dbus_message_iter_close_container(&iter, &dict);
			dbus_connection_send(conn, reply, NULL);
			dbus_message_unref(reply);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}

	/* Characteristic methods and properties */
	Characteristic *chrc = find_chrc(path);
	if (chrc) {
		if (strcmp(iface, GATT_CHRC_IFACE) == 0) {
			if (strcmp(member, "ReadValue") == 0) {
				return handle_read_value(conn, msg);
			} else if (strcmp(member, "WriteValue") == 0) {
				return handle_write_value(conn, msg);
			}
		} else if (strcmp(iface, DBUS_PROP_IFACE) == 0) {
			if (strcmp(member, "Get") == 0) {
				return handle_chrc_properties_get(conn, msg, chrc);
			} else if (strcmp(member, "GetAll") == 0) {
				return handle_chrc_properties_get_all(conn, msg, chrc);
			}
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable vtable = {
	.message_function = message_handler,
};

/* Create characteristic */
static Characteristic *create_chrc(const char *uuid, const char *path, int flags,
				   char *(*read_fn)(size_t *),
				   int (*write_fn)(const char *, size_t))
{
	Characteristic *chrc = calloc(1, sizeof(Characteristic));
	if (!chrc)
		return NULL;

	chrc->uuid = strdup(uuid);
	chrc->path = strdup(path);
	chrc->flags = flags;
	chrc->read_handler = read_fn;
	chrc->write_handler = write_fn;

	return chrc;
}

/* Register all characteristics */
static void register_characteristics(AppContext *ctx)
{
	int idx = 0;

	ctx->chrcs[idx++] = create_chrc(WIFI_SCAN_UUID, SERVICE_PATH "/char0",
					FLAG_READ | FLAG_ENCRYPT_READ, wifi_scan_read, NULL);
	ctx->chrcs[idx++] = create_chrc(WIFI_SSID_UUID, SERVICE_PATH "/char1",
					FLAG_WRITE, NULL, wifi_ssid_write);
	ctx->chrcs[idx++] = create_chrc(WIFI_PASS_UUID, SERVICE_PATH "/char2",
					FLAG_WRITE | FLAG_ENCRYPT_WRITE, NULL, wifi_password_write);
	ctx->chrcs[idx++] = create_chrc(STATUS_UUID, SERVICE_PATH "/char3",
					FLAG_READ | FLAG_NOTIFY, status_read, NULL);
	ctx->chrcs[idx++] = create_chrc(ADMIN_USER_UUID, SERVICE_PATH "/char4",
					FLAG_WRITE, NULL, admin_username_write);
	ctx->chrcs[idx++] = create_chrc(ADMIN_PASS_UUID, SERVICE_PATH "/char5",
					FLAG_WRITE | FLAG_ENCRYPT_WRITE, NULL, admin_password_write);
	ctx->chrcs[idx++] = create_chrc(HOSTNAME_UUID, SERVICE_PATH "/char6",
					FLAG_WRITE, NULL, hostname_write);
	ctx->chrcs[idx++] = create_chrc(PROXY_ENABLED_UUID, SERVICE_PATH "/char7",
					FLAG_WRITE, NULL, proxy_enabled_write);
	ctx->chrcs[idx++] = create_chrc(PROXY_HOST_UUID, SERVICE_PATH "/char8",
					FLAG_WRITE, NULL, proxy_host_write);
	ctx->chrcs[idx++] = create_chrc(PROXY_PORT_UUID, SERVICE_PATH "/char9",
					FLAG_WRITE, NULL, proxy_port_write);

	ctx->num_chrcs = idx;

	/* Register D-Bus paths */
	dbus_connection_register_object_path(ctx->conn, APP_PATH, &vtable, ctx);
	dbus_connection_register_object_path(ctx->conn, SERVICE_PATH, &vtable, ctx);
	/* ADV_PATH not needed - using direct HCI advertising instead of D-Bus */

	for (int i = 0; i < ctx->num_chrcs; i++) {
		dbus_connection_register_object_path(ctx->conn, ctx->chrcs[i]->path, &vtable, ctx);
	}
}

/* Register GATT application with BlueZ */
static int register_application(AppContext *ctx)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, dict;
	DBusError error;
	DBusPendingCall *pending;

	printf("[App] Registering GATT application at %s...\n", APP_PATH);

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    GATT_MANAGER_IFACE,
					    "RegisterApplication");
	if (!msg) {
		fprintf(stderr, "Failed to create RegisterApplication message\n");
		return -1;
	}

	dbus_message_iter_init_append(msg, &iter);
	const char *app_path = APP_PATH;
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &app_path);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
	dbus_message_iter_close_container(&iter, &dict);

	/* Send asynchronously so we can pump messages while waiting */
	if (!dbus_connection_send_with_reply(ctx->conn, msg, &pending, -1)) {
		fprintf(stderr, "Failed to send RegisterApplication message\n");
		dbus_message_unref(msg);
		return -1;
	}
	dbus_message_unref(msg);

	if (!pending) {
		fprintf(stderr, "Pending call is NULL\n");
		return -1;
	}

	printf("[App] Waiting for registration reply (pumping message queue)...\n");

	/* Pump messages until we get a reply */
	int timeout = 100; /* 10 seconds (100 * 100ms) */
	while (!dbus_pending_call_get_completed(pending) && timeout > 0) {
		/* Read/write I/O and dispatch messages */
		dbus_connection_read_write_dispatch(ctx->conn, 100);
		timeout--;
	}

	if (timeout == 0) {
		fprintf(stderr, "Timeout waiting for registration reply\n");
		dbus_pending_call_unref(pending);
		return -1;
	}

	/* Get the reply */
	reply = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);

	if (!reply) {
		fprintf(stderr, "No reply received\n");
		return -1;
	}

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, reply)) {
		fprintf(stderr, "Failed to register application: %s\n", error.message);
		dbus_error_free(&error);
		dbus_message_unref(reply);
		return -1;
	}

	dbus_message_unref(reply);
	printf("[App] GATT application registered successfully\n");
	return 0;
}

/* Send raw HCI command */
static int hci_send_cmd(int hci_sock, uint16_t opcode, uint8_t *data, uint8_t len)
{
	uint8_t buf[260];
	int ret;

	buf[0] = HCI_COMMAND_PKT;
	buf[1] = opcode & 0xff;
	buf[2] = (opcode >> 8) & 0xff;
	buf[3] = len;

	if (len > 0 && data)
		memcpy(buf + 4, data, len);

	ret = write(hci_sock, buf, 4 + len);
	if (ret < 0) {
		fprintf(stderr, "HCI command write failed: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* UUID to advertising data bytes (full 128-bit UUID) */
static void uuid_to_adv_data(const char *uuid_str, uint8_t *adv_data, uint8_t *adv_len)
{
	/*
	 * AD Structure: [Length][Type][Data...]
	 * For custom 128-bit service UUID: 00000001-0000-1000-8000-00805f9b34fb
	 * Must advertise full 128-bit UUID (type 0x07) in little-endian format
	 */
	uint8_t pos = 0;

	/* Flags */
	adv_data[pos++] = 0x02;  /* Length */
	adv_data[pos++] = 0x01;  /* Type: Flags */
	adv_data[pos++] = 0x06;  /* LE General Discoverable, BR/EDR not supported */

	/* 128-bit Service UUID */
	adv_data[pos++] = 0x11;  /* Length: 17 bytes (1 type + 16 UUID) */
	adv_data[pos++] = 0x07;  /* Type: Complete list of 128-bit Service UUIDs */

	/* UUID: 00000001-0000-1000-8000-00805f9b34fb
	 * BLE spec requires the entire 16-byte UUID to be reversed (little-endian)
	 * Canonical form: 00 00 00 01 00 00 10 00 80 00 00 80 5f 9b 34 fb
	 * Wire format (fully reversed): fb 34 9b 5f 80 00 00 80 00 10 00 00 01 00 00 00
	 */
	adv_data[pos++] = 0xfb;  /* Byte 15 (MSB of node) */
	adv_data[pos++] = 0x34;  /* Byte 14 */
	adv_data[pos++] = 0x9b;  /* Byte 13 */
	adv_data[pos++] = 0x5f;  /* Byte 12 */
	adv_data[pos++] = 0x80;  /* Byte 11 */
	adv_data[pos++] = 0x00;  /* Byte 10 (LSB of node) */
	adv_data[pos++] = 0x00;  /* Byte 9 (MSB of clock_seq) */
	adv_data[pos++] = 0x80;  /* Byte 8 (LSB of clock_seq) */
	adv_data[pos++] = 0x00;  /* Byte 7 (MSB of time_hi) - FIXED! */
	adv_data[pos++] = 0x10;  /* Byte 6 (LSB of time_hi) - FIXED! */
	adv_data[pos++] = 0x00;  /* Byte 5 (MSB of time_mid) */
	adv_data[pos++] = 0x00;  /* Byte 4 (LSB of time_mid) */
	adv_data[pos++] = 0x01;  /* Byte 3 (MSB of time_low) */
	adv_data[pos++] = 0x00;  /* Byte 2 */
	adv_data[pos++] = 0x00;  /* Byte 1 */
	adv_data[pos++] = 0x00;  /* Byte 0 (LSB of time_low) */

	*adv_len = pos;
}

/* Configure controller for LE-only operation */
static int configure_le_only_mode(int hci_sock)
{
	uint8_t params[2];

	printf("[HCI] Disabling BR/EDR scan to prevent Classic Bluetooth connections...\n");

	/* Disable BR/EDR scanning (No Inquiry Scan, No Page Scan) */
	params[0] = 0x00;  /* Scan Enable: 0 = No scans enabled */
	if (hci_send_cmd(hci_sock, HCI_OP_WRITE_SCAN_ENABLE, params, 1) < 0) {
		fprintf(stderr, "[HCI] Warning: Failed to disable BR/EDR scan\n");
		/* Continue anyway - might already be disabled */
	}

	usleep(50000);

	printf("[HCI] Enabling LE-only mode...\n");

	/* Write LE Host Supported: Enable LE, Disable Simultaneous LE/BR/EDR */
	params[0] = 0x01;  /* LE Supported (Host) = 1 */
	params[1] = 0x00;  /* Simultaneous LE and BR/EDR = 0 (disabled) */

	if (hci_send_cmd(hci_sock, HCI_OP_WRITE_LE_HOST_SUPPORTED, params, 2) < 0) {
		fprintf(stderr, "[HCI] Warning: Failed to set LE host supported\n");
		/* Continue anyway - might already be set */
	}

	usleep(50000);

	printf("[HCI] Controller configured for LE-only operation\n");
	return 0;
}

/* Enable BLE advertising via direct HCI commands */
static int enable_ble_advertising(int hci_sock, const char *device_name)
{
	uint8_t params[15];
	uint8_t adv_data[31];
	uint8_t adv_len;
	uint8_t scan_rsp_len;
	uint8_t enable;
	int name_len;

	/* First, configure LE-only mode to prevent BR/EDR connections */
	if (configure_le_only_mode(hci_sock) < 0) {
		fprintf(stderr, "[HCI] Failed to configure LE-only mode\n");
		return -1;
	}

	printf("[HCI] Configuring BLE advertising parameters...\n");

	/* Set advertising parameters */
	memset(params, 0, sizeof(params));
	params[0] = 0x00; params[1] = 0x08;  /* min interval: 1.28s (2048 * 0.625ms) */
	params[2] = 0x00; params[3] = 0x08;  /* max interval: 1.28s */
	params[4] = 0x00;  /* ADV_IND - connectable undirected */
	params[5] = 0x00;  /* Own address type: public */
	params[6] = 0x00;  /* Peer address type */
	/* params[7-12] = peer address (all zeros) */
	params[13] = 0x07;  /* All channels */
	params[14] = 0x00;  /* Filter policy: allow all */

	if (hci_send_cmd(hci_sock, HCI_OP_LE_SET_ADV_PARAMS, params, sizeof(params)) < 0) {
		fprintf(stderr, "[HCI] Failed to set advertising parameters\n");
		return -1;
	}

	usleep(50000);  /* 50ms delay for command to process */

	/* Set advertising data - HCI spec requires exactly 32 bytes (1 length + 31 data, zero-padded) */
	memset(adv_data, 0, sizeof(adv_data));
	uuid_to_adv_data(SERVICE_UUID, adv_data + 1, &adv_len);
	adv_data[0] = adv_len;  /* First byte is advertising data length */

	printf("[HCI] Setting advertising data (len=%d)...\n", adv_len);

	/* Always send 32 bytes as per HCI spec: 1 length byte + 31 data bytes (zero-padded) */
	if (hci_send_cmd(hci_sock, HCI_OP_LE_SET_ADV_DATA, adv_data, 32) < 0) {
		fprintf(stderr, "[HCI] Failed to set advertising data\n");
		return -1;
	}

	usleep(50000);

	/* Set scan response data (device name) - HCI spec requires exactly 32 bytes */
	memset(adv_data, 0, sizeof(adv_data));
	name_len = strlen(device_name);
	if (name_len > 29)
		name_len = 29;  /* Max: 31 - 2 bytes overhead */

	/* Build scan response data */
	scan_rsp_len = name_len + 2;  /* AD length + type byte + name */
	adv_data[0] = scan_rsp_len;   /* Scan Response Data Length */
	adv_data[1] = name_len + 1;   /* AD Structure length */
	adv_data[2] = 0x09;           /* AD Type: Complete local name */
	memcpy(adv_data + 3, device_name, name_len);

	printf("[HCI] Setting scan response data (device name: %s)...\n", device_name);

	/* Always send 32 bytes as per HCI spec: 1 length byte + 31 data bytes (zero-padded) */
	if (hci_send_cmd(hci_sock, HCI_OP_LE_SET_SCAN_RSP_DATA, adv_data, 32) < 0) {
		fprintf(stderr, "[HCI] Failed to set scan response data\n");
		return -1;
	}

	usleep(50000);

	/* Enable advertising */
	enable = 0x01;
	printf("[HCI] Enabling BLE advertising...\n");

	if (hci_send_cmd(hci_sock, HCI_OP_LE_SET_ADV_ENABLE, &enable, 1) < 0) {
		fprintf(stderr, "[HCI] Failed to enable advertising\n");
		return -1;
	}

	printf("[HCI] BLE advertising enabled successfully\n");
	return 0;
}

/* Open HCI socket */
static int open_hci_socket(int dev_id)
{
	struct sockaddr_hci addr;
	int sock;

	sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (sock < 0) {
		fprintf(stderr, "Failed to create HCI socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.hci_family = AF_BLUETOOTH;
	addr.hci_dev = dev_id;
	addr.hci_channel = 0;  /* HCI_CHANNEL_RAW */

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Failed to bind HCI socket: %s\n", strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

/* Register BLE advertisement with BlueZ (REMOVED - doesn't work without mgmt API) */
static int register_advertisement_unused(AppContext *ctx)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, dict;
	DBusError error;
	DBusPendingCall *pending;

	printf("[App] Registering BLE advertisement at %s...\n", ADV_PATH);

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    LE_ADVERTISING_MANAGER_IFACE,
					    "RegisterAdvertisement");
	if (!msg) {
		fprintf(stderr, "Failed to create RegisterAdvertisement message\n");
		return -1;
	}

	dbus_message_iter_init_append(msg, &iter);
	const char *adv_path = ADV_PATH;
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &adv_path);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
	dbus_message_iter_close_container(&iter, &dict);

	/* Send asynchronously so we can pump messages while waiting */
	if (!dbus_connection_send_with_reply(ctx->conn, msg, &pending, -1)) {
		fprintf(stderr, "Failed to send RegisterAdvertisement message\n");
		dbus_message_unref(msg);
		return -1;
	}
	dbus_message_unref(msg);

	if (!pending) {
		fprintf(stderr, "Pending call is NULL\n");
		return -1;
	}

	printf("[App] Waiting for advertisement registration reply (pumping message queue)...\n");

	/* Pump messages until we get a reply */
	int timeout = 100; /* 10 seconds (100 * 100ms) */
	while (!dbus_pending_call_get_completed(pending) && timeout > 0) {
		/* Read/write I/O and dispatch messages */
		dbus_connection_read_write_dispatch(ctx->conn, 100);
		timeout--;
	}

	if (timeout == 0) {
		fprintf(stderr, "Timeout waiting for advertisement registration reply\n");
		dbus_pending_call_unref(pending);
		return -1;
	}

	/* Get the reply */
	reply = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);

	if (!reply) {
		fprintf(stderr, "No reply received\n");
		return -1;
	}

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, reply)) {
		fprintf(stderr, "Failed to register advertisement: %s\n", error.message);
		dbus_error_free(&error);
		dbus_message_unref(reply);
		return -1;
	}

	dbus_message_unref(reply);
	printf("[App] BLE advertisement registered successfully - device should now be visible\n");
	return 0;
}

/* Find adapter path */
static char *find_adapter(DBusConnection *conn)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, array, dict_entry, variant;
	DBusError error;
	char *adapter_path = NULL;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    "/",
					    DBUS_OM_IFACE,
					    "GetManagedObjects");
	if (!msg)
		return NULL;

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &error);
	dbus_message_unref(msg);

	if (!reply) {
		dbus_error_free(&error);
		return NULL;
	}

	dbus_message_iter_init(reply, &iter);
	dbus_message_iter_recurse(&iter, &array);

	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
		const char *path;

		dbus_message_iter_recurse(&array, &dict_entry);
		dbus_message_iter_get_basic(&dict_entry, &path);

		if (strstr(path, "/hci0")) {
			adapter_path = strdup(path);
			break;
		}

		dbus_message_iter_next(&array);
	}

	dbus_message_unref(reply);
	return adapter_path;
}

/* D-Bus message dispatcher callback for GLib integration */
static gboolean dbus_dispatch_callback(gpointer user_data)
{
	DBusConnection *conn = (DBusConnection *)user_data;

	/* Read/write I/O (non-blocking) */
	dbus_connection_read_write(conn, 0);

	/* Process all pending messages */
	while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS)
		;

	return G_SOURCE_CONTINUE;
}

/* Set adapter powered and discoverable */
static int setup_adapter(AppContext *ctx)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, variant;
	DBusError error;
	char *device_name;

	/* Power on */
	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    DBUS_PROP_IFACE,
					    "Set");
	if (!msg)
		return -1;

	dbus_message_iter_init_append(msg, &iter);
	const char *adapter_iface = ADAPTER_IFACE;
	const char *powered_prop = "Powered";
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &adapter_iface);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &powered_prop);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
	dbus_bool_t powered = TRUE;
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &powered);
	dbus_message_iter_close_container(&iter, &variant);

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(ctx->conn, msg, -1, &error);
	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "Failed to power on adapter: %s\n", error.message);
		dbus_error_free(&error);
		return -1;
	}
	dbus_message_unref(reply);

	/* Set pairable to allow connections */
	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    DBUS_PROP_IFACE,
					    "Set");
	if (msg) {
		dbus_message_iter_init_append(msg, &iter);
		const char *pairable_prop = "Pairable";
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &adapter_iface);
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &pairable_prop);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
		dbus_bool_t pairable = TRUE;
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &pairable);
		dbus_message_iter_close_container(&iter, &variant);

		dbus_error_init(&error);
		reply = dbus_connection_send_with_reply_and_block(ctx->conn, msg, -1, &error);
		dbus_message_unref(msg);

		if (!reply) {
			printf("[App] Warning: Failed to set pairable: %s\n", error.message);
			dbus_error_free(&error);
		} else {
			dbus_message_unref(reply);
			printf("[App] Adapter set to pairable\n");
		}
	}

	/* Try to set discoverable (optional - may not be supported on BLE-only devices) */
	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    DBUS_PROP_IFACE,
					    "Set");
	if (msg) {
		dbus_message_iter_init_append(msg, &iter);
		const char *discoverable_prop = "Discoverable";
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &adapter_iface);
		dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &discoverable_prop);
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
		dbus_bool_t discoverable = TRUE;
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &discoverable);
		dbus_message_iter_close_container(&iter, &variant);

		dbus_error_init(&error);
		reply = dbus_connection_send_with_reply_and_block(ctx->conn, msg, -1, &error);
		dbus_message_unref(msg);

		if (!reply) {
			printf("[App] Note: Discoverable property not supported (BLE-only device): %s\n", error.message);
			dbus_error_free(&error);
			/* Don't fail - continue without discoverable mode */
		} else {
			dbus_message_unref(reply);
			printf("[App] Discoverable mode enabled\n");
		}
	}

	/* Set device name */
	device_name = get_device_name();

	msg = dbus_message_new_method_call(BLUEZ_SERVICE,
					    ctx->adapter_path,
					    DBUS_PROP_IFACE,
					    "Set");
	if (!msg) {
		free(device_name);
		return -1;
	}

	dbus_message_iter_init_append(msg, &iter);
	const char *alias_prop = "Alias";
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &adapter_iface);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &alias_prop);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &device_name);
	dbus_message_iter_close_container(&iter, &variant);

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(ctx->conn, msg, -1, &error);
	dbus_message_unref(msg);

	if (!reply) {
		printf("[App] Warning: Failed to set device name: %s\n", error.message);
		dbus_error_free(&error);
		/* Continue anyway - name is not critical */
	} else {
		dbus_message_unref(reply);
		printf("[App] Device name set to: %s\n", device_name);
	}

	printf("[App] Adapter setup complete (powered on)\n");

	/*
	 * Note: BLE advertising will be handled by the patched ATBM driver
	 * which adds HCI LE Advertising API support. This allows BlueZ's
	 * LEAdvertisingManager1 to control advertising properly.
	 */

	free(device_name);
	return 0;
}

int main(int argc, char **argv)
{
	AppContext ctx = {0};
	DBusError error;

	g_ctx = &ctx;
	ensure_provision_dir();

	/* Create GLib main loop first */
	ctx.loop = g_main_loop_new(NULL, FALSE);

	/* Connect to system bus */
	dbus_error_init(&error);
	ctx.conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (!ctx.conn) {
		fprintf(stderr, "Failed to connect to D-Bus: %s\n", error.message);
		dbus_error_free(&error);
		return 1;
	}

	/* Set up D-Bus message dispatcher with GLib - dispatch every 10ms */
	g_timeout_add(10, dbus_dispatch_callback, ctx.conn);

	/* Find adapter */
	ctx.adapter_path = find_adapter(ctx.conn);
	if (!ctx.adapter_path) {
		fprintf(stderr, "No Bluetooth adapter found\n");
		return 1;
	}

	printf("[App] Using adapter: %s\n", ctx.adapter_path);

	/* Setup adapter */
	if (setup_adapter(&ctx) < 0) {
		fprintf(stderr, "Failed to setup adapter\n");
		return 1;
	}

	/* Register characteristics */
	register_characteristics(&ctx);

	/* Flush and process any pending D-Bus messages */
	dbus_connection_flush(ctx.conn);
	while (dbus_connection_dispatch(ctx.conn) == DBUS_DISPATCH_DATA_REMAINS)
		;

	/* Register GATT application */
	if (register_application(&ctx) < 0) {
		fprintf(stderr, "Failed to register GATT application\n");
		return 1;
	}

	/* Enable BLE advertising via direct HCI commands */
	/* BlueZ 5.79 requires mgmt API advertising support, which this old kernel doesn't have */
	/* So we send HCI commands directly, which our driver patch intercepts and translates */
	int hci_sock = open_hci_socket(0);  /* hci0 */
	if (hci_sock < 0) {
		fprintf(stderr, "Failed to open HCI socket for advertising\n");
		return 1;
	}

	char *device_name = get_device_name();
	if (enable_ble_advertising(hci_sock, device_name) < 0) {
		fprintf(stderr, "Failed to enable BLE advertising\n");
		free(device_name);
		close(hci_sock);
		return 1;
	}
	free(device_name);
	close(hci_sock);

	printf("\n");
	printf("=======================================================\n");
	printf("  Thingino BLE Provisioning Service Active\n");
	printf("=======================================================\n");
	printf("Service UUID: %s\n", SERVICE_UUID);
	printf("Registered %d characteristics\n", ctx.num_chrcs);
	printf("\nDevice is now discoverable and ready for provisioning\n");
	printf("Use a BLE app to connect and configure:\n");
	printf("  - WiFi credentials\n");
	printf("  - Admin password\n");
	printf("  - Device hostname\n");
	printf("  - BLE proxy settings\n");
	printf("=======================================================\n\n");

	/* Run main loop (already created earlier) */
	g_main_loop_run(ctx.loop);

	/* Cleanup */
	g_main_loop_unref(ctx.loop);
	dbus_connection_unref(ctx.conn);
	free(ctx.adapter_path);

	for (int i = 0; i < ctx.num_chrcs; i++) {
		free(ctx.chrcs[i]->uuid);
		free(ctx.chrcs[i]->path);
		free(ctx.chrcs[i]);
	}

	return 0;
}
