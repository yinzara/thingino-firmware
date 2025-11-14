/*
 * BLE Proxy Daemon for Thingino
 *
 * Scans for BLE advertisements and forwards them to Home Assistant
 * via ESPHome Native API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <glib.h>

#include "ble-scanner.h"
#include "esphome-api.h"

#define DEFAULT_HA_HOST		"homeassistant.local"
#define DEFAULT_HA_PORT		6053
#define DEFAULT_ADAPTER		"/org/bluez/hci0"
#define PING_INTERVAL_SEC	60
#define RECONNECT_INTERVAL_SEC	30

struct proxy_context {
	struct ble_scanner *scanner;
	struct esphome_client *client;
	char ha_host[256];
	uint16_t ha_port;
	char adapter[128];
	char device_name[64];
	int batch_size;
	int batch_count;
	time_t last_ping;
	time_t last_connect_attempt;
	int running;
};

static struct proxy_context *g_ctx = NULL;

static void signal_handler(int sig)
{
	if (g_ctx) {
		printf("\n[Daemon] Received signal %d, shutting down\n", sig);
		g_ctx->running = 0;
		if (g_ctx->scanner)
			ble_scanner_quit(g_ctx->scanner);
	}
}

static void handle_ble_device(const struct ble_device *device, void *user_data)
{
	struct proxy_context *ctx = user_data;
	struct esphome_ble_adv adv = {0};

	if (!ctx->client || !esphome_client_is_connected(ctx->client))
		return;

	/* Convert device to ESPHome advertisement */
	strncpy(adv.address_str, device->address, sizeof(adv.address_str) - 1);
	adv.rssi = device->rssi;
	adv.name = device->name[0] ? device->name : NULL;

	if (device->has_mfg_data) {
		adv.adv_data = device->mfg_data;
		adv.adv_data_len = device->mfg_data_len;
	}

	/* Send advertisement */
	if (esphome_client_send_ble_adv(ctx->client, &adv) < 0) {
		fprintf(stderr, "[Daemon] Failed to send BLE advertisement\n");
		/* Connection likely lost, will reconnect on next iteration */
		esphome_client_disconnect(ctx->client);
	} else {
		printf("[Daemon] Forwarded device %s (RSSI: %d)\n",
		       device->address, device->rssi);
		ctx->batch_count++;
	}
}

static int connect_to_ha(struct proxy_context *ctx)
{
	time_t now = time(NULL);

	/* Don't retry too frequently */
	if (now - ctx->last_connect_attempt < RECONNECT_INTERVAL_SEC)
		return -1;

	ctx->last_connect_attempt = now;

	if (ctx->client)
		esphome_client_destroy(ctx->client);

	ctx->client = esphome_client_create(ctx->ha_host, ctx->ha_port,
					    ctx->device_name);
	if (!ctx->client) {
		fprintf(stderr, "[Daemon] Failed to create ESPHome client\n");
		return -1;
	}

	if (esphome_client_connect(ctx->client) < 0) {
		fprintf(stderr, "[Daemon] Failed to connect to Home Assistant\n");
		esphome_client_destroy(ctx->client);
		ctx->client = NULL;
		return -1;
	}

	printf("[Daemon] Connected to Home Assistant at %s:%d\n",
	       ctx->ha_host, ctx->ha_port);
	ctx->last_ping = time(NULL);

	return 0;
}

static gboolean maintenance_timer(gpointer user_data)
{
	struct proxy_context *ctx = user_data;
	time_t now = time(NULL);

	/* Try to reconnect if not connected */
	if (!ctx->client || !esphome_client_is_connected(ctx->client)) {
		printf("[Daemon] Not connected, attempting reconnection\n");
		connect_to_ha(ctx);
		return G_SOURCE_CONTINUE;
	}

	/* Send ping if needed */
	if (now - ctx->last_ping >= PING_INTERVAL_SEC) {
		if (esphome_client_ping(ctx->client) < 0) {
			fprintf(stderr, "[Daemon] Ping failed, disconnecting\n");
			esphome_client_disconnect(ctx->client);
		} else {
			ctx->last_ping = now;
			printf("[Daemon] Sent ping (batch_count: %d)\n", ctx->batch_count);
		}
	}

	return G_SOURCE_CONTINUE;
}

static int read_env_config(struct proxy_context *ctx)
{
	FILE *f;
	char line[512];
	char key[128], value[384];

	/* Read from fw_printenv or environment */
	f = popen("fw_printenv ble_proxy_ha_host 2>/dev/null", "r");
	if (f) {
		if (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "ble_proxy_ha_host=%s", value) == 1) {
				strncpy(ctx->ha_host, value, sizeof(ctx->ha_host) - 1);
			}
		}
		pclose(f);
	}

	f = popen("fw_printenv ble_proxy_ha_port 2>/dev/null", "r");
	if (f) {
		if (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "ble_proxy_ha_port=%s", value) == 1) {
				ctx->ha_port = atoi(value);
			}
		}
		pclose(f);
	}

	f = popen("fw_printenv ble_proxy_adapter 2>/dev/null", "r");
	if (f) {
		if (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "ble_proxy_adapter=%s", value) == 1) {
				strncpy(ctx->adapter, value, sizeof(ctx->adapter) - 1);
			}
		}
		pclose(f);
	}

	/* Get hostname for device name */
	f = popen("hostname 2>/dev/null", "r");
	if (f) {
		if (fgets(line, sizeof(line), f)) {
			sscanf(line, "%63s", ctx->device_name);
		}
		pclose(f);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct proxy_context ctx = {0};
	int ret = 0;

	g_ctx = &ctx;

	/* Set defaults */
	strncpy(ctx.ha_host, DEFAULT_HA_HOST, sizeof(ctx.ha_host) - 1);
	ctx.ha_port = DEFAULT_HA_PORT;
	strncpy(ctx.adapter, DEFAULT_ADAPTER, sizeof(ctx.adapter) - 1);
	strncpy(ctx.device_name, "thingino-camera", sizeof(ctx.device_name) - 1);
	ctx.batch_size = 10;

	/* Read configuration from environment */
	read_env_config(&ctx);

	printf("[Daemon] Starting BLE Proxy\n");
	printf("[Daemon] Home Assistant: %s:%d\n", ctx.ha_host, ctx.ha_port);
	printf("[Daemon] Adapter: %s\n", ctx.adapter);
	printf("[Daemon] Device Name: %s\n", ctx.device_name);

	/* Set up signal handlers */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Create scanner */
	ctx.scanner = ble_scanner_create(ctx.adapter);
	if (!ctx.scanner) {
		fprintf(stderr, "[Daemon] Failed to create BLE scanner\n");
		return 1;
	}

	/* Connect to Home Assistant */
	if (connect_to_ha(&ctx) < 0) {
		fprintf(stderr, "[Daemon] Initial connection to HA failed, will retry\n");
	}

	/* Start scanning */
	if (ble_scanner_start(ctx.scanner, handle_ble_device, &ctx) < 0) {
		fprintf(stderr, "[Daemon] Failed to start BLE scanning\n");
		ble_scanner_destroy(ctx.scanner);
		if (ctx.client)
			esphome_client_destroy(ctx.client);
		return 1;
	}

	/* Set up maintenance timer (runs every 10 seconds) */
	g_timeout_add_seconds(10, maintenance_timer, &ctx);

	ctx.running = 1;
	printf("[Daemon] BLE Proxy daemon running\n");

	/* Run event loop */
	ble_scanner_run(ctx.scanner);

	/* Cleanup */
	printf("[Daemon] Shutting down\n");
	ble_scanner_stop(ctx.scanner);
	ble_scanner_destroy(ctx.scanner);

	if (ctx.client) {
		esphome_client_disconnect(ctx.client);
		esphome_client_destroy(ctx.client);
	}

	return ret;
}
