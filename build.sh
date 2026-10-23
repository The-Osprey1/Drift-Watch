#!/usr/bin/env bash
# Builds the firmware and makes one merged .bin to flash at address 0x0.
# Needs arduino-cli with the esp32 core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17.
# Usage: ./build.sh [extra arduino-cli args, e.g. --config-file my.yaml]
# Set DRIFT_DIAG=1 to build a version that logs frame and memory timings.
set -euo pipefail
cd "$(dirname "$0")"

FQBN="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,CDCOnBoot=cdc,USBMode=hwcdc"

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
  --output-dir build --warnings default DriftWatch

# build/ now holds the bootloader, partition table and app; join them into one image.
BOOT_APP0=$(find "$(arduino-cli "$@" config get directories.data)" -path '*esp32/2.0.17/tools/partitions/boot_app0.bin' | head -1)
esptool.py --chip esp32s3 merge_bin -o out/drift_watch_v$(grep -oP 'FW_VERSION "\K[^"]+' DriftWatch/board.h)$SUFFIX.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/DriftWatch.ino.bootloader.bin \
  0x8000 build/DriftWatch.ino.partitions.bin \
  0xe000 "$BOOT_APP0" \
  0x10000 build/DriftWatch.ino.bin
ls -l out/
