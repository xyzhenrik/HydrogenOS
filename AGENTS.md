# HydrogenOS – Codex Arbeitskontext

codex resume 019dee4a-6f0a-7e00-8cf0-89bad2d52191

## Projektkontext

HydrogenOS ist ein eigenständiges Desktop-Betriebssystem.

- Kein Linux
- Kein BSD
- Kein Fork

Es wird von Grund auf in C und x86_64-Assembly entwickelt und zielt auf produktionsreife Systemsoftware mit eigener Desktop-Erfahrung, .exe-Kompatibilität und langfristiger Erweiterbarkeit.

Primäre aktuelle Baustellen:

- Virtio-Treiber (`blk`, `gpu`, `input`)
- Ausbau von `init`
- Grafikstack

## Technische Leitplanken

- Keine Linux-Kernel-APIs, keine glibc-Annahmen, kein POSIX als Zielmodell.
- Vorschlaege, Abstraktionen und Implementierungen muessen auf die Eigenheiten von HydrogenOS zugeschnitten sein.
- Kernel-Pfade muessen auf Korrektheit ausgelegt sein; Race Conditions, Use-after-free und Nullpointer-Dereferenzen sind inakzeptabel.
- Fehler werden propagiert und geprueft. `panic`/`assert` nur fuer Invarianten, nicht fuer normale Laufzeitfehler.
- Modulschnittstellen klar halten. Interna bleiben intern. Kein globaler Zustand ohne guten Grund.

## Code-Standards

- Kein Wegwerf-Code. Aenderungen muessen das Niveau produktiver Systemsoftware halten.
- Rueckgabewerte immer pruefen; keine still ignorierten Fehler.
- Benennung:
  - Funktionen und Variablen: `snake_case`
  - Typen und Structs: `UpperCamelCase`
  - Makros und Konstanten: `UPPER_CASE`
- Kein auskommentierter toter Code, keine ungenutzten Variablen, keine Debug-`printf`s im Kernel.
- Abhaengigkeiten minimal halten. Keine Fremdbibliotheken im Kernel.
- Kommentare sparsam einsetzen und nur fuer nicht offensichtliches Warum.

## Arbeitsweise

- Bei nicht-trivialen Aenderungen zuerst Struktur klaeren: betroffene Dateien, Schnittstellen, Abhaengigkeiten, Risiken.
- Vor Implementierungen den vorhandenen Code und relevante Doku lesen, statt Linux- oder Standard-OS-Verhalten zu unterstellen.
- Subagent-Hierarchie aktiv nutzen, wenn eine Aufgabe sinnvoll zerlegbar ist.
- Der hauptverantwortliche Agent bleibt fuer Architektur, Priorisierung, Synthese und Endabnahme zustaendig.
- Recherche, eng abgegrenzte Implementierungen und Verifikation koennen an Subagents delegiert werden.
- Subagents duerfen weitere Subagents nur fuer klar abgegrenzte Unteraufgaben einsetzen, wenn dadurch Kontext sauber getrennt und Parallelitaet sinnvoll erhoeht wird.
- Delegation muss entlang klarer Verantwortungen erfolgen: jeder Agent braucht einen konkreten Auftrag, bekannte Dateibereiche oder Schnittstellen und ein eindeutiges erwartetes Ergebnis.
- Parallelisierung bevorzugen, wenn Teilaufgaben unabhaengig sind und getrennte Write-Sets haben.
- Blockierende Kernentscheidungen nicht blind delegieren; diese bleiben beim hauptverantwortlichen Agent.
- Ergebnisse von Subagents werden nicht ungeprueft uebernommen. Integration, Konfliktaufloesung und Konsistenzpruefung liegen beim hauptverantwortlichen Agent.
- Commits nur auf explizite Anfrage des Nutzers.
- Hooks oder Verifikation nicht umgehen.

## Verifikation

Nach Codeaenderungen nach Moeglichkeit die passenden Checks ausfuehren:

```bash
make ARCH=x86_64 build
tools/boot_smoke.sh
```

Wenn ein Task nur Teilbereiche betrifft, mindestens die engsten sinnvollen Checks ausfuehren und verbleibende Luecken klar benennen.

## Wichtige Referenzen

- `docs/STATUS.md`
- `docs/SYSCALL_ABI.md`
- `docs/TOOLCHAIN.md`
- `docs/X86_64_MIGRATION.md`
- `plan.md`

## UI-Hinweis

Wenn spaeter UI- oder Grafikcode entsteht, auf Klarheit, Konsistenz, Tiefe und Reaktionsfreude achten. Entscheidungen sollen systematisch wirken, nicht zusammengewuerfelt.
