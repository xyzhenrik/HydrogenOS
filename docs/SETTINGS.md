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
reduce_motion=off
```

| Schluessel | Werte | Wirkung |
| --- | --- | --- |
| `theme` | `dark`, `light` | Wallpaper, Settings-Flaechen, Akzentton |
| `accent` | `blue`, `purple`, `pink`, `red`, `orange`, `green`, `graphite` | Akzentfarbe (Auswahl, Schalter, Taskbar-Unterstrich) |
| `glass` | `on`, `off` | Taskbar und Menue als Glas bzw. deckend |
| `layout` | `de`, `us` | Tastaturlayout (`SYS_SET_LAYOUT`) |
| `reduce_motion` | `on`, `off` | Schalter "Reduce motion" (Appearance): Fenster, Launcher und Dock aendern sich ohne Animation |

Unbekannte Schluessel und ungueltige Werte werden ignoriert und lassen das Feld unveraendert; eine beschaedigte Datei kann also kein ungueltiges Modell erzeugen. Fehlt die Datei, gelten die Defaults (`dark`, `blue`, `on`, `de`, `off`).

## Ablauf

- Beim Start von `desktopdemo` wird die Datei geladen, das Layout per `SYS_SET_LAYOUT` gesetzt und die Palette abgeleitet.
- Jede Aenderung in der App wirkt sofort auf die ganze Session (Live-Vorschau: die Themenkacheln zeigen den aktuellen Akzent) und wird mit `SYS_WRITEF` gespeichert. Schlaegt das Speichern fehl, zeigt die App "Settings could not be saved."
- Der Gateway-Ping laeuft mit 1 s Timeout; "Pinging ..." wird vor dem blockierenden Syscall gezeichnet.
- `desktoptest` prueft Parser, Serializer, Persistenz, Palette (Theme-Wechsel), Formatierer, Layout-Geometrie, Klick-/Hover-Logik und das Zeichnen deterministisch.

## Animationen

Die Desktop-Session animiert mit einer kleinen Engine in `userspace/lib` (`anim.{h,c}`, `window_motion.{h,c}`):

- `anim`: kritisch gedaempfte Federn in 16.16-Festkomma (kein FPU-Einsatz), mit festen 4-ms-Teilschritten vom Frame-Takt getrieben. Eine langsame Frame-Zeit bewegt die Animation weiter statt sie zu dehnen. Ziele koennen jederzeit neu gesetzt werden; Position und Geschwindigkeit bleiben erhalten.
- `window_motion`: Bewegung eines Fensters zwischen zwei Posen (Rechteck + Deckkraft). Oeffnen/Schliessen (Skalierung 0,92 + Fade), Minimieren/Wiederherstellen (zum bzw. vom Taskbar-Icon), Maximieren/Wiederherstellen (Geometrie-Interpolation) und das Ein-/Ausblenden des Launchers sind dieselbe Bewegung mit anderen Endposen. Ein unterbrochener Ablauf startet bei der aktuellen Pose.
- Der Compositor stellt dafuer `comp_set_window_pose()` bereit: das Fenster wird bilinear skaliert und mit Deckkraft samt Schatten in das Pose-Rechteck gezeichnet; Geometrie, Surface und Hit-Tests bleiben unveraendert. Nur beschaedigte Bereiche werden neu komponiert, `desktoptest` prueft Frame fuer Frame, dass keine Restpixel bleiben.
- Das Dock animiert Hover-Hervorhebung und Laufindikator mit Federn.

"Reduce motion" setzt alle Federn sofort auf ihr Ziel und startet keine Fensterbewegung; laufende Animationen landen beim Umschalten sofort am Ziel.
