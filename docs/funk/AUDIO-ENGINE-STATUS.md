> **Historischer Prüfbericht der Audio-Integration.** Die damaligen Portwerte
> bleiben unten als Nachweis erhalten. Der gesonderte spätere
> [Docker-Portstandard und Bestandsschutz](docker.md#9-aktualisieren) betrifft
> `feature/docker-port-default` (Ziel: `dev`), nicht den damaligen Prüfstand.

# Audio-Engine Phase 1: Integrations- und Prüfbericht

Stand: 19. Oktober 2026. **Experimentell**, kein neuer Release und keine
Produktionsfreigabe. Alle Änderungen gehören ausschließlich zu
`feature/audio-engine`; kein automatischer Merge nach `dev` oder `stable`.

## Herkunft und Integration

| Ausgangspunkt | Vollständiger Commit |
| --- | --- |
| Aktuelles `origin/dev`, zugleich Stable-Kandidatenbasis | `510f90618212511c8bac2c0c4732dca317d1d205` |
| Historische funktionierende Audio-Preview | `8b295e0cd889933951f7c2e941ad914a5a7c5269` |

Die vier Dateien `AudioFeedback.cpp/.h` und `AudioSignal.cpp/.h` sind bytegleich
zur bisherigen Audio-Implementierung. Übernommen wurden private Busy-Zustellung,
synthetischer Doppelton, PCM-WAV-Import, Lautstärke 0–100 %, begrenzte Queues,
Wiederholungsschutz, Session-/Kanalprüfung, Tests und die benötigte Opus-Anbindung.
Kein neuer Ton, keine zusätzlichen Funk-Effekte oder WAV-Assets.

Der SimplexController erhält nur das bisherige Abweisungssignal und dessen
Einmal-pro-Versuch-Merker. Vergabe, Sperre, Freigabe, Timeout und Retry-Regeln
bleiben bestehen. Die bestehenden Simplex-Tests wurden unverändert übernommen.
Die gemeinsame Testhilfe bekommt nur einen optionalen Zusatzkonfigurationsparameter.

Die dreiseitige Integration hatte Konflikte in `Server.cpp` und `Harness.h`:
inhaltlich gleichwertige Kommentare rund um bereits vorhandene UDP-Korrekturen.
Die aktuelle `dev`-Fassung wurde erhalten. Decoder-Version nach Authentifizierung,
Authentifizierung vor öffentlicher Ping-Klassifikation und der threadlokale
Sendepuffer bleiben einschließlich sämtlicher Regressionstests bestehen.
Kein alter Docker-Nonce-Adapter wurde wieder eingeführt.

Ubuntu-/Docker-Installationswege bleiben getrennt kompatibel. Dockerfile ergänzt
`libopus-dev` beim Bauen und `libopus0` zur Laufzeit; CI und natives Setup erhalten
die Build-Abhängigkeit. Compose, Laufzeitverwaltung, `.env.example`, Ports und
Volume-Namen sind unverändert. `.env.example` und Compose-Fallback behalten
vorerst 64739, obwohl die verbindliche bestehende Docker-Zuordnung
Host **64740 TCP/UDP** → Container **64739 TCP/UDP** lautet. Bestehende `.env`
mit `FUNK_HOST_PORT=64740` und individuelle Host-Ports werden nicht überschrieben.
Audio benötigt keine weiteren Ports. Die CI prüft ausdrücklich Host-Port 64740. Die native manuelle Installation verwendet nun
ebenfalls die bestehende Quellstandprüfung: Ein experimenteller Branch darf den
bisherigen `mumble-funk`-Dienst nicht ersetzen.

Die neue Preview trägt bewusst **eigene** Namen/Pfade `mumble-funk-audio-engine`
und Port **64741**. Sie lässt Ports 64738–64740 sowie den alten Dienst und dessen
Verwaltungsbefehle unangetastet. Ein Worktree-tauglicher Git-Lock ersetzt den alten
festen `.git/`-Verzeichnispfad. Audio bleibt bei Neuinstallation standardmäßig aus.
Alte Preview-Dokumente bleiben als historische Nachweise mit deutlichem Hinweis
erhalten; aktuelle Bedienung steht in [audio-engine.md](audio-engine.md).

## Nachgewiesene Prüfungen

| Prüfung dieses Integrationsstands | Ergebnis |
| --- | --- |
| Nativer Server-only-Build, GCC 14 / Qt 5.15 / Opus 1.5.2 | Erfolgreich |
| Vollständige CTest-Suite | **20/20** Testprogramme, keine ausgelassen |
| Bestehende Quellstand-/Tag-/Worktree-Prüfungen | **14/14** |
| Bestehende native Setup-Orchestrierung | **16/16** |
| Native Installation/Datenerhalt/OS-Prüfung | **5/5** |
| WAV-Validierung und Audio-Verwaltung | **8/8** |
| Separate Preview, Datenerhalt und vier parallele reale Serverprozesse | **5/5** |
| Docker-Laufzeit-Hilfsprogramme | **11/11** |
| Ubuntu-24.04-Docker-Build inklusive CTest und Docker-Helfern | Erfolgreich, **20/20 + 11/11** |
| Vollständige reale Compose-Integration, Busy Tone und Sprachrouting | Erfolgreich |
| Update vom vorherigen Stable-Image und tatsächlicher Rollback | Erfolgreich; vollständiger SQLite-Inhalt, Passwort, Konfiguration und Zertifikat erhalten |
| Explizite Veröffentlichung Host **64740 TCP/UDP** → Container **64739 TCP/UDP** | Erfolgreich; TCP/verschlüsseltes UDP und privater Busy Tone |
| Compose-Konfiguration aus `.env`, eigener Host-Port und unveränderter Fallback | Erfolgreich: 64740, 65001 und 64739; jeweils genau zwei Ports, Datei unverändert |
| ShellCheck und Git-Diff-Prüfung | Erfolgreich |

Die beiden Audio-Testprogramme prüfen Opus-dekodierbaren Fallback, Lautstärke,
WAV und Fallback bei ungültiger Datei, private Zustellung für Legacy-/Protobuf-
Framing über TCP und verschlüsselten UDP-Eingang, kein Kanal-Leak, kein
automatisches Übernehmen, Wiederholungsschutz und begrenzte Ressourcen.
Kanalwechsel, Abbruch und tatsächliche Wiederverwendung einer Session werden
ebenfalls geprüft. Die komplette bisherige UDP-Regressionssuite bleibt aktiv. Die separate Docker-
Audiofixture verlängert nur für die Dauer des Hörpaket-Probes ihre Leases auf 5 s,
damit dessen absichtliches Warten keinen Timeout simuliert. Danach stellt sie die
Originalkonfiguration bytegenau wieder her. Der normale Docker-Test prüft weiter
500/500 ms als Neuinstallationswerte; echte Server-Defaults bleiben unverändert.

Installationstests verwenden ausschließlich temporäre Wurzeln. Sie bewahren
Sentinel-Dateien der Standard-, Simplex- **und alten Audio-Preview-Instanz** samt
Konfiguration, Datenbank und Dienstdateien. Vier reale Serverprozesse verwenden
eigene temporäre Ports und Zustandsverzeichnisse. Ein erfolgreicher solcher Test
ist ausdrücklich kein systemd-Boot-Test und kein Hörtest mit echten Geräten.

## Ressourcen, offene Abnahme und Phase 2

Ein zusätzlicher Ubuntu-26.04-Containercheck wurde wegen knappen gemeinsamen
Cloud-Speichers während des Docker-Builds kontrolliert angehalten. Der Docker-Build
selbst und seine Tests wurden erfolgreich abgeschlossen; nach Entfernung ausschließlich
eigener temporärer Build-Cache-Einträge bestanden auch alle Compose-Tests. Das ist kein
erfolgreicher Ubuntu-26.04-Build dieser Revision. Vorherige Ergebnisse anderer
Branches ersetzen diese Prüfung nicht. Die OS-Prüfung für 24.04/26.04 bleibt.
Die konservative 8-GiB-Setup-Vorprüfung wird weder abgeschaltet noch simuliert.

Noch offen: vollständige neue Ubuntu-VM-Installation einschließlich echtem
systemd-Neustart/Boot, der zusätzliche Ubuntu-26.04-Build, erneuter Hörtest dieses
Branches mit Windows-Mumble/Mumla, reale Paketverluste, Langzeit-/Lastbetrieb und
weitere Architekturen. Die vom Projektinhaber gemeldete hörbare alte Preview ist
ein Praxisergebnis des alten Branches und wird nicht für diesen Stand ausgegeben.

Für Phase 2 sind Signal-ID, PCM-Lader/Encoder, vorbereitete Clips und Zustellung
vorhandene Erweiterungspunkte. Persönliche WAV-Dateien und ihre ausdrücklichen
Lizenzhinweise werden erst dann eingebunden. Keine Platzhalter als finale Sounds.
**Phase 1 ist als experimentelle Entwicklungsgrundlage für Phase 2 geeignet.**
Die aufgeführten offenen VM-/Geräteprüfungen bleiben vor einer Freigabe nötig;
die Integration ist keine Produktionszusage. Es wurden keine neuen Effekte vorweggenommen.

## Lizenz und Schutz

Keine fremden Audioressourcen, keine heruntergeladenen Samples, keine kopierten
Hersteller-Signaltöne und kein neuer fremder Quellbaum. Der unveränderte Fallback
ist mathematisch aus Sinuswellen erzeugt. Die übernommene zusätzliche dynamische
Server-Abhängigkeit **Opus** ist Drittanbieter-Software; ihre BSD-Hinweise sind
mitgeliefert und in die bestehende Lizenzanzeige eingebunden.
[Herkunfts-/Lizenzinventar](AUDIO-RESOURCE-LICENSES.md).

Keine VM oder laufende Installation wird durch diesen Branch aktualisiert.
Bestehende Tags sowie `stable`, `dev`, `master`, `funk-v0.1`, beide historischen
Feature-Branches und Weblate dürfen nicht verändert werden. Die veröffentlichten
Commit-/PR-Nachweise und der abschließende Referenzvergleich werden im
Abschlussbericht verlinkt. Git-Autor und Committer entsprechen `AGENTS.md`.
