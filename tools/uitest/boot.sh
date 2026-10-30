#!/bin/bash
# Boot the current build in QEMU for interactive UI testing (see uitest.py).
# usage: tools/uitest/boot.sh   (after 'make ARCH=x86_64 build kernel32')
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN_DIR="${UITEST_DIR:-$ROOT/build/uitest}"
mkdir -p "$RUN_DIR"
cd "$RUN_DIR"
python3 "$ROOT/tools/uitest/uitest.py" cmd quit >/dev/null 2>&1 || true
sleep 1
rm -f mon.sock serial.log
cp "$ROOT/build/x86_64/disk.img" disk.img
setsid qemu-system-x86_64 \
  -kernel "$ROOT/build/x86_64/kernel32.elf" -initrd "$ROOT/build/x86_64/initrd.cpio" \
  -drive if=virtio,format=raw,file=disk.img -m 256M -vga std \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -serial file:serial.log -display none -monitor unix:mon.sock,server,nowait -no-reboot \
  >qemu.err 2>&1 &
for _ in $(seq 1 240); do grep -q 'FRAME CHECKSUM' serial.log 2>/dev/null && break; sleep 0.5; done
sleep 2
echo "booted; run dir: $RUN_DIR"
