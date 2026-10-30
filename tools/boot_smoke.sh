#!/usr/bin/env bash
set -euo pipefail

ARCH="${ARCH:-x86_64}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/$ARCH"
KERNEL_BIN_REL="build/$ARCH/kernel.bin"
DISK_IMG_REL="build/$ARCH/disk.img"
LOG_FILE="$BUILD_DIR/boot_smoke.log"
SUCCESS_STR="HydrogenOS bootet"
# BOOT_MODE=iso boots through GRUB (needs grub-mkrescue + xorriso) and gets a
# VBE framebuffer. BOOT_MODE=kernel uses QEMU's multiboot loader directly; it
# provides no VBE mode, so the desktop runs on the native BGA (std VGA) driver.
BOOT_MODE="${BOOT_MODE:-}"
if [ -z "$BOOT_MODE" ]; then
  if command -v grub-mkrescue >/dev/null 2>&1 && command -v xorriso >/dev/null 2>&1; then
    BOOT_MODE=iso
  else
    BOOT_MODE=kernel
  fi
fi
REQUIRE_DESKTOP="${REQUIRE_DESKTOP:-1}"

if [ "$ARCH" = "x86_64" ]; then
  SUCCESS_STR="init: shell start"
  rm -f "$ROOT_DIR/build/$ARCH/disk.img"
  if [ "$BOOT_MODE" = "iso" ]; then
    make -C "$ROOT_DIR" ARCH="$ARCH" iso build
  else
    make -C "$ROOT_DIR" ARCH="$ARCH" build kernel32
  fi
else
  make -C "$ROOT_DIR" ARCH="$ARCH" "$KERNEL_BIN_REL" "$DISK_IMG_REL"
fi

rm -f "$LOG_FILE"
if [ "$ARCH" = "x86_64" ] && [ "$BOOT_MODE" = "kernel" ]; then
  qemu-system-x86_64 \
    -kernel "$ROOT_DIR/build/$ARCH/kernel32.elf" \
    -initrd "$ROOT_DIR/build/$ARCH/initrd.cpio" \
    -drive if=virtio,format=raw,file="$ROOT_DIR/build/$ARCH/disk.img" \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -m 256M \
    -vga std \
    -serial stdio \
    -display none \
    -no-reboot -no-shutdown \
    >"$LOG_FILE" 2>&1 &
elif [ "$ARCH" = "x86_64" ]; then
  qemu-system-x86_64 \
    -cdrom "$ROOT_DIR/build/$ARCH/hydrogen.iso" \
    -drive if=virtio,format=raw,file="$ROOT_DIR/build/$ARCH/disk.img" \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
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
# Generous ceiling: TCG boots slow down a lot when several QEMUs run in parallel.
while [ "$i" -lt "${SMOKE_TICKS:-1500}" ]; do
  if [ "$REQUIRE_DESKTOP" = "1" ]; then
    DONE_MARKER="FRAME CHECKSUM:"
  else
    DONE_MARKER="desktopdemo started as daemon"
  fi
  if grep -q "$DONE_MARKER" "$LOG_FILE" 2>/dev/null; then
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
  grep -q "DESKTOPTEST PASS" "$LOG_FILE" || MISSING="$MISSING DESKTOPTEST_PASS"
  grep -q "LOGIN PASS" "$LOG_FILE" || MISSING="$MISSING LOGIN_PASS"
  grep -q "PKG PASS" "$LOG_FILE" || MISSING="$MISSING PKG_PASS"
  grep -q "stdout captured: pollwaitcheck: wait ok" "$LOG_FILE" || MISSING="$MISSING POLLWAIT_PASS"
  grep -q "stdout captured: pollmanycheck: many ok" "$LOG_FILE" || MISSING="$MISSING POLLMANY_PASS"
  grep -q "stdout captured: dupfallbackcheck: implicit stdout live" "$LOG_FILE" || MISSING="$MISSING DUPFALLBACK_PASS"
  grep -q "RTC PASS" "$LOG_FILE" || MISSING="$MISSING RTC_PASS"
  grep -q "PROC PASS" "$LOG_FILE" || MISSING="$MISSING PROC_PASS"
  grep -q "DIRTEST PASS" "$LOG_FILE" || MISSING="$MISSING DIRTEST_PASS"
  grep -q "MMAP PASS" "$LOG_FILE" || MISSING="$MISSING MMAP_PASS"
  grep -q "MPROTECT PASS" "$LOG_FILE" || MISSING="$MISSING MPROTECT_PASS"
  grep -q "ACPI OK" "$LOG_FILE" || MISSING="$MISSING ACPI_OK"
  grep -q "POWERCHECK PASS" "$LOG_FILE" || MISSING="$MISSING POWERCHECK_PASS"
  grep -q "NET PASS" "$LOG_FILE" || MISSING="$MISSING NET_PASS"
  grep -q "SHELL PASS" "$LOG_FILE" || MISSING="$MISSING SHELL_PASS"
  grep -q "LIBC PASS" "$LOG_FILE" || MISSING="$MISSING LIBC_PASS"
  grep -q "stdout captured: cwdcheck: cwd ok" "$LOG_FILE" || MISSING="$MISSING CWDCHECK_PASS"
  grep -q "LINK PASS" "$LOG_FILE" || MISSING="$MISSING LINK_PASS"
  grep -q "SYSLOG PASS" "$LOG_FILE" || MISSING="$MISSING SYSLOG_PASS"
  grep -q "UDP PASS" "$LOG_FILE" || MISSING="$MISSING UDP_PASS"
  grep -q "FAULT PASS" "$LOG_FILE" || MISSING="$MISSING FAULT_PASS"
  grep -q "SCHED PASS" "$LOG_FILE" || MISSING="$MISSING SCHED_PASS"
  grep -q "SIG PASS" "$LOG_FILE" || MISSING="$MISSING SIG_PASS"
  grep -q "WP PASS" "$LOG_FILE" || MISSING="$MISSING WP_PASS"
  if [ "$REQUIRE_DESKTOP" = "1" ]; then
    grep -qE "compositor: ready|COMP READY" "$LOG_FILE" || MISSING="$MISSING COMPOSITOR_READY"
    grep -q "FRAME CHECKSUM:" "$LOG_FILE" || MISSING="$MISSING FRAME_CHECKSUM"
  fi
  if [ -z "$MISSING" ]; then
    echo "BOOT SMOKE PASS (mode=$BOOT_MODE desktop=$REQUIRE_DESKTOP)"
    exit 0
  fi
  echo "BOOT SMOKE FAIL (missing:$MISSING)"
  echo "Log: $LOG_FILE"
  exit 1
fi

echo "BOOT SMOKE FAIL"
echo "Log: $LOG_FILE"
exit 1
