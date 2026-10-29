# Syscall ABI (x86_64) - Draft v0

## Ziele
- Klare und stabile User/Kernel ABI fuer Userspace.
- Minimaler Satz an Syscalls fuer Init, Shell, Filesystem und IPC.
- Erweiterbar ohne ABI-Bruch.

## Calling Convention
- Instruktion: `syscall` (Legacy: `int $0x80` bleibt als Debug-Fallback)
- Syscall-Nummer: `RAX`
- Argumente: `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`
- Return: `RAX`
- Clobbered: `RCX`, `R11`
- Stack: 16-byte aligned (SysV)

## Rueckgabewerte
- Erfolg: `RAX >= 0`
- Fehler: `RAX = -errno`

Beispiel: `-2` entspricht `ENOENT`.

## Pointer/Buffer Regeln
- Alle User-Pointer muessen im User-Adressraum liegen.
- Kernel validiert Pointer und Laenge, sonst `-EFAULT`.
- Strings sind UTF-8, null-terminiert.
- Pfadbasierte Syscalls loesen relative Pfade gegen das prozesslokale aktuelle Working Directory auf; `.` und `..` werden dabei normalisiert.

## Handle/FD Modell
- Integer Handles pro Prozess.
- `0,1,2` sind stdin/stdout/stderr.
- Der aktuelle Kernel nutzt dafuer eine getypte prozesslokale Handle-Tabelle.
- Dateihandles tragen jetzt eine eigene gemeinsame File-Referenz mit Dateioffset; `read` arbeitet deshalb sequentiell weiter, und `dup`/Handle-Vererbung teilen sich denselben Offset-Zustand.
- Zusaetzlich koennen Prozesse eigene getypte stdio-Eintraege fuer `0,1,2` haben; ohne Bindung fallen diese intern auf duplizierbare Console-Reader/Writer fuer Keyboard/TTY zurueck.
- Bisher sind `PROCESS_HANDLE_FILE`, `PROCESS_HANDLE_PIPE_READER`, `PROCESS_HANDLE_PIPE_WRITER`, `PROCESS_HANDLE_NULL_READER`, `PROCESS_HANDLE_NULL_WRITER`, `PROCESS_HANDLE_CONSOLE_READER` und `PROCESS_HANDLE_CONSOLE_WRITER` umgesetzt.
- Normale Handle-Slots sind standardmaessig close-on-spawn; beim `sys_spawn` werden nur explizit markierte Handles mit denselben Integer-Slots ans Child weitergereicht.
- Normales `sys_spawn` vererbt konfiguriertes stdio des Parent-Prozesses; `sys_spawn_with_stdio` kann Child-`stdin/stdout/stderr` explizit aus Parent-FDs binden.
- `sys_dup` klont einen vorhandenen FD auf den naechsten freien nicht-stdio Handle-Slot; `sys_dup2` klont explizit auf einen Ziel-FD einschliesslich `0/1/2`. Das gilt jetzt auch fuer das implizite stdio-Console-Fallback.
- `sys_dup3` klont wie `sys_dup2`, versteht aber zusaetzliche Spawn-Flags fuer den Ziel-FD: `DUP3_INHERIT` markiert das Duplikat sofort als vererbbar, `DUP3_CLOSE_ON_SPAWN` markiert es explizit als close-on-spawn.
- `sys_lseek` arbeitet aktuell fuer Dateihandles (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`) auf dem gemeinsamen File-Offset; nicht seekbare Handles wie Pipes oder Console-Fallback liefern `-ESPIPE`.
- `sys_poll` liefert eine sofortige Readiness-Maske fuer Handles: `POLL_READ`, `POLL_WRITE`, `POLL_HUP`. Fuer Pipes bedeutet das "wuerde ein Read/Write jetzt blockieren?".
- `sys_poll_wait` erweitert dieselben Events um blockierendes Warten mit Millisekunden-Timeout; `timeout_ms = 0` ist ein reiner Snapshot wie `sys_poll`, `timeout_ms = -1` wartet unbegrenzt.
- `sys_poll_many` erweitert das Modell auf kleine Poll-Listen (`fd`, `events`, `revents`) fuer mehrere FDs in einem Syscall; Rueckgabe ist die Anzahl der Eintraege mit gesetztem `revents`. `fd = -1` wird ignoriert, ungueltige FDs markieren `revents = POLL_NVAL` statt den ganzen Call scheitern zu lassen.
- Handles tragen intern Read-/Write-Rechte passend zum Ressourcentyp; `sys_spawn_with_stdio` weist Bindings mit falscher Richtung (z. B. Writer auf `stdin`, Reader auf `stdout`) jetzt direkt ab.
- `sys_handle_set_inherit` arbeitet jetzt auf beliebigen FDs, nicht mehr nur auf `fd >= 3`; damit koennen auch explizite stdio-Bindings fuer spaetere `spawn`-Aufrufe als vererbbar bzw. close-on-spawn markiert werden.
- `sys_spawn_with_stdio` versteht zusaetzlich negative Steuerwerte: `-2` = stdio vom Parent unveraendert erben, `-3` = Null-stdio binden; `-1` bleibt "Binding loeschen".
- Das aktuelle Userspace-`init` nutzt `sys_spawn_with_stdio`, um die in `/init.rc` beschriebenen einfachen stdio-Topologien (`stdin=inherit|null|pipe|channel|tee`, `stdout=inherit|null|capture|channel|tee|logsink`, `stderr=inherit|null|capture|channel|tee|logsink`) aufzubauen.
- Die aktuelle `init`-Channel-Schicht modelliert einen Reader pro benanntem Channel; mehrere Writer auf denselben Channel werden unterstuetzt und nutzen denselben Pipe-Stream.
- Zusaetzlich baut `init` rein im Userspace gepufferte `tee`-Kanaele fuer spaetere Mehrfach-Leser und deklarative `logsink`s fuer aggregierte Service-Ausgaben ueber dieselbe Kernel-ABI.
- Das implizite Keyboard/TTY-Fallback fuer ungebundene stdio-Slots ist jetzt als interner Console-Handle-Typ modelliert und kann daher ueber `dup`/`dup2` geklont, auf andere FDs gelegt und wieder auf `0/1/2` restauriert werden.
- EOF auf geerbten Pipe-Read-Enden ist umgesetzt.
- `read` auf leerer Pipe mit aktivem Writer blockiert aktuell, bis ein Write den Leser aufweckt.
- `write` auf voller Pipe blockiert aktuell, bis ein Reader Platz schafft.
- `write` ohne Reader liefert aktuell `-EPIPE`.
- Null-`stdin` liefert sofort EOF; Null-`stdout`/`stderr` verwerfen Daten und melden Erfolg mit der geschriebenen Bytezahl.
- `close` und implizites Prozess-Exit wecken wartende Pipe-Gegenseiten fuer EOF/Broken-Pipe auf.
- Weitergehende Referenzsemantik folgt spaeter; das aktuelle Rechte-Modell deckt bisher Read/Write plus `inherit` ab.

## Syscall Tabelle (v0)
| Nr | Name | Signatur | Beschreibung |
|----|------|----------|--------------|
| 0 | sys_exit | `int status` | Prozess beenden |
| 1 | sys_write | `int fd, const void* buf, size_t len` | in FD schreiben |
| 2 | sys_read | `int fd, void* buf, size_t len` | aus FD lesen |
| 3 | sys_open | `const char* path, int flags` | Datei oeffnen |
| 4 | sys_close | `int fd` | FD schliessen |
| 5 | sys_lseek | `int fd, int64_t off, int whence` | Dateiposition |
| 6 | sys_mmap | `void* addr, size_t len, int prot, int flags, int fd, int64_t off` | Mapping |
| 7 | sys_munmap | `void* addr, size_t len` | Mapping entfernen |
| 8 | sys_spawn | `const char* path, const char* const* argv` | Prozess starten |
| 9 | sys_wait | `int pid, int* status` | blockierend auf direkten Child-Prozess warten |
| 10 | sys_getpid | `void` | PID |
| 11 | sys_sleep | `uint64_t ms` | Sleep |
| 12 | sys_yield | `void` | Scheduler yield |
| 13 | sys_time_ms | `void` | monotone Zeit |
| 14 | sys_tty_set_color | `uint8_t fg, uint8_t bg` | TTY-Farben fuer Textausgabe |
| 15 | sys_pipe | `int fds[2]` | Read-/Write-Ende einer lokalen Pipe erzeugen |
| 16 | sys_handle_set_inherit | `int fd, int inherit` | Handle fuer Spawn-Vererbung setzen/loeschen |
| 17 | sys_spawn_with_stdio | `const char* path, int stdin_fd, int stdout_fd, int stderr_fd` | Prozess mit explizitem Child-stdio starten |
| 18 | sys_dup | `int fd` | FD auf den naechsten freien nicht-stdio Slot klonen |
| 19 | sys_dup2 | `int src_fd, int dst_fd` | FD explizit auf Ziel-FD klonen/umlenken |
| 20 | sys_dup3 | `int src_fd, int dst_fd, uint32_t flags` | Wie `dup2`, aber mit Spawn-Flags fuer den Ziel-FD |
| 21 | sys_poll | `int fd, uint32_t events` | Sofortige Readiness-Abfrage fuer einen Handle |
| 22 | sys_poll_wait | `int fd, uint32_t events, int64_t timeout_ms` | Blockierend auf Handle-Readiness warten |
| 23 | sys_poll_many | `struct poll_fd* fds, uint32_t count, int64_t timeout_ms` | Mehrere FDs gleichzeitig pollen/warten |
| 47 | sys_rmdir | `const char* path` | Leeres Verzeichnis entfernen; `-ENOTEMPTY` wenn nicht leer, `-ENOTDIR` fuer Nicht-Verzeichnisse, `-EBUSY` fuer Mount-Wurzeln |
| 54 | sys_rtc_time | `struct hydrogen_rtc_time* out` | UTC-Wanduhr: Epoch-Sekunden, Millisekunden und aufgeschluesselte Felder (Jahr, Monat 1-12, Tag, Stunde, Minute, Sekunde, Wochentag 0=Sonntag); `-EFAULT` bei ungueltigem Pointer, `-EIO` wenn die CMOS-RTC beim Boot nicht lesbar war |

> Weitere Syscalls werden fortlaufend ergaenzt. Neue Syscalls erhalten **neue Nummern**; bestehende Nummern werden nie umgebogen.
> Aktuell implementiert: `sys_exit`, `sys_write`, `sys_read`, `sys_open`, `sys_close`, `sys_lseek`, `sys_spawn`, `sys_spawn_with_stdio`, `sys_wait`, `sys_getpid`, `sys_yield`, `sys_sleep`, `sys_time_ms`, `sys_tty_set_color`, `sys_pipe`, `sys_handle_set_inherit`, `sys_dup`, `sys_dup2`, `sys_dup3`, `sys_poll`, `sys_poll_wait`, `sys_poll_many` (und ein undokumentiertes `sys_putchar` fuer Debug-Ausgaben).
> Dateisystem-Syscalls (`sys_mkdir`, `sys_rmdir`, `sys_unlink`, `sys_rename`, `sys_writef`) liefern die errno-Werte des Treibers durch: `ENOENT`, `EEXIST`, `ENOTDIR`, `EISDIR (21)` (`sys_unlink` auf ein Verzeichnis), `ENOTEMPTY (39)`, `ENOSPC (28)`, `EINVAL`, `EBUSY`, `EPERM`, `EROFS (30)`, `EMLINK (31)`; alles andere wird zu `EIO`.
> `sys_rtc_time` liest die CMOS-RTC einmal beim Boot (Jahrhundert-Register aus der ACPI-FADT, sonst 20xx) und fuehrt die Wanduhr danach ueber den monotonen Timer fort; Layout siehe `kernel/include/rtc_time.h`.
> Der aktuelle Kernel behandelt `sys_wait` als blockierendes Warten auf einen direkten Child-Prozess; unbekannte oder fremde PIDs liefern `-ENOENT`.

## Error Codes (Subset)
Diese errno-Werte sind als Startmenge vorgesehen:
- `EPERM (1)`
- `ENOENT (2)`
- `EIO (5)`
- `EBADF (9)`
- `ENOMEM (12)`
- `EACCES (13)`
- `EFAULT (14)`
- `EBUSY (16)`
- `ENODEV (19)` – z. B. `SYS_FB_INFO` (36), wenn kein linearer Framebuffer vorhanden ist
- `EINVAL (22)`
- `EMFILE (24)`
- `ESPIPE (29)`
- `EPIPE (32)`
- `ENOSYS (38)`

## Future Extensions
- `sys_fork` / `sys_execve` (falls POSIX-Nahe gewuenscht)
- `sys_pipe`, `sys_shm_create`, `sys_shm_map`
- `sys_epoll`/`sys_poll`
- `sys_socket` (wenn Netzwerktrack startet)
