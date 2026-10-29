# System-Log (syslogd)

`syslogd` ist der Log-Server des Systems. `init` startet ihn als erstes Service
(`service syslogd daemon /syslogd 100 log=server`).

## init.rc-Optionen
- `log=server` markiert den Log-Server. Er muss ein `daemon` mit unveraendertem stdio sein, es darf nur einen geben, und er muss vor allen Log-Nutzern in der Datei stehen.
- `stdout=syslog`, `stderr=syslog`, `log=syslog` (beide Streams) binden den Strom eines Services an eine eigene Pipe zum Log-Server. `init` gibt `syslogd` beim Start das Leseende jeder Pipe als vererbtes Handle zusammen mit dem Servicenamen (`<service>:<fd>` als argv). Die Schreibenden bleiben in `init`, bis der Service beendet ist, damit `syslogd` pro Quelle EOF sieht.
- Der Modus `syslog` ist unabhaengig vom `logsink`-Mechanismus (der Name `syslog` eines `logsink` ist nur ein Label).

## Format und Rotation
- Eine Zeile pro Eintrag: `YYYY-MM-DD HH:MM:SS <service>: <text>` (UTC aus `SYS_RTC_TIME`). Ohne lesbare RTC steht `[ uptime.mmm]` an der Stelle des Datums.
- Zeilen werden bei 256 Bytes umgebrochen, Steuerzeichen (ausser Tab) durch `?` ersetzt, leere Zeilen verworfen.
- Ausgabe nach `/var/log/system.log` (angelegt, falls noetig) und auf die Konsole/serielle Schnittstelle.
- Ueberschreitet die Datei 64 KiB, wird sie nach `/var/log/system.log.1` rotiert (eine Generation).
- `syslogd` beendet sich, sobald alle Quellen EOF gemeldet haben.

## Werkzeuge
- `logread` gibt `system.log.1` und `system.log` aus, `logread -c` nur die aktuelle Datei.
- `logtest` ist der Boot-Test (`SYSLOG PASS`).

## Verzeichnislayout des Disk-Images
`/bin`, `/etc`, `/tmp`, `/home` und `/var/log` werden beim Bau des Images angelegt. Die Programme liegen weiterhin im Wurzelverzeichnis (die Shell sucht standardmaessig nur in `/`).
