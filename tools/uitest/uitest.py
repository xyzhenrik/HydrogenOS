#!/usr/bin/env python3
"""Drive a running HydrogenOS QEMU guest like a user: keyboard, mouse, screenshots.

All state lives in a run directory (default build/uitest next to the repo root,
override with UITEST_DIR) so parallel worktrees never share a monitor socket.
The monitor socket is addressed relatively because AF_UNIX paths are limited
to 108 bytes.

  uitest.py shot NAME            screenshot -> NAME.png
  uitest.py crop NAME X Y W H S  crop region, scale S times -> NAME.png
  uitest.py type 'text\\n'       type on a German-layout keyboard
  uitest.py key QEMU_KEY         e.g. ret, esc, ctrl-c, alt-tab
  uitest.py move DX DY           relative mouse move (guest cursor tracks 1:1)
  uitest.py click | down | up    left button
  uitest.py cmd 'monitor cmd'... raw QEMU monitor commands
"""
import os, socket, struct, sys, time, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUN_DIR = os.environ.get('UITEST_DIR') or os.path.join(ROOT, 'build', 'uitest')

# QEMU sends physical (US-position) keys; the guest defaults to the DE layout.
DE_KEYS = {
    ' ': 'spc', '\n': 'ret', '\t': 'tab', '/': 'shift-7', '.': 'dot', '-': 'slash', '_': 'shift-slash',
    '>': 'shift-less', '<': 'less', '|': 'alt_r-less', '$': 'shift-4', '&': 'shift-6', '"': 'shift-2',
    "'": 'shift-backslash', ';': 'shift-comma', '=': 'shift-0', ':': 'shift-dot', ',': 'comma',
    '(': 'shift-8', ')': 'shift-9', '*': 'shift-bracket_right', '+': 'bracket_right',
    '~': 'alt_r-bracket_right', '%': 'shift-5', '{': 'alt_r-7', '}': 'alt_r-0', '[': 'alt_r-8',
    ']': 'alt_r-9', '!': 'shift-1', '?': 'shift-minus', '#': 'backslash', '@': 'alt_r-q', '\\': 'alt_r-minus',
}
SWAP_YZ = {'y': 'z', 'z': 'y', 'Y': 'Z', 'Z': 'Y'}


def monitor(cmds, delay=0.4):
    os.chdir(RUN_DIR)
    s = socket.socket(socket.AF_UNIX)
    s.connect('mon.sock')
    time.sleep(0.3)
    s.recv(65536)
    out = []
    for c in cmds:
        s.send((c + '\n').encode())
        time.sleep(delay)
        s.settimeout(0.2)
        try:
            out.append(s.recv(65536).decode(errors='replace'))
        except OSError:
            pass
    s.close()
    return out


def read_ppm(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def write_png(path, w, h, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def screendump(tmp='_shot.ppm'):
    monitor(['screendump ' + tmp], 1.2)
    time.sleep(0.5)
    return read_ppm(os.path.join(RUN_DIR, tmp))


def shot(name):
    w, h, px = screendump()
    write_png(os.path.join(RUN_DIR, name + '.png'), w, h, [px[y * w * 3:(y + 1) * w * 3] for y in range(h)])
    os.remove(os.path.join(RUN_DIR, '_shot.ppm'))
    print(os.path.join(RUN_DIR, name + '.png'))


def crop(name, x, y, cw, ch, scale):
    w, h, px = screendump()
    rows = []
    for yy in range(y, y + ch):
        row = b''.join(px[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3] * scale for xx in range(x, x + cw))
        rows += [row] * scale
    write_png(os.path.join(RUN_DIR, name + '.png'), cw * scale, ch * scale, rows)
    os.remove(os.path.join(RUN_DIR, '_shot.ppm'))
    print(os.path.join(RUN_DIR, name + '.png'))


def type_text(text, delay=0.12):
    cmds = []
    for c in text:
        c = SWAP_YZ.get(c, c)
        k = DE_KEYS.get(c) or ('shift-' + c.lower() if c.isupper() else c)
        cmds.append('sendkey ' + k)
    monitor(cmds, delay)


def main(argv):
    a = argv[1]
    if a == 'shot': shot(argv[2])
    elif a == 'crop': crop(argv[2], *map(int, argv[3:8]))
    elif a == 'type': type_text(argv[2].replace('\\n', '\n'))
    elif a == 'key': monitor(['sendkey ' + argv[2]])
    elif a == 'move': monitor(['mouse_move %s %s' % (argv[2], argv[3])], 0.3)
    elif a == 'click': monitor(['mouse_button 1', 'mouse_button 0'], 0.1)
    elif a == 'down': monitor(['mouse_button 1'], 0.1)
    elif a == 'up': monitor(['mouse_button 0'], 0.1)
    elif a == 'cmd': print(''.join(monitor(argv[2:])))
    else: sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
