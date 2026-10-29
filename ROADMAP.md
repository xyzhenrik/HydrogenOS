# HydrogenOS – 100-Schritte Roadmap

> Erstellt: 2026-05-02. Reihenfolge nach Abhängigkeiten und Impact.
> Status: [ ] offen · [x] fertig · [~] in Arbeit

---

## Block A – Kernel Foundation (Schritte 1–20)

### A1: Kern-Infrastruktur
- [x] **1.** `PROCESS_MAX_HANDLE` 8 → 64 erhöhen (process.h + process.c anpassen, Tests)
- [x] **2.** Gemeinsame `kernel/include/errno.h` — alle verstreuten `#define ENOENT 2` konsolidieren
- [ ] **3.** `sys_mmap` / `sys_munmap` implementieren (Userspace-Heap-Erweiterung via `brk`-Semantik)
- [x] **4.** `sys_getcwd` / `sys_chdir` — VFS-Arbeitsverzeichnis pro Prozess
- [x] **5.** `sys_fstat` / `sys_stat` — Datei-Metadaten (size, type, mtime)
- [x] **6.** `sys_readdir` — Verzeichnis-Iteration für `ls`-artige Tools
- [x] **7.** `sys_mkdir` / `sys_unlink` / `sys_rename` auf VFS-Ebene
- [x] **8.** Scheduler: Prioritäts-Level (IDLE / NORMAL / RT) + sleep queue statt spin-sleep
- [x] **9.** Kernel-Panic-Verbesserung: vollständiger Registerdump + RSP-Backtrace über Serial
- [x] **10.** SMP-Vorbereitung: per-CPU-State-Struct (single CPU jetzt, SMP später)

### A2: VFS & Dateisystem
- [ ] **11.** VFS: mkdir / rmdir / unlink / rename implementieren
- [x] **12.** ext2: Schreib-Support (Datei anlegen, Daten schreiben, Inode-Allokation)
- [ ] **13.** ext2: Verzeichnis anlegen
- [ ] **14.** `/proc`-Stub-Filesystem (PID-Liste, `/proc/<pid>/status`)
- [ ] **15.** Pfad-Cache / dcache (einfacher Hash, LRU eviction)
- [ ] **16.** Block-Cache: Writeback-Policy statt nur Read-Cache
- [ ] **17.** Block-Device-Abstraktion: mehrere Geräte gleichzeitig (`/dev/blk0`, `/dev/blk1`)
- [ ] **18.** GPT-Partition-Table-Support (neben MBR)
- [ ] **19.** `sys_link` / `sys_symlink` / `sys_readlink`
- [ ] **20.** Ext2: journaling-Stub (prepare für ext3-Kompatibilität)

---

## Block B – PCI & Virtio-Treiber (Schritte 21–40)

- [x] **21.** PCI Config-Space-Enumeration (Type-0, Bus/Device/Function-Scan)
- [x] **22.** PCI-Device-Registry (Vendor/Device-ID → Treiber-Binding-Table)
- [x] **23.** PCI BAR-Mapping (MMIO-Regionen in Kernel-VMA mappen)
- [x] **24.** Virtqueue-Implementierung (Split-Virtqueue, Descriptor-Ring, Available/Used-Ring)
- [x] **25.** `virtio-blk` — PCI-Probe, Virtqueue-Setup, Read/Write-Requests
- [x] **26.** `virtio-blk` in Block-Layer integrieren, Makefile + QEMU-Smoke-Test
- [x] **27.** `virtio-gpu` — Linear-Framebuffer-Modus (VIRTIO_GPU_CMD_SET_SCANOUT)
- [x] **28.** `virtio-gpu` in Framebuffer-Subsystem integrieren
- [x] **29.** `virtio-input` — Keyboard-Events via Virtio (EV_KEY Events)
- [x] **30.** `virtio-input` in Keyboard-Subsystem integrieren
- [x] **31.** APIC/LAPIC-Init (PIC-Ablösung für moderne IRQ-Routierung)
- [x] **32.** IOAPIC-Setup
- [x] **33.** MSI/MSI-X-Support für Virtio-PCI
- [ ] **34.** AHCI-Stub (PCI-Detect, kein Full-Driver — Real-HW-Pfad vorbereiten)
- [ ] **35.** `virtio-net` — PCI-Probe + Loopback-Test
- [ ] **36.** Einfacher Netzwerk-Stack-Stub (Ethernet-Frame-RX/TX)
- [ ] **37.** ARP + IPv4-Grundstruktur
- [ ] **38.** UDP-Grundimplementierung
- [ ] **39.** TCP (minimal: SYN/ACK, Send/Receive)
- [ ] **40.** `sys_socket` / `sys_bind` / `sys_connect` / `sys_send` / `sys_recv`

---

## Block C – Userspace & libc (Schritte 41–60)

- [x] **41.** mini-libc: `printf` (Basis-Format: `%s %d %x %u %c`, kein Float)
- [x] **42.** mini-libc: `sprintf` / `snprintf`
- [ ] **43.** mini-libc: `malloc` / `free` (slab-Allocator oder tlsf, über `sys_mmap`)
- [ ] **44.** mini-libc: `fopen` / `fread` / `fwrite` / `fclose` (FILE-Wrapper)
- [ ] **45.** mini-libc: `getenv` / `setenv` (envp-Array aus `sys_spawn` Args)
- [ ] **46.** mini-libc: `strdup` / `strtol` / `strtoul` / `atoi` / `atol`
- [ ] **47.** mini-libc: `qsort` / `bsearch`
- [ ] **48.** mini-libc: `time` / `gettimeofday` / `clock_gettime` Stub
- [ ] **49.** `sys_execve`: aktuelles Prozess-Image ersetzen (Lade neues ELF in selben AS)
- [ ] **50.** `sys_fork`: Copy-on-Write-Fork
- [ ] **51.** Environment-Variable-Passing via `sys_spawn` (argv + envp im User-Stack)
- [ ] **52.** Dynamischer ELF-Loader (Shared-Library-Support, PLT/GOT)
- [ ] **53.** `/bin`, `/lib`, `/etc`, `/tmp`-Verzeichnisstruktur im initialen FS
- [ ] **54.** Shell: Pipe-Operator `|` (Shell-seitige Pipe-Verkettung)
- [ ] **55.** Shell: Output-Redirection `>` / `>>`
- [ ] **56.** Shell: Input-Redirection `<`
- [ ] **57.** Shell: Hintergrund-Job `&` + `wait`
- [x] **58.** Shell: `ls`, `pwd`, `cd`, `mkdir`, `rm`, `touch`
- [x] **59.** Shell: `cp`, `mv`, `find` (minimal)
- [ ] **60.** Shell: Environment-Variable-Expansion `$VAR` + `export`

---

## Block D – Grafikstack & Compositor (Schritte 61–80)

- [ ] **61.** Framebuffer: Double-Buffering (Back-Buffer + vsync-Flip)
- [ ] **62.** Software-2D-Renderer: gefüllte Rechtecke, Linien, Kreise, Clipping
- [ ] **63.** Software-2D-Renderer: Alpha-Blending / Porter-Duff-Compositing
- [ ] **64.** Font-Rendering: Bitmap-Fonts (8×16 → 16×32 hochskaliert, dann psf2-Support)
- [ ] **65.** Font-Rendering: TrueType via stb_truetype (portiert, statisch gelinkt)
- [ ] **66.** Window-Manager-Protokoll-Design (IPC via Shared-Memory + Event-Pipe)
- [ ] **67.** Compositor-Server: Fenster-Liste, Z-Order, Focus-Verwaltung
- [ ] **68.** Compositor: Damage-Tracking + selektive Repaints
- [ ] **69.** Client-Library: `hydrogen_window_create` / `destroy` / `surface_get`
- [ ] **70.** Client-Library: Event-Loop (Key/Mouse-Events aus Compositor-Pipe)
- [ ] **71.** Input-Routing: Keyboard/Mouse → focused Window
- [ ] **72.** Widget-Toolkit: `Label`, `Button`, `TextInput`, `Container`
- [ ] **73.** Widget-Toolkit: Layout-Engine (Flex-artig: horizontal/vertical + grow)
- [ ] **74.** Widget-Toolkit: Theme-System (Color-Tokens, Spacing-Scale, Font-Token)
- [ ] **75.** Fenster-Dekorationen: Titelbar, Close/Min/Max-Buttons (iOS-Stil: rund, clean)
- [ ] **76.** Desktop-Hintergrund-Renderer (Gradient oder PNG)
- [ ] **77.** Taskbar (unten, laufende Apps als Icons, Uhrzeit)
- [ ] **78.** App-Launcher / Spotlight-artiges Suchfeld
- [ ] **79.** Datei-Manager (Grid-View, Ordner-Navigation, Doppelklick)
- [ ] **80.** virtio-gpu 2D-Befehle nutzen (GPU-beschleunigte Blits statt Software)

---

## Block E – System-Services & Distribution (Schritte 81–100)

- [ ] **81.** System-Logger `syslogd` (Userspace-Daemon, liest logsink, schreibt `/var/log/system.log`)
- [ ] **82.** RTC-Treiber (CMOS, Wanduhrzeit beim Boot lesen)
- [ ] **83.** ACPI-Tabellen-Parser (RSDP → RSDT/XSDT → FADT, Shutdown via SLP_TYPx)
- [ ] **84.** Power-Management: sauberes Shutdown / Reboot via ACPI
- [ ] **85.** Benutzer-Authentifizierung (`/etc/passwd`-Stil, `sys_getuid` / `sys_setuid`)
- [ ] **86.** Datei-Permissions (Inode-Bits rwxrwxrwx, UID/GID, Kernel-Enforcement)
- [x] **87.** Paketformat `.hpkg` (tar + JSON-Manifest: name, version, deps, files)
- [~] **88.** Paketmanager-CLI `hpkg` (install / remove / list / update)
- [ ] **89.** Paket-Repository-Metadaten-Format + lokales Repo-Tool
- [ ] **90.** UEFI-Boot-Stub (EFI-Handoff → Kernel, neben BIOS/GRUB erhalten)
- [ ] **91.** Boot-Splash-Screen (Framebuffer-Logo + Ladebalken)
- [ ] **92.** GRUB-Konfiguration: Timeout, Menü-Styling, Kernel-Parameter-Passing
- [ ] **93.** Kernel-Crash-Dump: Oops-State auf Disk schreiben (`/var/crash/`)
- [ ] **94.** Kernel-Perf-Counter: Syscall-Counts, Scheduler-Wakeups, Pipe-Latenz
- [ ] **95.** OOM-Policy: Prozess-Killer bei Speicherdruck (Scoring nach Größe/Prio)
- [ ] **96.** `sys_mprotect` (RW/RX-Flags auf Mappings setzen, SIGSEGV-Basis)
- [ ] **97.** Signal-Grundgerüst: `sys_kill` / `sys_signal` / `sys_sigreturn` (SIGTERM, SIGKILL, SIGSEGV)
- [ ] **98.** Settings-App (GUI, Theme-Wechsel, Display-Helligkeit, Netzwerk-Status)
- [ ] **99.** Installer-ISO (Live-System + `install`-Kommando → Disk-Image schreiben)
- [ ] **100.** Erster öffentlicher Alpha-Boot-Demo (QEMU-Recording, Screenshot, Changelog)

---

## Abhängigkeitsgraph (grob)

```
A1 (Handles, errno) ──→ A2 (VFS writes) ──→ C (libc, Shell)
                    ──→ B1 (PCI) ──→ B2 (virtio-blk) ──→ B3 (virtio-gpu)
                                   ──→ B4 (virtio-input)
C (fork/exec) ──→ D (Compositor, Client-Library)
B (virtio-gpu) ──→ D
D ──→ E (Desktop-Services, Paketmanager)
```

---

## Nächste 5 Schritte (sofort)

1. **Schritt 1** — Handle-Tabelle 8 → 64
2. **Schritt 2** — gemeinsame `errno.h`
3. **Schritt 21** — PCI-Enumeration (unabhängig, hoher Impact)
4. **Schritt 41** — mini-libc `printf` (unabhängig von Kernel-Änderungen)
5. **Schritt 8** — Scheduler-Prioritäten + Sleep-Queue
