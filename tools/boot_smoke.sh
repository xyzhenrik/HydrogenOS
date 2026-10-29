#!/usr/bin/env bash
set -euo pipefail

ARCH="${ARCH:-x86_64}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/$ARCH"
KERNEL_BIN_REL="build/$ARCH/kernel.bin"
DISK_IMG_REL="build/$ARCH/disk.img"
LOG_FILE="$BUILD_DIR/boot_smoke.log"
SUCCESS_STR="HydrogenOS bootet"
if [ "$ARCH" = "x86_64" ]; then
  SUCCESS_STR="init: shell start"
  rm -f "$ROOT_DIR/build/$ARCH/disk.img"
  make -C "$ROOT_DIR" ARCH="$ARCH" iso build
else
  make -C "$ROOT_DIR" ARCH="$ARCH" "$KERNEL_BIN_REL" "$DISK_IMG_REL"
fi

rm -f "$LOG_FILE"
if [ "$ARCH" = "x86_64" ]; then
  qemu-system-x86_64 \
    -cdrom "$ROOT_DIR/build/$ARCH/hydrogen.iso" \
    -drive if=virtio,format=raw,file="$ROOT_DIR/build/$ARCH/disk.img" \
    -m 256M \
    -serial stdio \
    -display none \
    -no-reboot -no-shutdown \
    >"$LOG_FILE" 2>&1 &
else
  qemu-system-x86_64 \
    -kernel "$ROOT_DIR/$KERNEL_BIN_REL" \
    -m 256M \
    -serial stdio \
    -display none \
    -drive format=raw,file="$ROOT_DIR/$DISK_IMG_REL" \
    -no-reboot -no-shutdown \
    >"$LOG_FILE" 2>&1 &
fi

QEMU_PID=$!
FOUND=0

i=0
while [ "$i" -lt 400 ]; do
  if grep -q "FRAME CHECKSUM:" "$LOG_FILE" 2>/dev/null; then
    FOUND=1
    break
  fi
  if grep -q "$SUCCESS_STR" "$LOG_FILE" 2>/dev/null && grep -q "FRAME CHECKSUM:" "$LOG_FILE" 2>/dev/null; then
    FOUND=1
    break
  fi
  sleep 0.1
  i=$((i + 1))
done

kill "$QEMU_PID" >/dev/null 2>&1 || true
sleep 0.1
kill -9 "$QEMU_PID" >/dev/null 2>&1 || true

if [ "$FOUND" -eq 1 ]; then
  MISSING=""
  grep -q "FSTEST PASS" "$LOG_FILE" || MISSING="$MISSING FSTEST_PASS"
  grep -qE "compositor: ready|COMP READY" "$LOG_FILE" || MISSING="$MISSING COMPOSITOR_READY"
  grep -q "DESKTOPTEST PASS" "$LOG_FILE" || MISSING="$MISSING DESKTOPTEST_PASS"
  grep -q "LOGIN PASS" "$LOG_FILE" || MISSING="$MISSING LOGIN_PASS"
  grep -q "PKG PASS" "$LOG_FILE" || MISSING="$MISSING PKG_PASS"
  grep -q "FRAME CHECKSUM:" "$LOG_FILE" || MISSING="$MISSING FRAME_CHECKSUM"
  if [ -z "$MISSING" ]; then
    echo "BOOT SMOKE PASS"
    exit 0
  fi
  echo "BOOT SMOKE FAIL (missing:$MISSING)"
  echo "Log: $LOG_FILE"
  exit 1
fi

echo "BOOT SMOKE FAIL"
echo "Log: $LOG_FILE"
exit 1
