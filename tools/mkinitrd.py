#!/usr/bin/env python3
import os
import sys


def pad4(f, count):
    pad = (4 - (count % 4)) % 4
    if pad:
        f.write(b"\x00" * pad)


def write_entry(f, name, data, mode):
    name = name.lstrip("/")
    if not name:
        raise ValueError("empty name in initrd entry")
    namesize = len(name) + 1
    filesize = len(data)
    header = (
        "070701"
        + f"{0:08x}"  # ino
        + f"{mode:08x}"
        + f"{0:08x}"  # uid
        + f"{0:08x}"  # gid
        + f"{1:08x}"  # nlink
        + f"{0:08x}"  # mtime
        + f"{filesize:08x}"
        + f"{0:08x}"  # devmajor
        + f"{0:08x}"  # devminor
        + f"{0:08x}"  # rdevmajor
        + f"{0:08x}"  # rdevminor
        + f"{namesize:08x}"
        + f"{0:08x}"  # check
    )
    f.write(header.encode("ascii"))
    f.write(name.encode("ascii") + b"\x00")
    pad4(f, CPIO_HDR_LEN + namesize)
    f.write(data)
    pad4(f, filesize)


def usage():
    print("usage: mkinitrd.py <output> name=path [name=path ...]")
    sys.exit(1)


CPIO_HDR_LEN = 110
CPIO_MODE_FILE = 0o100644


def main():
    if len(sys.argv) < 3:
        usage()
    out_path = sys.argv[1]
    entries = []
    for arg in sys.argv[2:]:
        if "=" in arg:
            name, path = arg.split("=", 1)
        else:
            path = arg
            name = os.path.basename(path)
        if not name:
            raise SystemExit(f"invalid name for '{arg}'")
        if not os.path.isfile(path):
            raise SystemExit(f"not a file: {path}")
        with open(path, "rb") as src:
            data = src.read()
        entries.append((name, data))

    with open(out_path, "wb") as f:
        for name, data in entries:
            write_entry(f, name, data, CPIO_MODE_FILE)
        write_entry(f, "TRAILER!!!", b"", CPIO_MODE_FILE)


if __name__ == "__main__":
    main()
