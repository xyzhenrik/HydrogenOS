# uitest: drive HydrogenOS like a user

Interactive UI testing through the QEMU monitor: screenshots, typing, mouse.

```
make ARCH=x86_64 build kernel32
tools/uitest/boot.sh                          # boots with a monitor socket in build/uitest
tools/uitest/uitest.py shot s1                # build/uitest/s1.png
tools/uitest/uitest.py type 'ls /\n'          # German-layout typing (guest default)
tools/uitest/uitest.py move 100 -40           # relative mouse move; cursor starts at screen centre
tools/uitest/uitest.py click                  # left click (down/up for drags)
tools/uitest/uitest.py crop z 228 118 360 52 3  # zoomed crop to inspect glyphs
tools/uitest/uitest.py cmd quit               # stop the guest
```

`boot.sh` also attaches a `virtio-tablet-pci` and a QMP socket (`qmp.sock`).
The tablet is absolute and reachable only through QMP, so these commands are
pixel exact and need no tracking:

```
tools/uitest/uitest.py abs 100 200            # cursor tip lands on pixel (100, 200)
tools/uitest/uitest.py tap 164 697            # abs + left click, e.g. a dock icon
tools/uitest/uitest.py absdown                # left button via the tablet (drags)
tools/uitest/uitest.py absup
```

The monitor's `move`/`click` keep driving the PS/2 mouse; whichever device
reported last decides the cursor position (see `docs/POINTER_INPUT.md`).

The guest cursor follows `move` 1:1, so track its position yourself: take a
screenshot, compute the delta to the target, move, click. Set `UITEST_DIR` to
run several guests side by side.
