# Bluetooth - Complete Guide

**Complete Bluetooth Low Energy support for Thingino cameras**

## Table of Contents

- [Overview](#overview)
- [Supported Hardware](#supported-hardware)
- [Installation & Setup](#installation--setup)
- [BLE Provisioning](#ble-provisioning) - **Setup camera via phone**
- [BLE Proxy](#ble-proxy) - **Extend Home Assistant Bluetooth**
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)

---

## Overview

Thingino firmware supports Bluetooth Low Energy on compatible hardware, enabling:

### Modern Features (Recommended)

1. **BLE Provisioning** - Complete device setup via phone
   - Configure WiFi, admin password, hostname via Bluetooth
   - No computer or SSH needed
   - 60-second setup time
   - Secure encrypted transmission (AES-128)

2. **BLE Proxy** - Extend Home Assistant's Bluetooth range
   - Camera scans for nearby BLE devices
   - Forwards advertisements to Home Assistant
   - ESPHome API compatible
   - Works with Bermuda for room-level tracking

---

## Supported Hardware

### WiFi Chipsets with BLE Capability

**SSV6158/SV6158** (iComm Semiconductor) ✅ **Fully Supported**
- BLE 5.0
- WiFi 802.11b/g/n
- SDIO interface
- Cameras: Wuuk Y0510, Imou Ranger2 (SSV6155)

**ATBM6062 Series** (AltoBeam) ⚠️ **Planned**
- BLE 5.0
- WiFi 802.11b/g/n/ax (WiFi 6)
- USB/SDIO interface
- Cameras: Various with ATBM6062U/CU/S

**BCM43438** (Broadcom AP6212) ⚠️ **Under Investigation**
- BLE 4.1
- WiFi 802.11b/g/n

---

## Installation & Setup

### Building with BLE Support

#### Option 1: Use BLE-enabled Camera Configuration

```bash
make CAMERA=wuuk_y0510_t31x_sc4336p_ssv6158_ble
```

#### Option 2: Add BLE to Existing Configuration

Add to your camera's defconfig:
```makefile
# FRAG: ... bluetooth

# Enable BLE WiFi driver
BR2_PACKAGE_WIFI_SSV6158_BLE=y

# Enable Bluetooth utilities
BR2_PACKAGE_THINGINO_BLUETOOTH=y
```

### Runtime Configuration

Enable Bluetooth in `/etc/thingino.conf`:
```bash
enable_bluetooth=true
```

Or via environment:
```bash
fw_setenv enable_bluetooth true
```

### Verify Installation

```bash
# Check Bluetooth interface
hciconfig

# Expected output:
# hci0: Type: Primary Bus: SDIO
#       BD Address: XX:XX:XX:XX:XX:XX
#       UP RUNNING

# Check BlueZ daemon
ps | grep bluetoothd

# Check init script
/etc/init.d/S39bluetooth status
```

---

## BLE Provisioning

**Complete device setup via phone - no computer or SSH required**

### Quick Start

**What You Need:**
1. Thingino camera with Bluetooth (first boot)
2. Phone with BLE app (nRF Connect or LightBlue)
3. 60 seconds

**Minimal Setup:**
```
1. Power on camera (first boot)
2. Open nRF Connect → Scan → Connect to "Thingino-XXXX"
3. Discover Services → "Thingino Provisioning Service"
4. Write Admin Password (0x0007): "MySecurePassword"
5. Write WiFi SSID (0x0003): "MyNetwork"
6. Write WiFi Password (0x0004): "MyWiFiPassword"
7. Read Status (0x0005) → Wait for "completed"
8. Disconnect → Camera reboots
✅ Done! Camera on WiFi with secure credentials
```

**Complete Setup (with BLE Proxy):**
```
1-3. [Same as above]
4. Write Admin Password (0x0007): "SecurePass123"
5. Write Hostname (0x0008): "garage-camera"
6. Write WiFi SSID (0x0003): "HomeNetwork"
7. Write WiFi Password (0x0004): "WiFiPassword"
8. Write BLE Proxy Enabled (0x000C): "true"
9. Write BLE Proxy HA Host (0x000D): "homeassistant.local"
10. Read Status (0x0005) → Wait for "completed"
11. Disconnect → Camera reboots
✅ Camera accessible at http://garage-camera.local
✅ BLE proxy active in Home Assistant
```

### What Can Be Configured

| Feature | UUID | Required | Description |
|---------|------|----------|-------------|
| WiFi SSID | `0003` | ✅ Yes | Network name |
| WiFi Password | `0004` | ✅ Yes | Network password |
| Admin Password | `0007` | ✅ Yes | Web UI/SSH password |
| Provision Status | `0005` | Read | Current status |
| Admin Username | `0006` | ⬜ Optional | Username (default: root) |
| Hostname | `0008` | ⬜ Optional | mDNS hostname |
| BLE Proxy Enabled | `000C` | ⬜ Optional | Enable proxy (true/false) |
| BLE Proxy HA Host | `000D` | ⬜ Optional | Home Assistant host |
| BLE Proxy HA Port | `000E` | ⬜ Optional | ESPHome API port |

**Full UUIDs:** All use pattern `0000XXXX-0000-1000-8000-00805f9b34fb`

**Service UUID:** `00000001-0000-1000-8000-00805f9b34fb`

### Security Features

**🔒 Critical: Initial Setup Only**
- Admin password can ONLY be set during first boot
- After WiFi + admin configured, BLE provisioning **permanently disables**
- Prevents post-deployment password changes via BLE

**Encryption:**
- ✅ AES-128-CCM (Bluetooth LE Security Level 2)
- ✅ ECDH key exchange
- ✅ Ephemeral session keys
- ✅ No plaintext transmission

### Provisioning Status Values

| Status | Meaning |
|--------|---------|
| `waiting` | Waiting for credentials |
| `connecting` | Connecting to WiFi |
| `connected` | WiFi connected |
| `admin_setting` | Setting admin credentials |
| `completed` | ✅ All done |
| `failed` | WiFi connection failed |

### 📖 Full Provisioning Documentation

For complete details, see **[BLE Provisioning Complete Guide](./ble-provisioning-complete.md)**

---

## BLE Proxy

**Turn camera into BLE scanner for Home Assistant - extends Bluetooth range**

### Overview

The BLE Proxy makes your camera scan for nearby BLE devices and forward advertisements to Home Assistant, exactly like ESPHome's bluetooth_proxy or Shelly BLE Gateway.

**What it does:**
- 🔍 Scans for nearby BLE devices (phones, watches, trackers, sensors)
- 📡 Forwards advertisements to Home Assistant via ESPHome API
- 📍 Enables room-level presence tracking with Bermuda
- 🏠 Compatible with Home Assistant's Bluetooth integration

**Use cases:**
- Track family members via phones/watches
- Monitor BLE temperature/humidity sensors
- Presence-based automation triggers
- Room-level location tracking (multiple cameras)

### Quick Setup

**Option A: Configure via BLE (During Initial Setup)**

Include in provisioning workflow:
```
Write BLE Proxy Enabled (0x000C): "true"
Write BLE Proxy HA Host (0x000D): "homeassistant.local"
```

**Option B: Configure via SSH (After Deployment)**

```bash
fw_setenv ble_proxy_enabled true
fw_setenv ble_proxy_ha_host homeassistant.local
reboot
```

### Configuration Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `ble_proxy_enabled` | ✅ Yes | `false` | Master enable/disable |
| `ble_proxy_ha_host` | ✅ Yes | (none) | Home Assistant hostname/IP |
| `ble_proxy_ha_port` | ⬜ Optional | `6053` | ESPHome API port |
| `ble_proxy_adapter` | ⬜ Optional | `hci0` | Bluetooth adapter |
| `ble_proxy_batch_size` | ⬜ Optional | `10` | Advertisements per batch |

### Home Assistant Integration

**Automatic Discovery:**
1. Camera connects to HA ESPHome API (port 6053)
2. Sends HelloRequest with device name (hostname)
3. Camera appears in ESPHome integration
4. Nearby BLE devices detected by camera appear in Bluetooth integration

**Example automation:**
```yaml
# Turn on lights when phone enters room
automation:
  - alias: "Arrived at Garage"
    trigger:
      - platform: state
        entity_id: sensor.bermuda_phone_area
        to: "Garage"
    action:
      - service: light.turn_on
        target:
          entity_id: light.garage
```

### Bermuda Room Tracking

Install Bermuda via HACS for room-level tracking:
1. Multiple cameras act as BLE receivers
2. Bermuda measures RSSI from each camera
3. Triangulation determines which room device is in
4. Creates area sensors for each tracked device

### Verify BLE Proxy

```bash
# Check if enabled
fw_printenv ble_proxy_enabled  # Should show: true

# Check HA host
fw_printenv ble_proxy_ha_host  # Should show hostname/IP

# Check service status
/etc/init.d/S65ble-proxy status

# View logs
logread | grep ble-proxy
```

### 📖 Full Proxy Documentation

For complete details, see **[BLE Proxy Guide](./ble-proxy-homeassistant.md)**

---

## Troubleshooting

### Bluetooth Interface Not Found

**Check if BLE is enabled:**
```bash
hciconfig
```

**Expected output:**
```
hci0:   Type: Primary  Bus: SDIO
        BD Address: XX:XX:XX:XX:XX:XX  ACL MTU: 1021:8  SCO MTU: 64:1
        UP RUNNING
```

**If not found:**
```bash
# Check WiFi driver
lsmod | grep ssv

# Check firmware
ls /lib/firmware/*ble*

# Start Bluetooth
/etc/init.d/S39bluetooth start

# Check logs
logread | grep -i bluetooth
```

### BLE Provisioning Not Working

**Cannot find "Thingino-XXXX":**
- Camera may already be provisioned (BLE only runs on first boot)
- Check if enabled: `fw_printenv enable_bluetooth`
- Ensure within 10 meters range
- Reboot camera

**Characteristics not writable:**
- Verify correct service: UUID `00000001-...`
- Admin password (0x0007) only works during initial setup
- For reset: Factory reset camera

**Status stuck at "connecting":**
- Wrong WiFi password
- WiFi out of range
- Check logs: `logread | grep wpa_supplicant`

### BLE Proxy Not Starting

**Check 1: Is it enabled?**
```bash
fw_printenv ble_proxy_enabled  # Should return: true
```

**Check 2: Is HA host configured?**
```bash
fw_printenv ble_proxy_ha_host  # Should return hostname/IP
```

**Check 3: Can camera reach HA?**
```bash
ping homeassistant.local
nc -zv homeassistant.local 6053
```

**Check 4: Is service running?**
```bash
/etc/init.d/S65ble-proxy status
ps | grep ble-proxy-daemon
```

**Check 5: View logs:**
```bash
logread | grep ble-proxy
```

### Camera Not Appearing in Home Assistant

- Verify camera can ping HA: `ping homeassistant.local`
- Check ESPHome API port: `nc -zv homeassistant.local 6053`
- Ensure ESPHome integration enabled in HA
- Check HA logs for connection attempts

### BlueZ Daemon Not Running

```bash
# Check status
ps | grep bluetoothd

# Start manually
/etc/init.d/S39bluetooth start

# Check logs
logread | grep bluetooth
```

### BLE Scan Returns No Devices

```bash
# Ensure HCI is up
hciconfig hci0 up

# Test low-level scan
hcitool lescan

# Check for interference
# WiFi and BLE share 2.4GHz - ensure wifi_bt_comb=1
```

### Poor BLE Range

BLE range is typically 10-30 meters indoors.

**Improve range:**
- Position camera centrally
- Avoid metal obstacles
- Reduce WiFi interference (use 5GHz if possible)
- Check antenna connections

---

## Technical Details

### Bluetooth Hardware

**SSV6158 Chipset:**
- BLE Version: 5.0
- Protocol: Bluetooth Low Energy
- Range: ~10-30 meters (indoor)
- Frequency: 2.4 GHz ISM band
- Interface: SDIO

### BlueZ Stack

**Version:** 5.37 (compatible with kernel 3.10)
**Components:**
- `bluetoothd` - Main daemon
- `hciconfig` - HCI configuration
- `hcitool` - HCI tools
- `bluetoothctl` - CLI interface

**D-Bus API:** Used for GATT server and scanning

### BLE Provisioning Architecture

```
Phone (BLE Client)
    ↓ BLE GATT Protocol
BlueZ (bluetoothd)
    ↓ D-Bus IPC
ble-provision-server.py (GATT Server)
    ↓ Shell Commands
ble-provision (Provisioning Logic)
    ↓ fw_setenv, wpa_supplicant
System Configuration
```

### BLE Proxy Architecture

```
BLE Devices (Broadcasting)
    ↓ BLE Advertisements
BlueZ (bluetoothd)
    ↓ D-Bus Signals
ble_scanner.py (Scanner)
    ↓ Python Callbacks
ble-proxy-daemon.py (Batching)
    ↓ ESPHome API (Protobuf)
Home Assistant (Port 6053)
    ↓ Bluetooth Callbacks
Bermuda (Room Tracking)
```

### Resource Usage

**BLE Provisioning (during setup):**
- CPU: ~10-15%
- Memory: ~15 MB
- Duration: 30-60 seconds

**BLE Proxy (continuous):**
- CPU: ~3-5% (idle), ~10% (high traffic)
- Memory: ~5-10 MB
- Network: 1-5 KB/s typical

### Files and Scripts

**Init Scripts:**
- `/etc/init.d/S39bluetooth` - Start bluetoothd
- `/etc/init.d/S41ble-provision` - BLE provisioning service
- `/etc/init.d/S65ble-proxy` - BLE proxy service

**Python Scripts:**
- `/usr/sbin/ble-provision-server.py` - GATT server
- `/usr/sbin/ble-proxy-daemon.py` - Proxy daemon
- `/usr/sbin/esphome_api.py` - ESPHome API client
- `/usr/sbin/ble_scanner.py` - BLE scanner

**Shell Scripts:**
- `/usr/sbin/ble-provision` - Provisioning logic

### Performance Considerations

**Memory:**
- BlueZ stack: ~3 MB
- Python runtime: ~3 MB
- Per-service overhead: ~2-5 MB

**CPU:**
- Idle: <1%
- Active scanning: ~5%
- GATT server: ~10% during provisioning
- Proxy forwarding: ~3-5%

**WiFi Impact:**
- Minimal (hardware coexistence management)
- Ensure `wifi_bt_comb=1` parameter set

**Power:**
- BLE is very low power
- No significant battery impact

---

## Security Considerations

### BLE Provisioning Security

**Transport Encryption:**
- ✅ AES-128-CCM (BLE Security Level 2)
- ✅ ECDH key exchange
- ✅ Ephemeral session keys

**Initial Setup Only:**
- ⚠️ Admin password can ONLY be set during first boot
- ⚠️ After WiFi + admin configured, BLE provisioning disables
- ✅ Prevents post-deployment attacks

**Best Practices:**
- ✅ Use strong passwords (12+ characters)
- ✅ Provision in private (not public spaces)
- ✅ Verify status shows "completed"

### BLE Proxy Security

**Data Transmitted:**
- ✅ MAC addresses (public information)
- ✅ RSSI (signal strength)
- ✅ Advertisement data
- ❌ No camera credentials
- ❌ No video/images

**Protocol Security:**
- ⚠️ Plaintext ESPHome API (current implementation)
- ✅ Local network only (no cloud)
- 🔄 Noise Protocol encryption (planned)

**Privacy:**
- BLE MAC addresses are public
- Modern devices use random MAC rotation
- Enable MAC randomization on phones

### General BLE Security

1. **Broadcast Nature** - Anyone can scan for beacons
2. **No Pairing** - Scanning doesn't require pairing
3. **Limited Range** - 10-30 meters typical
4. **Disable When Unused** - `fw_setenv enable_bluetooth false`

---

## Advanced Usage

### Direct BlueZ Interaction

```bash
# Interactive Bluetooth control
bluetoothctl
> scan on
> devices
> quit
```

### GATT Characteristic Access

```bash
# Connect to BLE device
gatttool -b AA:BB:CC:DD:EE:FF -I
> connect
> characteristics
> char-read-uuid <uuid>
```

### Monitor BLE Traffic

```bash
# Capture HCI traffic
hcidump -X

# Monitor specific device
hcitool lescan | grep AA:BB:CC:DD:EE:FF
```

### Custom BLE Scanning

```bash
# Low-level LE scan
hcitool lescan

# LE scan with RSSI
hcitool lescan --duplicates

# Set scan parameters
hcitool cmd 0x08 0x000b 01 10 00 10 00 00 00
```

### Command Line Provisioning

For scripted deployments:
```bash
# Set credentials via command line
ble-provision set-admin root "MyAdminPassword"
ble-provision set-hostname "my-camera"
ble-provision apply "MyWiFiSSID" "MyWiFiPassword"
ble-provision status
```

---

## Camera Compatibility

### Confirmed Working

**WUUK Y0510** ✅
- SoC: T31X
- WiFi/BLE: SSV6158
- Full support: Provisioning + Proxy

**Imou Ranger 2** ✅
- SoC: T31L
- WiFi/BLE: SSV6155 (similar to SSV6158)
- Full support

### In Testing

**Various ATBM6062-based cameras** ⏳
- Requires driver updates
- BLE 5.0 capable
- WiFi 6 support

### Build Configuration

For specific camera models:
```bash
# WUUK Y0510 with BLE
make CAMERA=wuuk_y0510_t31x_sc4336p_ssv6158_ble

# Other SSV6158 cameras
# Add "_ble" suffix to camera name
make CAMERA=<your_camera>_ble
```

---

## Migration Guide

### From Manual WiFi Setup to BLE Provisioning

**Old Way:**
1. Connect to captive portal
2. Configure WiFi
3. SSH to camera
4. Set admin password
5. Configure hostname

**New Way:**
1. Open phone BLE app
2. Connect to "Thingino-XXXX"
3. Write all settings via BLE characteristics
4. Done in 60 seconds!

---

## Future Enhancements

### Planned Features

1. **BLE Proxy Enhancements**
   - ✅ Implemented: Plaintext ESPHome API
   - 🔄 Planned: Noise Protocol encryption
   - 🔄 Planned: Active scanning mode
   - 🔄 Planned: BLE connections (GATT client)

2. **Provisioning Enhancements**
   - ✅ Implemented: Complete device setup
   - 🔄 Planned: Web UI configuration
   - 🔄 Planned: QR code setup
   - 🔄 Planned: Pairing with PIN

3. **Additional Features**
   - 🔄 BLE mesh support
   - 🔄 Native sensor support (Xiaomi, Govee, Ruuvi)
   - 🔄 BLE file transfer
   - 🔄 Multi-camera coordination

### Community Contributions

Contributions welcome for:
- Additional camera support
- Driver improvements
- Feature requests
- Bug fixes

---

## References

### Documentation

- **[BLE Provisioning Complete Guide](./ble-provisioning-complete.md)** - Full provisioning documentation
- **[BLE Proxy Guide](./ble-proxy-homeassistant.md)** - Complete proxy documentation

### External Resources

- **OpenIPC ssv6x5x driver:** https://github.com/OpenIPC/ssv6x5x
- **BlueZ Documentation:** http://www.bluez.org/
- **ESPHome Bluetooth Proxy:** https://esphome.io/components/bluetooth_proxy.html
- **Home Assistant Bluetooth:** https://www.home-assistant.io/integrations/bluetooth/
- **Bermuda BLE Trilateration:** https://github.com/agittins/bermuda

### Community

- **Thingino GitHub:** https://github.com/themactep/thingino-firmware
- **Issues:** https://github.com/themactep/thingino-firmware/issues
- **Home Assistant Community:** https://community.home-assistant.io

---

## Summary

Thingino's Bluetooth support provides comprehensive BLE functionality:

### For New Users

**Recommended Setup:**
1. Use **BLE Provisioning** for initial camera setup (60 seconds via phone)
2. Enable **BLE Proxy** for Home Assistant integration
3. Install **Bermuda** for room-level tracking

### For Advanced Users

- Direct BlueZ access for custom applications
- Command-line provisioning for bulk deployments

### Key Benefits

- ✅ **Easy Setup** - Configure entirely from phone
- ✅ **Secure** - Encrypted BLE, initial setup only for admin
- ✅ **Integrated** - Native Home Assistant support
- ✅ **Extensible** - Open for custom applications
- ✅ **Low Resource** - Minimal CPU/memory impact

**Ready to get started? See the [Quick Start](#ble-provisioning) section!**

---

## Development History

### Implementation Phases

**Phase 1: Initial BLE Support**
- SSV6158 BLE driver integration (wifi-ssv6158-ble package)
- Apache NimBLE stack support
- BlueZ 5.37 downgrade for kernel 3.10 compatibility
- Initial Bluetooth stack integration

**Phase 2: WiFi Provisioning**
- BLE GATT service for WiFi configuration
- 4 initial characteristics (WiFi Scan, SSID, Password, Status)
- Auto-activation on first boot
- Phone-based setup without captive portal

**Phase 3: Complete Provisioning**
- Extended provisioning to include admin credentials and hostname
- Added 3 new characteristics (Admin Username, Admin Password, Hostname)
- Single workflow for complete device setup
- BLE encryption via AES-128-CCM

**Phase 4: Security Hardening**
- Critical fix: Admin password only settable during initial setup
- BLE provisioning auto-disables after WiFi + admin configured
- Prevents post-deployment password changes via BLE
- Initial setup only enforcement

**Phase 5: BLE Proxy (Current)**
- Complete paradigm shift to scanner/proxy model
- ESPHome Native API implementation (Protobuf + TCP)
- BlueZ D-Bus API scanner
- Advertisement batching and forwarding
- Home Assistant ESPHome integration
- Bermuda compatibility for room-level tracking
- BLE proxy configuration via provisioning characteristics

### Key Architectural Decisions

**ESPHome API vs MQTT:**
- Chose ESPHome API for native Home Assistant integration
- Plaintext protocol initially (Noise encryption planned)
- Compatible with existing HA Bluetooth infrastructure
- Works with Bermuda out of the box

**Proxy Model:**
- Implemented camera as BLE scanner/proxy (not beacon)
- Scans for nearby BLE devices and forwards to Home Assistant
- More versatile than beacon broadcasting
- Extends HA's Bluetooth range throughout home

**Security Model:**
- BLE provisioning only during initial setup
- Automatic disable after WiFi + admin configured
- Defense-in-depth: init script + runtime validation
- Prevents post-deployment attacks

**BlueZ Version:**
- Downgraded to BlueZ 5.37 for kernel 3.10 compatibility
- UHID kernel module issues resolved
- Ensures compatibility with embedded hardware

### Total Implementation

**Files Created:** ~20 files
**Lines of Code:** ~5,000+ lines (Python, Shell, Makefiles)
**Documentation:** ~4,000+ lines
**Characteristics:** 10 GATT characteristics
**Services:** 3 init scripts (bluetooth, provisioning, proxy)
**Packages:** 2 buildroot packages (wifi-ssv6158-ble, thingino-bluetooth)

### Contributors

Developed with assistance from AI code review and user feedback throughout the implementation process.

---

**Last Updated:** November 2025
**Version:** 2.0
**Compatible:** SSV6158-based cameras (WUUK Y0510, Imou Ranger 2, etc.)
