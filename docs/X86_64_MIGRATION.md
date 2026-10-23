# x86_64 Migration Status

## Stand (2026-03-24)
Die Migration von der fruehen i386-Basis auf einen x86_64-Kernel ist fuer den aktuellen Bring-up-Stand erfolgreich abgeschlossen. HydrogenOS bootet im Long Mode, wechselt sauber in den Userspace und startet standardmaessig `/init` aus dem Initrd/RAMFS; `init` laedt seine Startkonfiguration aus `/init.rc`, startet damit Boot-Selbsttests und danach die superviste Shell, versteht explizite Restart-Policies und setzt deklarativen Child-stdio-Handoff fuer `stdin`, `stdout` und `stderr` zusammen. Ueber denselben Pfad werden inzwischen neben einfachen Pipes auch benannte Channels, gepufferte `tee`-Kanaele, Logsinks und Null-stdio fuer Bootdienste verifiziert.

## Erreichte Punkte
- 64-Bit-Kernelpfad aktiv und stabil.
- x86_64 GDT/IDT/ISR vorhanden.
- Paging mit User-Address-Spaces pro Prozess.
- Syscalls via `syscall`-Instruktion aktiv.
- Statischer ELF64-Loader fuer Userspace-Binaries vorhanden.
- Userspace-Programme werden aus dem Initrd/RAMFS geladen.
- Scheduler rotiert Userspace-Prozesse ueber Timer-IRQs.
- Blockierendes `sys_wait`, prozesslokale Handle-Tabellen und Pipe-/stdio-Handoff fuer den Userspace-Bootpfad sind aktiv.
- Ein minimales Userspace-`init` traegt den Standard-Boot inzwischen selbst bis zur Shell.

## Wichtige Folgearbeiten
- Prozess-Exit und Speicherfreigabe weiter haerten.
- Rechte-/Referenzmodell der FD-/Handle-Verwaltung weiter ausbauen.
- `init` von der aktuellen `/init.rc`-Orchestrierung in Richtung reichhaltigere Dienstdefinitionen und vollwertige stdio-/TTY-Topologien strukturieren.
- Virtio-Treiberbasis fuer QEMU aufbauen.

## Nicht Ziel dieser Migration
- GUI/Desktop.
- Paketmanager.
- Windows-Kompatibilitaet.
- Reale Hardware-Unterstuetzung jenseits der aktuellen Minimalbasis.

## Relevante Referenzen
- `docs/STATUS.md`
- `docs/SYSCALL_ABI.md`
- `docs/TOOLCHAIN.md`
