# HydrogenOS – Implementationsplan (detailliert)

> Planstand aktualisiert: 2026-03-24. Dieses Dokument beschreibt Zielbild, Architektur und die naechsten Ausbauschritte. Der konkrete Ist-Stand des Repositories wird in `docs/STATUS.md` festgehalten.

## Zielbild
Ein eigenständiges, **nicht auf Linux basierendes** Desktop‑Betriebssystem mit:
- Windows‑ähnlicher Desktop‑UX, aber **voll anpassbar** (Themes, Blur, Extensions, Shell‑Tweaks).
- **.exe‑Support** mit **Doppelklick‑Install/Start** (Installer‑UX wie unter Windows).
- Paketverwaltung und Erweiterbarkeit wie Arch (Repos, optional AUR‑ähnlich, stark modifizierbar).

> Hinweis zur .exe‑Kompatibilität: Vollständige Win32/Win64‑Abdeckung ist ein **eigenes Großprojekt**. Realistisch ist ein Stufenplan: erst PE‑Loader + Teil‑API, später breitere Abdeckung. Als Fallback bleibt VM/Container. **Kernel‑Mode Windows‑Treiber lassen sich nicht direkt nutzen** – Hardware wird mit nativen Treibern unterstützt.

---

## Aktueller Stand (aus dem Repository)
Stand März 2026, basierend auf dem aktuellen Code:
- **x86_64 Long-Mode Kernel** bootet stabil.
- **GDT/IDT/ISR**, PIC/PIT, Keyboard/Mouse, x86_64-Syscall-Entry.
- **PMM + Paging + KHeap**; pro Prozess eigener User-Address-Space.
- **Einfacher präemptiver Prozessscheduler** fuer Userspace-Prozesse.
- **Framebuffer + VGA-Fallback**, TTY/fb_console.
- **ATA + Block-Layer + MBR + ext2 + VFS**.
- **Userspace vorhanden**: statischer ELF-Loader, Mini-CRT0, Mini-Libc, Userspace-Shell und Testprogramme.
- **Handle-/IPC-Basis vorhanden**: prozesslokale getypte Handle-Tabelle, Pipes mit Blocking/EOF/Broken-Pipe, explizite Spawn-Vererbung, `dup`/`dup2`/`dup3`, konfigurierbares stdio inklusive Null- und Console-Fallback.
- **Minimales Userspace-`init` vorhanden**: `init.rc`-Parser, Restart-Policies und einfache Dienst-/stdio-Topologien ueber `pipe`, `channel`, `tee` und `logsink`.
- **Initrd/RAMFS-Bootfluss** fuer eingebettete Userspace-ELFs.
- **Makefile-Build** mit automatischen Header-Abhaengigkeiten, ISO-Erzeugung, QEMU-Boot-Smoke-Test.

### Offene Lücken im Ist‑Stand
- Prozessmodell ist noch frueh: grundlegender Exit-Cleanup fuer User-Address-Spaces ist vorhanden, aber Ressourcenverwaltung bleibt noch simpel.
- Dateideskriptoren und Pipes laufen jetzt ueber eine getypte prozesslokale Handle-Tabelle; selektive Child-Vererbung, EOF, Read-/Write-Blocking und Broken-Pipe sind da, aber Rechte-/Referenzmodell und feinere Semantik fehlen noch.
- Treiberbasis minimal; kein virtio, kein moderner Storage (AHCI/NVMe).
- Ein minimales `init` orchestriert bereits deklarative One-Shot-/supervised-Dienste aus `/init.rc` und bildet einfache stdio-Graphen im Userspace ab, aber mehrere Live-Reader, freiere Dienstgraphen, echte TTY-Sessions und ein belastbares Service-Manager-Modell fehlen noch.
- Grafikstack/Desktop, Paketmanager und Kompatibilitaetsschicht sind noch nicht begonnen.

---

## Leitentscheidungen (bestaetigt)
1. **Architektur‑Ziel: x86_64 Long Mode** ist gesetzt und bereits als Kernel-Basis umgesetzt.
2. **Userspace‑First** bleibt die Leitlinie: Syscall‑ABI, ELF‑Loader und Prozessisolation werden vor hoeheren Komfortschichten verfeinert.
3. **Driver‑Priorität: virtio** (QEMU/CI‑freundlich), danach real HW.
4. **GUI‑Stack als eigenes Subsystem** (Compositor, WM, Toolkit, Theme‑Engine).
5. **Windows‑Kompatibilität als Stufenplan**, nicht „alles sofort“.

---

## Architektur‑Definition (Kurzfassung)
**Kernel‑/Userspace‑Contract**
- Syscall‑ABI für x86_64 (`syscall` Instruktion), klare Fehler‑Konvention (z. B. negative errno).
- Handle‑basierte Ressourcenverwaltung (FDs/Handles).
- Strikte User/Kernel‑Adressraum‑Trennung.

**Prozess‑/Thread‑Modell**
- Prozess = Address Space + Handle‑Table.
- Thread = Scheduling‑Einheit.
- Preemptive Scheduler mit Prioritäten + Sleep/Wait.

**Speicher‑Modell**
- Paged VM, per‑Process Address Space.
- User‑Heap + Shared Memory Regionen.
- Später: COW, mmap‑ähnliche API.

**I/O‑ und Dateisystem‑Modell**
- VFS als einheitliches API.
- Dateideskriptoren + epoll‑ähnliche Events (später).
- Initrd/RAMFS für Boot, ext2/ext4‑ähnlich als erstes Disk‑FS.

**Treiber‑Modell**
- Kernel‑Treiber für Block/Input/GPU‑Baseline.
- Modularer Treiber‑Lader (später).
- Fokus auf virtio für QEMU und CI.

**Graphics/Windowing**
- Compositor‑Server + Clients.
- 2D Rendering (CPU‑Fallback), später GPU‑Beschleunigung.
- Theme‑Engine und Effects‑Pipeline (Blur, Shadows).

**Packaging/Extensions**
- Paketmanager mit Repos + Signaturen.
- Extensions als isolierte Plugins (z. B. JS/Wasm).
- Theme‑Assets als paketierbare Einheiten.

**Windows‑Kompatibilität**
- PE‑Loader, Win32 Subset, Registry‑Layer.
- Installer‑UX mit Doppelklick, MSI/EXE‑Handling.
- Einschränkung: Kernel‑Mode‑Treiber und Anti‑Cheat/DRM benötigen VM‑Fallback.

---

## Phasenplan (vom jetzigen Stand bis Desktop‑OS)

> Status März 2026: Phase 0 bis Phase 4 sind in groben Zuegen erreicht, Phase 6 ist mit einem kleinen Userspace-`init`, Shell und Konfig-Bootpfad bereits begonnen. Der Schwerpunkt liegt jetzt auf Härtung des Prozess-/Userspace-Modells, genauer Handle-/IPC-Semantik, Ausbau des Userspace-Boots und dem Uebergang zur modernen Treiberbasis.

### Phase 0 – Stabilisierung & Basishygiene (2–4 Wochen)
Ziel: aktuelle i386‑Basis stabilisieren, damit Migration sauber möglich ist.
- Build reproducible machen (Compiler‑Version, Flags, deterministische Outputs).
- Crash‑Logs/Serial‑Output, Panic‑Mechanismus, Assert‑Macros.
- Kernel‑Subsysteme entkoppeln (console vs framebuffer vs tty).
- Grundlegende Tests: Boot‑Test, IRQ‑Test, Paging‑Smoke‑Test, ext2‑Mount‑Test.

**Verifikation**
- QEMU Boot + Serial‑Logs ok.
- Paging aktiviert, Shell läuft stabil 5+ Minuten.

**Deliverables**
- Stabiler Boot‑Log, deterministische Build‑Artefakte.
- Minimaler Test‑Runner (Boot‑Smoke‑Test).

---

### Phase 1 – Toolchain & CI (2–3 Wochen)
Ziel: sauberer Cross‑Compiler und CI‑Pipeline.
- Cross‑Toolchain festlegen (`x86_64‑elf‑gcc` + Version).
- Build‑Script mit festen Pfaden.
- CI‑Job: build + QEMU‑Boot‑Smoke‑Test.

**Verifikation**
- CI erzeugt ISO, Boot‑Test grün.

**Deliverables**
- Reproduzierbarer Build auf cleanem System.
- CI‑Artifacts (ISO, Kernel‑Binär).

---

### Phase 2 – x86_64 Long Mode Migration (4–8 Wochen)
Ziel: 64‑bit Kernel bootet stabil.
- 64‑bit GDT/IDT/ISR Stubs.
- Paging: PML4/PDPT/PD/PT.
- New kernel entry (Multiboot2 oder eigener Loader).
- Übergang i386 → x86_64 (Dual‑build kurzzeitig möglich).

**Verifikation**
- 64‑bit Kernel bootet, IRQs funktionieren, Timer tickt.

**Deliverables**
- 64‑bit Kernel mit minimalem Console‑Output.
- Grundlegende IRQs aktiv und stabil.

---

### Phase 3 – Prozesse, Syscalls, Userspace (6–10 Wochen)
Ziel: echte User‑Prozesse mit Isolation.
- Syscall‑ABI (Registers, Fehler‑Codes, Handles).
- ELF‑Loader (statisch).
- User‑Mode, eigene Address Spaces.
- Scheduler (preemptive, Round‑Robin + Sleep).
- IPC: Pipes + Shared Memory (minimal).

**Verifikation**
- User‑Binary läuft, kann syscalls machen, Scheduler wechselt Tasks.

**Deliverables**
- Erste Userspace‑Binaries (Hello, File‑Read, IPC‑Test).
- Syscall‑Doku als lebendes Dokument.

---

### Phase 4 – Speicher & Dateisystem (5–8 Wochen)
Ziel: robuster Speicher + VFS + Filesystem‑Support.
- VM‑Subsystem: demand paging, copy‑on‑write (später).
- VFS API stabilisieren.
- ext2 stabil + RAMFS/Initrd.
- Block‑Layer abstrahieren, Block‑Cache verfeinern.

**Verifikation**
- initrd mount, Dateien lesen, einfache Writes (falls unterstützt).

**Deliverables**
- VFS‑API Freeze v1.
- Read‑only ext2 stabil im QEMU‑Flow.

---

### Phase 5 – Treiber‑Priorisierung (6–10 Wochen)
Ziel: moderne, testbare Treiberbasis.
1. **virtio‑blk + virtio‑gpu + virtio‑input** (QEMU).
2. AHCI/NVMe (später), USB (später).
3. ACPI basic, APIC (für SMP‑Optionen später).

**Verifikation**
- QEMU virtio‑stack läuft, Filesystem‑IO stabil.

**Deliverables**
- virtio‑blk + virtio‑gpu + virtio‑input lauffähig.
- Log‑Level für Treiberdiagnose.

---

### Phase 6 – Userspace‑Grundsystem (6–10 Wochen)
Ziel: init + Services + minimale libc.
- init‑Prozess + Service‑Manager.
- Minimal libc (stdio, malloc, string, file IO).
- Shell im Userspace.
- System‑Konfig (feste Pfade, /etc, /bin, /lib).

**Status jetzt**
- Ein minimales `init` laedt bereits `/init.rc`, startet One-Shot- und supervised-Dienste mit Dependencies und versteht `restart=never|on-failure|always`.
- Child-stdio wird bereits deklarativ ueber `stdin/stdout/stderr` aus Parent-FDs verdrahtet; aktuell sind `inherit`, `null`, `pipe`, `capture`, `channel`, `tee` und `logsink` vorhanden.
- Die aktuelle Schicht taugt fuer Boot-Orchestrierung und IPC-Selbsttests, ist aber noch kein vollwertiger Service-Manager.

**Verifikation**
- System bootet bis Userspace‑Shell.

**Deliverables**
- Init startet Services zuverlässig.
- Userspace‑Shell nutzt VFS + Syscalls.

---

### Phase 7 – Grafikstack & Compositor (8–14 Wochen)
Ziel: Windowing‑System + 2D‑Rendering.
- Compositor (Server) + Window Manager (Client).
- 2D‑Renderer (software/CPU‑fallback).
- Input‑Stack (keyboard/mouse events).
- Font‑Rendering (bitmap zuerst, später TrueType).

**Verifikation**
- Mehrere Fenster, Z‑Order, Repaint stabil.

**Deliverables**
- Fenster‑API v1.
- Elementare Widgets (Buttons, Text).

---

### Phase 8 – Desktop Environment & Customization (8–12 Wochen)
Ziel: „Windows‑ähnlich“ plus Arch‑artige Anpassung.
- Taskbar, Start‑Menu, Desktop, File‑Manager, Settings‑App.
- Theme‑Engine (CSS‑ähnliche Definition, Color‑Tokens).
- Blur/Effects Pipeline (Shader oder CPU‑Fallback).
- Extension API (z. B. JS/wasm‑based plugins).

**Verifikation**
- Themes/Extensions laden ohne Reboot.

**Deliverables**
- Theme‑SDK + Beispiel‑Themes.
- Extension‑API v1 + Beispiel‑Extension.

---

### Phase 9 – App‑Ecosystem & Package Manager (6–10 Wochen)
Ziel: Installierbare Apps & Erweiterungen.
- Paketmanager (CLI + GUI).
- Repo‑Metadaten + Signaturen.
- Optional AUR‑ähnlicher Build‑Flow.

**Verifikation**
- Apps installieren/entfernen, Updates laufen.

**Deliverables**
- Signierte Paket‑Repos.
- GUI‑Store‑App v1.

---

### Phase 10 – Windows‑Kompatibilität (.exe) (12–24 Wochen, parallel)
Ziel: schrittweiser .exe‑Support.
Option A: **Kompatibilitätsschicht**
- PE‑Loader, Win32 API Subset (kernel32/user32/gdi32).
- Registry‑Layer, Win32‑Filesystem‑Mapping.
- Integration mit Compositor (Window Mapping, Input).
- **Installer‑UX:** Doppelklick öffnet Installer‑Wizard, MSI/EXE‑Handling, Datei‑Assoziation, Desktop‑/Start‑Einträge.
- **Runtime‑Abhängigkeiten:** VC‑Runtime, .NET, DirectX‑Layer (z. B. DXVK/vkd3d‑ähnliche Übersetzung).
- **Pro‑App Prefixes:** isolierte App‑Umgebungen, pro‑App Settings.

Option B: **VM/Container Fallback**
- QEMU/VM‑Integration für inkompatible Apps.

**Verifikation**
- Früh: einfache Windows‑Tools starten.
- Später: populäre Apps laufen (Office‑Viewer, Notepad++, etc.).

---

### Phase 11 – Stabilität, Security, Distribution (laufend)
Ziel: nutzbares OS, installierbar.
- Installer + Updates.
- Crash‑Dump, Telemetrie (optional).
- Permissions/Sandboxing für Apps.

**Verifikation**
- Clean‑Install + GUI + Update‑Flow stabil.

---

## Meilensteine (neu)
| Meilenstein | Ziel | Inhalt |
|-------------|------|--------|
| M1 | Stabiler Kernel‑Boot | Paging + IRQ + Logging |
| M2 | 64‑bit Kernel | Long‑Mode + Syscalls vorbereitet |
| M3 | Userspace läuft | ELF + init + Shell |
| M4 | Grafik‑Stack | Fenster + Compositor |
| M5 | Desktop nutzbar | WM + Settings + Themes |
| M6 | Paketmanager | Apps installierbar |
| M7 | .exe‑Support v1 | PE‑Loader + Win32 Subset |

---

## Zeitplanung (realistisch)
Bei 10–15h/Woche:
- **12–18 Monate** für „nutzbares Desktop‑OS“ ohne .exe‑Kompatibilität.
- **18–30 Monate** für brauchbaren .exe‑Support (je nach Tiefe).

---

## Parallel‑Workstreams (empfohlen)
**Core OS Track**
- Kernel, VM, Scheduler, VFS, Treiber.

**Userspace Track**
- libc, init, Services, Shell.

**Graphics/Desktop Track**
- Compositor, WM, Toolkit, Desktop, Themes.

**Compatibility Track**
- PE‑Loader, Win32‑API‑Subset, Installer‑UX, Prefix‑Management.

---

## Nächste konkrete Schritte (aus aktuellem Stand)
1. **Handle-/Referenzmodell haerten**: Rechte, Lebensdauer, Referenzen und spaetere Poll-/Eventfaehigkeit sauber auf dem bestehenden FD-/Pipe-Fundament modellieren.
2. **`init` vom Boot-Orchestrator zum echten Service-Manager entwickeln**: reichhaltigere Dienstdefinitionen, mehrere Live-Reader, freiere Pipe-/TTY-Topologien, klarere Failure- und Restart-Semantik.
3. **Userspace-Basis vervollstaendigen**: feste Pfade, mehr libc-Funktionen, Shell-Kommandos und klarere Trennung zwischen Bootdiensten, Session und spaeterem Login/Desktop-Pfad.
4. **virtio-Treiber** (`virtio-blk`, `virtio-gpu`, `virtio-input`) als Primärziel fuer QEMU umsetzen.
5. **Verifikation ausbauen**: den vorhandenen Boot-Smoke-Test schrittweise um service- und IPC-spezifische Assertions ergaenzen.

---

## Verifikation (Minimal‑Testmatrix)
- QEMU: Boot, IRQs, Paging, FS‑Read, User‑Process.
- Grafik: 2–3 Fenster, Input‑Events, Repaint‑Stress.
- Packaging: Install/Remove/Update.
- Windows: 3–5 Test‑Apps mit klarer Ziel‑Matrix.

---

## Dokumente (laufend gepflegt)
- docs/STATUS.md
- docs/TOOLCHAIN.md
- docs/SYSCALL_ABI.md
- docs/X86_64_MIGRATION.md


