# HydrogenOS Status Snapshot (2026-03-24)

## Memory (Kurzstand)
- x86_64 Kernel bootet stabil; Stack-Alignment in `boot.S` gefixt.
- Syscalls: `sys_exit`, `sys_write`, `sys_read`, `sys_open`, `sys_close`, `sys_lseek`, `sys_spawn`, `sys_spawn_with_stdio`, `sys_wait`, `sys_getpid`, `sys_yield`, `sys_sleep`, `sys_time_ms`, `sys_pipe`, `sys_handle_set_inherit`, `sys_dup`, `sys_dup2`, `sys_dup3`, `sys_poll`, `sys_poll_wait`, `sys_poll_many` (+ Debug `sys_putchar`).
- VFS: ext2 auto-mount, RAMFS-Fallback mit eingebetteten Userspace-ELFs.
- Userspace `cat` testet `open/read/close` und dumpft `/user` (ELF-Header sichtbar).
- `sys_wait` blockiert jetzt im Kernel auf den Exit eines direkten Child-Prozesses; die Userspace-Shell pollt nicht mehr mit `yield/sleep`.
- Kernel-Heap wird jetzt ueber eine zusammenhaengende PMM-Reservation angelegt; dadurch ueberlappt er nicht mehr mit dem Multiboot-Initrd.
- Handle-Verwaltung im Userspace-Syscall-Pfad ist jetzt pro Prozess statt globalem VFS-FD-Offset.
- Dateihandles tragen jetzt gemeinsame File-Referenzen mit Offset und Refcount; `read` auf Dateien ist damit sequentiell, und `dup`/Spawn-Vererbung teilen denselben Dateizustand.
- `sys_lseek` ist jetzt fuer Dateihandles vorhanden; `SEEK_SET`, `SEEK_CUR` und `SEEK_END` arbeiten auf dem gemeinsamen File-Offset, Pipes liefern `-ESPIPE`.
- `sys_poll` liefert jetzt eine sofortige Readiness-Maske fuer Handles; `sys_poll_wait` erweitert dieselben `POLL_READ`-/`POLL_WRITE`-/`POLL_HUP`-Events um blockierendes Warten mit Timeout, und `sys_poll_many` deckt mehrere FDs in einem Syscall ab, inklusive ignorierter `fd = -1`-Slots und `POLL_NVAL` fuer ungueltige Eintraege.
- Relative Pfade werden jetzt konsistent gegen das prozesslokale Working Directory aufgeloest; damit greifen `open`, `spawn`, `stat`, `readdir`, `mkdir`, `unlink`, `rename`, `writef` und `chdir` nicht mehr implizit immer auf `/` zu.
- `sys_pipe` liefert jetzt Pipe-Handles; `read`/`write` koennen Dateihandles und Pipe-Enden unterscheiden.
- FDs koennen jetzt explizit fuer `spawn`-Vererbung markiert oder auf close-on-spawn gesetzt werden; ungekennzeichnete normale Handle-Slots bleiben im Child verborgen, stdio-Bindings koennen nun ebenfalls explizit aus der Spawn-Vererbung genommen werden.
- Child-Prozesse koennen jetzt zusaetzlich eigene prozesslokale `stdin/stdout/stderr`-Eintraege aus Parent-Handles erhalten; normales `spawn` vererbt konfiguriertes stdio, `sys_spawn_with_stdio` kann es explizit ueberschreiben.
- Handles tragen jetzt neben `inherit` auch interne Read-/Write-Rechte; damit werden falsche stdio-Bindings bei `sys_spawn_with_stdio` frueh abgewiesen.
- `sys_dup` und `sys_dup2` decken jetzt neben expliziten Handle-Slots auch das implizite Console-Fallback fuer ungebundene `stdin/stdout/stderr` ab; damit koennen Pipe-/Datei-Handles, gebundene stdio-FDs und das Keyboard/TTY-Fallback innerhalb eines Prozesses geklont oder umgelenkt werden.
- `sys_dup3` setzt zusaetzlich explizite Spawn-Flags auf dem Ziel-FD; damit lassen sich neue Duplikate direkt als vererbbar oder close-on-spawn markieren.
- `open`, `pipe` und `dup` unterscheiden jetzt sauber zwischen ungueltigen Eingaben und ausgeschopfter prozesslokaler Handle-Tabelle; ein voller Handle-Satz liefert `-EMFILE` statt irrefuehrender `ENOENT`/`EIO`.
- Null-`stdin`/`stdout`/`stderr` sind jetzt als eigene stdio-Bindings im Kernel umgesetzt.
- Vollstaendiger stdio-Handoff (`stdin`, `stdout`, `stderr`) ist jetzt auch ueber Pipes im `init`-Bootpfad verifiziert.
- `sys_read` auf leerer Pipe mit aktivem Writer blockiert jetzt und wird beim Pipe-Write wieder aufgeweckt.
- `sys_write` auf voller Pipe blockiert jetzt, bis ein Reader Platz schafft; `write` ohne Reader liefert `-EPIPE`.
- Implizites Pipe-Handle-Cleanup bei `close`/Prozess-Exit weckt Gegenenden fuer EOF-/Broken-Pipe-Pfade auf.
- Makefile erzeugt jetzt automatische `.d`-Headerabhaengigkeiten; Header-Aenderungen fuehren zu konsistenten Rebuilds statt zu stale Objekten.
- IRQ0-Serial-Debugpunkte deaktiviert (saubere Shell-Ausgabe).

## Aktueller Stand (x86_64)
- 64-bit Kernel bootet stabil und startet jetzt `/init` aus dem Initrd/RAMFS; `init` startet die Userspace-Shell und die Kernel-Shell bleibt nur noch Fallback.
- Userspace-ELF Loader funktioniert (statisches ELF, Load + Map + User-Stack).
- ELF-Loader mappt ueberlappende `PT_LOAD`-Segmente jetzt korrekt, ohne bereits belegte User-Seiten neu zu ueberschreiben. Damit startet die Userspace-Shell wieder stabil und ihre `.rodata`-Strings bleiben intakt.
- Syscalls via `syscall`-Instruktion (MSR STAR/LSTAR/FMASC + Entry-Stub).
- Syscall-Set implementiert:
  - `sys_exit`, `sys_write`, `sys_read`, `sys_open`, `sys_close`, `sys_lseek`, `sys_spawn`, `sys_wait`, `sys_getpid`, `sys_yield`, `sys_sleep`, `sys_time_ms`, `sys_tty_set_color`
  - plus `sys_pipe` fuer lokale Pipe-Endpunkte im Prozess, `sys_handle_set_inherit` fuer FD-weite Spawn-Flags, `sys_spawn_with_stdio` fuer explizites Child-stdio aus Parent-Handles, `sys_dup`/`sys_dup2`/`sys_dup3` fuer FD-Klone, `sys_poll` fuer sofortige Handle-Readiness, `sys_poll_wait` fuer blockierendes Warten mit Timeout und `sys_poll_many` fuer kleine FD-Listen.
  - und ein undokumentiertes `sys_putchar` fuer Debug.
- Initrd (cpio) wird beim Boot geladen und fuellt RAMFS mit den eingebetteten Userspace-ELFs.
- Long-Mode Stack-Alignment fix fuer SysV-ABI (vermeidet SSE-GPF in Kernel-C-Code).
- IRQ0-Serial-Debugpunkte deaktiviert (saubere Shell-Ausgabe).
- Einfacher x86_64-Prozessscheduler: User-Prozesse werden ueber Timer-IRQ/Scheduler-Yield rotiert; `spawn` startet nicht mehr synchron im aufrufenden Prozess.
- `sys_wait` liefert fuer direkte Child-Prozesse jetzt blockierend erst dann zurueck, wenn das Child beendet ist.
- User-Address-Spaces werden beim Prozess-Destroy wieder freigegeben (User-Seiten + User-Page-Tables).
- `sys_open`/`sys_read`/`sys_close` nutzen im Userspace-Pfad jetzt getypte prozesslokale Handle-Slots (Dateien als erster Ressourcentyp).
- Pipe-Handles (`sys_pipe`) koennen jetzt selektiv an Child-Prozesse vererbt werden; Parent/Child-IPC ueber geerbte Handles funktioniert.
- EOF auf einem geerbten Pipe-Read-Ende ohne Writer wird sauber bis ins Child durchgereicht.
- `read` auf leerer Pipe mit aktivem Writer blockiert jetzt bis Daten verfuegbar sind; ein Pipe-Write weckt wartende Leser.
- `write` auf voller Pipe blockiert jetzt bis ein Reader Platz schafft; `write` ohne Reader liefert `-EPIPE`.
- Pipe-Cleanup bei explizitem `close` und implizitem Prozess-Exit weckt Gegenenden fuer Resume/EOF/Broken-Pipe auf.
- Der initiale Userspace-Stack wurde fuer das gewachsene Userspace-`init` auf 16 Seiten erhoeht; 8 Seiten reichten nach dem Ausbau der Konfig-/Service-Tabellen nicht mehr.
- Prozesse haben jetzt neben der Handle-Tabelle auch eigene stdio-Eintraege fuer `0/1/2`, die auf Dateien/Pipes gebunden werden koennen.
- `dup`/`dup2` koennen jetzt explizite FD-Eintraege und das implizite Console-Fallback von `stdin/stdout/stderr` klonen; `dup2` darf auch auf `stdin/stdout/stderr` zielen und ermoeglicht damit FD-Umlenkung inklusive Restore auf das Default-Console-Backing im Userspace.
- `dup3` kann dieselben Ziel-FDs zusaetzlich direkt mit `inherit` oder close-on-spawn markieren; normale Handle-Slots bleiben dabei standardmaessig close-on-spawn, stdio-Ziele standardmaessig vererbbar.
- Minimales Userspace-`init` laedt seine Dienstliste jetzt aus `/init.rc` statt aus einer hart verdrahteten Service-Tabelle; aktuell werden `bootprobe`, `handoffcheck`, `stdiocheck`, `stdiobadbind`, `dupcheck`, `handlelimitcheck`, `fileoffsetcheck`, `seekcheck`, `pollcheck`, `pollwaitcheck`, `pollmanycheck`, `cwdcheck`, `logproducer_a`, `logproducer_b`, `logcollector`, `logsink_writer_a`, `logsink_writer_b`, `teewriter`, `teereader_a`, `teereader_b`, `pipeproducer`, `pipeconsumer`, `stdionullcheck` und danach die superviste `/shell` gestartet.
- `dupcheck` verifiziert `dup` auf einen normalen Handle-Slot sowie `dup2` auf `stdin` und `stdout` gegen echte Pipes.
- `handlelimitcheck` fuellt die prozesslokale Handle-Tabelle, verifiziert `-EMFILE` fuer `dup`, `open` und `pipe` und prueft anschliessend, dass freigegebene Handle-Slots wieder nutzbar werden.
- `fileoffsetcheck` verifiziert, dass Dateihandles jetzt einen gemeinsamen Offset ueber `dup` teilen und sequentielle Datei-Reads nicht mehr immer wieder bei Byte 0 starten.
- `seekcheck` verifiziert `lseek` auf Dateihandles inklusive gemeinsamem Offset ueber `dup` sowie `-ESPIPE` auf Pipes.
- `pollcheck` verifiziert minimale Pipe-Readiness ueber `sys_poll` fuer `POLL_READ`, `POLL_WRITE` und `POLL_HUP`.
- `pollwaitcheck` verifiziert blockierendes `sys_poll_wait` inklusive Timeout und Wakeup durch einen anderen Prozess.
- `pollmanycheck` verifiziert `sys_poll_many` fuer gemischte Listen mit `fd = -1`, `POLL_NVAL` auf ungueltigen FDs, Timeout, Single-Wakeup innerhalb einer Poll-Liste und EOF/HUP auf mehreren Readern.
- `cwdcheck` verifiziert `getcwd`/`chdir`, relative Dateioperationen (`writef`, `open`, `stat`, `rename`, `unlink`), `..`-Aufloesung und relatives `spawn` ueber das prozesslokale Working Directory.
- `init` setzt Child-stdio jetzt aus der `init.rc` auf: `stdin=inherit|null|pipe "..."|channel <name>|tee <name>`, `stdout=inherit|null|capture "..."|channel <name>|tee <name>|logsink <name>`, `stderr=inherit|null|capture "..."|channel <name>|tee <name>|logsink <name>` werden ueber `sys_spawn_with_stdio` an den Child gebunden.
- `init` verifiziert mit diesen konfigurierten stdio-Regeln den vollstaendigen stdio-Handoff: `handoffcheck` liest konfigurierten Pipe-Input auf `fd 0`, `stdiocheck` schreibt auf getrennte `stdout`-/`stderr`-Pipes, und `init` wertet die Rueckleitung nach dem Exit aus.
- `init` startet zusaetzlich `stdiobadbind`, das verifiziert, dass `sys_spawn_with_stdio` Reader/Writer nicht mehr in die falsche stdio-Richtung binden laesst.
- `init` kann jetzt einfache benannte stdio-Kanaele zwischen Diensten anlegen; aktuell ist ein Reader pro Channel vorgesehen, aber mehrere Writer koennen auf denselben Channel fan-in'en.
- `pipeproducer` schreibt auf `stdout=channel bootseq`, `pipeconsumer` liest spaeter ueber `stdin=channel bootseq` und bestaetigt den Payload ueber `stdout=capture`.
- `logproducer_a` und `logproducer_b` schreiben beide auf `stdout=channel logbus`; `logcollector` liest den kombinierten Stream spaeter ueber `stdin=channel logbus` und bestaetigt den Multiwriter-Pfad.
- `init` hat jetzt zusaetzlich gepufferte `tee`-Kanaele fuer Broadcast/Replays im Userspace: `teewriter` schreibt auf `stdout=tee teebus`, `teereader_a` und `teereader_b` lesen spaeter denselben Payload unabhaengig ueber `stdin=tee teebus`.
- `init` unterstuetzt deklarative `logsink`-Eintraege in `/init.rc`; `logsink_writer_a` und `logsink_writer_b` schreiben auf `stdout=logsink syslog`, und `init` verifiziert den aggregierten Sink gegen den erwarteten Text aus `logsink syslog "..."`.
- `init` startet zusaetzlich `stdionullcheck`, das `stdin=null`, `stdout=null` und `stderr=null` gegen die neue Kernel-Null-stdio prueft.
- `init` versteht explizite Restart-Policies aus `/init.rc`: `restart=never`, `restart=on-failure`, `restart=always`.
- Serial-I/O fuer die Userspace-Shell funktioniert wieder sauber; doppelte Zeichenausgabe im Textmode wurde bereinigt.

## Kernel-Shell Commands
`help`, `uptime`, `mem`, `clear`, `elfinfo`, `elfload`, `user`, `echo`, `time`, `sleep`

## Userspace-Programme
- `user`: klassisches Hello-Programm (ASM)
- `echo`: liest eine Zeile von stdin und schreibt sie zurueck
- `cat`: testet `open/read/close` und dumpft erste Bytes aus `/user`
- `time`: gibt `time_ms` aus
- `sleep`: wartet 1000 ms und meldet "done"
- `bootprobe`: One-Shot-Startdienst, prueft `/shell` im RAMFS und loggt den Bootzeitpunkt
- `handoffcheck`: One-Shot-Startdienst, liest ueber `fd 0` den per `init.rc` via `stdin=pipe` uebergebenen Payload und bestaetigt den Handoff
- `stdiocheck`: One-Shot-Startdienst, prueft festen `stdin`-Payload und schreibt getrennte Erfolgsmeldungen auf `stdout` und `stderr`, die `init` ueber `stdout=capture`/`stderr=capture` auswertet
- `stdiobadbind`: One-Shot-Startdienst, prueft, dass ungueltige `spawn_with_stdio`-Bindings fuer `stdin/stdout/stderr` sauber abgewiesen werden
- `dupcheck`: One-Shot-Startdienst, prueft `dup`, `dup2` und `dup3` gegen Pipe-Handles, Spawn-Vererbung, close-on-spawn, stdio-Umlenkung und per Child-Selbsttest auch Restore auf implizites Console-`stdout`
- `handlelimitcheck`: One-Shot-Startdienst, prueft `-EMFILE` bei ausgeschopfter Handle-Tabelle fuer `dup`, `open` und `pipe` sowie die Wiederverwendbarkeit freigegebener Slots
- `fileoffsetcheck`: One-Shot-Startdienst, prueft sequentielle Datei-Reads und gemeinsamen Dateioffset ueber `dup`
- `seekcheck`: One-Shot-Startdienst, prueft `lseek` (`SEEK_SET`/`SEEK_CUR`) auf Dateihandles und `-ESPIPE` auf Pipes
- `pollcheck`: One-Shot-Startdienst, prueft Pipe-Readiness ueber `POLL_READ`, `POLL_WRITE` und `POLL_HUP`
- `pollwaitcheck`: One-Shot-Startdienst, prueft `sys_poll_wait` fuer Timeout und Wakeup auf einer Pipe
- `pollmanycheck`: One-Shot-Startdienst, prueft `sys_poll_many` fuer mehrere FDs in einem Wait
- `cwdcheck`: One-Shot-Startdienst, prueft relative Pfadauflosung ueber das prozesslokale Working Directory
- `pollwakewriter`: Helper-Programm fuer `pollwaitcheck`, schreibt nach kurzem Yield auf `stdout`
- `dupfallbackcheck`: Helper-Programm fuer `dupcheck`, startet mit geloeschtem `stdout`-Binding und verifiziert `dup(1)`/`dup2(..., 1)` auf dem impliziten Console-Fallback
- `pipeproducer`: One-Shot-Startdienst, schreibt einen Test-Payload auf einen benannten `init`-Channel
- `pipeconsumer`: One-Shot-Startdienst, liest einen benannten `init`-Channel ueber `stdin` und bestaetigt den Payload
- `logproducer_a` / `logproducer_b`: One-Shot-Startdienste, schreiben nacheinander auf denselben benannten `init`-Channel
- `logcollector`: One-Shot-Startdienst, liest einen Multiwriter-Channel und bestaetigt den kombinierten Payload
- `logsink_writer_a` / `logsink_writer_b`: One-Shot-Startdienste, schreiben auf einen benannten `init`-Logsink
- `teewriter`: One-Shot-Startdienst, schreibt einen Replay-/Broadcast-Payload in einen benannten `init`-Tee
- `teereader_a` / `teereader_b`: One-Shot-Startdienste, lesen denselben benannten `init`-Tee unabhaengig und bestaetigen den Payload
- `stdionullcheck`: One-Shot-Startdienst, prueft `stdin`-EOF sowie schreibbares Null-`stdout`/`stderr`
- `init`: minimales Userspace-Bootprogramm mit Parser fuer `/init.rc`, Restart-Policies und konfigurierbarem stdio-/Handle-Handoff
- `/init.rc`: einfache Init-Konfiguration mit `service <name> <oneshot|supervised> <path> <restart_ms> [restart=<never|on-failure|always>] [stdin=inherit|null|pipe "text"|channel <name>|tee <name>] [stdout=inherit|null|capture "text"|channel <name>|tee <name>|logsink <name>] [stderr=inherit|null|capture "text"|channel <name>|tee <name>|logsink <name>] [deps...]` sowie `logsink <name> "expected"`; Channels erlauben aktuell einen Reader und mehrere Writer, Tees puffern einen Stream fuer mehrere spaetere Leser, Logsinks sammeln und verifizieren Writer-Ausgaben.
- `shell`: Userspace-Shell mit farbigem Prompt (`hydrogen$ `), `help`, `time`, `sleep`, `cat`, `echo`, `pipe`, `piperun`, `pipenoinherit`, `pipeblock`, `pipefull`, `pipebroken`, `pipeeof`, `pollpipe`, `pollwaitpipe`, `pollmanypipe`, `run`
- `pipedelayread`: kleines Helper-Programm fuer den Full-Pipe-Backpressure-Test

## Userspace Runtime
- Minimaler CRT0 (`userspace/lib/crt0.S`) ruft `main()` und macht `sys_exit`.
- Mini-Libc (`userspace/lib/mini_libc.c/.h`) mit `read/write/puts/strlen/memcpy/memset/sleep_ms/time_ms`, `poll_fd`, `poll_wait_fd` und `poll_many_wait`.
- Syscall-Wrapper in `userspace/lib/syscall.h` (nutzt `syscall`).

## Shell-Stand
- Prompt: `hydrogen$ `
- Farben: Prompt blau, Eingabetext weiss, Info cyan, Fehler rot, Erfolg gruen
- Zeilenbearbeitung: `Left`/`Right`, `Home`/`End`, `Delete`, `Backspace`, Einfuegen mitten in der Zeile
- History: `Up`/`Down`
- Cursor wieder als klassischer `_`

## Wichtige Dateien
- Kernel: `kernel/core/kernel64.c`, `kernel/core/syscall.c`
- Kernel-Fallback-Shell: `kernel/apps/kernel_shell.c`, `kernel/apps/kernel_shell.h`
- Prozesse/Handles: `kernel/core/process.c`, `kernel/core/process.h`, `kernel/core/pipe.c`, `kernel/core/pipe.h`
- Syscall-Arch: `kernel/arch/x86_64/syscall_entry.S`, `kernel/arch/x86_64/syscall_arch.c`, `kernel/arch/x86_64/msr.h`
- ABI-Doku: `docs/SYSCALL_ABI.md`
- Userspace: `userspace/lib/*`, `userspace/init/init.c`, `userspace/init/init.rc`, `userspace/handoffcheck/handoffcheck.c`, `userspace/stdiocheck/stdiocheck.c`, `userspace/stdiobadbind/stdiobadbind.c`, `userspace/dupcheck/dupcheck.c`, `userspace/handlelimitcheck/handlelimitcheck.c`, `userspace/fileoffsetcheck/fileoffsetcheck.c`, `userspace/seekcheck/seekcheck.c`, `userspace/pollcheck/pollcheck.c`, `userspace/pollwaitcheck/pollwaitcheck.c`, `userspace/pollmanycheck/pollmanycheck.c`, `userspace/cwdcheck/cwdcheck.c`, `userspace/pollwakewriter/pollwakewriter.c`, `userspace/dupfallbackcheck/dupfallbackcheck.c`, `userspace/logproducer_a/logproducer_a.c`, `userspace/logproducer_b/logproducer_b.c`, `userspace/logcollector/logcollector.c`, `userspace/logsink_writer_a/logsink_writer_a.c`, `userspace/logsink_writer_b/logsink_writer_b.c`, `userspace/teewriter/teewriter.c`, `userspace/teereader_a/teereader_a.c`, `userspace/teereader_b/teereader_b.c`, `userspace/pipeproducer/pipeproducer.c`, `userspace/pipeconsumer/pipeconsumer.c`, `userspace/stdionullcheck/stdionullcheck.c`, `userspace/shell/shell.c`

## Build/Test
- `make ARCH=x86_64 build` baut Kernel + Userspace + Initrd und ist OK (2026-03-24)
- `make -n -W kernel/core/process.h ARCH=x86_64 build` und `make -n -W kernel/include/keyboard.h ARCH=x86_64 build` zeigen den erwarteten Rebuild-Graphen (2026-03-20)
- `make ARCH=x86_64 iso` benoetigt `grub-mkrescue` + `xorriso`
- `ARCH=x86_64 tools/boot_smoke.sh` ist OK (2026-03-24) und prueft fuer x86_64 jetzt nicht mehr nur das fruehe Boot-Banner, sondern den `init: shell start`-Marker.
- Headless QEMU-Start ueber `-serial stdio` zeigt jetzt `/init` -> Laden von `/init.rc` -> `bootprobe` -> `handoffcheck` -> `stdiocheck` -> `stdiobadbind` -> `dupcheck` -> `handlelimitcheck` -> `fileoffsetcheck` -> `seekcheck` -> `pollcheck` -> `pollwaitcheck` -> `pollmanycheck` -> `cwdcheck` -> `logproducer_a` -> `logproducer_b` -> `logcollector` -> `logsink_writer_a` -> `logsink_writer_b` -> `teewriter` -> `teereader_a` -> `teereader_b` -> `pipeproducer` -> `pipeconsumer` -> `stdionullcheck` -> Userspace-Shell; `handoffcheck` bekommt seinen stdin-Payload aus `stdin=pipe`, `stdiocheck` bestaetigt getrennte `stdout`-/`stderr`-Rueckleitung aus `stdout=capture` und `stderr=capture`, `stdiobadbind` bestaetigt die Ablehnung falscher stdio-Richtungen, `dupcheck` verifiziert `dup`/`dup2`/`dup3` fuer Handle- und stdio-Umlenkung inklusive `dupfallbackcheck` auf implizitem Console-`stdout`, `handlelimitcheck` verifiziert `-EMFILE` und Slot-Recovery bei ausgeschoepfter Handle-Tabelle, `fileoffsetcheck` verifiziert gemeinsamen Dateioffset ueber `dup`, `seekcheck` verifiziert `lseek` und `-ESPIPE` auf Pipes, `pollcheck` verifiziert `sys_poll` fuer Pipe-Readiness, `pollwaitcheck` verifiziert blockierendes `sys_poll_wait` inklusive Timeout und Wakeup durch `pollwakewriter`, `pollmanycheck` verifiziert `sys_poll_many` fuer Snapshot, Timeout und selektives Wakeup innerhalb einer Poll-Liste, `cwdcheck` verifiziert Working-Directory-basierte Pfadauflosung fuer Dateioperationen und relatives `spawn`, `logproducer_a`/`logproducer_b`/`logcollector` verifizieren einen Multiwriter-Channel, `logsink_writer_a`/`logsink_writer_b` verifizieren den deklarativen Logsink, `teewriter`/`teereader_a`/`teereader_b` verifizieren Broadcast-/Replay-`tee`, `pipeproducer`/`pipeconsumer` verifizieren weiter den einfachen Channel-Fall, `stdionullcheck` bestaetigt `stdin/stdout/stderr = null`, und danach startet die Shell.
- `cat` liest `/user` aus RAMFS (ELF-Header sichtbar)
- `user` / `echo` / `cat` / `time` / `sleep` / `bootprobe` / `handoffcheck` / `stdiocheck` / `stdiobadbind` / `dupcheck` / `handlelimitcheck` / `fileoffsetcheck` / `seekcheck` / `pollcheck` / `pollwaitcheck` / `pollmanycheck` / `cwdcheck` / `pollwakewriter` / `dupfallbackcheck` / `logproducer_a` / `logproducer_b` / `logcollector` / `logsink_writer_a` / `logsink_writer_b` / `teewriter` / `teereader_a` / `teereader_b` / `pipeproducer` / `pipeconsumer` / `stdionullcheck` / `init` / `shell` werden gebaut
- Kernel-Shell bleibt nur Fallback und hat weiterhin kein Argument-Parsing

## Nächste sinnvolle Schritte
1. Rechte-/Referenzmodell fuer Handles/Pipes weiter ausbauen; Read-/Write-Blocking, selektive Vererbung, close-on-spawn, EOF/Broken-Pipe und Console-Fallback-`dup` sind jetzt vorhanden.
2. `init` von der aktuellen einfachen `/init.rc` in Richtung reichhaltigere Dienstdefinitionen und spaeter echte TTY-/Pipe-Topologien erweitern; Channels koennen jetzt einen Reader und mehrere Writer, `tee` und `logsink` sind als Userspace-Schicht dazugekommen, aber mehrere Live-Reader, Pipesets und freiere Graphen fehlen noch.
3. Virtio-Treiberbasis fuer QEMU (`virtio-blk`, `virtio-gpu`, `virtio-input`) angehen.

## Desktop v0 (2026-10-23)

- Kernel graphics: double-buffered framebuffer (`fb_back_buffer`/`fb_flip`), software 2D renderer (`renderer2d`), PSF2 font parser, `stb_truetype` wrapper (`ttf`), virtio-gpu modern driver with display-info/scanout plumbing and framebuffer integration (VGA fallback).
- Virtio hardening: `virtio_blk` interrupt-driven completion with descriptor reuse and flush; writeback `block_cache`; `virtio_input` keyboard driver wired into `kernel/arch/x86_64/keyboard.c`; MSI/MSI-X helpers and basic LAPIC/IOAPIC support.
- Critical fixes discovered while booting the full `init.rc`:
  - LAPIC LINT0 must be programmed as `ExtINT`; masking it silently disables legacy PIT IRQs.
  - A non-bootstrap process exiting as the only runnable task no longer jumps to the kernel shell.
  - Process handles are released at exit time so pipe EOF/HUP wakes blocked pollers.
  - `handlelimitcheck` now probes the real 64-slot handle table instead of assuming 8 slots.
  - `INIT_MAX_SERVICES` raised to 48 and large init buffers moved to static storage; `init.rc` grew beyond the old limits.
- Userspace desktop: `compositor` (window list, Z-order, focus, damage, render/event loop, ready banner), `hydrogen_window` client library, `input_router`, widget toolkit (Label/Button/TextInput/Container), flex layout, theme tokens, window decorations, taskbar with clock, Spotlight-style launcher, file-manager grid, desktop background.
- Verification programs: `fstest` (mkdir/write/stat/read/rename/unlink) and `desktoptest` (compositor + widgets + layout + theme + taskbar + launcher + filemanager).
- `init` now supports `daemon` services: they are spawned once and the init loop stays alive, so compositor, desktop demo and shell run concurrently.
- `tools/boot_smoke.sh` asserts `FSTEST PASS`, `DESKTOPTEST PASS`, the compositor ready banner and `FRAME CHECKSUM:`.
- Boot smoke passes locally (with grub extracted from the Arch package because `grub-mkrescue` is not installed system-wide). (The former pollwaitcheck/pollmanycheck gap is resolved in milestone 0.3, see below.)

### Desktop v0 follow-up: visible framebuffer + keyboard (2026-10-23)

- Multiboot1 header now requests a linear 1024x768x32 video mode. The video
  fields must sit after the five unused a.out-kludge fields; placing them right
  after the checksum makes GRUB report `unsupported graphical mode type`.
- Added `SYS_FB_INFO` (36) and `SYS_FB_BLIT` (37). `sys_fb_blit` writes into the
  framebuffer console's shadow buffer via `fb_console_blit` and presents it, so
  userspace frames are not overwritten by the console's own back buffer.
- `comp_present()` blits the compositor front buffer to the framebuffer.
  `desktopdemo` renders a full-screen gradient, title, a sample window with
  title bar/traffic-light buttons and a taskbar, then presents.
- Keyboard fix: `process_console_fallback_read` no longer executes
  `sti; hlt; cli` inside the read syscall. That let IRQs arrive with a kernel
  frame; the scheduler then returned to a corrupt user RIP (invalid opcode at
  0x303). Console reads now return immediately and the shell retries with
  `yield_cpu()`, so key IRQs are handled in user context.
- Verified: PS/2 key injection produces shell input (`help` output) with no
  panic, and the desktop frame is visible in the QEMU GTK window.

### Shell-app + mouse (2026-10-23)

- `desktopdemo` is now a real desktop session: full-screen background, taskbar,
  a `Terminal` window and a mouse cursor. The terminal is the shell as an app
  (line editor, scrollback, `help/time/ls/cat/pwd/cd/mkdir/rm/touch/mv/cp/echo/
  clear`) rendered through the compositor instead of the raw console.
- New syscalls:
  - `SYS_KBD_READ` (39): non-blocking keyboard read for windowed apps.
  - `SYS_MOUSE_STATE` (38): returns absolute PS/2 mouse position and buttons
    after polling the mouse controller.
- PS/2 mouse works via IRQ12; the desktop polls `SYS_MOUSE_STATE`, redraws and
  draws an arrow cursor into the compositor front buffer before presenting.
- Removed the console `shell` and the separate `compositor` daemons from
  `init.rc`; `desktopdemo` is the single desktop daemon.
- Verified: `help` typed into the windowed terminal renders the command list;
  QEMU `mouse_move` moves the on-screen cursor. Boot smoke still passes.

### Desktop v1: window manager, editor, taskbar, resolutions (2026-10-23)

- Window manager in `desktopdemo`: windows can be focused, raised, dragged by
  the title bar, minimized/restored from the taskbar and maximized via the
  green button. Traffic-light buttons: red = minimize/close, yellow = minimize,
  green = maximize/restore.
- Taskbar (KDE-ish): vector OS hexagon logo, terminal icon, editor icon, running
  indicators and a live clock. Clicking an icon restores or minimizes the app.
- Terminal is a window app with line editor, scrollback and commands:
  `help version uptime time ls cat pwd cd mkdir rm touch mv cp echo clear
  run <path> edit [path] mouse`.
- Text editor window: type, newline, backspace, `Ctrl+S` saves `/notes.txt`,
  `Ctrl+O` loads it, `Ctrl+N` clears. `edit [path]` opens a file in the editor.
- Performance: input now damages only the terminal prompt line and presents
  only the damaged rectangles (`SYS_FB_BLIT_STRIDED`, `comp_present_damage`)
  instead of repainting the full screen. The kernel text cursor is hidden and
  the console is cleared before the desktop presents, removing the old shell
  residue at the bottom.
- Resolution: the multiboot video request is parameterized. Default
  `GFXMODE ?= 1920x1080x32`; build 2K with
  `make ARCH=x86_64 GFXMODE=2560x1440x32 build iso`. The desktop queries the
  real framebuffer size (`SYS_FB_INFO`) and scales window geometry.
- Kernel heap raised to 32 MiB so the framebuffer console back buffer fits at
  1080p/2K.
- Verified: full boot smoke PASS at 1920x1080; 2K renders; mouse move/click,
  minimize/restore and editor typing work in QEMU.

## Full desktop roadmap pass (2026-10-23)

**Desktop shell**
- Start menu from the OS logo with Terminal, Editor, Files, Settings, About.
- Files app lists the mounted filesystem; activating a file opens it in the
  editor. Multiple terminal instances, real close (red button destroys the
  window/instance), minimize/restore, maximize, title-bar drag and corner
  resize.

**Persistence**
- Full ext2 read/write: block and inode bitmap allocation, direct and single
  indirect blocks, sparse-hole reads, mkdir/unlink/rename, inode/dir-count
  bookkeeping. `e2fsck -fn` is clean after guest writes.
- The build stages `fsroot` + all userspace ELFs + `init.rc` into
  `build/x86_64/disk.img`; the kernel mounts it as `/` over virtio-blk, with
  RAMFS as the boot fallback. Boot smoke attaches the disk.
- Persistence verified end-to-end: free-text write in the guest, power off,
  `debugfs -R "cat /persist.txt"` -> `hello`, reboot, `cat persist.txt` ->
  `hello`.
- `df`, `free`, `mount`, `write <path> <text>` terminal commands.

**System shell**
- Settings app: background/theme toggle, keyboard layout (DE/US), resolution
  and version display.
- `DesktopTheme` tokens drive all desktop colors (`COL_*` now resolve to theme
  fields); dark/light presets.
- Icons: vector drawings in `tools/mkicons.py` (rasteriser `tools/vecraster.py`),
  rendered at build time into premultiplied ARGB at exactly the sizes the UI
  draws (`ICON_SIZE_*`), per theme for colour icons, as tintable masks for
  glyphs. `userspace/lib/iconset.c` blends them 1:1 in the taskbar, launcher,
  Files and Settings; `icons.h` only keeps the logo bitmap.

**Comfort/stability**
- IntelliMouse 4-byte packets + wheel field in `SYS_MOUSE_STATE`; terminal and
  editor scroll offsets respond to the wheel.
- Click-to-focus/raise, arrow cursor and a dedicated resize-cursor shape over
  the bottom-right corner.
- `comp_present_damage` merges overlapping/adjacent damage rects and presents
  only those regions via `SYS_FB_BLIT_STRIDED`.
- DE/US keyboard tables selected at runtime through `SYS_SET_LAYOUT`.

**Users / packaging**
- `/etc/passwd` installed on the disk; `login` reads it and bootstraps the
  session (`login: root`, `LOGIN PASS`).
- `hpkg` package manager (`docs/HPKG.md`): `.hpkg` archives with SHA-256,
  package database in `/var/lib/hpkg/db`, file ownership, dependency check,
  transactional install/upgrade with rollback, `remove`, `verify`, and a
  local repository (`/var/lib/hpkg/repo`, built by `tools/hpkg_repo.py`);
  `hpkgtest` prints `HPKG PASS`.

**Verification**
- `make ARCH=x86_64 build` OK; `ARCH=x86_64 tools/boot_smoke.sh` PASS
  (FSTEST, DESKTOPTEST, LOGIN, PKG, compositor and frame checksum checks).
- 2K build (`GFXMODE=2560x1440x32`) boots and renders the desktop with the same
  markers; 1080p is the default.

## Milestone 0.3 "Solid Foundations" (2026-10-29)

- **Poll/scheduler fix:** the excluded poll tests were not a refcount bug. `process_begin_fd_poll_wait()` left pipe-read wait state set, so a woken poll performed a phantom pipe read and consumed the child's byte. `process_schedule()` now keeps trying candidates until one actually resumes and idles in the kernel otherwise. `pollwaitcheck`, `pollmanycheck` and `dupfallbackcheck` are back in `init.rc`. Pipe reference counters are 32-bit and acquisition can fail.
- **Display:** native Bochs/QEMU std-VGA (BGA) driver (`kernel/drivers/bochs_display.c`). Backend order: multiboot VBE, virtio-gpu, BGA, text mode. `pci_init` now runs before `fb_init`. `SYS_FB_INFO` returns `-ENODEV` without a framebuffer.
- **Memory:** `SYS_MMAP` (45) / `SYS_MUNMAP` (46) with a per-process region list (`kernel/mm/vmm_user.c`), a dedicated mmap window, NX for non-exec pages (EFER.NXE), partial unmap and teardown at exit. The old brk syscalls 34/35 are retired. The userspace `malloc`/`free`/`calloc`/`realloc` is built on mmap.
- **ext2:** proper directory create/remove (`SYS_RMDIR` 47), multi-group allocation, cleanup of half-created inodes, and `unlink` refuses directories. `e2fsck -fn` is clean after boot.
- **/proc:** `kernel/fs/procfs.c` provides `/proc/<pid>/status`, `/proc/meminfo`, `/proc/uptime` and `/proc/version`, plus a `ps` tool.
- **ACPI:** RSDP/RSDT/XSDT/FADT parsing and DSDT `\_S5` scan (`kernel/arch/x86_64/acpi.c`). `SYS_SHUTDOWN` (48) and `SYS_REBOOT` (49) flush the block cache first. `/poweroff` and `/reboot` exist. The RTC reads the FADT century through the ACPI module.
- **RTC:** CMOS wall clock (`kernel/arch/x86_64/rtc.c`), `SYS_RTC_TIME` (54), a userspace time library, `date`, and the taskbar shows real time.
- **Network:** virtio-net driver and a minimal Ethernet/ARP/IPv4/ICMP stack (`kernel/net/`) with static QEMU user-net config 10.0.2.15/24. `SYS_NET_INFO` (50) and `SYS_NET_PING` (51) exist, plus a `ping` tool. The smoke test pings 10.0.2.2.
- **Shell:** modular rewrite (lexer/parser/expand/env/exec/jobs/builtins). It supports `>`, `>>`, `<`, `2>`, `2>&1`, pipelines, `&` with `jobs`/`wait` (and/or lists background as one job), `$VAR`/`${VAR}`/`$?`, `export`/`unset`/`env`, and line continuation. New kernel syscalls: `SYS_SPAWN_ARGS` (52), `SYS_TRY_WAIT` (53), and open flags for create/truncate/append.
- **Desktop materials:** cached box-blur glass for the taskbar and launcher, nine-slice soft window shadows, anti-aliased rounded corners, all driven by theme tokens.
- **Fixes:** PS/2 IntelliMouse packet buffer overflow (wheel byte written past `packet_bytes[3]`) and a sequence-point UB in desktopdemo.
- **Verification:** `tools/boot_smoke.sh` now boots grub-free by default (`BOOT_MODE=kernel`, QEMU `-kernel` with `kernel32.elf`, `-vga std`, virtio-net) and requires the desktop. It passes with FSTEST, DIRTEST, MMAP, PROC, RTC, NET, SHELL, POWERCHECK, LOGIN, PKG, DESKTOPTEST, ACPI OK, COMP READY and FRAME CHECKSUM.

### Open follow-ups
- `cwdcheck` is still not in `init.rc`: re-check it against the new ext2 directory code.
- Blocking syscalls still `sti;hlt`-wait inside the kernel when nothing else is runnable; they should block through the scheduler.
- `init` `restart=on-failure` has no retry limit.
- Positioned file writes are whole-file read-modify-write (O(file size)).
- BGA LFB is mapped write-back; write-combining needs PAT.

## Milestone 0.4 "Robust Userland" (2026-10-29)

- **Scheduler blocking:** every blocking syscall (read, write, sleep, poll_wait, poll_many, wait, net_ping, UDP) blocks through the scheduler, with no more in-kernel `sti;hlt` waits. IRQ0 preempts only CPL3 frames. Pipe wakeups are targeted. New test `schedtest`.
- **init:** `retries=N` (default 3) with exponential backoff, failed services are marked, and dependents are skipped with a log line. Syntax is in `docs/INIT_RC.md`. `cwdcheck` is back: its directory name collided with the `/cwdcheck` binary, and VFS/ext2 were correct.
- **User memory:** syscalls validate user pointers with a page-table walk (`paging64.c`). Unmapped, kernel, non-canonical and wrapping pointers return `-EFAULT` (`faulttest`).
- **Signals:** `SYS_KILL` (56), `SYS_SIGACTION` (57), `SYS_SIGRETURN` (58). User faults deliver SIGSEGV/SIGILL instead of panicking, blocked syscalls return `-EINTR`, and init cannot be killed. The shell has `kill`, and Ctrl+C works via virtio-input (`sigtest`).
- **mprotect:** `SYS_MPROTECT` (55), `PROT_NONE`, W^X, a stack guard page and guarded large malloc mappings (`mprotecttest`).
- **libc:** modular (string, stdlib, sort, stdio, format, clock, environ, errno). Buffered `FILE` streams, a new printf family, `qsort`/`bsearch`, `strto*`, `getenv`/`setenv`, `time`/`clock_gettime`. `fprintf` takes `FILE*`; the fd form is `dprintf` (`libctest`).
- **UDP:** socket handles (`SYS_SOCKET` 59, `SYS_BIND` 60, `SYS_SENDTO` 61, `SYS_RECVFROM` 62) with poll support, a DNS resolver and `nslookup` (`udptest`).
- **Links:** `SYS_LINK` (63), `SYS_SYMLINK` (64), `SYS_READLINK` (65), fast and slow ext2 symlinks, and path resolution with depth limit 8 (`-ELOOP`). Adds `ln [-s]` and `readlink` (`linktest`). `e2fsck` is clean.
- **syslogd:** the first daemon started by init, with `stdout=syslog`/`stderr=syslog`/`log=syslog`. It writes `/var/log/system.log` with 64 KiB rotation. Adds `logread` and the `/bin /etc /tmp /home /var/log` layout (`logtest`).
- **Settings app:** Appearance (theme, accent, glass, live preview), Keyboard, Display, Network (ping) and About. Persisted in `/etc/settings.conf`.
- **Verification:** clean build; `tools/boot_smoke.sh` passed 3 times in a row with all 21 markers.

### Open follow-ups
- CR0.WP is off, so kernel writes into READ-only user pages are not blocked; `user_range_ok` has no direction.
- The `SYS_WAIT` completion path writes the status through a stored user pointer without re-validation.
- `process_exit_records` has 32 slots and silently overwrites when full.
- Pipe waiters are found by an O(n) process scan; per-pipe wait queues are needed.
- init daemons are never restarted, and there is no per-service start timeout.
- Ctrl+C only via virtio-input, not serial; there is no syscall to block signals (sigprocmask).
- Positioned file writes are still whole-file read-modify-write.

## Milestone 0.5 "Hardening & Throughput" (2026-10-30)

Integrated on branch `milestone-0.5` (smoke passes, ext2 `e2fsck` clean).

- Write protection (CR0.WP asserted), NX user stack/data, exit records published before status copy.
- Generic wait queues; init supervises daemons (restart policies, timeouts); `SYS_SIGPROCMASK`.
- ext2 positioned writes, `SYS_TRUNCATE`, write-back block cache (flush after ~1 s, on unmount, power-off).
- Minimal TCP (stream sockets, loopback, retransmission); `execve`; users/groups/file permissions.
- Directory entry cache, performance counters (`top`), boot splash.
- Keyboard: DE/US tables fixed (AltGr, `< > |`, Caps Lock), virtio-input shares the PS/2 path; font glyph fixes; `tools/uitest`.

Open follow-ups: no TCP congestion control/reassembly, orphans are not reparented, init polls children
every 10 ms (no SIGCHLD). Closed in 0.6 (see the end of this file): sync syscall, ftruncate, ext2 mtime from the RTC.

### Desktop polish (merged into 0.5, 2026-10-30)

Window-manager fixes (no stale shadow pixels, taskbar restore, themed chrome, maximize to work area, Alt+Tab),
Terminal hosting the real shell, redesigned Files app, Spotlight-style launcher, gap-buffer editor,
new root layout (`/bin`, `/sbin`, `/lib/tests`, `/etc`, `/tmp`, `/home/user`; see `docs/FS_LAYOUT.md`).
Older sections above still mention the pre-layout paths `/init` and `/init.rc`.
Open: no top bar.

### Shell polish (2026-10-30)

- Escape reaches the desktop through the shared PS/2 and virtio-input path (`KEY_ESCAPE`, `KBD_KEY_ESCAPE`); it now
  also cancels the Files rename field and delete dialog, next to the launcher and the editor prompts that already used it.
- Shell: `{a,b}` brace expansion (`userspace/shell/brace.c`, runs before quote removal; no `{1..3}` ranges, and
  `NAME={a,b}` assigns the text as written), `ls -a` (without it `./` and `../` are hidden, `-l` and `-a` combine),
  new `/bin/wc` (`-l -w -c`, stdin or files). Terminal completion now offers `chmod`, `chown`, `id`, `whoami` and the
  programs in `/bin` and `/sbin` (it used to scan `/`).
- Open: Ctrl+D cannot end the input of a running foreground command (see `docs/TERMINAL.md`).
Top bar (focused app name, clock), themed Editor surface, windows kept inside the work area.
Open: no Escape key from the kernel keyboard driver, brace expansion and Ctrl+D EOF in the shell,
`wc` missing, `ls` lists `./` and `../`.

## Milestone 0.6 follow-ups: fsync and polish (2026-10-30)

- **`SYS_SYNC` (84) / `SYS_FTRUNCATE` (85):** `sync` flushes the ext2 write-back cache (and with it every dirty
  superblock, descriptor, bitmap and inode block) to the device; `sys_ftruncate` resizes the file behind a writable
  fd without touching its offset. Libc has `sync()` and `ftruncate()`, the shell has a `sync` builtin and the Editor's
  Save calls `sync()`. `iotest` covers both.
- **Write errors:** a write that stores only part of its data now returns the short count (offset and size follow it);
  the next call reports `-ENOSPC` on a full disk or `-EFBIG` beyond 2 GiB - 1 instead of a generic `-EIO`.
  `iotest` fills the disk to check this and that the space comes back.
- **ext2 timestamps** come from the RTC wall clock; `st_mtime` of `sys_stat` is now seconds since the Unix epoch
  (0 on RAMFS and /proc). Without a usable RTC the old superblock-based lower bound is used.
- **Files:** double-click on a text file opens the Editor; on a program in `/bin` or `/sbin` (or any ELF) it opens a
  Terminal window and types the command at its prompt (`terminal_run_line`), so output and keyboard input work.
- **Version:** `kernel/include/version.h` is the single source (now `0.6.0-dev`); `/proc/version`, the shell's
  `version` and Settings -> About all read it.
- **Settings -> Network:** `drop` counted loopback datagrams discarded by the stack (UDP queue overflow, malformed
  TCP segments from the tests), which made it larger than `rx`. It now counts only frames that arrived on the link.
- Verification: `e2fsck -fn` on the image after guest writes (Editor save, `iotest`) is clean.

Open follow-ups: no TCP congestion control/reassembly, orphans are not reparented, init polls children every 10 ms
(no SIGCHLD), no `fsync(fd)` for a single file (`sync` flushes everything), programs started from Files have no way
to run in the background without a Terminal window.

## Absolute pointer (2026-10-31)

- `virtio_input` now drives `virtio-tablet-pci` (EV_ABS, buttons, wheel) into the shared mouse state, so the cursor follows the host pointer without a QEMU grab; PS/2 stays the fallback, last report wins. See `docs/POINTER_INPUT.md`.
- The desktop clips the cursor blit at the screen edge; before, the cursor vanished in the bottom 13 pixel rows.

## Milestone 0.6 (2026-10-31)

hpkg package manager with local repository (`docs/HPKG.md`), crash dumps and OOM policy (`docs/CRASH_OOM.md`),
`SYS_SYNC`/`SYS_FTRUNCATE`, RTC-based ext2 mtimes, real logo artwork and a vector icon set
(`tools/mklogo.py`, `tools/mkicons.py`), absolute pointer via virtio-tablet (`docs/POINTER_INPUT.md`).
Boot-time regression fixed: virtio-blk register access is volatile (write cache/flush now negotiated) and the
block cache writes back sorted, merged runs; boot to desktop takes ~22 s headless.
