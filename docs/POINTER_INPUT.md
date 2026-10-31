# Pointer input

The desktop reads one pointer state through `SYS_MOUSE_STATE`: position in
framebuffer pixels, button bits (left, right, middle) and an accumulated wheel
delta. Two devices feed that state in `kernel/arch/x86_64/mouse.c`:

| Device | Driver | Reports | Needs a host grab |
| --- | --- | --- | --- |
| PS/2 mouse (IRQ12) | `mouse.c` | relative deltas | yes |
| `virtio-tablet-pci` | `kernel/drivers/virtio_input.c` | absolute position | no |

The most recent report from either device wins: a PS/2 delta moves the cursor
from wherever the tablet last put it, and a tablet report overwrites the
position. PS/2 stays the fallback when no tablet is attached.

## Why a tablet

A relative device only works while the host window grabs the pointer. On
Wayland/XWayland the grab can fail, so the guest cursor never moves. An
absolute device lets QEMU forward the host pointer position directly; nothing
is grabbed and the cursor tracks the host pointer one to one. QEMU's GTK and SDL
frontends prefer an absolute device automatically when one exists.

## virtio-input driver

`virtio_input_init()` probes every virtio-input PCI function (up to four) and
classifies it from its config space (`select`/`subsel` pairs):

- `EV_BITS/EV_ABS` has `ABS_X` and `ABS_Y`: tablet. The axis range comes from
  `ABS_INFO` (`max - min`); QEMU reports `0..0x7fff`.
- otherwise `EV_BITS/EV_REL` present (a relative virtio mouse) or no `EV_KEY`:
  ignored.
- otherwise keyboard; key events go to the queue read by
  `virtio_input_get_key_event()`.

Tablet events accumulate per device and are published on `EV_SYN`, so the
desktop never sees a position from one frame combined with buttons from
another. `BTN_LEFT/RIGHT/MIDDLE` map to the button bits. `REL_WHEEL` is
inverted into the PS/2 convention (positive scrolls down). Events are drained
whenever `SYS_MOUSE_STATE` polls, with the syscall running with interrupts
masked, so no extra locking is needed.

A report that carries no `ABS_X`/`ABS_Y` (a button-only frame) does not touch
the position. That keeps relative movement intact if a host routes button
events to the tablet while motion arrives on PS/2.

## Scaling

`mouse_set_position_scaled()` maps device units `[0, range]` onto the
framebuffer by rounding `value * width / (range + 1)` to the nearest pixel and
clamping to `width - 1`. QEMU produces `pixel * 2^15 / width`, so rounding
recovers exactly the host pixel for any screen width up to 32768.

## Running it

`make ARCH=x86_64 run`, `tools/uitest/boot.sh` and `tools/boot_smoke.sh` all add
`-device virtio-tablet-pci`. Test automation drives it through QMP
(`input-send-event` with `abs` axes); see `tools/uitest/README.md`.
