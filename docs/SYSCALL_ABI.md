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
- Alle User-Pointer muessen im User-Adressraum liegen (kanonische untere Haelfte, unterhalb `0x0000_8000_0000_0000`).
- Der Kernel laeuft vor jedem Zugriff die Seitentabellen des Prozesses seitenweise ab (`kernel/core/usercopy.c`): jede Seite des Bereichs muss gemappt und fuer User-Mode zugaenglich sein, Ausgabepuffer zusaetzlich beschreibbar. Supervisor-only-Speicher (Kernel-Identity-Map), nicht gemappte Seiten, nicht kanonische Adressen, ueberlaufende `addr + len`-Bereiche und Schreibzugriffe auf schreibgeschuetzte Mappings liefern `-EFAULT`; es wird nichts teilweise kopiert.
- Optionale Pointer (`argv`/`envp` bei `sys_spawn_args`, `status` bei `sys_wait`/`sys_try_wait`) duerfen `NULL` sein; fuer alle anderen Pointer ist `NULL` ungueltig (`-EFAULT`). `sys_write`/`sys_read` mit `len = 0` liefern `0`, ohne den Pointer zu pruefen.
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
- Zusaetzlich kann `init` `stdout`/`stderr` eines Services mit `stdout=syslog`, `stderr=syslog` oder `log=syslog` (beides) an den Log-Daemon `syslogd` binden; Details in `docs/SYSLOG.md`.
- Die aktuelle `init`-Channel-Schicht modelliert einen Reader pro benanntem Channel; mehrere Writer auf denselben Channel werden unterstuetzt und nutzen denselben Pipe-Stream.
- Zusaetzlich baut `init` rein im Userspace gepufferte `tee`-Kanaele fuer spaetere Mehrfach-Leser und deklarative `logsink`s fuer aggregierte Service-Ausgaben ueber dieselbe Kernel-ABI.
- Das implizite Keyboard/TTY-Fallback fuer ungebundene stdio-Slots ist jetzt als interner Console-Handle-Typ modelliert und kann daher ueber `dup`/`dup2` geklont, auf andere FDs gelegt und wieder auf `0/1/2` restauriert werden.
- EOF auf geerbten Pipe-Read-Enden ist umgesetzt.
- `read` auf leerer Pipe mit aktivem Writer blockiert aktuell, bis ein Write den Leser aufweckt.
- `write` auf voller Pipe blockiert aktuell, bis ein Reader Platz schafft.
- `write` ohne Reader liefert aktuell `-EPIPE`.
- Null-`stdin` liefert sofort EOF; Null-`stdout`/`stderr` verwerfen Daten und melden Erfolg mit der geschriebenen Bytezahl.
- `close` und implizites Prozess-Exit wecken wartende Pipe-Gegenseiten fuer EOF/Broken-Pipe auf.
- Alle blockierenden Syscalls (`read`, `write`, `wait`, `sleep`, `poll_wait`, `poll_many`, `net_ping`) blockieren ueber den Scheduler: der Prozess wird mit Warte-Grund markiert, der Scheduler wechselt weg (Leerlauf im Kernel, wenn nichts lauffaehig ist) und schliesst den Syscall beim Aufwecken ab. Pipe-Ereignisse wecken nur Prozesse, die auf genau diese Pipe warten (Reader bei Daten/EOF, Writer bei Platz/Broken-Pipe, Poller auf dieser Pipe).
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
| 45 | sys_mmap | `void* hint, size_t len, uint32_t prot, uint32_t flags` | Anonymen, genullten Speicher mappen; Rueckgabe = Adresse |
| 46 | sys_munmap | `void* addr, size_t len` | Seitenbereich (auch teilweise) entfernen |
| 47 | sys_rmdir | `const char* path` | Leeres Verzeichnis entfernen; `-ENOTEMPTY` wenn nicht leer, `-ENOTDIR` fuer Nicht-Verzeichnisse, `-EBUSY` fuer Mount-Wurzeln |
| 48 | sys_shutdown | `uint32_t flags` (muss 0 sein) | Block-Cache flushen, dann ACPI S5 (Soft-Off). Kehrt nur bei Fehler zurueck: `-ENOSYS` ohne nutzbares `\_S5`/PM1-Register, `-EIO` wenn der Flush scheitert oder S5 nicht greift, `-EINVAL` bei Flags != 0 |
| 49 | sys_reboot | `uint32_t flags` (muss 0 sein) | Block-Cache flushen, dann FADT-Reset-Register, Fallback 8042-Reset-Puls (`0xFE`), Fallback Triple Fault. Kehrt nur bei Fehler zurueck: `-EIO` wenn der Flush scheitert, `-EINVAL` bei Flags != 0 |
| 50 | sys_net_info | `struct net_info* out` | MAC, IPv4-Konfiguration, Link-Status und Frame-Zaehler der Netzwerkschnittstelle |
| 51 | sys_net_ping | `uint32_t ipv4, int64_t timeout_ms` | ICMP-Echo senden und blockierend auf die Antwort warten; liefert RTT in ms |
| 54 | sys_rtc_time | `struct hydrogen_rtc_time* out` | UTC-Wanduhr: Epoch-Sekunden, Millisekunden und aufgeschluesselte Felder (Jahr, Monat 1-12, Tag, Stunde, Minute, Sekunde, Wochentag 0=Sonntag); `-EFAULT` bei ungueltigem Pointer, `-EIO` wenn die CMOS-RTC beim Boot nicht lesbar war |
| 55 | sys_mprotect | `void* addr, size_t len, uint32_t prot, uint32_t flags` | Schutz eines vollstaendig gemappten Bereichs aendern (inkl. `PROT_NONE`) |

| 52 | sys_spawn_args | `const char* path, const char* const* argv, const char* const* envp, int stdin_fd, int stdout_fd, int stderr_fd` | Prozess mit argv/envp und explizitem Child-stdio starten |
| 53 | sys_try_wait | `int pid, int* status` | Nicht blockierend pruefen, ob ein direkter Child-Prozess beendet ist |
| 56 | sys_kill | `int pid, int sig` | Signal an einen Prozess senden; `sig = 0` prueft nur, ob der Prozess existiert. `-EINVAL` bei unbekannter Signalnummer, `-ESRCH` ohne lebenden Prozess, `-EPERM` fuer ein Signal, das den Bootstrap-Prozess (`init`) beenden wuerde |
| 57 | sys_sigaction | `int sig, const struct sigaction* act, struct sigaction* old` | Signalbehandlung setzen und/oder die bisherige lesen (`act` bzw. `old` duerfen 0 sein). `-EINVAL` fuer unbekannte Signale, `SIGKILL`, reservierte Flags oder ungueltige Handler-Adressen, `-EFAULT` bei nicht gemappten Zeigern |
| 58 | sys_sigreturn | `void` | Nur fuer den Signal-Trampolin: stellt den beim Handler-Eintritt gesicherten Zustand wieder her; kehrt nicht zum Aufrufer zurueck |
| 59 | sys_socket | `int domain, int type, int protocol` | UDP-Socket anlegen (`AF_INET = 2`, `SOCK_DGRAM = 2`, Protokoll `0` oder `17`); liefert einen Handle-fd |
| 60 | sys_bind | `int fd, uint32_t ipv4, uint32_t port` | Lokale Adresse (`0` = alle) und Port (`0` = ephemer) binden |
| 61 | sys_sendto | `int fd, const void* buf, size_t len, uint32_t ipv4, uint32_t port` | Ein Datagramm senden; liefert `len` |
| 62 | sys_recvfrom | `int fd, void* buf, size_t len, uint32_t flags, struct net_sockaddr* from` | Ein Datagramm empfangen (blockierend, `NET_MSG_DONTWAIT` = 1 fuer nicht blockierend); liefert die kopierte Laenge |
| 63 | sys_link | `const char* old_path, const char* new_path` | Hartlink auf eine Nicht-Verzeichnis-Datei (auch auf einen Symlink selbst) anlegen und `i_links_count` erhoehen; `-EEXIST`, `-ENOENT`, `-EISDIR` (Verzeichnis), `-EMLINK`, `-EXDEV` (anderes Dateisystem), `-EOPNOTSUPP` (kein ext2), `-EROFS` |
| 64 | sys_symlink | `const char* target, const char* link_path` | Symlink anlegen. `target` wird unveraendert gespeichert (kein cwd-Bezug, hoechstens 255 Bytes) und erst beim Folgen interpretiert; Ziele bis 60 Bytes stehen in `i_block` (fast symlink), laengere in einem Datenblock. Fehler wie `sys_link`, zusaetzlich `-ENOENT` bei leerem Ziel |
| 65 | sys_readlink | `const char* path, char* buf, size_t cap` | Kopiert das Ziel (ohne NUL, auf `cap` gekuerzt) nach `buf` und liefert die Anzahl kopierter Bytes; `-EINVAL` wenn `path` kein Symlink ist, `-ENOENT`, `-EFAULT` |

> Weitere Syscalls werden fortlaufend ergaenzt. Neue Syscalls erhalten **neue Nummern**; bestehende Nummern werden nie umgebogen.
> Aktuell implementiert: `sys_exit`, `sys_write`, `sys_read`, `sys_open`, `sys_close`, `sys_lseek`, `sys_spawn`, `sys_spawn_with_stdio`, `sys_wait`, `sys_getpid`, `sys_yield`, `sys_sleep`, `sys_time_ms`, `sys_tty_set_color`, `sys_pipe`, `sys_handle_set_inherit`, `sys_dup`, `sys_dup2`, `sys_dup3`, `sys_poll`, `sys_poll_wait`, `sys_poll_many`, `sys_kill`, `sys_sigaction`, `sys_sigreturn` (und ein undokumentiertes `sys_putchar` fuer Debug-Ausgaben).
> Dateisystem-Syscalls (`sys_mkdir`, `sys_rmdir`, `sys_unlink`, `sys_rename`, `sys_writef`) liefern die errno-Werte des Treibers durch: `ENOENT`, `EEXIST`, `ENOTDIR`, `EISDIR (21)` (`sys_unlink` auf ein Verzeichnis), `ENOTEMPTY (39)`, `ENOSPC (28)`, `EINVAL`, `EBUSY`, `EPERM`, `EROFS (30)`, `EMLINK (31)`; alles andere wird zu `EIO`.
> Symlinks (nur ext2): Pfadaufloesung folgt Symlinks in allen Komponenten (relative Ziele ab dem Verzeichnis des Links, absolute Ziele ab `/`), insgesamt hoechstens 8 Expansionen pro Aufruf, danach `-ELOOP (40)`; zu lange Ergebnispfade liefern `-ENAMETOOLONG (36)`. Die letzte Komponente wird bei `sys_open`, `sys_stat`, `sys_readdir`, `sys_chdir`, `sys_spawn*` und `sys_writef` gefolgt, bei `sys_unlink`, `sys_rmdir`, `sys_mkdir`, `sys_rename`, `sys_link`, `sys_symlink` und `sys_readlink` nicht (diese wirken auf den Link selbst). `sys_stat` hat als dritten Parameter `flags`; `STAT_NOFOLLOW (1)` beschreibt einen Symlink selbst (`st_mode` mit `S_IFLNK`, `st_size` = Zielaenge), unbekannte Flags liefern `-EINVAL`. `sys_readdir` meldet Symlinks mit `d_type = 10`. `..` in dem vom Aufrufer uebergebenen Pfad wird weiterhin lexikalisch gegen den cwd-Pfad aufgeloest, `..` in Symlink-Zielen dagegen physisch. Nach `sys_chdir` durch einen Symlink ist der cwd der aufgeloeste Pfad. Die Fehlerliste der Dateisystem-Syscalls oben gilt auch fuer `sys_link`/`sys_symlink`, erweitert um `EXDEV (18)`, `ELOOP (40)`, `ENAMETOOLONG (36)` und `EOPNOTSUPP (95)`.
> `sys_rtc_time` liest die CMOS-RTC einmal beim Boot (Jahrhundert-Register aus der ACPI-FADT, sonst 20xx) und fuehrt die Wanduhr danach ueber den monotonen Timer fort; Layout siehe `kernel/include/rtc_time.h`.
> Der aktuelle Kernel behandelt `sys_wait` als blockierendes Warten auf einen direkten Child-Prozess; unbekannte oder fremde PIDs liefern `-ENOENT`.

## Anonymer Speicher (sys_mmap / sys_munmap)
- Alle Mappings liegen in einem eigenen Fenster `0x0000_0080_0000_0000 .. 0x0000_7000_0000_0000` (ab PML4-Slot 1) und kollidieren daher nie mit ELF-Segmenten oder dem User-Stack.
- Seitengranular: `len` wird auf 4 KiB aufgerundet, Seiten werden sofort alloziert und genullt.
- `prot`: `MMAP_PROT_READ (1)`, `MMAP_PROT_WRITE (2)`, `MMAP_PROT_EXEC (4)` oder `MMAP_PROT_NONE (0)`. Ausser bei `NONE` ist `READ` Pflicht; ohne `EXEC` werden die Seiten per NX nicht ausfuehrbar markiert (sofern die CPU NX kann).
- `PROT_NONE`-Seiten sind angelegt (Frame vorhanden, zaehlt zum Speicherverbrauch), aber jeder Zugriff aus dem User-Mode loest einen Fault aus. Typischer Einsatz: Guard-Pages.
- W^X: `WRITE|EXEC` zusammen wird mit `-EACCES` abgelehnt, ausser `flags` enthaelt `MMAP_FLAG_ALLOW_WX (2)`.
- `hint`: `0` oder seitenausgerichtete Wunschadresse; ist der Bereich im Fenster frei, wird er genommen, sonst waehlt der Kernel den niedrigsten freien Bereich.
- `flags` ist `0` oder `MMAP_FLAG_ALLOW_WX (2)`; alle anderen Bits sind reserviert.
- Fehler: `-EINVAL` (len 0, unbekannte/fehlende prot-Bits, unbekannte flags, unausgerichteter hint/addr, Bereich ausserhalb des Fensters), `-EACCES` (`WRITE|EXEC` ohne `MMAP_FLAG_ALLOW_WX`), `-ENOMEM` (kein Adressraum, kein physischer Speicher oder Region-Limit erreicht). Ein fehlgeschlagenes `sys_mmap` hinterlaesst keine Teil-Mappings.
- `sys_munmap` darf Teile eines Mappings entfernen (Kopf, Ende, Mitte); nicht gemappte Teile des Bereichs werden ignoriert.
- `sys_mprotect` aendert `prot` fuer `[addr, addr+len)` (`len` auf Seiten aufgerundet). Der Bereich muss lueckenlos in Mappings liegen: ausserhalb des Fensters oder unausgerichtet `-EINVAL`, nicht vollstaendig gemappt `-ENOMEM`. Mappings werden bei Bedarf geteilt und gleich geschuetzte Nachbarn wieder zusammengefuehrt; reicht das Region-Limit fuer eine Teilung nicht, kommt `-ENOMEM`. `prot`/`flags` gelten wie bei `sys_mmap`. Bei einem Fehler bleibt alles unveraendert; der Seiteninhalt bleibt bei jeder Schutzaenderung erhalten.
- Ein Zugriff, den die Seitenrechte verbieten (Schreiben auf `READ`, jeder Zugriff auf `NONE`, Ausfuehren ohne `EXEC`, Zugriff auf ungemappte Seiten), loest einen Fault aus, der den Prozess per `SIGSEGV` beendet (Exit-Status `SIGNAL_STATUS(SIGSEGV)`, siehe Signale). Der User-Stack hat eine `PROT_NONE`-Guard-Page unter sich (ausserhalb des mmap-Fensters, nicht per `sys_mprotect` aenderbar), und `malloc` haengt an jedes grosse Mapping (ab 128 KiB) eine `PROT_NONE`-Seite; dadurch zaehlt jede solche Allokation zwei Regionen, und `malloc` liefert bei erschoepftem Region-Limit 0.
- Syscalls mit User-Puffern, die eine `PROT_NONE`-Seite beruehren, schlagen mit `-EFAULT` fehl. Das gilt auch fuer Ausgabepuffer auf `READ`-Seiten.
- Beim Prozess-Exit werden alle Mappings automatisch freigegeben.
- Die frueheren Nummern 34/35 (brk-artiges Mapping) sind stillgelegt und werden nicht wiederverwendet.
## Netzwerk (sys_net_info / sys_net_ping)
- IPv4-Adressen werden als `uint32_t` mit dem ersten Oktett im hoechstwertigen Byte uebergeben (`10.0.2.15` = `0x0A00020F`).
- `struct net_info` (32 Byte): `uint8_t mac[6]`, `uint16_t flags` (`NET_INFO_PRESENT = 1`, `NET_INFO_LINK_UP = 2`), `uint32_t ipv4`, `netmask`, `gateway`, `rx_frames`, `tx_frames`, `rx_dropped`.
- `sys_net_info` liefert `-EFAULT` fuer ungueltige Zeiger und `-ENODEV` ohne Netzwerkgeraet.
- `sys_net_ping` erwartet `1 <= timeout_ms <= 60000` (sonst `-EINVAL`). Broadcast-, Netz-, Multicast- und `0.0.0.0`-Ziele liefern `-EINVAL`, Loopback `-ENETUNREACH`, fehlendes Geraet `-ENODEV`, Link down `-ENETDOWN`, keine freien Ping-Slots `-EBUSY`. Ohne Antwort (inkl. unbeantwortetem ARP) innerhalb des Timeouts kommt `-ETIMEDOUT`. Die eigene Adresse antwortet sofort mit RTT 0.
- Der Aufrufer blockiert wie bei `sys_poll_wait`; der Scheduler pollt die NIC waehrenddessen. Die RTT-Aufloesung entspricht dem Timer-Tick (10 ms).
- Statische Konfiguration passend zu QEMU User-Networking: `10.0.2.15/24`, Gateway `10.0.2.2`.

### sys_open Flags
- `OPEN_READ (1<<0)`, `OPEN_WRITE (1<<1)`, `OPEN_CREATE (1<<2)`, `OPEN_TRUNCATE (1<<3)`, `OPEN_APPEND (1<<4)`.
- `flags = 0` bleibt aus Kompatibilitaet gleichbedeutend mit `OPEN_READ`.
- Mindestens `OPEN_READ` oder `OPEN_WRITE` muss gesetzt sein; `OPEN_CREATE`, `OPEN_TRUNCATE` und `OPEN_APPEND` verlangen `OPEN_WRITE`. Unbekannte Bits oder ungueltige Kombinationen liefern `-EINVAL`.
- Ohne `OPEN_CREATE` liefert eine fehlende Datei `-ENOENT`; scheitert Anlegen oder Kuerzen, kommt `-EIO`. Die Datei wird erst veraendert, wenn Handle-Slot und Kernel-Ressourcen gesichert sind.
- `sys_write` auf beschreibbare Dateihandles schreibt ab dem gemeinsamen Dateioffset (mit `OPEN_APPEND` immer am aktuellen Dateiende) und verlaengert die Datei bei Bedarf; Luecken lesen sich als Null-Bytes. Dateisystemfehler liefern `-EIO`.
- Die VFS-Backends ersetzen derzeit nur ganze Dateien; ein positioniertes Schreiben ist deshalb intern ein Read-Modify-Write der kompletten Datei.

### sys_spawn_args
- Wie `sys_spawn_with_stdio` (gleiche stdio-Steuerwerte `-1/-2/-3`), zusaetzlich mit NULL-terminierten `argv`- und `envp`-Vektoren; beide duerfen `NULL` sein.
- Der Kernel kopiert alle Strings und legt sie oben auf den initialen User-Stack des Childs. Beim Eintritt gilt `RDI = argc`, `RSI = argv`, `RDX = envp` (SysV-kompatibel, `crt0` reicht sie unveraendert an `main(int argc, char **argv, char **envp)` weiter). Prozesse ohne argv starten mit `argc = 0`, `argv = envp = NULL`.
- Grenzen: hoechstens 64 Eintraege (argv + envp zusammen) und 3072 Bytes Stringdaten inklusive Terminatoren, sonst `-E2BIG (7)`. Ungueltige Pointer liefern `-EFAULT`, nicht ladbare Programme `-ENOENT`, unzulaessige stdio-Bindings `-EBADF`.

### sys_try_wait
- Liefert die PID und schreibt den Exit-Status nach `*status` (optional), wenn der Child-Prozess bereits beendet war, und raeumt ihn dabei wie `sys_wait` ab.
- Liefert `0`, solange der Child-Prozess noch laeuft; unbekannte oder fremde PIDs liefern `-ENOENT`.

## UDP-Sockets (sys_socket / sys_bind / sys_sendto / sys_recvfrom)
- Ein Socket ist ein normaler Handle (`PROCESS_HANDLE_SOCKET`): `sys_close`, `sys_dup`/`dup2`/`dup3`, `sys_poll*` und Vererbung per `sys_handle_set_inherit` funktionieren. Duplikate teilen sich denselben Socket; Port und Empfangswarteschlange werden mit dem letzten `close` freigegeben (auch beim Prozess-Exit). `sys_read`/`sys_write` auf Sockets schlagen fehl.
- `struct net_sockaddr` (8 Byte): `uint32_t ipv4`, `uint16_t port`, `uint16_t reserved` (0), jeweils in Host-Byte-Order (`10.0.2.15` = `0x0A00020F`). `from` darf `NULL` sein.
- Der Kernel haelt hoechstens 16 Sockets; danach `-ENFILE`. `sys_socket` funktioniert auch ohne Netzwerkgeraet (Loopback braucht keines) und liefert `-EAFNOSUPPORT`, `-EPROTOTYPE`, `-EPROTONOSUPPORT` fuer andere Familien, Typen und Protokolle, `-EMFILE` bei vollem Handle-Table.
- `sys_bind`: nur lokale Adressen (`0`, `10.0.2.15`, `127.0.0.0/8`), sonst `-EADDRNOTAVAIL`; Port > 65535 oder bereits gebundener Socket `-EINVAL`; belegter Port `-EADDRINUSE` (ein Wildcard-Bind kollidiert mit jedem Bind auf demselben Port). Ephemere Ports kommen aus `49152..65535`; der Startpunkt der Suche wird pro Zuweisung aus dem TSC gestreut; bei Erschoepfung `-EADDRINUSE`.
- `sys_sendto` bindet einen ungebundenen Socket zuerst an einen ephemeren Port. Nutzlast bis 1472 Byte (sonst `-EMSGSIZE`), Ziel-Port `1..65535` und Unicast-Ziel (sonst `-EINVAL`; `0.0.0.0`, Broadcast und Multicast sind abgelehnt), Laenge 0 ist erlaubt. UDP-Pruefsumme mit IPv4-Pseudo-Header wird gesendet und beim Empfang geprueft (Pruefsumme 0 = nicht berechnet). Ziele auf dem eigenen Host (`10.0.2.15`, `127.0.0.0/8`) laufen ueber Loopback ohne NIC; der Empfaenger sieht als Absender die Zieladresse. Fuer Ziele ausserhalb parkt der Kernel das Datagramm bis zu 1 s, bis ARP den naechsten Hop aufgeloest hat; der Aufrufer schlaeft dabei im Scheduler (andere Prozesse laufen weiter) und bekommt danach `len` oder `-EHOSTUNREACH`. Hoechstens 4 Datagramme warten gleichzeitig, weitere liefern `-ENOMEM`. Link down liefert `-ENETDOWN`. Puffer und `from` muessen im Aufrufer-Adressraum gemappt und fuer User-Mode zugreifbar sein (bei `recvfrom` zusaetzlich beschreibbar), sonst `-EFAULT`; die Pruefung geschieht vor jeder Queue-Aenderung, Nicht-Socket-fd `-ENOTSOCK (88)`, ungueltiger fd `-EBADF`.
- `sys_recvfrom` liefert das aelteste Datagramm; ist es laenger als `len`, wird es abgeschnitten und der Rest verworfen. Bei leerer Warteschlange blockiert der Aufrufer ueber den Scheduler (Prozesszustand `socket-recv`), mit `NET_MSG_DONTWAIT` kommt `-EAGAIN`. Unbekannte Flags `-EINVAL`. Jeder Socket puffert hoechstens 8 Datagramme; weitere werden verworfen (Zaehler `rx_dropped`).
- `sys_poll`, `sys_poll_wait` und `sys_poll_many` melden `POLL_READ`, sobald ein Datagramm bereitliegt; `POLL_WRITE` ist immer gesetzt. Die NIC wird waehrenddessen vom Scheduler gepollt.
- Nicht implementiert: ICMP Port Unreachable, IP-Fragmentierung (Datagramme > 1472 Byte), Broadcast, `connect`/`getsockname`.
- Beispiel: `userspace/lib/dns.c` ist ein kleiner A-Record-Resolver (`dns_resolve_a`, Standardserver 10.0.2.3 von QEMU User-Networking); `nslookup <host> [server]` nutzt ihn.

## Error Codes (Subset)
Diese errno-Werte sind als Startmenge vorgesehen:
- `EPERM (1)`
- `ENOENT (2)`
- `ESRCH (3)`
- `EINTR (4)`
- `EIO (5)`
- `E2BIG (7)`
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
- `ENOTSOCK (88)`, `EMSGSIZE (90)`, `EPROTOTYPE (91)`, `EPROTONOSUPPORT (93)`, `EAFNOSUPPORT (97)`, `EADDRINUSE (98)`, `EADDRNOTAVAIL (99)`
- `ENODEV (19)`
- `ENETDOWN (100)`
- `ENETUNREACH (101)`
- `EHOSTUNREACH (113)`
- `ETIMEDOUT (110)`

## Future Extensions
- `sys_fork` / `sys_execve` (falls POSIX-Nahe gewuenscht)
- `sys_pipe`, `sys_shm_create`, `sys_shm_map`
- `sys_epoll`/`sys_poll`
- `sys_socket` (wenn Netzwerktrack startet)

## Signale (sys_kill / sys_sigaction / sys_sigreturn)
- Gueltige Signale: `SIGINT 2`, `SIGILL 4`, `SIGTRAP 5`, `SIGABRT 6`, `SIGBUS 7`, `SIGFPE 8`, `SIGKILL 9`, `SIGUSR1 10`, `SIGSEGV 11`, `SIGUSR2 12`, `SIGTERM 15`, `SIGCHLD 17`. Masken haben Bit N fuer Signal N. Die Definitionen stehen in `kernel/include/signal_abi.h` (Userspace-Spiegel: `userspace/lib/signal.h`).
- `struct sigaction` (24 Byte): `uint64_t handler` (`SIG_DFL = 0`, `SIG_IGN = 1` oder Handler-Adresse), `uint64_t restorer` (Adresse des Trampolins, Pflicht bei einem Handler), `uint32_t mask` (zusaetzlich blockierte Signale waehrend des Handlers), `uint32_t flags` (reserviert, muss 0 sein). Das Userspace-Trampolin `sigreturn_trampoline` ruft `sys_sigreturn`.
- Jeder Prozess hat eine Pending-Maske, eine Blocked-Maske und eine Handler-Tabelle; ein neuer Prozess startet mit Default-Aktionen und leeren Masken. `SIGKILL` kann weder gefangen, ignoriert noch blockiert werden. Synchrone Fault-Signale werden vor allen anderen anstehenden Signalen zugestellt, damit ihr Handler den fehlerhaften Kontext sieht. Der Bootstrap-Prozess ist vor `sys_kill` mit beendender Wirkung geschuetzt (`-EPERM`).
- Standardaktion ist Beenden, ausser `SIGCHLD` (ignoriert). Der Exit-Status eines durch ein Signal beendeten Prozesses ist `0x40000000 | signo` (`WIFSIGNALED`/`WTERMSIG` in `userspace/lib/signal.h`); `sys_wait` und `sys_try_wait` liefern ihn unveraendert. `sys_exit` behaelt nur das untere Byte seines Arguments, ein normaler Exit-Status kann daher nie als Signal-Tod erscheinen.
- Zustellung bei der Rueckkehr in den User-Mode (nach Syscall und Timer-Tick): Der Kernel legt unter dem User-Stack (ausserhalb der 128-Byte-Red-Zone, 16-Byte-ausgerichtet wie bei einem Funktionsaufruf) die Rueckkehradresse (`restorer`) und eine `hydrogen_sigcontext` mit dem gesicherten Zustand ab und springt in den Handler: `rdi = signo`, `rsi = Zeiger auf die Sigcontext`. Waehrend des Handlers sind das zugestellte Signal und `mask` blockiert; `sys_sigreturn` stellt Register, `rip`, `rsp`, `rflags` und die Blocked-Maske wieder her, auch `rax` (das Ergebnis des unterbrochenen Syscalls).
- `sys_sigreturn` prueft den Kontext: `cs`/`ss` muessen die User-Selektoren sein, `rip` und `rsp` im kanonischen User-Adressraum liegen; `rflags` wird auf die User-Bits (`CF PF AF ZF SF DF OF`) plus `IF` reduziert. Ein ungueltiger Kontext oder nicht beschreibbarer Handler-Stack beendet den Prozess mit `SIGSEGV`.
- Page Faults, #GP, #UD, #DE und weitere CPU-Ausnahmen im User-Mode loesen kein Kernel-Panic mehr aus, sondern `SIGSEGV` (`SIGILL`, `SIGFPE`, `SIGBUS`, `SIGTRAP`) im verursachenden Prozess. Diese Signale koennen nicht blockiert oder ignoriert werden; ohne Handler wird der Prozess beendet und der Kernel schreibt eine Zeile `signal: pid N (name) killed by SIGSEGV at rip=... addr=...` auf die serielle Konsole. Die Sigcontext enthaelt `fault_addr` (CR2), `trap` (Vektor) und `error_code`. Faellt der Bootstrap-Prozess so aus und kann nichts anderes laufen, kehrt der Kernel wie bei `sys_exit` in seine Shell zurueck. Kernel-Mode-Ausnahmen loesen weiterhin Panic aus. Zeiger, die ein Syscall liest oder beschreibt, werden gegen die Seitentabellen des Prozesses geprueft (`-EFAULT` bei nicht gemappten, nicht User-zugaenglichen oder fuer Schreibzugriffe nicht beschreibbaren Seiten).
- Blockierende Syscalls (`sys_wait`, `sys_sleep`, `sys_read`/`sys_write` auf Pipes, `sys_poll_wait`, `sys_poll_many`, `sys_net_ping`) werden von einem zustellbaren Signal abgebrochen und liefern `-EINTR` (4), nachdem der Handler gelaufen ist. Ein bereits abgeschlossenes `sys_wait` wird nicht abgebrochen. Die Userspace-Funktion `wait_pid` startet bei `-EINTR` neu.
- Ein beendeter Prozess sendet `SIGCHLD` an seinen Parent.
- Ctrl+C: Der Kernel verfolgt den Besitzer der Konsolen-Eingabe (der letzte Leser, der kein Nachkomme des bisherigen Besitzers ist). Blockiert der Besitzer in `sys_wait`, erhaelt die Kette der Prozesse, auf die er (transitiv) wartet, `SIGINT`, und das Zeichen wird verworfen; sonst kommt `0x03` als normale Eingabe an (die Shell bricht damit die Zeile ab).
- Noch nicht umgesetzt: Stop/Continue, Prozessgruppen, ein Syscall zum direkten Setzen der Blocked-Maske (sie wird nur durch Handler-Ausfuehrung und den Kontext bei `sys_sigreturn` veraendert). Ctrl+C wirkt fuer PS/2- und virtio-Tastaturen, nicht fuer serielle Eingabe.
