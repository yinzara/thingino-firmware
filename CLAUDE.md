# Thingino Firmware - Project Context for LLMs

## Project Overview

**Thingino** is an open-source firmware for Ingenic SoC-based IP cameras. It provides a complete Linux-based operating system with video streaming, web UI, ONVIF support, and extensive hardware compatibility.

- **Pronunciation**: /θinˈdʒiːno/ (thin-jee-no)
- **Repository**: https://github.com/themactep/thingino-firmware
- **License**: Open source
- **Architecture**: Buildroot-based embedded Linux system

### Repository Branches

- **stable**: Production-ready, tested version with original ONVIF server and Prudynt with libconfig
- **master**: Development branch with experimental features (Matroska, Opus, improved file recording)
- Current branch: **master**

## Build System Architecture

### Platform Requirements

**CRITICAL: Linux-Only Build System**
- **Supported Host Architectures**: x86_64 (amd64) or aarch64 (arm64) Linux ONLY
- **NOT supported**: macOS (Darwin), Windows, BSD
- **Required**: GNU coreutils (dd version >= 9.0), glibc >= 2.31 (or musl on Alpine)
- **Checked by**: `/scripts/dep_check.sh` at build start

**Building on macOS or Windows:**

Since the build system only supports Linux, Mac and Windows users must use one of these approaches:

1. **Docker** (Recommended for Mac users)
   ```bash
   # Use Debian/Ubuntu-based container
   docker run -it --rm \
     -v ~/github/thingino-firmware:/build \
     -v ~/dl:/dl \
     debian:bookworm bash

   # Inside container:
   cd /build
   apt-get update
   ./scripts/dep_check.sh  # Install dependencies
   make
   ```

2. **Linux VM** (UTM, Parallels, VMware, VirtualBox)
   - Install Ubuntu 22.04+ or Debian 12+
   - Clone repository in VM
   - Follow normal build process

3. **Remote Linux Server** (SSH to build machine)
   - Use a remote Linux server or cloud instance
   - Build via SSH session

4. **GitHub Actions** (CI/CD)
   - The project has GitHub Actions workflows
   - Fork and use CI to build
   - Download artifacts from Actions tab

**Reference**: Buildroot has a Dockerfile at `/buildroot/support/docker/Dockerfile` for CI builds.

### Build System Type
- **Buildroot External Tree** - Uses BR2_EXTERNAL mechanism for clean separation from upstream Buildroot
- **Makefile-based** - Top-level Makefile orchestrates all build operations
- **Configuration-driven** - Hierarchical configuration system with fragments

### Quick Build Commands

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/themactep/thingino-firmware
cd thingino-firmware

# Build (interactive camera selection)
make

# Build specific camera
make CAMERA=atom_cam2_t31x_gc2053_atbm6031

# Fast parallel build
make fast

# Clean rebuild
make cleanbuild

# Release build (no local overrides)
make release

# Rebuild specific package
make rebuild-<package>

# OTA updates
make upgrade_ota CAMERA_IP_ADDRESS=192.168.1.10
```

### Build System Key Variables

- `BR2_DL_DIR` - Download cache directory (default: ~/dl)
- `OUTPUT_DIR` - Build output (branch/camera specific: ~/output-<branch>/<camera>)
- `CAMERA_IP_ADDRESS` - Target IP for OTA updates (default: 192.168.1.10)
- `SDCARD_DEVICE` - SD card device for flashing
- `TFTP_IP_ADDRESS` - TFTP server for firmware uploads

### Build Process Flow

1. **Board Selection** (board.mk) - Interactive whiptail menu or CAMERA variable
2. **Configuration Assembly** - Merges common.config + fragments + module config + camera defconfig
3. **Configuration Validation** - Buildroot olddefconfig resolves dependencies
4. **Package Building** - Buildroot builds all selected packages
5. **Filesystem Assembly** - Creates RootFS from packages + overlays
6. **Partition Creation** - Generates config.jffs2 and extras.jffs2
7. **Firmware Assembly** - Combines U-Boot + env + config + kernel + rootfs + extras
8. **Image Generation** - Creates full and update images with SHA256 checksums

## Directory Structure

```
thingino-firmware/
├── buildroot/              # Buildroot submodule (build system foundation)
├── configs/                # All hardware and build configurations
│   ├── cameras/           # Camera-specific defconfigs (100+ cameras)
│   ├── fragments/         # Reusable config fragments
│   ├── common.config      # Baseline settings for all builds
│   ├── local.config       # Local dev overrides (gitignored)
│   └── local.fragment     # Local buildroot additions (gitignored)
├── package/               # Custom packages (116 packages)
│   ├── prudynt-t/        # Main video streamer
│   ├── thingino-webui/   # Web interface
│   ├── ingenic-sdk/      # Vendor SDK
│   ├── wifi/             # WiFi drivers (ATBM, Realtek, Broadcom, etc.)
│   └── all-patches/      # Package patches
├── overlay/               # Filesystem overlay layers
│   ├── lower/            # Read-only base overlay (system scripts)
│   ├── config/           # Writable config partition files
│   ├── upper/            # Additional writable overlay
│   └── opt/              # Extras partition files
├── scripts/               # Build and utility scripts
├── docs/                  # Documentation
├── board/                 # Board-specific files
├── linux/                 # Linux kernel customizations
├── retired/               # Deprecated configurations
├── Makefile              # Main build orchestration
├── board.mk              # Camera selection logic
├── thingino.mk           # SoC/sensor/kernel configuration
└── external.mk           # Build info display

Output (gitignored):
~/output-<branch>/<camera>/  # Build artifacts
~/dl/                        # Download cache
```

## Configuration System

### Configuration Hierarchy (3 levels)

1. **Common Configuration** - `/configs/common.config` (baseline for all builds)
2. **Module Configuration** - Shared settings for similar hardware (referenced via MODULE:)
3. **Camera Configuration** - `/configs/cameras/<camera>/<camera>_defconfig` (device-specific)

### Configuration Fragments

Located in `/configs/fragments/`, referenced via `FRAG:` directive in camera defconfigs.

**Toolchain Fragments:**
- `toolchain-xburst1.fragment` - XBurst1 architecture
- `toolchain-xburst2.fragment` - XBurst2 architecture
- `toolchain-xb1-gcc14-musl-br.fragment` - GCC 14 + musl libc
- `toolchain-xb1-gcc15-glibc-br.fragment` - GCC 15 + glibc

**System Fragments:**
- `system.fragment` - Base system configuration
- `rootfs.fragment` - Root filesystem settings
- `kernel.fragment` - Kernel configuration
- `brand.fragment` - Branding
- `ssl.fragment`, `ssl-mbedtls.fragment`, `ssl-openssl.fragment` - SSL/TLS options
- `soc-xburst1.fragment`, `soc-xburst2.fragment` - SoC-specific settings

**Example Camera Defconfig:**
```bash
# NAME: ATOM Cam 2 (T31X, GC2053, ATBM6031)
# FRAG: soc-xburst1 toolchain-xburst1 ccache brand rootfs kernel system target uboot ssl
BR2_PACKAGE_WIFI=y
BR2_PACKAGE_WIFI_ATBM6031=y
BR2_SENSOR_GC2053=y
BR2_SOC_INGENIC_T31X=y
BR2_AVPU_CLK_600MHZ=y
BR2_ISP_CLK_200MHZ=y
FLASH_SIZE_16=y
```

### Local Development Overrides

For development without committing changes:
- `/configs/local.fragment` - Local buildroot config additions
- `/configs/local.config` - Local settings
- `/configs/local.uenv.txt` - Local U-Boot environment variables
- `/local.mk` - Local makefile overrides

## Supported Hardware

### Ingenic SoC Families

**XBurst 1 Architecture (MIPS):**
- T10 series: T10L, T10N, T10A
- T20 series: T20L, T20N, T20X, T20Z
- T21 series: T21L, T21N, T21X, T21Z, T21ZL
- T23 series: T23N, T23DL, T23ZN
- T30 series: T30L, T30N, T30X, T30A
- T31 series: T31L, T31LC, T31N, T31X, T31A, T31AL, T31ZL, T31ZX
- C100

**XBurst 2 Architecture (MIPS64):**
- T40 series: T40N, T40NN, T40XP, T40A
- T41 series: T41LQ, T41NQ, T41ZL, T41ZN, T41ZX, T41A
- A1 series: A1N, A1NT, A1X, A1L, A1A

**RAM Configurations:** 32MB - 512MB (depending on SoC)

### Supported Image Sensors (100+ sensors)

**Major Vendors:**
- **GalaxyCore (GC)**: GC0328, GC1034, GC2053, GC2083, GC4653, GC5035, etc.
- **SmartSens (SC)**: SC1235, SC2235, SC2336, SC3338, SC4335, SC4653, etc.
- **OmniVision (OV)**: OV2735B, OV5648, OV9712, OV9732
- **Sony (IMX)**: IMX307, IMX327, IMX335, IMX415, IMX664
- **JXC (JXF/JXH/JXK/JXQ)**: Multiple models
- **OmniSensor (OS)**: OS02G10, OS04B10, OS05A20, etc.

### WiFi Chipsets Supported

Located in `/package/wifi/`:
- **ATBM**: 6011, 6012, 6031, 6062, 6132, etc.
- **Realtek**: RTL8188EU, RTL8188FU, RTL8189FS, RTL8192FS, RTL8733BU, etc.
- **Broadcom**: AP6181, AP6212, AP6214, AP6256
- **SSW**: SSV6355, SSV6256P
- **AIC**: AIC8800
- **Hisilicon**: Hi3881
- **Wireless**: WQ9001

### Linux Kernel Versions

- **3.10** - Default for older SoCs (T10-T31)
- **4.4** - For T40, T41, A1, and optionally T31
- Source: https://github.com/gtxaspec/thingino-linux

## Key Software Components

### Video Streaming (Streamer Packages)

**Primary Streamer - prudynt-t**
- Location: `/package/prudynt-t/`
- Installed to: `/usr/bin/prudynt`
- Features:
  - Hardware-accelerated encoding via Ingenic ISP (libimp)
  - RTSP, RTMP, WebRTC support
  - Multi-stream capability
  - Optional FFmpeg support for Matroska/Opus (master branch)
  - Debug builds with AddressSanitizer support
- Configuration: `/etc/prudynt.cfg`

**Alternative Streamers:**
- **raptor-ipc** - Lightweight alternative
- **go2rtc** - Go-based RTSP/WebRTC gateway
- **thingino-live555** - RTSP server library

**Audio Support:**
- **ingenic-audiodaemon** - Audio processing daemon
- **faac** - AAC encoder
- **libhelix-aac** - AAC decoder
- **thingino-opus** - Opus codec support

### Web Interface (thingino-webui)

- Location: `/package/thingino-webui/`
- Web server: BusyBox httpd (default) or nginx (optional)
- Web root: `/var/www/`
- Features:
  - Configuration interface
  - Live video streaming (MJPEG, via `/var/www/x/video.mjpg`)
  - Camera control (pan/tilt, IR, audio)
  - System management
  - Firmware updates
- CGI scripts for dynamic content
- Files: `httpd.conf`, `nginx.conf`, `mjpeg_frame.c`, `mjpeg_inotify.c`

### ONVIF Support

**Packages:**
- **thingino-onvif** - Custom ONVIF server implementation (stable branch)
- **onvif-simple-server** - Lightweight ONVIF alternative
- Features: Device discovery, PTZ control, stream management, event handling

### Core System Packages

**Ingenic SDK & Libraries:**
- **ingenic-sdk** - Vendor SDK (libimp, libalog, sysutils)
- **ingenic-lib** - Hardware abstraction libraries
- **ingenic-diag-tools** - Diagnostic utilities
- **ingenic-pwm** - PWM control for IR-cut/LEDs
- **openimp** - Open-source IMP library alternative

**System Services:**
- **thingino-system** - Base system scripts and configuration
- **thingino-uboot** - U-Boot bootloader
- **dropbear** - SSH server
- **busybox** - Core utilities

**Network & Security:**
- **thingino-libcurl** - HTTP/MQTT/RTSP client
- **thingino-wolfssl** / **mbedtls** - SSL/TLS libraries
- **wpa_supplicant** - WiFi authentication
- **thingino-ethernet** - Wired networking support
- **zerotier-one** - VPN support

**Hardware Control:**
- **thingino-motors** - Pan/tilt motor control (SPI/TCU drivers)
- **thingino-ircut** - IR-cut filter control
- **thingino-ledd** - LED control daemon
- **thingino-button** - Button event handler
- **thingino-daynight** - Day/night mode switching
- **thingino-gpio** - GPIO utilities

**Additional Features:**
- **telegrambot** - Telegram bot integration
- **thingino-portal** - Captive portal for WiFi setup
- **thingino-mmc** - SD card support
- **thingino-ffmpeg** - FFmpeg with hardware acceleration
- **lightnvr** - Lightweight Network Video Recorder
- **mosquitto** - MQTT broker

### Development Packages

Enabled with `BR2_THINGINO_DEV_PACKAGES`:
- gdb, gdbserver
- strace, ltrace
- tcpdump, iperf3
- valgrind
- NFS support for remote debugging

## Firmware Partitioning

### Flash Layout

1. **U-Boot** (256KB fixed)
   - Offset: 0x000000
   - Bootloader

2. **U-Boot Environment** (32KB fixed)
   - Offset: 0x040000
   - Boot parameters and configuration

3. **Config Partition** (288KB fixed, JFFS2)
   - Offset: 0x048000
   - Persistent configuration storage (writable)
   - Built from: `overlay/config/` + `overlay/upper/`

4. **Kernel** (variable size, 32KB aligned)
   - uImage format
   - Compressed Linux kernel

5. **RootFS** (variable size, SquashFS)
   - Read-only root filesystem
   - Compressed with squashfs

6. **Extras** (remaining space, JFFS2)
   - Offset: After rootfs
   - Contains `/opt` directory contents
   - Additional software and data (writable)

### Flash Sizes Supported
- **NOR**: 8MB, 16MB, 32MB
- **NAND**: 128MB, 256MB, 512MB, 1GB

### Generated Firmware Images

Located in `~/output-<branch>/<camera>/images/`:
- `thingino-<camera>.bin` - Full firmware (all partitions)
- `thingino-<camera>-update.bin` - Update image (no bootloader/env)
- `*.sha256sum` - Checksum files

## Overlay Filesystem Structure

### Overlay Directories

**Lower Overlay** (`/overlay/lower/`)
- Read-only base filesystem overlay
- Contains:
  - `/usr/sbin/` - System utilities (blink, led, motion, service, conf, sysupgrade, etc.)
  - `/usr/sbin/send2*` - Notification scripts (telegram, email, mqtt, ftp, webhook)
  - `/usr/lib/mdev/` - Device management scripts
  - `/usr/share/udhcpc/` - DHCP client scripts
  - `/init` - Init script

**Config Overlay** (`/overlay/config/`)
- Files go into config partition (JFFS2, writable)
- Persistent configuration storage
- Survives firmware updates

**Upper Overlay** (`/overlay/upper/`)
- Additional writable overlay files
- Merged with config partition

**Opt Overlay** (`/overlay/opt/`)
- Files installed to extras partition
- Application data and extended features

## Important Scripts

### Build Scripts (`/scripts/`)

**Dependency Management:**
- `dep_check.sh` - Check and install build dependencies (Debian/Ubuntu)

**Build Helpers:**
- `update_buildroot.sh` - Update buildroot submodule
- `dl_buildroot_cache.sh` - Download cached build artifacts
- `cleanup_build_memo.sh` - Clean build memo files

**Firmware Tools:**
- `fw_ota.sh` - OTA firmware flashing
- `stitcher.sh` - Binary stitching for firmware assembly
- `binpadder.sh` - Binary padding for alignment

**SD Card Tools:**
- `sd_card_flasher.sh` - Flash firmware to SD card
- `sd_card_monitor.sh` - Monitor SD card operations
- `sd_utils.sh` - SD card utilities

**Development:**
- `check-git-package-updates.py` - Check for package updates
- `hijacker.sh` - Debugging helper
- `thingino_config_gen.sh` - Configuration generation
- `tabulate_gpio.py` - GPIO documentation generator

**Menu System:**
- `/scripts/menu/` - Interactive menu scripts
- `user-menu.sh` (top-level) - Main user menu interface

### Runtime System Scripts

Located in `/overlay/lower/usr/sbin/`:
- `blink` - LED blinking control
- `led` - LED control (on/off/status)
- `motion` - Motion detection control
- `service` - Service management
- `sysupgrade` - System upgrade utilities
- `send2telegram`, `send2email`, `send2mqtt`, `send2ftp`, `send2webhook` - Notification helpers
- `conf` - Configuration file management

## Package System

### Package Structure

Each package in `/package/<name>/` contains:
- `<name>.mk` - Buildroot makefile (build instructions)
- `Config.in` - Kconfig menu entry
- `files/` - Package-specific files (optional)
- Patches in `/package/all-patches/` or package-specific directories

### Package Categories

1. **System Packages** (`BR2_THINGINO_SYSTEM_PACKAGES`)
   - Essential firmware components
   - Networking, security, hardware control

2. **Streamer Packages** (`BR2_THINGINO_STREAMER_PACKAGES`)
   - Video/audio streaming components
   - Selected via menuconfig

3. **Extra Packages**
   - Optional features (ONVIF, Mosquitto, Zerotier, Telegram, etc.)

4. **Development Packages** (`BR2_THINGINO_DEV_PACKAGES`)
   - Debug tools, NFS support

### Key Package Count
116 total custom packages in `/package/`

## Documentation

### Key Documentation Files

**Hardware:**
- `/docs/supported_hardware.md` - Full list of supported cameras
- `/docs/sensors.md` - Image sensor information
- `/docs/connectors.md` - Hardware connector pinouts
- `/docs/gpio.md` - GPIO usage
- `/docs/ir-leds.md` - IR LED control

**Firmware:**
- `/docs/firmware.md` - Firmware overview
- `/docs/bootloader.md` - U-Boot information
- `/docs/overlayfs.md` - Overlay filesystem usage
- `/docs/camera-recovery.md` - Recovery procedures
- `/docs/hacking.md` - Development/hacking guide

**Setup & Configuration:**
- `/docs/Provisioning-System.md` - Device provisioning
- `/docs/wifi.md` - WiFi configuration
- `/docs/audio.md` - Audio setup
- `/docs/streamer.md` - Streamer configuration

**Development:**
- `/docs/buildroot.md` - Buildroot usage
- `/docs/diagnostics.md` - Troubleshooting
- `/docs/best-practices.md` - Development guidelines
- `/docs/glossary.md` - Terminology

## Common Development Tasks

### Adding a New Camera

1. Create camera defconfig in `/configs/cameras/<camera>/`
2. Reference appropriate fragments via `FRAG:` directive
3. Set SoC, sensor, WiFi chipset, flash size
4. Test build: `make CAMERA=<camera>`

### Modifying a Package

1. Edit package files in `/package/<name>/`
2. Rebuild package: `make rebuild-<package>`
3. Test changes

### Adding a New Package

1. Create `/package/<name>/<name>.mk`
2. Add Kconfig entry in `/package/<name>/Config.in`
3. Include Config.in in parent menu
4. Build and test

### Local Development

1. Create `/configs/local.fragment` for buildroot config additions
2. Create `/local.mk` for makefile overrides
3. Changes are gitignored, won't be committed

### Debugging on Target

1. Enable dev packages: `BR2_THINGINO_DEV_PACKAGES=y`
2. SSH to camera: `ssh root@<camera-ip>` (default password: thingino)
3. Use gdb, strace, tcpdump, etc.

## Important Configuration Variables

### SoC Selection
```makefile
BR2_SOC_INGENIC_T31X=y        # T31X SoC
BR2_AVPU_CLK_600MHZ=y         # AVPU clock speed
BR2_ISP_CLK_200MHZ=y          # ISP clock speed
```

### Sensor Selection
```makefile
BR2_SENSOR_GC2053=y           # GC2053 sensor
```

### WiFi Selection
```makefile
BR2_PACKAGE_WIFI=y
BR2_PACKAGE_WIFI_ATBM6031=y   # ATBM6031 WiFi chipset
```

### Flash Size
```makefile
FLASH_SIZE_8=y                # 8MB flash
FLASH_SIZE_16=y               # 16MB flash
FLASH_SIZE_32=y               # 32MB flash
```

### Streamer Selection
```makefile
BR2_PACKAGE_PRUDYNT_T=y       # Prudynt-t streamer
BR2_PACKAGE_RAPTOR_IPC=y      # Raptor streamer
BR2_PACKAGE_GO2RTC=y          # go2rtc
```

## Networking & Services

### Default Network Configuration
- **Wired**: DHCP client (if ethernet available)
- **Wireless**: WPA supplicant
- **Hostname**: thingino-<MAC>
- **SSH**: dropbear on port 22
- **HTTP**: BusyBox httpd on port 80
- **RTSP**: Port 554 (via streamer)
- **ONVIF**: Port 8080 (if enabled)

### Default Credentials
- **Username**: root
- **Password**: thingino (configurable)

### Service Management
Services controlled via `/etc/init.d/` scripts:
- `S01syslogd` - System logger
- `S35wifi` - WiFi initialization
- `S49ntp` - NTP client
- `S60prudynt` - Video streamer
- `S95thingino` - Thingino system services

## Build Optimization

### Parallel Building
```bash
make fast                      # Parallel build with all cores
make -j$(nproc)               # Explicit parallel jobs
```

### Caching
- Download cache: `~/dl/` (shared across builds)
- Buildroot cache: `BR2_CCACHE=y` (enabled by default)
- Configuration memo: `.thingino` files track config changes

### Incremental Builds
- Makefile tracks config changes
- Only regenerates configuration when needed
- Package rebuilds are minimal unless sources change

## Firmware Update Methods

### OTA (Over-The-Air)
```bash
make upgrade_ota CAMERA_IP_ADDRESS=192.168.1.10
make update_ota               # Update only (no bootloader)
make upboot_ota               # Bootloader only
```

### SD Card
```bash
make sdcard SDCARD_DEVICE=/dev/sdX
```

### TFTP
```bash
make tftp TFTP_IP_ADDRESS=192.168.1.100
```

### Recovery via Serial Console
See `/docs/camera-recovery.md` for UART-based recovery procedures.

## Key Files to Know

- `Makefile` - Main build orchestration
- `board.mk` - Camera selection and board configuration
- `thingino.mk` - SoC, sensor, kernel selection logic (1800+ lines)
- `external.mk` - Build information display
- `Config.in` - Main Kconfig menu structure
- `Config.soc.in` - SoC configuration menu (56KB, extensive)
- `Config.sensor.in` - Sensor configuration menu (10KB)
- `/configs/common.config` - Baseline configuration for all builds
- `/overlay/lower/init` - Init script

## References

- **Project Website**: https://thingino.com/
- **Wiki**: https://github.com/themactep/thingino-firmware/wiki
- **Discord**: https://discord.gg/xDmqS944zr
- **Telegram**: https://t.me/thingino
- **Buildroot Manual**: https://buildroot.org/downloads/manual/manual.html

## Development Guidelines

1. **Never edit buildroot submodule** - Use BR2_EXTERNAL mechanism
2. **Use configuration fragments** - Don't duplicate settings across cameras
3. **Test on real hardware** - Emulation is limited for camera SoCs
4. **Follow existing patterns** - Look at similar cameras/packages for examples
5. **Use local overrides** - Don't commit temporary dev changes
6. **Check dependencies** - Use `make show-depends` to see package dependencies
7. **Clean builds** - Use `make cleanbuild` when making significant changes
8. **Version control** - Commit logical, atomic changes with clear messages

## Common Issues & Solutions

### Build Fails with Missing Dependencies
Run: `./scripts/dep_check.sh` to install required packages

### Configuration Changes Not Taking Effect
- Delete `.thingino` memo file
- Run `make clean` then `make`

### Camera Won't Boot After Update
- Check flash size configuration matches hardware
- Verify partition sizes in Makefile
- Use recovery via UART/SD card

### WiFi Not Working
- Verify correct WiFi driver selected in defconfig
- Check kernel module loading in dmesg
- Verify wpa_supplicant configuration

### Video Stream Not Available
- Check prudynt configuration: `/etc/prudynt.cfg`
- Verify sensor driver loaded: `lsmod | grep sensor`
- Check ISP library initialization in logs

## Technology Stack Summary

- **Build System**: Buildroot 2024.x
- **Kernel**: Linux 3.10 / 4.4 (SoC-dependent)
- **Libc**: musl or glibc (configurable)
- **Toolchain**: GCC 14/15 for XBurst (MIPS/MIPS64)
- **Init System**: BusyBox init
- **Shell**: BusyBox ash
- **Filesystem**: SquashFS (rootfs) + JFFS2 (config/extras)
- **Video**: Ingenic ISP (libimp) + H.264/H.265 encoding
- **Streaming**: Prudynt-t (default) with RTSP/RTMP/WebRTC
- **Web Server**: BusyBox httpd or nginx
- **SSL/TLS**: WolfSSL or mbedTLS
- **MQTT**: Mosquitto client/broker
- **Languages**: C (system), Shell (scripts), Go (go2rtc), Python (tools)

---

This document provides comprehensive context about the Thingino firmware project for LLM assistants. When working with this codebase, refer to specific sections for detailed information about build processes, hardware support, software components, and development workflows.