# Settings-App und `/etc/settings.conf`

Die Settings-App ist eine App-Art der Desktop-Session (`userspace/desktopdemo`), erreichbar ueber Taskbar-Icon und Startmenue ("About HydrogenOS" oeffnet direkt die Seite About). Sie besteht aus drei Bibliotheksmodulen in `userspace/lib`:

| Modul | Inhalt |
| --- | --- |
| `settings.{h,c}` | Modell (`Settings`), Parser/Serializer, Laden/Speichern, Akzentfarben, Farbpalette (`SettingsPalette`) |
| `settings_info.{h,c}` | Systemwerte (`SYS_FB_INFO`, `SYS_NET_INFO`, `SYS_SYSINFO`, `/proc/meminfo`, `/proc/uptime`, `/proc/version`), Parser und Formatierer, Gateway-Ping |
| `settings_ui.{h,c}` | Sidebar + Karten-Layout, Zeichnen, Hit-Tests, Hover, Klick-Ereignisse |

## Dateiformat

Zeilen `schluessel=wert`; Leerzeilen und `#`-Kommentare werden uebersprungen, Gross-/Kleinschreibung und Leerzeichen um `=` sind egal.

```
# HydrogenOS desktop settings
theme=dark
accent=blue
glass=on
layout=de
```

| Schluessel | Werte | Wirkung |
| --- | --- | --- |
| `theme` | `dark`, `light` | Wallpaper, Settings-Flaechen, Akzentton |
| `accent` | `blue`, `purple`, `pink`, `red`, `orange`, `green`, `graphite` | Akzentfarbe (Auswahl, Schalter, Taskbar-Unterstrich) |
| `glass` | `on`, `off` | Taskbar und Menue als Glas bzw. deckend |
| `layout` | `de`, `us` | Tastaturlayout (`SYS_SET_LAYOUT`) |

Unbekannte Schluessel und ungueltige Werte werden ignoriert und lassen das Feld unveraendert; eine beschaedigte Datei kann also kein ungueltiges Modell erzeugen. Fehlt die Datei, gelten die Defaults (`dark`, `blue`, `on`, `de`).

## Ablauf

- Beim Start von `desktopdemo` wird die Datei geladen, das Layout per `SYS_SET_LAYOUT` gesetzt und die Palette abgeleitet.
- Jede Aenderung in der App wirkt sofort auf die ganze Session (Live-Vorschau: die Themenkacheln zeigen den aktuellen Akzent) und wird mit `SYS_WRITEF` gespeichert. Schlaegt das Speichern fehl, zeigt die App "Settings could not be saved."
- Der Gateway-Ping laeuft mit 1 s Timeout; "Pinging ..." wird vor dem blockierenden Syscall gezeichnet.
- `desktoptest` prueft Parser, Serializer, Persistenz, Palette (Theme-Wechsel), Formatierer, Layout-Geometrie, Klick-/Hover-Logik und das Zeichnen deterministisch.
