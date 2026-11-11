> **Aktueller Dev-Stand:** Der Boost ist seit PR #5 integriert. Für neue Dev-INIs
> gilt +8 dB für Busy/Open/Close; Funkrauschen hat eine eigene Lautstärke und
> wird nicht mitgeboostet. Unten bleiben die damaligen Feature-Tests erhalten.
> [Dev installieren](install-dev.md) · [Audio-Defaults](audio-defaults.md) ·
> [Verwaltung](ubuntu-commands.md).

# Audio Boost: experimenteller Lautstärketest

**Neuer Nachfolgestand:** [Standard-Audioeinstellungen](audio-defaults.md) auf
`feature/audio-defaults` aktiviert Funk-Audio bei Neuinstallationen mit +8 dB.
Die branchspezifischen Defaults und Installationsoptionen unten dokumentieren den
vorherigen Entwicklungsstand; bestehende Installationen werden nicht migriert.

**Aktueller Nachfolgestand:** [Standard-Busy-Tone Phase A](busy-tone.md) auf
`feature/busy-tone-asset` enthält diesen Boost bereits. Für dessen Installation
die dortige Option `--experimental-busy-tone` verwenden. Die unten dokumentierten
Messungen und Befehle für `feature/audio-volume` beschreiben den vorherigen Stand;
die Busy-Pegel der neuen Aufnahme stehen im neuen Dokument.

Arbeitsbranch: **feature/audio-volume**, aus `dev` nach dem Merge von PR #4.
Ausgangscommit: `e8894e5b6f42aaeee4e71262e58fadc471f4bea4` (Mumble 1.5.939).
Keine Stable-Freigabe. Keine Änderungen an Sprache, Simplex, Signalzustellung,
Busy-Klang, Timeout-Close, Clients oder Netzwerkports.

## Volume und Boost

Die bisherige Verarbeitung senkt WAV-Spitzen oberhalb 0,2 PCM auf 0,2 ab
(ungefähr −14 dBFS); leisere Dateien werden nicht automatisch normalisiert.
Darauf wirkt die jeweilige Lautstärke von 0 bis 100 %. Deshalb konnte selbst
100 % im praktischen Hörtest zu leise sein. Diese bisherige Stufe bleibt erhalten.

**`feedbackboostdb=0`** ist eine zusätzliche gemeinsame Verstärkung aller drei
Effekte: Open, Close und Busy, einschließlich eigener WAVs und synthetischem Busy.
Fehlt der Eintrag, gilt ebenfalls 0 dB. Bestehende Installationen werden weder
lauter gestellt noch automatisch auf neue Einstellungen umgestellt.

| Boost | zusätzlicher Amplitudenfaktor | maximaler PCM-Eingangspegel bei Volume 100 |
| --- | ---: | ---: |
| 0 dB | 1,000 | 0,200 |
| 3 dB | 1,413 | 0,283 |
| 6 dB | 1,995 | 0,399 |
| 9 dB | 2,818 | 0,564 |
| 12 dB | 3,981 | 0,796 |

Berechnung: `alter Gain × 10^(dB/20)`. dB beschreibt hier ein Verhältnis, keinen
absoluten Lautsprecherpegel. +6 dB entspricht etwa doppelter Signalamplitude,
aber nicht automatisch doppelter wahrgenommener Lautstärke. Die Einzellautstärken
balancieren die Effekte gegeneinander; Boost hebt alle gemeinsam an. Volume 0
bleibt stumm. Der Boost normalisiert leise WAVs nicht auf einen festen Zielpegel.

Erlaubt sind ausschließlich ganze Zahlen **0 bis 12**. Fehlerhafte Eingaben der
Verwaltung ändern nichts und starten keinen Dienst. Ein ungültiger direkter
INI-Eintrag wird beim Serverstart ausdrücklich abgelehnt. Für den Standardwert
0 bleibt sogar die bisherige Opus-Kodierung bytegleich. Der zusätzliche Faktor
wird nur beim Start angewendet, nicht während der Sprachweiterleitung. Keine
neue Abhängigkeit, Aufnahme, Audiodatei oder fremder Quellcode.

Die 0,796-Grenze gilt **vor** der verlustbehafteten Opus-Kodierung. Nach dem
Dekodieren können Spitzen leicht abweichen; 0,8 ist keine harte Decodergrenze.
Unsere Messung und Übersteuerungsprüfung erfolgt deshalb am wieder dekodierten
Signal. Wiedergabegerät, Clientlautstärke und akustische Wahrnehmung bleiben Teil
des noch erforderlichen Hörtests.

## Ubuntu-Test-VM aktualisieren

Nur auf deiner eigenen Snapshot-Test-VM ausführen. Snapshot und Datensicherung
verwaltet der Betreiber; Codex greift nicht auf Proxmox oder bestehende Server zu.
Grundvoraussetzungen und Erstinstallation: [Ubuntu-Snapshot-Anleitung](squelch-ubuntu.md).
Der normale Dienst und alle normalen Datenpfade bleiben erhalten.

Bei einer vorhandenen Test-Quellkopie `~/mumble-funk`:

```bash
cd ~/mumble-funk
git status --short
```

Wenn dort Änderungen erscheinen, **anhalten und diese zuerst sichern**. Danach:

```bash
git fetch origin refs/heads/feature/audio-volume:refs/remotes/origin/feature/audio-volume
git switch -c feature/audio-volume --no-track origin/feature/audio-volume
git rev-parse HEAD
bash scripts/funk/setup.sh --experimental-audio-volume
sudo mumble-funk-audio status
```

`git switch -c … --no-track` wird nur beim erstmaligen Anlegen des lokalen Branches
benutzt. Das funktioniert auch bei älteren `--single-branch`-Klonen. Eine neue
Tracking-Konfiguration ist nicht nötig; spätere Updates nennen den Remote-Zweig
ausdrücklich.
Für spätere Updates auf diesem Branch stattdessen:

```bash
cd ~/mumble-funk
git switch feature/audio-volume
git pull --ff-only origin feature/audio-volume
bash scripts/funk/setup.sh --experimental-audio-volume
```

Frische VM ohne Quellkopie:

```bash
sudo apt update
sudo apt install --yes git ca-certificates
git clone --branch feature/audio-volume --single-branch \
  https://github.com/Shawna7han/mumble-funk.git ~/mumble-funk
cd ~/mumble-funk
bash scripts/funk/setup.sh --experimental-audio-volume
```

Der neue Zweig benötigt **seine eigene ausdrückliche Freigabe**
`--experimental-audio-volume`. Der Quellschutz prüft weiterhin die offizielle
Herkunft, unveränderte Quellen und Übereinstimmung mit dem veröffentlichten Stand.
Keine Umbenennung zu Stable; alte `--experimental-squelch`-Aufrufe für den alten
Feature-Zweig bleiben möglich. Native Ports: **64739 TCP/UDP**. Ubuntu 24.04 und
die bestehende Freigabe für 26.04 bleiben erhalten. Setup kompiliert und testet,
bevor es ausschließlich den normalen Dienst `mumble-funk` aktualisiert.

Existierende INI, Passwörter, Datenbank, Zertifikate, WAV-Auswahl und eigene Dateien
bleiben erhalten, auch ein bereits eingestellter Boost. Für Neuinstallationen
bleiben Audio/Squelch aus und Boost 0. Die Verwaltung sichert die private INI wie
bisher nach `server.ini.before-audio-change`; diese Sicherung nicht veröffentlichen.

## Hörtest 0 / 3 / 6 / 9 / 12 dB

Zwei Clients im selben Kanal verwenden, Windows-Mumble und Android-Mumla
abwechselnd als Sender. Optional einen dritten Client in einem anderen Kanal
zur Kontrolle verwenden. Empfängerlautstärke während des Vergleichs gleich lassen;
mit angenehmer Wiedergabelautstärke und zunächst 0 dB beginnen.

Nur falls Squelch noch ausgeschaltet ist:

```bash
sudo mumble-funk-audio enable-squelch
sudo mumble-funk-audio enable-open
sudo mumble-funk-audio enable-close
```

Für Mumla ohne zuverlässige Ende-Markierung bei Bedarf bewusst freigeben:

```bash
sudo mumble-funk-audio enable-timeout-close
```

Timeout beweist weiterhin kein tatsächliches Loslassen der PTT-Taste. Der
[vorhandene Timeout-Hörtest](squelch-ubuntu.md) gilt unverändert.

Für einen direkten Vergleich die drei Volume-Werte auf 100 setzen (vorherige
Werte aus `status` für die spätere Rückkehr notieren):

```bash
sudo mumble-funk-audio open-volume 100
sudo mumble-funk-audio close-volume 100
sudo mumble-funk-audio volume 100
sudo mumble-funk-audio boost 0
sudo mumble-funk-audio status
```

Nach jedem Boost-Befehl kurz auf die Wiederverbindung der Clients warten.
Einen Satz mit klarer erster und letzter Silbe sprechen: Empfänger prüft Open,
vollständige Sprache und Close. Dann hält A den Kanal und B versucht zu senden:
Nur B soll Busy hören; B darf den Kanal nicht automatisch übernehmen. Rollen
tauschen und die gleiche Probe nach jedem der folgenden Befehle wiederholen:

```bash
sudo mumble-funk-audio boost 3
```

```bash
sudo mumble-funk-audio boost 6
```

```bash
sudo mumble-funk-audio boost 9
```

```bash
sudo mumble-funk-audio boost 12
```

Mit +3 oder +6 beginnen; höhere Werte nur bei Bedarf. Bei Verzerrungen oder
unangenehmer Lautstärke zurückgehen. Pro Wert Open/Close/Busy, Sprachverständlichkeit,
Sender-/Empfängergerät, Übertragungsweg und Commit-ID notieren. Bei schnellen
PTT-Wechseln können die bestehenden optionalen Effekte weiterhin entfallen.
Automatisierte Tests ersetzen diesen Gerätetest nicht.

Zur bisherigen Verstärkung zurückkehren (die Volume-Werte bleiben dabei erhalten):

```bash
sudo mumble-funk-audio boost 0
sudo mumble-funk-audio status
```

Alle ändernden Audio-Befehle starten nur `mumble-funk` neu und unterbrechen die
Verbindung kurz; keine Neukompilierung nötig. `status` ist schreibgeschützt und
zeigt `feedbackboostdb` in dB neben den vorhandenen Audioeinstellungen, kein Passwort.
Boost aktiviert keine deaktivierten Effekte. Alle bisherigen Verwaltungsbefehle
bleiben erhalten.

## Docker

Image aus diesem Feature-Stand bauen; die bestehende
[Backup-/Update-Anleitung](docker.md#9-aktualisieren) beachten. Im **bestehenden
Compose-Projekt** bleiben `.env`, benutzerdefinierte Host-Ports, Projektname und
Volumes unverändert. Nicht `down -v` verwenden. Keine automatische Portmigration.
Hoststandard **64740 TCP/UDP → Container 64739 TCP/UDP**; keine zusätzlichen Ports.

```bash
bash scripts/docker/audio.sh status
bash scripts/docker/audio.sh boost 6
bash scripts/docker/audio.sh boost 9
bash scripts/docker/audio.sh boost 0
```

Der Wrapper validiert den Wert vor dem Stoppen, bearbeitet nur sein Compose-Projekt
und startet den Dienst danach nur wieder, wenn er vorher lief. Vorhandene Volume-
und Timeout-Close-Einstellungen sowie eigene WAVs bleiben erhalten. Die INI im
bestehenden Config-Volume speichert den Boost über Container-Neuerstellung und
Backup/Wiederherstellung hinweg. Kein neues Volume und keine `.env`-Einstellung.

## Messung und Schutz gegen Codec-Spitzen

Der zusätzliche Schutz arbeitet ausschließlich beim einmaligen Vorbereiten eines
verstärkten Clips: Opus wieder dekodieren; falls eine Spitze über 0,98 liegt, den
Gain des **gesamten** Clips gleichmäßig absenken und aus zurückgesetztem Encoder
neu kodieren, Ziel 0,95. Höchstens acht Versuche, sonst ausdrücklicher Fehler statt
unsicherem Audio. Keine abgeschnittenen Samples und kein Echtzeit-Limiter. Der
0-dB-Pfad bleibt exakt der bisherige Pfad. Bei besonders impulsreichen oder
rechteckigen eigenen WAVs kann der effektiv erreichte Boost deshalb kleiner als
der eingestellte Wert sein. Die INI behält den gewünschten Wert; dieser ist keine
Garantie eines bestimmten gemessenen Decoderpegels.

Lokale Messung mit Opus 1.5.2, Volume jeweils 100, frischer Decoder, PCM-Skala 1,0:

| Boost | Busy Spitzenpegel | Open Spitzenpegel | Close Spitzenpegel |
| --- | ---: | ---: | ---: |
| 0 dB | 0,207 | 0,191 | 0,185 |
| 3 dB | 0,286 | 0,250 | 0,220 |
| 6 dB | 0,404 | 0,361 | 0,373 |
| 9 dB | 0,573 | 0,499 | 0,565 |
| 12 dB | 0,811 | 0,771 | 0,748 |

Diese drei Standards benötigten keine zusätzliche Sicherheitsabsenkung. Am
synthetischen Busy ergab die RMS-Messung +2,985 / +5,996 / +8,994 / +11,968 dB.
Die kurzen WAVs weichen wegen verlustbehafteter Kodierung etwas stärker ab:
Open +2,594 / +5,502 / +8,313 / +11,856 dB; Close +2,889 / +6,383 / +9,566 /
+12,171 dB. Das sind gemessene digitale Signalpegel, keine Hörtests.

Zusätzlich geprüft: eigene Busy-WAV, leise und voll ausgesteuerte Sinuswellen,
Rechteck, Impulse, alternierende Vollaussteuerung und deterministisches Rauschen.
Das Rechteck überschritt ohne Schutz bei +9 dB nach Opus die Vollaussteuerung;
der Schutz begrenzte es auf 0,883 PCM (etwa +7,62 dB effektiver RMS-Zuwachs).
Dateien werden nicht verändert, Paketanzahl und Dauer bleiben gleich. Andere
Opus-Versionen, Decoderzustände, Paketverlustrekonstruktion, nachgeschaltete
Clientverstärkung und Audiogeräte können andere Wiedergabepegel erzeugen.
Keine neue Geräte-Hörtestfreigabe oder Produktionsreife wird daraus abgeleitet.
