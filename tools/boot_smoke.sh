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
  make -C "$ROOT_DIR" ARCH="$ARCH" iso
else
  make -C "$ROOT_DIR" ARCH="$ARCH" "$KERNEL_BIN_REL" "$DISK_IMG_REL"
fi

rm -f "$LOG_FILE"
if [ "$ARCH" = "x86_64" ]; then
  qemu-system-x86_64 \
    -cdrom "$ROOT_DIR/build/$ARCH/hydrogen.iso" \
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
while [ "$i" -lt 50 ]; do
  if grep -q "$SUCCESS_STR" "$LOG_FILE" 2>/dev/null; then
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
  echo "BOOT SMOKE PASS"
  exit 0
fi

echo "BOOT SMOKE FAIL"
echo "Log: $LOG_FILE"
exit 1
