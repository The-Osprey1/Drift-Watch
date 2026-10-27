#!/usr/bin/env bash
# Builds a factory/rescue image (0x0), an app0-only update (0x10000), and their manifest.
# Needs arduino-cli with the esp32 core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17.
# Usage: ./build.sh [extra arduino-cli args, e.g. --config-file my.yaml]
# Set DRIFT_DIAG=1 to build a version that logs frame and memory timings.
# Set DRIFT_PYTHON=python if your Python 3 executable is named python rather than python3.
# Serial runs on the USB-C port at 115200 baud.
set -euo pipefail
cd "$(dirname "$0")"

# CDCOnBoot=default keeps Serial on UART0 (GPIO 43/44). The board's USB-C port goes through a
# CH343P USB-to-serial chip wired to those pins, not to the ESP32-S3's own USB pins, so a build
# with CDCOnBoot=cdc sends Serial to a USB port that is not connected. v0.8.0 was built that way
# and stalled at start on the real watch with nothing on serial.
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc"

# TFT_eSPI screen setup for the round GC9A01 panel, passed in so no library files need editing.
TFT_FLAGS="-DUSER_SETUP_LOADED=1 -DGC9A01_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=240 \
-DTFT_MISO=12 -DTFT_MOSI=11 -DTFT_SCLK=10 -DTFT_CS=9 -DTFT_DC=8 -DTFT_RST=14 \
-DUSE_HSPI_PORT=1 -DSPI_FREQUENCY=80000000 \
-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1 -DLOAD_FONT6=1 -DLOAD_FONT7=1 -DLOAD_FONT8=1 -DLOAD_GFXFF=1 -DSMOOTH_FONT=1 -DDISABLE_ALL_LIBRARY_WARNINGS=1"
SUFFIX=""
if [ "${DRIFT_DIAG:-0}" = "1" ]; then TFT_FLAGS="$TFT_FLAGS -DDRIFT_DIAG=1"; SUFFIX="_diag"; fi

mkdir -p build out
arduino-cli "$@" compile ${CTAGS_DIR:+--build-property tools.ctags.path=$CTAGS_DIR} --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=$TFT_FLAGS" \
  --build-property "compiler.c.extra_flags=$TFT_FLAGS" \
  --output-dir build --warnings "${DRIFT_WARNINGS:-default}" DriftWatch

# Keep the legacy filename for the factory/rescue image. Its RAW padding erases NVS:
# existing settings/Wi-Fi survive only through the separate, compatible app0 update.
VERSION=$(grep -oP 'FW_VERSION "\K[^"]+' DriftWatch/board.h)
FACTORY="out/drift_watch_v${VERSION}${SUFFIX}.bin"
BOOT_APP0=$(find "$(arduino-cli "$@" config get directories.data)" -path '*esp32/2.0.17/tools/partitions/boot_app0.bin' | head -1)
esptool.py --chip esp32s3 merge_bin -o "$FACTORY" \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/DriftWatch.ino.bootloader.bin \
  0x8000 build/DriftWatch.ino.partitions.bin \
  0xe000 "$BOOT_APP0" \
  0x10000 build/DriftWatch.ino.bin
PACKAGE_FLAGS=()
if [ "${DRIFT_DIAG:-0}" = "1" ]; then PACKAGE_FLAGS+=(--diagnostic); fi
"${DRIFT_PYTHON:-python3}" tools/package_images.py "$VERSION" "$FACTORY" build/DriftWatch.ino.bin "${PACKAGE_FLAGS[@]}"
ls -l out/
