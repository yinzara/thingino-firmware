# Makefile for Thingino Bluetooth C components

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

# Package config for dependencies
PKG_CONFIG ?= pkg-config

# Dependencies (removed protobuf-c to save ~2MB)
DBUS_CFLAGS := $(shell $(PKG_CONFIG) --cflags dbus-1)
DBUS_LIBS := $(shell $(PKG_CONFIG) --libs dbus-1)
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0)

# Combined flags
ALL_CFLAGS = $(CFLAGS) $(DBUS_CFLAGS) $(GLIB_CFLAGS)
ALL_LDFLAGS = $(LDFLAGS) $(DBUS_LIBS) $(GLIB_LIBS)

# Targets
TARGETS = ble-gatt-server ble-proxy-daemon

# Source files
GATT_SERVER_SRCS = ble-gatt-server.c
GATT_SERVER_OBJS = $(GATT_SERVER_SRCS:.c=.o)

# Proxy daemon sources (no protobuf - using manual encoding)
PROXY_DAEMON_SRCS = ble-proxy-daemon.c ble-scanner.c esphome-api.c
PROXY_DAEMON_OBJS = $(PROXY_DAEMON_SRCS:.c=.o)

.PHONY: all clean install

all: $(TARGETS)

ble-gatt-server: $(GATT_SERVER_OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $^ $(ALL_LDFLAGS)

ble-proxy-daemon: $(PROXY_DAEMON_OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $^ $(ALL_LDFLAGS)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

# Dependencies
ble-scanner.o: ble-scanner.c ble-scanner.h
esphome-api.o: esphome-api.c esphome-api.h esphome-proto-simple.h
ble-proxy-daemon.o: ble-proxy-daemon.c ble-scanner.h esphome-api.h
ble-gatt-server.o: ble-gatt-server.c

clean:
	rm -f $(TARGETS) *.o

install: $(TARGETS)
	install -D -m 0755 ble-gatt-server $(DESTDIR)/usr/sbin/ble-gatt-server
	install -D -m 0755 ble-proxy-daemon $(DESTDIR)/usr/sbin/ble-proxy-daemon
