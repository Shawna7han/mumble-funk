> **Einordnung nach der Integration:** Die hier beschriebene Phase 1 wurde über PR #2
> nach Dev übernommen. Der folgende Text beschreibt den damaligen Branch und
> die alte getrennte Preview, nicht den heutigen Funktionsumfang. Aktuell:
> [Dev installieren](install-dev.md), [Audio-Defaults](audio-defaults.md),
> [Verwaltung](ubuntu-commands.md) und [gemeinsamer Effektstream](radio-noise.md).

# Audio-Engine, Phase 1 — experimenteller Entwicklungsstand

Branch: **`feature/audio-engine`**, aus `dev` Commit
`510f90618212511c8bac2c0c4732dca317d1d205`.
Dies ist keine Stable-Version, kein Release und keine Produktionsfreigabe.
Die bestehende Busy-Tone-Engine aus `feature/audio-feedback`
(`8b295e0cd889933951f7c2e941ad914a5a7c5269`) wird mit der gemeinsamen
Ubuntu-/Docker-Basis zusammengeführt. Die alte Audio-Preview bleibt eigenständig.

## Funktionsumfang und Grenzen

A belegt einen Kanal. B sendet gleichzeitig und wird von Simplex abgewiesen.
Nur B erhält den vorbereiteten Busy Tone; A und weitere Teilnehmer erhalten ihn
nicht. B übernimmt einen frei werdenden Kanal weiterhin nicht automatisch.
Die bisherigen Ende-Markierungen, Kanal-/Retry-Timeouts und Berechtigungen gelten.

Die Tonzustellung verwendet normale Opus-Sprachpakete im TCP-`UDPTunnel`, auch wenn
der abgewiesene Client Sprache über verschlüsseltes UDP sendet. Es gibt keine neue
Protokollnachricht, Bot-Verbindung oder Clientänderung. Empfang erfordert einen
Opus-fähigen Client; Ausgabe, Lautstärke und Deafen des Clients gelten weiterhin.
Die alte Preview wurde laut Projektinhaber mit Windows-Mumble und Android-Mumla
praktisch gehört. Das ersetzt keinen Praxistest dieses neuen Integrationsstands.

Keine neuen Effekte: kein Squelch, Grundrauschen, Roger Beep oder Dauer-Busy-Tone.
Es bleibt bei einer Meldung pro erkanntem Versuch, einer Sekunde Wiederholungsschutz
und begrenzten Warteschlangen. Physische PTT-Tasten erkennt der Server weiterhin
nicht direkt. Eine verlorene Ende-Markierung kann daher den nächsten Versuch verzögern.

## Aktivierung und eigene WAV

Audiofeedback ist **standardmäßig aus**, auch bei der neuen Preview. Mit aktivem
Simplex benötigt die jeweilige, ausdrücklich gewählte Testinstanz:

```ini
audiofeedback=true
busytone=true
busytonevolume=50
busytonefile=
```

`audiofeedback=false`, `busytone=false` oder Lautstärke `0` deaktivieren die Ausgabe.
Änderungen erfordern einen Neustart **nur dieser Testinstanz**. Wie bei Mumble
üblich können vorhandene virtuelle Servereinstellungen in SQLite INI-Werte
übersteuern. Bestehende Datenbanken werden nicht automatisch umgeschrieben.

Unterstützt: RIFF/WAVE PCM Format 1, vorzeichenbehaftet 16 Bit Little Endian,
Mono, **8000/16000/24000/48000 Hz**, **20–1000 ms**, insgesamt höchstens **128 KiB**.
44,1 kHz, Stereo, Float, komprimierte und extensible WAV werden abgelehnt.
Die Engine rechnet auf 48 kHz um und fügt einen stummen 20-ms-Codec-Nachlauf an.
Sie begrenzt den PCM-Spitzenpegel auf 0,2 (etwa −14 dBFS) und wendet danach die
Lautstärke 0–100 % an; leise Dateien werden nicht automatisch verstärkt.

Für deine eigene REAPER-/Dexed-Datei ist keine erneute Kompilierung erforderlich.
Nach einer später ausdrücklich ausgeführten neuen Preview-Installation:

```bash
# Datei zunächst z. B. als ~/busy-tone.wav auf den Ubuntu-Server kopieren.
sudo mumble-funk-audio-engine set-busy-tone "$HOME/busy-tone.wav"
sudo mumble-funk-audio-engine volume 50
sudo mumble-funk-audio-engine enable
sudo mumble-funk-audio-engine-info
```

Die Info-Ausgabe enthält standardmäßig **kein Passwort**, auch bei Umleitung in
eine Datei. Für die ausdrückliche lokale Anzeige im interaktiven Terminal:

```bash
sudo mumble-funk-audio-engine-info --show-password
```

Diese Option verweigert Pipes und umgeleitete Standardausgabe. Das Passwort wird
nur auf das steuernde Terminal geschrieben, nicht nach stdout/stderr.
`--hide-password` bleibt kompatibel und entspricht dem neuen Standard.
Details und Grenzen: [Sicherheitsprüfung zu PR #2](AUDIO-SECURITY-REVIEW.md).

Der Import prüft mit demselben C++-WAV-Prüfer wie der Server, kopiert nach
`/etc/mumble-funk-audio-engine/sounds/busy-<SHA256>.wav` und setzt `busytonefile`.
Er startet ausschließlich `mumble-funk-audio-engine` neu. `set-busy-tone` schaltet
Audiofeedback nicht nebenbei ein. Ungültige Importe lassen die Konfiguration
unverändert. Beim Serverstart führt eine unbrauchbare WAV dagegen zu einer
Logwarnung und dem synthetischen Fallback. Bei einem fehlgeschlagenen
Verwaltungs-Neustart wird die vorige Konfiguration wiederhergestellt.

```bash
sudo mumble-funk-audio-engine reset-busy-tone  # synthetischen Fallback wählen
sudo mumble-funk-audio-engine disable         # Audiofeedback abschalten
sudo tail -n 80 /var/log/mumble-funk-audio-engine/server.log
```

Die alten Befehle `mumble-funk-audio` und `mumble-funk-audio-info` gehören weiterhin
zur alten Preview auf 64740. Nicht für diese neue Testinstanz verwenden.

## Sichere spätere Ubuntu-Preview

**Hier wird nichts automatisch installiert.** Eine neue Instanz kann später nach
deiner Entscheidung parallel in einem **neuen Quellordner** eingerichtet werden.
Voraussetzungen: Ubuntu Server 24.04/26.04 LTS, normaler sudo-Benutzer, laufendes
systemd, mindestens 2 GiB RAM inklusive Swap (4 GiB empfohlen), 8 GiB freier Platz
und freier Testport TCP/UDP. Paketinstallation benötigt Internetzugang.

```bash
git clone --branch feature/audio-engine --recurse-submodules \
  https://github.com/Shawna7han/mumble-funk.git mumble-funk-audio-engine
cd mumble-funk-audio-engine
bash scripts/funk/setup-audio-preview.sh
sudo mumble-funk-audio-engine-info
```

| Instanz | Port TCP/UDP | Dienst und eigene Pfade |
| --- | --- | --- |
| Bestehender Standardserver | 64738 | Unverändert |
| Bestehender Simplex-Server | 64739 | `mumble-funk`, unverändert |
| Bestehende Audio-v0.3-Preview | 64740 | `mumble-funk-audio`, unverändert |
| **Neue Phase-1-Preview** | **64741** | `mumble-funk-audio-engine` |

Die neue Instanz verwendet `/opt/mumble-funk-audio-engine`,
`/etc/mumble-funk-audio-engine`, `/var/lib/mumble-funk-audio-engine` und
`/var/log/mumble-funk-audio-engine`. Ihre Installation verweigert die geschützten
Ports 64738–64740 sowie belegte Ports. Sie importiert keine alte Datenbank.
Bestehende Einstellungen einer späteren neuen Engine-Instanz bleiben erhalten;
vor einem Update werden ihre eigenen Daten gesichert. Es gibt kein automatisches
Git-Update oder Deployment. Die normalen Stable-Installationsbefehle verweigern
diesen experimentellen Quellbranch.

Erst nach bewusster Installation die obigen Audio-Verwaltungsbefehle benutzen.
Auf dem Client neuen Servereintrag mit eigener Portnummer und dem separat
erzeugten Passwort anlegen. Mit A/B/C prüfen: Ton nur bei B, A weiter hörbar,
keine automatische Übernahme; danach PTT loslassen und erneut versuchen.
Kanalwechsel, Verbindungsabbruch und TCP-/UDP-Betrieb gesondert prüfen.

## Docker

Dockerfile und Compose behalten Projekt-, Dienst-, Volume-Namen sowie die
bisherige Portabbildung. Hinzu kommen nur die benötigten Opus-Pakete und Tests.
Die [bestehenden Update-/Rollback-Verfahren](docker-stable-migration.md) bleiben
unverändert. **Diesen Entwicklungsstand nicht über eine bestehende Installation
deployen.** Für einen späteren parallelen Test sind eigener Quellordner, ein neuer
`COMPOSE_PROJECT_NAME` und ein freier externer Port erforderlich. Niemals die
Projektkennung oder Volumes einer laufenden Installation übernehmen oder löschen.

### Verbindliche Port-Stabilität

Für die bestehende Docker-Installation gilt dauerhaft:
**Host 64740 TCP und UDP → Container 64739 TCP und UDP**.
`FUNK_HOST_PORT=64740` in der vorhandenen `.env` bleibt erhalten und funktioniert
weiterhin. Updates dürfen weder diese Datei noch einen benutzerdefinierten
Host-Port überschreiben. Audiofeedback läuft über die bestehende Mumble-Verbindung;
es sind **keine zusätzlichen Audioports**, Firewall- oder Routeränderungen nötig.
Die native, separat angelegte Engine-Preview auf 64741 ist eine andere Instanz und
keine Zusatzverbindung für Docker-Audio.

Historisch behielt die Audio-Integration in Phase 1 den Beispiel-/Fallbackwert
64739 bei. Der gesonderte Entwicklungsstand `feature/docker-port-default`
(Ziel: `dev`) setzt nun `.env.example` und Compose-Fallback auf **64740**.
`stable` und die historische Audio-Integration werden dadurch nicht verändert.
**Keine Beispiel-Datei über eine bestehende `.env` kopieren.** Vor einem Update
aus einem alten Stand ohne ausdrücklich gesetzten Host-Port muss der bisherige
Port festgehalten werden: [Bestandsschutz und Rollback](docker.md#9-aktualisieren).
Freiwillig gewählte andere Host-Ports bleiben erlaubt; intern bleibt es bei 64739.
Compose-Projekt, Volumes, Datenbank, Passwörter und Zertifikate bleiben erhalten.

Die Engine ist in der Docker-Binärdatei enthalten, bleibt aber ohne ausdrückliche
Konfiguration deaktiviert. Für eine bewusst angelegte neue Testinstanz kann ihre
`/etc/mumble-funk/server.ini` im eigenen Konfigurationsvolume die obigen vier
Schlüssel enthalten. Eine eigene WAV gehört in ihr eigenes Datenvolume, z. B.
`/var/lib/mumble-funk/audio/busy-tone.wav`; `busytonefile` bezeichnet diesen
**Containerpfad**, nicht einen Hostpfad. Die Datei muss für UID 10001 lesbar sein.
Die Ubuntu-Verwaltung gilt nur für die separate native Preview.

## Architektur, künftige Assets und Lizenzen

`SimplexController::Transmission::feedback` meldet ausschließlich eine Abweisung;
es vergibt keine Rechte. Der Simplex-Lock wird vor dem Queue-Aufruf freigegeben.
`AudioSignal` prüft WAV, erzeugt den Fallback und kodiert einmalig beim Start.
`AudioFeedback` besitzt die vorbereiteten Pakete, eine try-lock-Warteschlange mit
maximal 128 Anfragen, höchstens 64 Empfänger und den Wiederholungsschutz.
Der Qt-Hauptthread versendet im 20-ms-Takt. Rückstau wird nicht als Audioburst
nachgeholt; langsame, taube oder getrennte Empfänger werden verworfen.
Verbindungskennung, Session und Kanal verhindern Zustellung an wiederverwendete
Sessions. Kanalwechsel und Abbruch löschen ausstehende Wiedergaben.

`Signal`, `AudioSignal`, Clip-Auswahl und private Zustellung sind die vorhandenen
Erweiterungspunkte. Weitere Signale können dort später gezielt ergänzt werden.
Es wurde keine hypothetische Mixer-/Rauscharchitektur implementiert.
Für Phase 2 bietet sich `assets/audio/` mit expliziten Herkunfts-/Lizenzhinweisen
und Installation in ein lesbares Ressourcenverzeichnis an. Konfigurierte WAV soll
Vorrang vor einem mitgelieferten Signal und dem synthetischen Fallback haben.
Das ist ein Vorschlag, **noch keine implementierte Asset-Suchreihenfolge**.
Es werden jetzt keine WAV-Platzhalter oder persönlichen Dateien eingecheckt.

Der unveränderte Fallback ist aus zwei generischen 600-Hz-Sinusimpulsen
programmiert: jeweils 100 ms, 40 ms Pause, 5 ms lineare Ein-/Ausblendung und
20 ms stummer Nachlauf; insgesamt 260 ms. Die Formel steht im Quellcode.
Keine Internet-Samples, Herstellerklänge oder externen Aufnahmen.
Opus ist eine zusätzliche dynamische **Server**-Abhängigkeit; die geprüften
BSD-Hinweise werden mitgeliefert. Siehe [Lizenzinventar](AUDIO-RESOURCE-LICENSES.md).

Testergebnisse und offene Prüfungen stehen im
[Phase-1-Prüfbericht](AUDIO-ENGINE-STATUS.md). Die alten Berichte bleiben als
historische Nachweise erhalten, gelten aber nicht als Test dieses Branches.
