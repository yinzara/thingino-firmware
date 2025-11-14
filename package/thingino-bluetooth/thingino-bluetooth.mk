################################################################################
#
# thingino-bluetooth
#
################################################################################

THINGINO_BLUETOOTH_VERSION = 1.0
THINGINO_BLUETOOTH_SITE_METHOD = local
THINGINO_BLUETOOTH_SITE = $(BR2_EXTERNAL_THINGINO_PATH)/package/thingino-bluetooth

# Dependencies: dbus, libglib2
THINGINO_BLUETOOTH_DEPENDENCIES = dbus libglib2

define THINGINO_BLUETOOTH_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		-C $(@D)/src all
endef

define THINGINO_BLUETOOTH_INSTALL_TARGET_CMDS
	# Install C binaries
	$(INSTALL) -D -m 0755 $(@D)/src/ble-gatt-server \
		$(TARGET_DIR)/usr/sbin/ble-gatt-server
	$(INSTALL) -D -m 0755 $(@D)/src/ble-proxy-daemon \
		$(TARGET_DIR)/usr/sbin/ble-proxy-daemon

	# Install shell script for provisioning logic
	$(INSTALL) -D -m 0755 $(THINGINO_BLUETOOTH_PKGDIR)/files/ble-provision \
		$(TARGET_DIR)/usr/sbin/ble-provision

	# Install init scripts
	$(INSTALL) -D -m 0755 $(THINGINO_BLUETOOTH_PKGDIR)/files/S34wlan_power \
		$(TARGET_DIR)/etc/init.d/S34wlan_power
	$(INSTALL) -D -m 0755 $(THINGINO_BLUETOOTH_PKGDIR)/files/S39bluetooth \
		$(TARGET_DIR)/etc/init.d/S39bluetooth
	$(INSTALL) -D -m 0755 $(THINGINO_BLUETOOTH_PKGDIR)/files/S41ble-provision \
		$(TARGET_DIR)/etc/init.d/S41ble-provision
	$(INSTALL) -D -m 0755 $(THINGINO_BLUETOOTH_PKGDIR)/files/S65ble-proxy \
		$(TARGET_DIR)/etc/init.d/S65ble-proxy
endef

$(eval $(generic-package))
