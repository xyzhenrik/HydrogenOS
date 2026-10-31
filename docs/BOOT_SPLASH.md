# Boot splash

`kernel/video/bootsplash.{c,h}` draws a calm boot screen as soon as the
framebuffer is initialised: the drop of the logo above the wordmark
"HYDROGEN OS" ("OS" in a lighter tone) on a dark gradient, with a thin progress
bar below. The wordmark is drawn at build time by `tools/mkwordmark.py` (a
geometric monoline sans with optically spaced letters, rasterised with
`tools/vecraster.py`) into 8-bit coverage bitmaps at two cap heights
(`build/<arch>/gen/wordmark.h`); the kernel blits the nearest one, bilinearly
resampled if the screen is not 1280x720 (cap height is 5% of the height), so
it needs no fonts or FPU state. The drop is the real logo: `tools/mklogo.py`
turns `assets/Hydrogen_Icon_Transparent.png` into coverage bitmaps
(`build/<arch>/gen/logo_splash.h`), which the splash resamples and colours
with the artwork's gradient. The progress bar is a signed distance function
in integer arithmetic.

While the splash is active the kernel log stays off the framebuffer. It is
still written to the serial port.

## Turning it off

Pass `verbose` on the kernel command line. The splash is then disabled and
the kernel console keeps the screen:

- QEMU with `-kernel`: `-append verbose`
- GRUB: add `verbose` to the `multiboot` line in `grub.cfg`

The command line is the multiboot command line, copied by `cmdline_init`
(`kernel/core/cmdline.{c,h}`) before the memory manager starts; the splash
reads it with `cmdline_has_flag("verbose")`. Without a 32-bit linear
framebuffer the splash is disabled as well.

## Progress

The bar is measured in per mille (0 to 1000) and never moves backwards.
`/proc/boot` shows `Splash:` (`active`, `finished`, `disabled`) and
`Progress:`.

| Per mille | Milestone |
|-----------|-----------|
| 80 | memory manager, kernel heap and PCI enumeration; the display driver needs both, so the splash is first drawn here with these two milestones already reached |
| 150 | storage (ATA/virtio block, block cache) |
| 230 | filesystem (VFS mounted) |
| 300 | init started |
| 300 to 1000 | init's services, in proportion to `DONE / TOTAL` |

Kernel milestones are reported with `bootsplash_stage`. Userspace reports by
writing one line to `/proc/boot` with `SYS_WRITEF` (no dedicated syscall):

| Line | Effect |
|------|--------|
| `services DONE TOTAL` | moves the bar to `300 + 700 * DONE / TOTAL`; `TOTAL` must be positive and `DONE` at most `TOTAL` |
| `end` | removes the splash and shows the console |

Only init (the first user process) may report: a well-formed line from any
other process fails with `-EROFS` and has no effect. A malformed line fails
with `-EINVAL`; reports after the splash has ended are accepted and ignored.
Other files in `/proc` stay read-only.

Init sends `end` in two cases. When startup has finished and none of the
daemons it started is marked `display=owner` in `init.rc` (see
`docs/INIT_RC.md`), no desktop is going to present a frame. And when a
`display=owner` daemon has not presented a full frame 30 seconds after
startup finished, the screen must not stay on the splash. Failed or skipped
services alone do not end the splash while a display owner is running.

## Hand-off to the desktop

The splash owns the screen until a program presents a frame that covers the
whole framebuffer through `SYS_FB_BLIT` or `SYS_FB_BLIT_STRIDED`. Until then
partial blits are dropped (they return success) and `SYS_FB_CLEAR` and
console output do nothing, so the compositor can set up behind the splash.
The first full-screen blit sets the bar to 100 %, then fades from the splash
into that frame in twelve eased steps (each step covers a quarter of the
remaining difference) and hands the screen over; the frame is then blitted
as usual. The fade is a fixed number of steps, not timed, because syscalls
run with interrupts disabled. The kernel logs `[SPLASH] handoff to desktop`
on serial.

The splash also ends, with the text console cleared and visible, when `/init`
cannot be started or init writes `end`. Kernel log lines printed while the
splash was up are not replayed on the screen; they are on serial.

## Tests

`/splashtest` (`SPLASH PASS`, checked by `tools/boot_smoke.sh`) runs while the
splash must still be up and checks that init's service reports have moved the
bar, that `verbose` in `/proc/cmdline` and the splash state agree, that
malformed reports and writes to other nodes are rejected, and that reports
from a non-init process are refused with `-EROFS` and a partial blit leaves
the splash in place. The smoke script also requires the `[SPLASH] handoff to
desktop` line, which proves the compositor's first full frame ended the
splash.
