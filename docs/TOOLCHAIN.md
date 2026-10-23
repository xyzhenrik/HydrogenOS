# Toolchain (x86_64-elf)

## Ziel
Eine **reproduzierbare Cross-Toolchain** fuer den Kernel/Userspace ohne Abhaengigkeit vom Host-Compiler. Die Toolchain wird fuer CI und lokale Builds genutzt.

## Target
- Triplet: `x86_64-elf`
- Binutils + GCC (C, optional C++)
- Newlib (spaeter, wenn Userspace waechst)

## Versionen (Pinning)
Diese Versionen sind **empfohlen** und sollen nach dem ersten CI-Lauf als "known-good" bestaetigt werden:
- Binutils: **2.41** (oder neuer, aber fix pinnen)
- GCC: **13.2** (oder neuer, aber fix pinnen)
- Newlib: **4.x** (optional)

> Wenn andere Versionen genutzt werden, **bitte in diesem Dokument aktualisieren** und CI neu validieren.

## Build-Voraussetzungen (Arch Linux)
```bash
sudo pacman -Syu --needed base-devel gmp libmpc mpfr python grub xorriso mtools qemu-desktop
```

## Build-Pfad
Empfohlen:
- Quellen: `~/src/toolchain/`
- Install: `~/opt/cross`
- PATH: `~/opt/cross/bin`

## Build-Schritte (binutils + gcc)
```bash
export TARGET=x86_64-elf
export PREFIX="$HOME/opt/cross"
export PATH="$PREFIX/bin:$PATH"

# binutils
mkdir -p ~/src/toolchain && cd ~/src/toolchain
tar xf binutils-2.41.tar.xz
mkdir -p build-binutils && cd build-binutils
../binutils-2.41/configure --target="$TARGET" --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j"$(nproc)"
make install

# gcc (C only)
cd ~/src/toolchain
tar xf gcc-13.2.0.tar.xz
mkdir -p build-gcc && cd build-gcc
../gcc-13.2.0/configure --target="$TARGET" --prefix="$PREFIX" --disable-nls --enable-languages=c --without-headers
make -j"$(nproc)" all-gcc
make -j"$(nproc)" all-target-libgcc
make install-gcc
make install-target-libgcc
```

## Nutzung im Projekt
Im [Makefile](/home/henne/HydrogenOS/Makefile) wird der Cross-Prefix ueber `CROSS ?= x86_64-elf-` gesetzt.  
Fuer lokale Builds:
```bash
export PATH="$HOME/opt/cross/bin:$PATH"
make ARCH=x86_64 build
```

Fuer ISO-Erzeugung und QEMU-Boot:
```bash
make ARCH=x86_64 iso
make ARCH=x86_64 run
```

Hinweis: `make ARCH=x86_64 iso` nutzt `grub-mkrescue`, das wiederum `xorriso` und `mtools` benoetigt.

## CI-Hinweise
- Toolchain-Build in CI cachen (binutils/gcc Builds sind teuer).
- `x86_64-elf-gcc --version` im Log ausgeben.
- Fail, wenn die Versionen nicht den Pins entsprechen.
