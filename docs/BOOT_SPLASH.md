# Boot splash

`kernel/video/bootsplash.{c,h}` draws a calm boot screen as soon as the
framebuffer is initialised: the HydrogenOS wordmark (the O is the drop) on a
dark gradient, with a thin progress bar below it. Everything is drawn
procedurally with integer arithmetic (signed distance functions turned into
anti-aliased coverage, a dithered gradient), so it scales with the
resolution and needs no assets, fonts or FPU state.

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
| 80 | memory manager, kernel heap and PCI enumeration (the splash is first drawn here, once the display exists) |
| 150 | storage (ATA/virtio block, block cache) |
| 230 | filesystem (VFS mounted) |
| 300 | init started |
| 300 to 1000 | init's services, in proportion to `DONE / TOTAL` |

Kernel milestones are reported with `bootsplash_stage`. Userspace reports by
writing one line to `/proc/boot` with `SYS_WRITEF` (no dedicated syscall):

| Line | Effect |
|------|--------|
| `services DONE TOTAL` | moves the bar to `300 + 700 * DONE / TOTAL`; `TOTAL` must be positive and `DONE` at most `TOTAL` |
| `end` | removes the splash and shows the console (init sends it when no desktop will follow) |

A malformed line fails with `-EINVAL`; reports after the splash has ended
are accepted and ignored. Other files in `/proc` stay read-only.

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
cannot be started or init writes `end`.

## Tests

`/splashtest` (`SPLASH PASS`, checked by `tools/boot_smoke.sh`) validates the
`/proc/boot` format, rejection of malformed reports and writes to other
nodes, and that progress is monotonic.
