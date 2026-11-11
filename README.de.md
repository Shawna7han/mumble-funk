# Mumble-Funk

**Serverseitige Simplex-Funkkommunikation für Mumble**

Mumble-Funk ist ein unabhängiger Open-Source-Fork von **Mumble Server 1.5.939**.
Er bildet Funkkommunikation mit einem Sender pro Kanal und optionalen,
serverseitigen Audioeffekten nach. **Die Mumble-Clients bleiben unverändert.**
Für die Praxistests werden Windows-Mumble und Android-Mumla verwendet.

**Diese README beschreibt `dev`: experimentelle Entwicklung, keine Stable-Freigabe.**
[English](README.md) · [Dev installieren](docs/funk/install-dev.md) ·
[Docker](docs/funk/docker.md) · [Branches und Nachweise](docs/funk/development.md)

## Funktionen

| Funktion in `dev` | Verhalten |
| --- | --- |
| Simplex-Engine v0.2 | Ein angenommener Sender pro Kanal; UDP und TCP teilen sich dieselbe Kanalbelegung. |
| Gleichzeitige Sendeversuche | Abgewiesene Sender übernehmen den frei werdenden Kanal nicht automatisch. Ein neuer Versuch ist erforderlich. |
| Busy Tone | Privater Hinweis für einen abgewiesenen Sender; hat Vorrang vor den anderen Effekten. |
| Squelch Open / Close | Optionale Töne für berechtigte Empfänger am Beginn und Ende einer angenommenen Übertragung. |
| Timeout-Close | Optionaler Close bei Freigabe durch Paketruhe ohne bestätigte Ende-Markierung. Ein Timeout beweist kein Loslassen der PTT-Taste. |
| Kontinuierliches Funkrauschen | Eigenständiger Effekt parallel zur unveränderten Sprache. Open → Rauschen → Close bilden einen privaten Effektstream mit fortlaufenden Sequenznummern. |
| Audioeinstellungen | Separate Lautstärken, Open-/Close-Schalter und 0–12 dB Boost für Busy/Open/Close. Rauschen hat eine eigene Lautstärke und wird nicht mitgeboostet. |
| Mitgelieferte und eigene Sounds | Vier CC0-WAV-Aufnahmen; eigene WAVs ohne Neukompilierung austauschbar. Synthetischer Busy bleibt als Fallback erhalten. |
| Verwaltung und Diagnose | Audioverwaltung, nur lesender JSON-Status sowie optionale Sprachpaket- und Simplex-Diagnose; keine Aufzeichnung von Sprachinhalten. |
| Installation und Wiederherstellung | Nativer Ubuntu-systemd-Dienst und Docker Compose; Sicherungs-, Restore- und Rollback-Verfahren für Konfiguration und Daten. |

Beispiel: Anna beginnt zu senden. Die Empfänger hören Open und Rauschen parallel
zu ihrer Sprache. Bens gleichzeitige Aussendung wird abgewiesen; nur Ben hört Busy.
Annas bestätigtes Ende oder ein Timeout durch Paketruhe geben den Kanal frei.
Ben muss seine Aussendung beenden und neu versuchen; er wird nicht automatisch
zum Sender.

## Aktueller Entwicklungsstand

| Quelle | Umfang |
| --- | --- |
| [`stable`](https://github.com/Shawna7han/mumble-funk/tree/stable) | Bisheriger gemeinsamer Ubuntu-/Docker-Simplex-Installationskandidat **ohne die neue Audio-Engine**. Weiterhin GitHub-Standardbranch. |
| [`dev`](https://github.com/Shawna7han/mumble-funk/tree/dev) | Integrierte Audio-Engine, Squelch, Boost, mitgelieferte Sounds, Audio-Defaults, Funkrauschen und experimenteller Dev-Installer. |

PR #9 wurde mit
[`a6b63175b7e94e503c410706dba87e23d861f46e`](https://github.com/Shawna7han/mumble-funk/commit/a6b63175b7e94e503c410706dba87e23d861f46e)
nach `dev` gemergt. Für Testberichte die vollständige Git-Revision verwenden:
„Mumble 1.5.939“ allein bezeichnet keine eindeutige Funkversion. Ubuntu und Docker
verwenden denselben Quellstand, wenn sie aus demselben Commit gebaut werden.
Die [vollständige Branch-Übersicht](docs/funk/development.md) beschreibt die Herkunft.

| Nachweis | Aussage |
| --- | --- |
| Implementiert | Die oben aufgeführten Funktionen sind im integrierten Dev-Code vorhanden. |
| Automatisiert vor dem Merge von PR #9 geprüft | 33/33 Server-CTests nativ und im Docker-Build, 131 Python-Prüfungen sowie echte temporäre Compose-Tests einschließlich TCP/verschlüsseltem UDP, Simplex, Effektdekodierung, Backup/Restore und Image-Update/Rollback. |
| Vom Betreiber gemeldete Gerätetests | Erfolgreiche native Ubuntu-Installation und Hörtests mit Windows-Mumble/Android-Mumla. Der dokumentierte Test von `6f976a2` bestätigte kontinuierliches Rauschen, behobene Aussetzer und Close auf Mumla beim Windows-Senden. |
| Noch offen | Breitere Geräte-/Netzabdeckung, separater Docker-Hörtest, Start-/Wiederherstellungsprüfung auf dem Zielhost und abgeschlossener Langzeit-/Lasttest. |

Das sind dokumentierte Ergebnisse, **kein laufendes CI-Badge und keine Zusage von
Produktionsreife**. Für diese Dokumentationsänderung ist kein Server-Neubau nötig.
Details und Grenzen: [Entwicklung und Prüfstand](docs/funk/development.md#tests-und-praxiserfahrung).

## Installation

Für den **aktuellen experimentellen Dev-Stand** eine eigene Ubuntu-Server-
**24.04- oder 26.04-LTS-Test-VM** verwenden. Als normaler sudo-Benutzer in einem
frischen Quellverzeichnis beginnen:

```bash
git clone --branch dev --single-branch --recurse-submodules https://github.com/Shawna7han/mumble-funk.git
cd mumble-funk
bash scripts/funk/setup.sh --experimental-dev
```

Die [Dev-Anleitung](docs/funk/install-dev.md) erklärt Voraussetzungen, Herkunftsprüfung,
Updates und Verbindung. Es wird der normale Dienst `mumble-funk` eingerichtet,
kein zusätzlicher Preview-Dienst. Bei vorhandener Installation wird dieser Dienst
aktualisiert; die Test-VM deshalb bewusst auswählen.

Für **Docker aus Dev** gilt die [Docker-Anleitung](docs/funk/docker.md). Sie baut
lokal aus dem Dev-Checkout, ohne zusätzliche Audioports oder automatisches
Registry-Deployment. Für **Stable ohne Dev-Audio** gilt die
[Stable-Setup-Anleitung](https://github.com/Shawna7han/mumble-funk/blob/stable/docs/funk/setup.md).
Dev-Audioanweisungen nicht auf einen historischen Stable-Checkout übertragen.

| Aktuelle Dev-Installation | Standardports, TCP und UDP |
| --- | --- |
| Ubuntu nativ | **64739** |
| Docker | **Host 64740 → Container 64739** |

Eigene Host-Ports bleiben erlaubt. Updates erhalten vorhandene `.env`, Projektnamen
und Volumes. Historische Stable-Compose-Vorgaben unterscheiden sich; bei einer
Migration den tatsächlich verwendeten Port erhalten. Siehe
[Bestandsschutz beim Update](docs/funk/docker.md#9-aktualisieren).

**Vorbereiteter Weg mit fertigem Image:** [GHCR-Dev-Anleitung](docs/funk/docker-image.md).
Die Veröffentlichung braucht eine gesonderte Freigabe; dieser Entwicklungsauftrag
hat noch kein Image veröffentlicht. Der bisherige lokale Build bleibt erhalten.

## Audio-Standardeinstellungen

**Nur neu erzeugte INIs über Ubuntu-Setup und Docker** aktivieren diese Funktionen:

| Einstellung | Wert bei Neuinstallation |
| --- | --- |
| `simplex`, `audiofeedback`, `busytone` | `true` |
| `squelch`, `squelchopen`, `squelchclose` | `true` |
| `squelchclosetimeoutfallback`, `radionoise` | `true` |
| `busytonevolume`, `squelchopenvolume`, `squelchclosevolume` | `50` |
| `feedbackboostdb` | `8` — nur Busy/Open/Close |
| `radionoisevolume` | `20` |

Die mitgelieferten WAVs werden aus `/usr/local/share/mumble-funk/audio/` ausgewählt:
`busy-tone.wav`, `squelch-open.wav`, `squelch-close.wav` und `radio-noise.wav`.
Open, Rauschen und Close folgen im Effektstream aufeinander. Sprache wird getrennt
weitergeleitet, ohne sie zu dekodieren, zu mischen oder neu zu kodieren.

**Updates migrieren vorhandene INIs nicht**, auch bei fehlenden Einträgen,
ausgeschalteten Effekten, Lautstärke 0 oder eigenen WAVs. Fehlende alte Rauschwerte
bleiben `false` / leerer Pfad / Lautstärke `20`; ein fehlender Timeout-Fallback
bleibt `false`. [Defaults und Bestandsschutz](docs/funk/audio-defaults.md) ·
[Audio-Lizenzen](assets/audio/LICENSE.md)

## Verwaltung

Unter Ubuntu nativ:

```bash
sudo mumble-funk-audio status
sudo mumble-funk-info --hide-password
```

Der Audio-Status ist nur lesendes JSON einschließlich der gespeicherten Rauschwerte.
Er startet keinen Dienst neu und bestätigt keinen erfolgreichen WAV-Ladevorgang.
`mumble-funk-info` **ohne** `--hide-password` zeigt das Zugangspasswort: Diese Ausgabe
privat halten. Ändernde Audiobefehle können den Dienst neu starten. Docker nutzt
`bash scripts/docker/audio.sh status` und die vorhandenen Compose-Verwaltungsskripte.
Siehe [Betreiberbefehle](docs/funk/ubuntu-commands.md) und [Docker-Verwaltung](docs/funk/docker.md).

## Dokumentation

Die Bedienungsanleitungen sind auf Deutsch. Historische Phasenberichte bleiben erhalten.

| Thema | Anleitung |
| --- | --- |
| Entwicklung, alle Branches und Integrationsnachweise | [Entwicklung](docs/funk/development.md) |
| Aktuelle Dev-Installation und Updates | [Dev unter Ubuntu](docs/funk/install-dev.md) |
| Technische Stable-Ubuntu-Installation | [Ubuntu-Anleitung](docs/funk/installation-ubuntu.md) |
| Docker, persistente Daten, Backup und Restore | [Docker](docs/funk/docker.md) |
| Simplex, PTT und Diagnose | [Simplex](docs/funk/simplex.md) · [Diagnose](docs/funk/diagnose.md) |
| Busy und synthetischer Fallback | [Busy Tone](docs/funk/busy-tone.md) |
| Squelch-Schalter, WAVs und Timeout-Verhalten | [Squelch](docs/funk/squelch-preview.md) |
| Gemeinsamer Effektstream und kontinuierliches Rauschen | [Funkrauschen](docs/funk/radio-noise.md) |
| Status, WAV-Auswahl und Lautstärken | [Audioverwaltung](docs/funk/ubuntu-commands.md) |
| Rechte und Herkunft der Aufnahmen | [CC0-Assets](assets/audio/LICENSE.md) |

## Bekannte Einschränkungen

- Physische PTT-Tasten sind für den Server nicht sichtbar. Paketruhe kann auch
  Netzunterbrechung oder Sprechpause bedeuten; Timeout-Close bleibt eine Vermutung.
- Unveränderte Clients haben keine native serverseitige Besetztanzeige. Busy
  erfordert einen Opus-fähigen, nicht taubgeschalteten Client und hörbare lokale Wiedergabe.
- Schnelle PTT-Wechsel, Paketverluste und Client-Jitterbuffer benötigen weitere
  Praxistests. Sprache und Effekte sind getrennte Streams, keine samplesynchrone Mischung.
- Simplex leitet angenommene Sprache im aktuellen Kanal weiter; Whisper,
  Kanalverknüpfungen, zusätzliche Kanal-Abonnements und Server-Loopback bieten
  keine alternativen Funk-Sprachwege.
- Begrenzte Praxiserfahrung. Weder Dev noch der ältere Stable-Kandidat werden als
  allgemein produktionsreifes Funksystem dargestellt.

## Roadmap

Implementiert sind Simplex, Busy, Squelch Open/Close, Timeout-Fallback,
Audioverwaltung, kontinuierliches Rauschen, gemeinsamer Effektstream sowie Ubuntu
und Docker. Aktuelle Ziele sind breitere Geräte-/Netztests, zuverlässige schnelle
PTT-Wechsel, Docker-Hörtests und längere Stabilitätsprüfungen. Roger Beep, digitale
Funkprofile und optionales späteres Mixing sind **unverbindliche Ideen**.
Squelch und kontinuierliches Rauschen sind bereits implementiert, keine Zukunftsversprechen.

## Herkunft und Lizenz

Mumble-Funk ist unabhängig vom ursprünglichen
[Mumble-Projekt](https://github.com/mumble-voip/mumble). Es ist keine offizielle
Mumble-Veröffentlichung und behauptet keine Unterstützung oder Empfehlung durch
die ursprünglichen Entwickler. Upstream-Website: [mumble.info](https://www.mumble.info/).

> Copyright (C) 2005-2022 The Mumble Developers
>
> All rights reserved.

Quellcode: [LICENSE](LICENSE) (BSD 3-Clause). Ursprüngliche Copyright-Hinweise und
Komponentenlizenzen bleiben erhalten; siehe [3rdPartyLicenses](3rdPartyLicenses/)
und Hinweise in Submodulen. Für die mitgelieferten Aufnahmen gelten gesondert
[CC0 1.0 Universal](assets/audio/LICENSE.md).
