# HydrogenOS – Claude Arbeitskontext

## Was ist HydrogenOS?

HydrogenOS ist ein eigenständiges Desktop-Betriebssystem – **kein Linux, kein BSD, kein Fork**. Es entsteht von Grund auf in C und x86_64-Assembly und soll ein echtes marktfähiges Produkt werden: mit einer durchdachten Desktop-Erfahrung, .exe-Kompatibilität und einer Community ähnlich dem Arch-Ökosystem.

**Langfristige Ziele:**
- Windows-ähnliche Desktop-UX, aber vollständig anpassbar (Themes, Blur, Extensions)
- .exe-Support mit Doppelklick-Install (PE-Loader + Win32-Subset, kein Wine-Klon)
- Paketverwaltung und Erweiterbarkeit à la Arch Linux

**Technische Basis (Stand März 2026):**
- x86_64 Long-Mode Kernel, stabiler Boot
- Syscall-ABI via `syscall`-Instruktion (23 Syscalls, s. `docs/SYSCALL_ABI.md`)
- Getypte prozesslokale Handle-Tabelle, Pipes mit Blocking/EOF/Broken-Pipe, dup/poll
- VFS + ext2 + RAMFS/Initrd, statischer ELF-Loader, preemptiver Scheduler
- Userspace-`init` mit `/init.rc`-Parser, Channels, Tee, Logsink, Restart-Policies
- Userspace-Shell `hydrogen$ `, Mini-Libc, CRT0
- Makefile-Build + QEMU-Boot-Smoke-Test

Primäre nächste Baustellen: **virtio-Treiber** (blk/gpu/input), init-Ausbau, Grafikstack.

---

## Code-Qualitätsstandards

Dieses Projekt soll auf dem Markt bestehen. Entsprechend gilt:

**Kein Wegwerf-Code.** Jede Zeile, die eingecheckt wird, muss das Niveau einer produktiven Systemsoftware haben.

- **Korrektheit vor Geschwindigkeit.** Race Conditions, Use-after-free und Nullpointer-Dereferenzen sind inakzeptabel. Jeder Kernel-Pfad muss nachweisbar korrekt sein.
- **Klare Schnittstellen.** Modul-Grenzen sind explizit. Interna bleiben intern (statische Symbole, opake Typen wo sinnvoll). Kein globaler Zustand ohne guten Grund.
- **Fehler werden propagiert, nicht ignoriert.** Rückgabewerte werden geprüft. Panic/Assert nur bei Invarianten, nicht bei Laufzeitfehlern.
- **Konsistente Benennung.** Kernel-Symbole: `snake_case`. Typen/Structs: `UpperCamelCase`. Makros: `UPPER_CASE`. Keine Abkürzungen, die nicht sofort klar sind.
- **Keine toten Pfade.** Kein auskommentierter Code, keine ungenutzten Variablen, kein Debug-`printf` im Kernel.
- **Minimale Abhängigkeiten.** Alles, was nicht explizit gebraucht wird, kommt nicht rein. Keine Fremd-Bibliotheken im Kernel.
- **Kommentare nur für das Nicht-Offensichtliche.** Kein Rauschen, kein Selbsterklärendes. Kommentare erklären das *Warum*, nicht das *Was*.

---

## UI/UX-Philosophie (für zukünftigen Grafikstack)

Das Desktop-System soll das ästhetische Niveau von **iOS/macOS** erreichen – nicht imitieren, sondern eigenständig auf demselben Niveau spielen:

- **Klarheit.** Weniger ist mehr. Jedes UI-Element hat eine klare Aufgabe. Kein visuelles Rauschen.
- **Tiefe und Material.** Glaseffekte, Blur, subtile Schatten – mit Bedacht, nicht als Dekoration.
- **Konsistenz.** Abstände, Typografie, Farben folgen einem System. Kein UI, das „zusammengewürfelt" wirkt.
- **Reaktionsfreude.** Animationen sind smooth (60fps+), physikbasiert wo sinnvoll, nie störend.
- **Systemweites Theme.** Eine Theme-Engine mit Color-Tokens, die konsistent durch alle System-Apps durchgeht.

Wenn UI-Code entsteht: auf Pixelebene denken, nicht auf Feature-Ebene.

---

## Arbeitsweise mit Claude

### Subagents für Planung und Umsetzung

Für nicht-triviale Aufgaben wird die Arbeit immer strukturiert:

1. **Plan-Phase:** Vor der Implementierung wird ein konkreter Plan erstellt – Dateien, Schnittstellen, Abhängigkeiten. Bei größeren Aufgaben wird ein `Plan`-Agent eingesetzt.
2. **Explore-Phase:** Codebase-Recherche läuft über einen `Explore`-Subagenten, nicht inline. Das schützt den Hauptkontext.
3. **Implementierungs-Phase:** Erst nach Klärung der Architektur wird Code geschrieben.
4. **Verifikation:** Build (`make ARCH=x86_64 build`) und Smoke-Test (`tools/boot_smoke.sh`) laufen nach jeder Änderung.

### Commit-Disziplin

Commits nur auf explizite Anfrage. Kein `--no-verify`, keine Umgehung von Hooks.

### Nicht-Linux-Kontext

Es gibt keine glibc, keine Linux-Kernel-APIs, kein POSIX als Ziel. Alle Systemschichten sind Eigenimplementierungen. Vorschläge, die auf Linux-Primitiven basieren, sind umzuformulieren.

### Build-Befehle

```bash
make ARCH=x86_64 build       # Kernel + Userspace + Initrd
make ARCH=x86_64 iso         # ISO (braucht grub-mkrescue + xorriso)
make ARCH=x86_64 run         # QEMU-Start
tools/boot_smoke.sh          # Boot-Smoke-Test headless
```

### Wichtige Docs

- `docs/STATUS.md` — aktueller Ist-Stand
- `docs/SYSCALL_ABI.md` — Syscall-Tabelle und Handle-Modell
- `docs/TOOLCHAIN.md` — Cross-Toolchain (x86_64-elf-gcc 13.2)
- `docs/X86_64_MIGRATION.md` — Migrationsnotizen
- `plan.md` — Phasenplan und nächste Schritte
