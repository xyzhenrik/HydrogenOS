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
