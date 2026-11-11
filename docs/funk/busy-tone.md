> **Aktueller Dev-Stand:** Die Standard-Busy-Aufnahme ist seit PR #6 integriert.
> Die Herkunfts- und Formatangaben unten bleiben gültig; Feature-Auswahl und
> frühere deaktivierte Effekte beschreiben den damaligen Stand. Heute:
> [Dev installieren](install-dev.md), [Audio-Defaults](audio-defaults.md) und
> [Audioverwaltung](ubuntu-commands.md).

# Neuer Standard-Busy-Tone — experimentelle Phase A

**Neuer Nachfolgestand:** [Standard-Audioeinstellungen](audio-defaults.md) auf
`feature/audio-defaults` aktiviert Funk-Audio bei Neuinstallationen mit +8 dB.
Die branchspezifischen Defaults und Installationsoptionen unten dokumentieren den
vorherigen Entwicklungsstand; bestehende Installationen werden nicht migriert.

Branch: **`feature/busy-tone-asset`**, Ausgangspunkt `dev` bei
`6b5322ef0fb404fc726b97eff44b7d213129fd85` (Squelch und Audio Boost bereits integriert).
Keine Stable-Freigabe. Windows-Mumble und Android-Mumla bleiben unverändert.
Ein neuer Geräte-Hörtest dieses Stands ist noch erforderlich.

## Datei, Herkunft und Integrität

Der Projektinhaber hat `assets/audio/busy-tone.wav` bereitgestellt und die
erforderlichen Rechte sowie die öffentliche Veröffentlichung unter **CC0 1.0**
ausdrücklich bestätigt; siehe [Asset-Lizenz](../../assets/audio/LICENSE.md).
Keine fremde Aufnahme, kein heruntergeladenes Sample und keine neue Abhängigkeit.
Der vorhandene mathematisch erzeugte synthetische Doppelton bleibt unverändert.

Die öffentliche Datei ist eine bereinigte Kopie. Ausschließlich der optionale
RIFF-Block `bext` mit einem nicht benötigten Herkunftshinweis wurde entfernt und
die RIFF-Gesamtlänge angepasst. Vorher wurde geprüft: Dieser Block enthielt
keinen Lizenz- oder Urheberhinweis. Andere Blöcke einschließlich des leeren
`junk`-Blocks blieben erhalten. Das unveränderte Original bleibt privat außerhalb
des Repositorys. Keine Neukodierung, Normalisierung oder Änderung der Audiosamples.

| Eigenschaft | Öffentliche WAV |
| --- | --- |
| Format | PCM-WAV, mono, signed 16 Bit little-endian |
| Abtastrate | 48.000 Hz |
| Länge | 57.468 Samples = **1197,25 ms** |
| PCM-Daten | 114.936 Bytes, bitgenau identisch mit dem Original |
| Dateigröße | 115.016 Bytes |
| SHA-256 Datei | `c24e7af67bc9aac5b040fad1118f006d79540a34b427d064e9a5596d0f11d107` |
| SHA-256 reine PCM-Daten | `ed0a899bf86c0d9772d49e2a88b9c04066ad9860094c518e16b3d2f583a1eae3` |

Der PCM-Hash wurde am privaten Original und an der öffentlichen Kopie verglichen;
zusätzlich wurden alle PCM-Bytes und die Formatparameter direkt verglichen.
Der automatisierte Asset-Test prüft beide öffentlichen Hashes und den fehlenden
Metadatenblock. Die eigene CC0-Erklärung ist weiterhin vorhanden.

## Grenzen und Wiedergabe

Busy-WAVs: **20 bis 1250 ms**, mono PCM16 LE, 8/16/24/48 kHz, höchstens **128 KiB**.
Squelch bleibt bei **1 bis 1000 ms**. Die Laufzeit lädt und kodiert einmal beim
Start; ein WAV-Wechsel benötigt einen Neustart, keine Neukompilierung.
Unpassende eigene Dateien werden vor der Auswahl abgelehnt. Ist eine direkt in
der INI ausgewählte Busy-Datei beim Start ungültig oder nicht lesbar, meldet der
Server eine Warnung und verwendet weiterhin den synthetischen Fallback.

Der vorhandene Wiedergabeweg ergänzt 20 ms Stille für den Opus-Vorlauf und füllt
das letzte 20-ms-Paket auf. Für diese Datei entstehen **61 Opus-Pakete / 1220 ms**,
einschließlich Vorlauf-/Endpuffer. Das letzte Paket trägt die Ende-Markierung.
Die nichtleeren letzten Samples passen vollständig in den dekodierten Stream.
Das WAV auf der Festplatte wird dadurch nicht verändert.

Busy bleibt privat beim abgewiesenen Sender. Auch bei eingehender verschlüsselter
UDP-Sprache wird das Feedback wie bisher über die vorhandene TCP-UDPTunnel-Verbindung
zugestellt. Keine Bots, erfundenen Sessions oder zusätzlichen Ports.
Simplex-Regeln, Sprachpakete und Squelch-/Timeout-Close-Architektur bleiben erhalten.

Cooldown weiterhin eine Sekunde ab Busy-Beginn: Während der längere Busy läuft,
startet kein zweiter Busy und es wird keiner angehängt. Danach ist ein neuer
Busy möglich. Busy hat weiter Vorrang vor optionalem Squelch; dadurch können
Squelch-Effekte während des jetzt längeren Tons häufiger entfallen. Bei Überlast
oder länger blockierter Ereignisschleife dürfen optionale Töne wie bisher
abgebrochen werden. Sprache wird dafür niemals verzögert oder gepuffert.

## Neuinstallation und Bestandsschutz

| Situation | Verhalten |
| --- | --- |
| Neue Ubuntu-/Docker-Konfiguration | `busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav` |
| Bereits vorhandene INI | Unverändert, einschließlich leerem/fehlendem `busytonefile` (synthetisch), eigener WAV, Lautstärke, Boost und Squelch |
| Audio / Squelch frisch | Weiterhin aus; Busy-Schalter true, Volume 50, Boost 0 wie bisher |
| Fehlender INI-Schlüssel / direkter Serverdefault | Weiterhin synthetischer Ton; keine globale Migration |

Der native Installer stellt die drei öffentlichen WAVs und ihre Lizenz mit
Modus 0644 im vorhandenen Standardverzeichnis bereit. Eigene importierte WAVs
liegen separat unter `/etc/mumble-funk/sounds/`; Docker nutzt dafür das bestehende
State-Volume unter `/var/lib/mumble-funk/audio/`. Benutzerdateien werden nicht ersetzt.
Falls auf Ubuntu bereits eine abweichende Datei oder ein Link genau am neuen
Standardpfad `busy-tone.wav` liegt, hält das Update vor den Installationsänderungen
an. Diese Datei zuerst separat sichern und über die vorhandene Verwaltung als
eigene WAV importieren; den belegten Standardpfad erst danach bewusst freigeben.

Datenbanken, Konten, Kanäle, Passwörter, Zertifikate und vorhandene `.env` bleiben
erhalten. Ubuntu: **64739 TCP/UDP**. Docker: Hoststandard **64740 TCP/UDP → 64739
TCP/UDP** im Container; individuelle Host-Ports bleiben erhalten. Kein neues
Compose-Projekt und keine neuen Volumes. Niemals `docker compose down -v` verwenden.

## Ubuntu-Snapshot-Test-VM installieren oder aktualisieren

Nur selbst auf der eigenen Test-VM ausführen. Snapshot-Verwaltung liegt beim
Betreiber; Codex setzt keine VM zurück. Voraussetzungen wie bisher: Ubuntu
24.04 oder 26.04 LTS, sudo-Benutzer, systemd, Internet, freier Port 64739,
mindestens 8 GiB freier Speicher (mehr empfohlen), 4 GiB RAM empfohlen.
Vor einem Update Snapshot und private Sicherung der bisherigen Konfiguration,
Datenbank und eigenen WAVs erstellen; siehe [Ubuntu-Anleitung](installation-ubuntu.md).

Vorhandene Quellkopie (Ordner gegebenenfalls anpassen):

```bash
cd ~/mumble-funk
git status --short
```

Wenn Änderungen erscheinen, **anhalten und zuerst sichern**. Danach beim ersten
Wechsel auf den neuen Feature-Branch:

```bash
git fetch origin refs/heads/feature/busy-tone-asset:refs/remotes/origin/feature/busy-tone-asset
git switch -c feature/busy-tone-asset --no-track origin/feature/busy-tone-asset
git rev-parse HEAD
bash scripts/funk/setup.sh --experimental-busy-tone
sudo mumble-funk-audio status
```

`--no-track` funktioniert auch bei älteren Single-Branch-Klonen.
Falls der lokale Branch bereits existiert, stattdessen:

```bash
cd ~/mumble-funk
git status --short
git switch feature/busy-tone-asset
git fetch origin refs/heads/feature/busy-tone-asset:refs/remotes/origin/feature/busy-tone-asset
git merge --ff-only origin/feature/busy-tone-asset
bash scripts/funk/setup.sh --experimental-busy-tone
```

Die explizite Fetch-Zuordnung aktualisiert auch die lokal gespeicherte
`origin/feature/busy-tone-asset`-Referenz in älteren Single-Branch-Klonen. Das ist
für die bestehende Herkunftsprüfung erforderlich; ein einfaches Pull auf einen
anderen Remote-Branch reicht dort nicht zuverlässig aus. Der anschließende
Fast-Forward-Merge erzeugt keinen eigenen Merge-Commit.

Bei einer frischen VM ohne Quellkopie:

```bash
sudo apt update
sudo apt install --yes git ca-certificates
git clone --branch feature/busy-tone-asset --single-branch \
  https://github.com/Shawna7han/mumble-funk.git ~/mumble-funk
cd ~/mumble-funk
bash scripts/funk/setup.sh --experimental-busy-tone
sudo mumble-funk-info --hide-password
```

Das Setup prüft die offizielle Herkunft, saubere versionierte Dateien, die
1.5.939-Abstammung und den veröffentlichten Feature-Stand. Nur dieser Branch
akzeptiert seine ausdrückliche Option `--experimental-busy-tone`; es wird kein
Stable-Stand vorgetäuscht. Es baut und testet vor der normalen Installation von
`mumble-funk`, ohne zusätzliche Preview-Dienste. Bestehende Konfiguration bleibt
erhalten. Info zeigt kein Passwort automatisch; für die Clientverbindung bei
Bedarf die vorhandene lokale Passwortanzeige bewusst aufrufen:

```bash
sudo mumble-funk-info
```

Nur im eigenen Terminal ausführen; Passwort nicht in Testberichte übernehmen.

## Ton auswählen und zurücksetzen

Bei einem Update bleibt die bisherige Auswahl absichtlich erhalten. Den neuen
Standard ausdrücklich wählen:

```bash
sudo mumble-funk-audio use-standard-busy-tone
sudo mumble-funk-audio status
```

Weitere Auswahlmöglichkeiten:

```bash
sudo mumble-funk-audio set-busy-tone /absoluter/pfad/mein-ton.wav
sudo mumble-funk-audio reset-busy-tone
```

`set-busy-tone` prüft und importiert eine eigene Datei; der Quellpfad bleibt
unverändert. **`reset-busy-tone` behält seine bisherige Bedeutung: synthetischer
Doppelton**, nicht die neue Aufnahme. `use-standard-busy-tone` wählt wieder die
mitgelieferte Aufnahme. Kein Befehl löscht alte importierte Dateien.

Die Auswahl allein schaltet Audio nicht ein. Für den bewussten Busy-Test auf der
VM muss Simplex bereits eingeschaltet sein (frisches automatisches Setup: ja)
und `busytone=true` gelten. Master-Audio bei Bedarf aktivieren:

```bash
sudo mumble-funk-audio enable
sudo mumble-funk-audio volume 100
sudo mumble-funk-audio boost 0
```

Zunächst mit angenehmer Clientlautstärke testen, dann bei Bedarf einzeln:

```bash
sudo mumble-funk-audio boost 6
```

```bash
sudo mumble-funk-audio boost 12
```

Boost ist weiterhin **gemeinsam für Busy, Open und Close**, Volume betrifft nur
Busy. Alle ändernden Befehle starten ausschließlich den eigenen Dienst kurz neu;
Clients müssen sich wieder verbinden. `status` liest nur die Audioeinstellungen.
Vorherige Werte notieren, um später zurückzukehren. Volume 0 bleibt mit jedem
Boost vollständig stumm; die neue Auswahl setzt keinen vorhandenen Gain zurück.

## Hörtest mit Windows-Mumble und Android-Mumla

1. Beide Clients mit VM-IP und Port 64739 verbinden, denselben Kanal wählen.
2. A hält PTT und spricht. B drückt ebenfalls PTT: Nur B soll den ganzen neuen
   Busy hören; B darf nicht bei Freigabe von A automatisch übernehmen. B muss PTT
   erst lösen und erneut drücken. Der Empfänger hört As Sprache unverändert.
3. Rollen tauschen. Bei Busy-Lautstärke 100 nacheinander Boost 0, 6 und 12 testen.
   Auf vollständiges Tonende, Verzerrungen und unveränderte Sprache achten.
4. `volume 0` setzen: kein Busy hörbar, auch bei Boost 12. Danach eigenen vorherigen
   Wert zurücksetzen. `reset-busy-tone` und `use-standard-busy-tone` vergleichen.
5. Bestehendes Open/Close sowie Mumla-Timeout-Close mit den bisherigen Einstellungen
   prüfen; [Squelch-Hörtest](squelch-ubuntu.md) bleibt gültig. Optional drittes Gerät
   im anderen Kanal: kein fremdes Feedback.
6. Commit-ID, Clientversionen, Lautstärke/Boost und Beobachtungen notieren. Erst
   danach über eine Freigabe entscheiden; automatisierte Tests ersetzen das nicht.

## Docker

Im bestehenden Checkout/Compose-Projekt erst nach der
[Backup-/Update-Anleitung](docker.md#9-aktualisieren) auf diesen Feature-Stand
wechseln, Image aus demselben Commit bauen und den eigenen Container neu erstellen.
`.env`, Projektname und Volumes behalten. Die öffentliche WAV und ihre Lizenz
liegen im Runtime-Image und sind für UID/GID 10001 lesbar. Neue Config wählt die
WAV, alte Config bleibt unverändert. Es wird nichts automatisch aktiviert.

```bash
bash scripts/docker/audio.sh status
bash scripts/docker/audio.sh use-standard-busy-tone
bash scripts/docker/audio.sh volume 100
bash scripts/docker/audio.sh boost 12
bash scripts/docker/audio.sh reset-busy-tone
bash scripts/docker/audio.sh set-busy-tone /absoluter/pfad/mein-ton.wav
```

Die Wrappernamen entsprechen der nativen Verwaltung. Änderungen stoppen nur den
eigenen Compose-Dienst und starten ihn nur wieder, wenn er zuvor lief. Importierte
WAVs sind Bestandteil des vorhandenen Backup-/Restore-Wegs. Ein Rollback auf den
alten Quellstand kann die neue 1197-ms-Datei nicht laden (früher maximal 1000 ms);
vorher synthetischen Busy oder eine kompatible kurze eigene WAV wählen, oder die
gesicherte alte INI wiederherstellen. Daten/Volumes nicht löschen.

## Automatisierte Prüfungen und Grenzen

`TestBusyToneAsset` prüft Dateihash, PCM-Hash, Länge, Grenzen bei allen vier
Abtastraten, Fallback, tatsächliche Opus-Dekodierung samt Tonende, Volume 0/50/100,
Boost 0/6/12, Cooldown und Busy-Priorität. Die erweiterten Server-Integrationstests
prüfen alle 61 Pakete und Ende-Markierung, TCP und verschlüsselten UDP-Eingang,
Legacy/Protobuf, Empfängeridentität/Kanaltrennung, Simplex und stummen Busy.
Native Tests prüfen Neuinstallation, Bestandsschutz, Dateirechte, Auswahl und
explizite experimentelle Freigabe. Docker-Tests prüfen zusätzlich Standards im
Image und eigene Busy-WAVs über Container-Neuerstellung und Backup/Restore.

Lokale Messung mit Opus 1.5.2 bei Busy Volume 100 (PCM-Skala 1,0):

| Boost | Dekodierter Spitzenpegel | RMS | RMS-Zuwachs |
| --- | ---: | ---: | ---: |
| 0 dB | 0,202321 | 0,081100 | 0 dB |
| 6 dB | 0,397085 | 0,162095 | +6,015 dB |
| 12 dB | 0,818035 | 0,325296 | +12,065 dB |

Keine zusätzliche Schutzabsenkung war für diese Aufnahme erforderlich. Der
bestehende Schutz gegen Opus-Spitzen bleibt aktiv; andere Decoder, Paketverluste,
Clientverstärkung und Audiogeräte können abweichen. Dies sind digitale Messungen,
keine behaupteten Hörtests auf echten Geräten. Keine Produktionsreife oder
Garantie gegen optionale Effektverluste unter Überlast.
