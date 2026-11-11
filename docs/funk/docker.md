# Mumble-Funk mit Docker — experimentell

Dieser Stand enthält den [Standard-Busy-Tone](busy-tone.md), Squelch und
**Audio Boost (0–12 dB)** aus den vorherigen Entwicklungsphasen. Bestehende Einstellungen und
eigene WAVs werden bei Updates nicht automatisch geändert.


Der aktuelle experimentelle **`dev`-Stand nach PR #9** enthält die gemeinsame Audio-Engine
und die experimentelle Squelch-Vorschau auf Mumble Server 1.5.939. Das ist keine
Stable-Freigabe. Neue Konfigurationen aktivieren Simplex, Busy, Squelch Open/Close
bei 50 % und +8 dB; Mumla-Timeout-Close ist ebenfalls aktiv. Vorhandene INIs bleiben unverändert.
Funkrauschen ist bei **neuen INIs eingeschaltet**, mit der mitgelieferten
CC0-Aufnahme `/usr/local/share/mumble-funk/audio/radio-noise.wav` bei Lautstärke 20.
Bestehende Einstellungen und auch fehlende Rauschschlüssel werden nicht migriert.
Die vorhandene Open/Noise/Close-Engine wird unverändert auch in Docker verwendet.
Details und Bestandsschutz: [Standard-Audioeinstellungen](audio-defaults.md).
Die Anleitung für Aktivierung, eigene WAVs und getrennte Open-/Close-Einstellungen
steht in [squelch-preview.md](squelch-preview.md#docker-assets-and-management).

Host **64740 TCP/UDP** → Container **64739 TCP/UDP**. `.env`, individuell gewählte
Host-Ports, Compose-Projektname und alle bestehenden Volumes bleiben erhalten.
Ein Update wechselt keine Ports automatisch. Native Ubuntu-Installation: 64739.

**Bereits installiert?** Im vorhandenen Projekt arbeiten und zuerst sichern.
Die [Stable-Migration](docker-stable-migration.md) beschreibt den historischen
Simplex-v0.2-Kandidaten; sie ist keine Freigabe, eine bestehende Stable-Installation
ungefragt durch den experimentellen Dev-Stand zu ersetzen. Der damalige
Stand verwendete ohne explizite Einstellung noch Host-Port 64739. Historische
Prüfberichte bleiben unverändert. Siehe [Bestandsschutz beim Update](#9-aktualisieren)
und den [Prüfbericht zum Portstandard](DOCKER-PORT-DEFAULT-STATUS.md).

**Zusätzlicher Weg ohne lokalen Compiler:** Die [GHCR-Image-Anleitung](docker-image.md)
beschreibt die vorbereitete `compose.ghcr.yaml`, denselben Datenbestand und den
sicheren Wechsel/Rollback. Veröffentlichung nur nach gesonderter Freigabe; in
diesem Auftrag wurde kein Registry-Image veröffentlicht. Die folgenden Befehle
mit `compose.yaml` behalten den bestehenden lokalen Build bei.

## Voraussetzungen

Führe die Befehle im Terminal deines **Docker-Hosts** aus, zum Beispiel auf einer
Ubuntu-VM. Es wird nichts automatisch auf einer anderen VM oder Proxmox installiert.

- Docker Engine mit BuildKit und **Docker Compose v2** müssen bereits installiert
  sein. Docker Engine ab 24 und Compose ab 2.24 werden empfohlen; geprüft wurden
  Engine **28.4.0** und Compose **2.40.3** auf einem Linux-amd64-Cloud-Host.
- `git`, Bash und die üblichen Linux-Werkzeuge müssen vorhanden sein. Die
  Docker-Befehle müssen für deinen Benutzer erlaubt sein. Falls du normalerweise
  `sudo docker` benötigst, verwende dies konsequent; die Sicherungsskripte dann
  entsprechend mit `sudo bash` ausführen.
- Empfohlen: **2 CPU-Kerne, 4 GiB RAM**, mindestens **12 GiB freier Docker-Speicher**,
  besser 20 GiB für Build-Cache und spätere Updates. Der Build verwendet standardmäßig
  zwei Prozesse; LTO und Unity-Build sind aus. Eine harte 4-GiB-Grenze wurde hier
  nicht separat nachgestellt. Die Laufzeit-Imagegröße hängt von Revision und Basisimage ab; Build-Werkzeuge,
  Quellen, Zwischenstände und Cache benötigen zusätzlich mehrere GiB.
- Internetzugriff auf Docker Hub, Ubuntu-Paketquellen und GitHub. Der erste Build
  lädt Pakete und die vorhandenen, auf konkrete Commits festgelegten Submodule.
- Ein freier **Host-Port für TCP und UDP**, standardmäßig **64740** (dieser Entwicklungsstand).
- Für die Bedienungs- und Sicherungsskripte wird ein Linux-Host vorausgesetzt.
  Windows-/Android-Clients benötigen selbst kein Docker.

Prüfen:

```bash
docker version
docker compose version
docker info
```

Falls Docker fehlt, verwende die offiziellen Anleitungen für
[Docker Engine auf Ubuntu](https://docs.docker.com/engine/install/ubuntu/) und das
[Compose-Plugin](https://docs.docker.com/compose/install/linux/). Diese Lösung
installiert oder verändert Docker selbst nicht.

## Ubuntu-Installation oder Docker?

| Eigenschaft | Ubuntu-Schnellinstallation | Docker-Installation |
| --- | --- | --- |
| Start | `scripts/funk/setup.sh --experimental-dev` auf Dev | `docker compose up -d --build`; Entwicklungsstand siehe unten |
| Betrieb | Eigener systemd-Dienst | Eigener Container mit Neustartrichtlinie |
| Dateien | Eigene Verzeichnisse direkt auf Ubuntu | Eigene benannte Docker-Volumes |
| Port (TCP/UDP) | Standardmäßig 64739 | Hier Host-Standard 64740, frei wählbar; intern immer 64739 |
| Änderungen am anderen Weg | Keine | Keine |

Docker ersetzt die vorhandene Installation nicht. Der neue Host-Standard **64740**
kann neben dem nativen Funkserver auf **64739** verwendet werden. Falls 64740 bereits
belegt ist (zum Beispiel durch eine historische Audio-Preview), wähle bewusst einen
anderen freien Host-Port. Audiofunktionen benötigen keine zusätzlichen Ports.

## 1. Herunterladen

Benutze ein neues Quellverzeichnis, damit vorhandene v0.2-/v0.3-Quellen erhalten
bleiben:

```bash
git clone --branch dev --single-branch \
  https://github.com/Shawna7han/mumble-funk.git mumble-funk-docker
cd mumble-funk-docker
```

`--recurse-submodules` ist nicht erforderlich: Der Docker-Build beschafft die
vorhandenen Abhängigkeiten anhand von `scripts/docker/submodules.lock.json`.
Es werden weder der Desktop-Client noch die Android-App gebaut.

## 2. Optional den Host-Port festlegen

Ohne `.env` verwendet dieser Stand **64740 TCP und UDP**. Für eine neue Installation
empfehlen wir, den gewünschten Port ausdrücklich festzuhalten:

```bash
# Nur eine fehlende .env anlegen; eine vorhandene Datei niemals überschreiben:
if [ ! -e .env ]; then cp -n .env.example .env; fi
nano .env
```

In `nano` steht für den Standard `FUNK_HOST_PORT=64740`. Für einen eigenen freien
Host-Port ändere ausschließlich diese Zeile, zum Beispiel auf `FUNK_HOST_PORT=65001`.
Speichern: `Strg+O`, Eingabetaste; schließen: `Strg+X`.

`.env` enthält nur Docker-Einstellungen, **kein Serverpasswort**. Vorhandene Werte
werden nicht automatisch ersetzt. Der ausdrücklich eingetragene Port gilt auch
bei späteren Updates oder beim Zurückwechseln zu einem älteren Compose-Stand.
Der Server im Container bleibt auf **64739 TCP/UDP**; Docker bildet beide auf den
gewählten Host-Port ab.

Weitere Einstellungen in `.env`:

```ini
FUNK_HOST_PORT=64740
FUNK_BUILD_JOBS=2
COMPOSE_PROJECT_NAME=mumble-funk-docker
```

Bei wenig Arbeitsspeicher kann `FUNK_BUILD_JOBS=1` helfen. Zulässig sind 1 bis 16.
Für eine zweite unabhängige Docker-Instanz brauchst du zusätzlich einen **anderen
Projektnamen**, einen freien Host-Port und am besten ein eigenes Quellverzeichnis.
Ein neuer Projektname erzeugt neue Volumes; ändere ihn bei gewöhnlichen Updates
nicht versehentlich.

## 3. Bauen und starten

```bash
docker compose config --quiet
docker compose up -d --build
docker compose ps
```

Der erste Build dauert einige Minuten, auf kleinen VMs länger. Dabei werden alle
20 Server-Testprogramme dieses Entwicklungsstands ausgeführt. Scheitert ein Test oder Build,
wird kein erfolgreiches Laufzeit-Image daraus bereitgestellt.

Beim ersten Start erzeugt die Instanz:

- ein zufälliges Zugangspasswort mit 48 Hex-Zeichen;
- ihre eigene Konfiguration mit `simplex=true`, `simplextimeout=500`,
  `simplexretrytimeout=500`, `simplexdiagnostics=true`;
- eine eigene SQLite-Datenbank und ein eigenes Mumble-Zertifikat.

Vorhandene Konfigurationen und Datenbanken werden bei normalen Neustarts oder
Updates **nicht neu erzeugt oder überschrieben**. Fehlt nur eines von beiden
unerwartet, wird zum Schutz vorhandener Daten angehalten. Eine unterbrochene
Erstinitialisierung hat einen eigenen Wiederanlaufmarker.

Der Server läuft als UID/GID **10001**, ohne Root, ohne Linux-Capabilities und mit
schreibgeschütztem Root-Dateisystem. Schreibbar sind seine eigenen Volumes und ein
begrenztes temporäres Verzeichnis. Er erhält weder Docker-Socket noch Host-Netzwerk
oder Zugriff auf native Datenbanken.

## 4. Zugangsdaten anzeigen und verbinden

```bash
docker compose exec mumble-funk funk-container info
docker compose port --protocol tcp mumble-funk 64739
docker compose port --protocol udp mumble-funk 64739
```

Das Passwort wird nur bei diesem ausdrücklich aufgerufenen Info-Befehl lokal
angezeigt, nicht bei jedem Start in die Logs geschrieben. Die Konfiguration hat
standardmäßig Dateirechte **0600**. Ein Benutzer mit Docker-Administratorrechten
kann grundsätzlich auf die Containerdaten zugreifen.

In **Mumla und Windows-Mumble** einen neuen Servereintrag erstellen:

| Feld | Eingabe |
| --- | --- |
| Name | Zum Beispiel Mumble-Funk Docker |
| Adresse | Die erreichbare IP-Adresse deines Docker-Hosts |
| Port | Gewählter **Host-Port**, zum Beispiel 64740 |
| Benutzername | Eigener Name, z. B. Android oder Windows |
| Passwort | Ausgabe von `funk-container info` |

`0.0.0.0` aus `docker compose port` ist keine Adresse, die du am Telefon eintragen
sollst: Verwende die IP-Adresse der VM/des Hosts. Verbinde die Geräte mit demselben
Kanal. Beide Clients bleiben unverändert.

Die Instanz erzeugt ein eigenes Zertifikat. Prüfe die Zertifikatsanzeige beim ersten
Verbinden nach deinem üblichen Mumble-Verfahren. Das Zertifikat bleibt bei Updates
in der eigenen Datenbank erhalten.

Firewalls und Router werden nicht verändert. Bei Fernzugriff müssen der gewählte
Host-Port **für TCP und UDP** erreichbar sein, gegebenenfalls auch durch Proxmox und
den Router. Beachte, dass Docker veröffentlichte Ports eigene Firewall-Regeln
verwenden; eine UFW-Regel allein beschreibt nicht zwingend den tatsächlichen Zugang.

## 5. Logs und Zustandsprüfung

```bash
docker compose logs --tail=100 mumble-funk
docker compose logs -f mumble-funk
docker compose exec mumble-funk funk-container health
```

Mit `Strg+C` beendest du die laufende Loganzeige, nicht den Server.

| Prüfung | Was sie nachweist |
| --- | --- |
| Laufender Container | Prozess läuft, noch kein Sprachtest |
| Healthcheck/`healthy` | Lokaler TLS-Handshake mit Mumble-Version-Header und passender lokaler UDP-Ping |
| Verbindung eines Clients | Tatsächliche Anmeldung über den veröffentlichten TCP-Port |
| Zwei/drei Clients mit PTT-Test | Sprache über das tatsächliche Netzwerk und Simplex-Verhalten |

Der Healthcheck beweist **keine verschlüsselte Sprachübertragung aus dem Internet**
und keine hörbare Android-/Windows-Ausgabe. Dafür ist der Praxistest unten nötig.

Docker begrenzt seine normalen Containerlogs auf drei Dateien zu je 10 MB. Die
Simplex-Diagnose schreibt separat in das Log-Volume und rotiert bei ungefähr
10 MiB je Datei. Es werden serverseitig keine Sprachinhalte aufgezeichnet.

## 6. Simplex-Diagnose ohne INI-Editor ändern

Die ersten beiden Befehle benötigen einen gestoppten Server, damit die Einstellung
nicht gleichzeitig verändert wird:

```bash
docker compose stop mumble-funk
docker compose run --rm --no-deps mumble-funk set simplexdiagnostics false
docker compose up -d
```

Mit `true` schaltest du sie wieder ein. Simplex funktioniert weiterhin, wenn die
Diagnose ausgeschaltet ist. Entsprechend sind `voicediagnostics`, `simplex`,
`simplextimeout` und `simplexretrytimeout` einstellbar. Die Zeitangaben sind
Millisekunden zwischen 50 und 60000; die bestehenden Regeln begrenzen die wirksame
Wiederholungszeit auf mindestens die Kanalzeit.

Diagnose ansehen:

```bash
docker compose exec mumble-funk tail -n 30 /var/log/mumble-funk/simplex.1.jsonl
```

Die Serverausgaben stehen in Docker-Logs; mit `logdays=-1` wird keine zusätzliche,
unbegrenzt wachsende Serverlog-Kopie in der SQLite-Datenbank angelegt. Benutzer- und
Kanaldaten bleiben davon unberührt.

## 7. Optional: Administratorkonto einrichten

Das Zugangspasswort verleiht keine Administratorrechte. `SuperUser` ist zunächst
für Passwortanmeldungen deaktiviert, damit der unveränderte Server beim Erststart
kein zusätzliches zufälliges Administratorpasswort in die Logs schreibt.

Wenn du Kanäle und Berechtigungen verwalten möchtest:

```bash
docker compose stop mumble-funk
docker compose run --rm --no-deps mumble-funk admin-password
docker compose up -d
```

Gib das neue Passwort zweimal verdeckt ein. Verwende 12 bis 200 druckbare
ASCII-Zeichen, beispielsweise ein langes vom Passwortmanager erzeugtes Passwort.
Es wird über die Standardeingabe an Mumble übergeben, nicht als Prozessargument
oder INI-Klartext gespeichert. Verwende es danach mit dem Benutzernamen `SuperUser`.
Zum normalen Sprechen wieder als gewöhnlicher Benutzer verbinden: Mumble erlaubt
SuperUser grundsätzlich kein Sprechen.

## 8. Stoppen, starten, neu starten oder neu erstellen

```bash
# Stoppen:
docker compose stop

# Vorhandenen Container starten:
docker compose start

# Neu starten:
docker compose restart

# Container neu erstellen, vorhandene Volumes weiterverwenden:
docker compose up -d --force-recreate

# Container und Projektnetzwerk entfernen, Daten behalten:
docker compose down

# Danach wieder erstellen:
docker compose up -d
```

`restart: unless-stopped` startet eine laufende Instanz nach einem Docker-/Host-
Neustart wieder, sofern Docker selbst automatisch startet. Eine ausdrücklich
gestoppte Instanz bleibt gestoppt. Ein echter Host-Neustart bleibt ein Praxistest.

Docker verwendet einen Init-Prozess zur Signalweitergabe. Das Startprogramm wird
per `exec` durch Mumble ersetzt; SIGTERM wird weitergegeben und bis zu 30 Sekunden
für einen ordentlichen Abschluss eingeräumt. Eine zusätzliche Dateisperre verhindert
zwei Serverprozesse oder Offline-Verwaltung auf denselben Daten gleichzeitig.

## 9. Aktualisieren

Bleibe im selben Verzeichnis mit demselben Projektnamen, denselben Compose-Optionen
und denselben Volumes. Kein Update wird hier automatisch ausgeführt.

**Vor dem ersten Wechsel auf diesen neuen Standard:** Prüfe den tatsächlich
veröffentlichten Port des bestehenden Containers:

```bash
docker compose port --protocol tcp mumble-funk 64739
docker compose port --protocol udp mumble-funk 64739
```

Steht in deiner `.env` schon `FUNK_HOST_PORT=64740` oder ein eigener Host-Port,
behalte ihn unverändert. **Fehlt `.env` oder fehlt/ist die Portzeile leer**, galt
bisher der alte Fallback 64739. Vor jedem Quellstandwechsel mit `nano .env` eine
Zeile `FUNK_HOST_PORT=64739` eintragen, sofern die obige Prüfung diesen bisherigen
Port bestätigt. Bei einem anderen tatsächlichen Port genau diesen festhalten.
Bestehende andere Zeilen nicht ersetzen. Das ist eine bewusste Festlegung des
bisherigen Ports, keine automatische Migration. Auch Shell-Variablen und
`--env-file`-Optionen beibehalten: sie können `.env` übersteuern. Wenn die beiden
Protokolle unterschiedliche Ports zeigen, vor dem Update anhalten und klären.

Ohne diese Festlegung würde Compose bei einem späteren `up`/Neuerstellen den neuen
Fallback verwenden. Ein bereits laufender Container wird durch Git oder die
Beispieldatei allein nicht verändert. Weder Firewall noch Router werden angepasst.

Danach sichern und vor dem Neuerstellen die Zuordnung vergleichen:

```bash
umask 077
backup_dir="$(pwd)/backups/vor-update-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$backup_dir"
git rev-parse HEAD > "$backup_dir/previous-commit.txt"
if test -f .env; then cp -p .env "$backup_dir/previous.env"; fi
docker compose config --format json > "$backup_dir/compose-before.json"
docker image tag "$(docker inspect "$(docker compose ps -q mumble-funk)" --format '{{.Image}}')" \
  mumble-funk-rollback:before-update
bash scripts/docker/manage.sh backup "$backup_dir/server.tar.gz"
git pull --ff-only
docker compose config --format json > "$backup_dir/compose-after.json"
python3 - "$backup_dir" <<'CHECK'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
a = json.loads((root / 'compose-before.json').read_text())
b = json.loads((root / 'compose-after.json').read_text())
assert a['name'] == b['name'], 'Projektname verändert: anhalten'
assert a['volumes'] == b['volumes'], 'Volume-Zuordnung verändert: anhalten'
assert set(a['services']) == set(b['services']), 'Dienste verändert: anhalten'
for key in ('ports', 'volumes', 'image'):
    assert a['services']['mumble-funk'][key] == b['services']['mumble-funk'][key], key + ' verändert: anhalten'
print('Ports, Projekt, Dienst, Image-Name und Volumes unverändert.')
CHECK
```

**Nur wenn die Prüfung erfolgreich ist**, in derselben Sitzung fortfahren:

```bash
docker compose build
docker compose up -d --no-build
docker compose ps
docker compose port --protocol tcp mumble-funk 64739
docker compose port --protocol udp mumble-funk 64739
```

`git pull --ff-only` aktualisiert nur den bereits ausgewählten Tracking-Branch;
es wechselt weder nach `dev` noch `stable`. Für einen bewussten Branchwechsel
verwende die [Migrationsanleitung](docker-stable-migration.md) mit denselben
Zuordnungsprüfungen. Wenn der Rollback-Image-Tag schon zu einer aufbewahrten
Sicherung gehört, vor dem Ablauf einen neuen eindeutigen Tag wählen.

Zur Rückkehr: den gespeicherten Commit und das gesicherte Image verwenden,
`.env` inklusive ausdrücklich festgelegtem Host-Port **behalten**:

```bash
docker image tag mumble-funk-rollback:before-update \
  "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["services"]["mumble-funk"]["image"])' "$backup_dir/compose-before.json")"
git switch --detach "$(cat "$backup_dir/previous-commit.txt")"
docker compose up -d --no-build
```

Bei eigenen Änderungen vor `git switch` anhalten. Es werden keine Branches
zurückgesetzt. Die festgelegte Portzeile funktioniert auch mit dem alten
Fallback. Kein `down -v`, kein Löschen und kein Neuerstellen von Datenvolumes.

Bei Git-Konflikten nicht mit Zurücksetzen/Erzwingen weiterarbeiten. Den Fehler erst
prüfen lassen. Ein fehlgeschlagener Image-Build ersetzt den laufenden Container
nicht. Das neue Image wird lokal gebaut, nicht aus einer öffentlichen
Mumble-Funk-Image-Registry heruntergeladen.

Für eine bewusste Erneuerung auch der zwischengespeicherten Ubuntu-Pakete:

```bash
docker compose build --pull --no-cache
docker compose up -d
```

Dabei bleiben die Datenvolumes ebenfalls erhalten. Cache-/Paketstände sind nicht
auf ewig eingefroren: Der Ablauf ist reproduzierbar, nicht bytegenau identisch über
alle späteren Ubuntu-Paketupdates hinweg.

**Zum Aktualisieren niemals `docker compose down -v` benutzen.** Dieser Zusatz
löscht die persistenten Volumes und gehört nicht zum Update-Verfahren.

## 10. Wo die Daten liegen

| Containerpfad | Benanntes Volume bei Standard-Projektname | Inhalt |
| --- | --- | --- |
| `/etc/mumble-funk` | `mumble-funk-docker_funk_config` | Konfiguration und Zugangspasswort |
| `/var/lib/mumble-funk` | `mumble-funk-docker_funk_state` | SQLite, Zertifikat, Konten, Kanäle, Rechte und eigene WAV-Dateien in `audio/` |
| `/var/log/mumble-funk` | `mumble-funk-docker_funk_logs` | Paket-/Simplex-Diagnose |

Anzeigen:

```bash
docker volume ls --filter label=com.docker.compose.project=mumble-funk-docker
```

Bei anderem Projektnamen den Filter anpassen. Docker verwaltet diese Volumes; nicht
mit `/var/lib/mumble-funk` der nativen Installation verwechseln. **Nie beide
Installationen auf dieselbe SQLite-Datei zeigen lassen.**

## 11. Konsistent sichern

Eine laufende SQLite-Datei nicht einfach mit `cp` kopieren. Verwende:

```bash
mkdir -p backups
bash scripts/docker/manage.sh backup "backups/mumble-funk-$(date +%Y%m%d-%H%M%S).tar.gz"
```

Das Skript stoppt **nur diesen Compose-Dienst**, hält während der Sicherung eine
exklusive Datensperre, prüft SQLite und archiviert Konfiguration, vollständiges
Datenverzeichnis einschließlich Zertifikat sowie Logs. War der Dienst vorher aktiv,
wird er anschließend wieder gestartet, auch bei einem Sicherungsfehler. Es gibt
also eine kurze Unterbrechung; Clients müssen gegebenenfalls neu verbinden.

Das Archiv wird erst nach erfolgreicher Sicherung mit Rechten **0600** veröffentlicht;
ein vorhandenes Zielarchiv wird nicht ersetzt. Es enthält Zugangsdaten und den
privaten Zertifikatsschlüssel und sollte deshalb vertraulich außerhalb des Hosts
aufbewahrt werden. Das Quellverzeichnis `backups/` ist von Git und Build-Kontext
ausgeschlossen. Die Images selbst müssen nicht gesichert werden.

## 12. Wiederherstellen, ohne vorhandene Daten zu überschreiben

Die Wiederherstellung akzeptiert absichtlich **nur leere Ziel-Volumes**. So kann ein
falsches Archiv keine laufende Installation ersetzen. Verwende ein neues
Verzeichnis, einen neuen Projektnamen und einen freien Port. Der hier gezeigte
64741 ist ausschließlich ein frei zu prüfender Wiederherstellungs-Testport, kein
zusätzlicher Audioport und keine Änderung des regulären Standards 64740.
Für identische Wiederherstellung den zur Sicherung notierten Dev-Quellcommit
verwenden; nach dem Klonen gegebenenfalls mit `git switch --detach COMMIT_ID`
auf genau diese vorhandene Revision wechseln, bevor das Image gebaut wird.
Beispiel für einen zur Sicherung passenden Dev-Stand:

```bash
cd ~
git clone --branch dev --single-branch \
  https://github.com/Shawna7han/mumble-funk.git mumble-funk-wiederherstellung
cd mumble-funk-wiederherstellung
if [ ! -e .env ]; then cp -n .env.example .env; fi
sed -i 's/^COMPOSE_PROJECT_NAME=.*/COMPOSE_PROJECT_NAME=mumble-funk-restored/' .env
sed -i 's/^FUNK_HOST_PORT=.*/FUNK_HOST_PORT=64741/' .env
docker compose build
bash scripts/docker/manage.sh restore /ABSOLUTER/PFAD/ZUR/SICHERUNG.tar.gz
docker compose up -d
docker compose exec mumble-funk funk-container info
```

**Nicht vor dem Wiederherstellen `up` ausführen**, sonst werden die neuen Volumes
bereits initialisiert. Falls dies versehentlich geschieht, verwende einen weiteren
neuen Projektnamen. Die ursprüngliche Instanz bleibt erhalten.

Vor dem Übertragen werden Archivpfade und SQLite geprüft. Symbolische Links,
Sonderdateien und Pfade außerhalb der drei vorgesehenen Bereiche werden abgewiesen.
Grenzen: 2 GiB komprimiert, 8 GiB entpackt und höchstens etwa 100000 Einträge. Für das
Zwischenlagern und Wiederherstellen muss zusätzlich ausreichend Platz vorhanden
sein. Die Konfiguration wird zuletzt bereitgestellt. Bei einem Speicher-/I/O-Fehler
bleiben ursprüngliche Instanzen unangetastet; ein teilweise gefülltes neues Ziel
muss geprüft bzw. für einen weiteren Versuch ein neues Ziel gewählt werden.

Teste die Anmeldung und PTT auf dem Wiederherstellungsport, bevor du später selbst
entscheidest, den alten Dienst abzuschalten. Es gibt kein automatisches Löschen
alter Volumes, keine automatische Migration und keine Freigabe eines belegten Ports.

## 13. Einfacher Praxistest mit echten Geräten

1. Mumla und Windows-Mumble am **Host-Port** anmelden, im gleichen Kanal.
2. Windows hält PTT und spricht. Android versucht gleichzeitig zu sprechen.
   Ein dritter Zuhörer soll ausschließlich Windows hören.
3. Android hält PTT weiter. Windows hört auf. Android darf nicht automatisch
   übernehmen. Android lässt PTT los, wartet mindestens eine Sekunde und startet neu.
4. Rollen tauschen und den Ablauf wiederholen.
5. Container neu starten und neu erstellen. Passwort, Zertifikat und eigene Kanäle
   müssen weiterhin passen.
6. Falls du Netzwerkprobleme untersuchen möchtest, TCP-erzwungenen Clientbetrieb
   und normalen UDP-Betrieb getrennt testen; Client-Version und Netzweg notieren.

Bei einer neuen Dev-INI sind Busy, Squelch Open/Close, Timeout-Close und
Funkrauschen bereits aktiv. Bei einer bestehenden INI zunächst `status` prüfen;
Updates schalten keine Effekte ein. Für die Hörprüfung zusätzlich den
[Funkrausch-Testplan](#hörtest-mit-windows-mumble-und-android-mumla) verwenden.
Die bisherigen realen Mumla-/Windows-Erfahrungen mit v0.2 ersetzen keinen neuen
Hörtest über deinen konkreten Docker-Host, Router und die Proxmox-Firewall.

## 14. Fehlerbehebung

- **Port belegt:** Keinen fremden Dienst stoppen. Einen freien `FUNK_HOST_PORT` in
  `.env` wählen und erneut `docker compose up -d` ausführen.
- **Anmeldung geht, Sprache nicht:** Host-Port für **UDP und TCP** und den gesamten
  Firewall-/Routerweg prüfen. `healthy` ist ein lokaler, kein Internet-Sprachtest.
- **Passwort vergessen:** Lokal `docker compose exec mumble-funk funk-container info`.
- **Unhealthy:** `docker compose logs --tail=100 mumble-funk` und den manuellen
  Healthcheck ausführen; anschließend Rechte/Platz und unveränderte internen
  Port-/Datenbankpfade prüfen. Der interne Port muss 64739 bleiben.
- **Fehlende Konfiguration oder Datenbank:** Keine neue Datei über vorhandene Daten
  kopieren. Richtigen Projektnamen und die zugehörigen Volumes prüfen, gegebenenfalls
  aus einer vollständigen Sicherung in eine neue Instanz wiederherstellen.
- **Build bricht wegen Speicher ab:** `FUNK_BUILD_JOBS=1` in `.env` setzen und freien
  Docker-Speicher prüfen. Ein Testfehler beendet den Build; er wird nicht ignoriert.
- **Mehrere Instanzen auf denselben Volumes:** Die Datensperre blockiert den zweiten
  Prozess. Jedem Server einen eigenen Projektnamen geben.
- **Wiederherstellung verweigert:** Ziel muss leer und Archiv vollständig sein;
  vorhandene Volumes werden bewusst nicht überschrieben.

## 15. Automatisierte Prüfung und bekannte Grenzen

Den letzten vor PR #9 geprüften Stand und seine Grenzen beschreibt die
[aktuelle Entwicklungsübersicht](development.md#tests-und-praxiserfahrung).
Der [Docker-Rausch-Prüfbericht](DOCKER-RADIO-NOISE-STATUS.md) bleibt der historische
Nachweis seiner dort genannten Revision.
Der [ursprüngliche Docker-Prüfbericht](DOCKER-STATUS.md) dokumentiert die damaligen Commits.
Die Multi-Stage-Builds führen alle **33** CTest-Prüfungen einschließlich Audio und Squelch aus.
Zusätzlich gibt es Docker-spezifische Tests für Initialisierung, sichere Rechte,
Sperren, Konfiguration und Archivprüfung sowie echte Compose-Integrationstests.

Die Protokollprobe benutzt authentifizierte Testclients und verschlüsselte UDP-
Pakete mit überprüften Testnutzdaten durch den veröffentlichten Host-Port. Sie
prüft die Weiterleitungs-/Simplex-Entscheidungen. Im Audio-Modus prüft sie zusätzlich
die exakte Busy-Paketanzahl, Sequenzen, Ende-Markierung und tatsächliche
Opus-Dekodierbarkeit ohne digitale Übersteuerung. Die Audio-/Squelch-Servertests
prüfen weitere Decoder- und Routingfälle. Der native Betreiber-Hörtest mit Windows-Mumble und Android-Mumla war für den
bisherigen Testumfang erfolgreich. Er ersetzt keinen Docker-Hörtest. Dieser bleibt
für den neuen Docker-Verwaltungsstand offen; keine allgemeine Stable-Freigabe.

Die Simplex-Regeln bleiben unverändert. Der gemeinsame Integrationsstand behebt
gezielt den UDP-Nonce-/Versionsfehler, nachdem ein deterministischer Test ihn am
alten Server nachgewiesen hat. Nach der Entschlüsselung wird die bekannte
Clientversion verwendet. Die acht Kombinationen aus alten/neuen Paketformaten,
bekanntem/unbekanntem Peer und Simplex an/aus werden direkt getestet.
Zusätzliche Regressionstests erzwingen verschlüsselte Pakete, die zufällig auch
wie öffentliche Pings aussehen. Die Authentifizierung hat Vorrang; echte
öffentliche Serverabfragen bleiben verfügbar und werden gesondert geprüft.
Docker erzeugt keine angepassten Kopien der Testquellen und vermeidet keine
kritischen Nonce-Werte mehr. Alle Routing-Assertions bleiben bestehen; der
zusätzliche Test der UDP-Ping-Warteschleife verlangt eine echte authentifizierte
Antwort auch nach einem ungültigen Datagramm.

Für die ursprüngliche gemeinsame Simplex-/Installationsintegration gilt der
[historische Prüfbericht](STABLE-INTEGRATION-STATUS.md). Der bisherige
[Docker-Prüfbericht](DOCKER-STATUS.md) beschreibt ausdrücklich den historischen
Feature-Branch. Seine dort genannten Testadapter sind kein Teil von Stable.

Keine Behauptung von Produktionsreife, abgeschlossenem Langzeittest oder getesteter
ARM-/Windows-/macOS-Hostunterstützung. Ein echter Host-Neustart und der externe
Netzweg bleiben Praxistests. Der zusätzliche [GHCR-Veröffentlichungsweg](docker-image.md) ist vorbereitet;
ein tatsächlicher Upload und die öffentliche Freigabe stehen noch aus.

## 16. Audioassets, Verwaltung und Lizenz

Die BSD-Projektlizenz und Copyright-Hinweise bleiben erhalten. Das Laufzeit-Image
enthält die Projektlizenz, Drittanbieter-Hinweise und Ubuntu-Paket-Lizenzdateien.
Die integrierte Audio-Engine benötigt Opus auch zur Laufzeit. Die vier
mitgelieferten Aufnahmen für Busy, Open, Close und Funkrauschen stammen vom
Projektinhaber und sind ausdrücklich unter CC0 1.0 Universal freigegeben.
Die [Audio-Lizenz](../../assets/audio/LICENSE.md) liegt zusammen mit ihnen im Image.
Für die Dokumentationsaktualisierung werden keine Abhängigkeiten oder Aufnahmen
hinzugefügt oder verändert. Die
öffentliche Busy-WAV ist eine von optionalen Metadaten bereinigte Kopie mit
bitgenau denselben PCM-Audiosamples; Format, Prüfsummen und Nachweis stehen in
[busy-tone.md](busy-tone.md). Das private Original wird nicht veröffentlicht.

Standards: `/usr/local/share/mumble-funk/audio/`. Eigene WAVs: bestehendes
State-Volume unter `/var/lib/mumble-funk/audio/`. Die sichere Verwaltung nutzt
`bash scripts/docker/audio.sh status` und die in
[squelch-preview.md](squelch-preview.md#docker-assets-and-management) dokumentierten
Befehle. Bei Änderungen wird nur der eigene Compose-Dienst kurz gestoppt und,
wenn vorher aktiv, wieder gestartet. Keine zusätzlichen Audioports oder Volumes.
Eigene WAVs benötigen keinen neuen Build und sind Teil der vorhandenen Sicherung.
Compiler, Git, CMake, Testprogramme und Quellbäume bleiben außerhalb des Laufzeit-Images.


## 17. Eigenes Funkrauschen einrichten

Nur auf deiner freigegebenen **Docker-Testinstanz** im vorhandenen Projektverzeichnis
arbeiten. Die Befehle verwenden den bestehenden Compose-Dienst und dessen Volumes.
Sie ändern weder `.env` noch Projektname oder Ports: Host **64740 TCP/UDP**, innen
**64739 TCP/UDP**, sofern du selbst keinen anderen Host-Port festgelegt hast.

Die vom Betreiber erstellte und ausdrücklich unter CC0 1.0 Universal freigegebene
Standardaufnahme ist bytegenau im Image enthalten: siehe
[Herkunft, Lizenz und SHA-256](../../assets/audio/LICENSE.md). UID 10001 kann sie
auch mit read-only Root-Dateisystem lesen. Bei frischen INIs ist sie bei
Lautstärke 20 aktiv; ein Update ändert vorhandene INIs nicht.

Zusätzlich kannst du eine eigene Datei mit geklärten Nutzungsrechten verwenden. Lege die private
Quelldatei außerhalb des Git-Verzeichnisses ab, beispielsweise unter
`~/mumble-funk-private/radio-noise.wav`. Nicht committen oder hochladen.

Unterstützt wird RIFF/WAV mit PCM, **mono, 16 Bit signed little endian, 48000 Hz**,
20 ms bis 60 Sekunden, höchstens **8 MiB** Gesamtdateigröße. Der Server prüft das
Format mit seinem vorhandenen Rausch-WAV-Parser; es findet keine Konvertierung statt.
Für Busy/Open/Close gelten weiterhin deren kürzere Grenzen und höchstens 128 KiB.

### Import, Lautstärke und Aktivierung

```bash
bash scripts/docker/audio.sh status
bash scripts/docker/audio.sh set-radio-noise "$HOME/mumble-funk-private/radio-noise.wav"
bash scripts/docker/audio.sh radio-noise-volume 20
bash scripts/docker/audio.sh enable-radio-noise
bash scripts/docker/audio.sh status
```

Simplex und Audio-Feedback müssen bereits aktiviert sein. Der Rauschbefehl schaltet
sie nicht heimlich ein. Bei Bedarf erst `bash scripts/docker/audio.sh enable`
verwenden; Simplex gemäß Abschnitt 6 prüfen. **Import verändert den Aktivierungszustand nicht.** Lautstärke 0–100, 0 bleibt stumm. Rauschen hat eine unabhängige Lautstärke;
der globale Audio-Boost gilt weiterhin für Busy/Open/Close, nicht für Rauschen. Für Open und Close müssen die jeweiligen Squelch-Schalter aktiv sein.

Die geprüfte Datei wird unverändert unter
`/var/lib/mumble-funk/audio/radio-noise-<SHA256>.wav` im vorhandenen State-Volume
abgelegt. Der Prüfsummenname statt eines überschriebenen `radio-noise.wav` bewahrt
frühere Dateien für Rollbacks. Die genaue Auswahl steht vollständig in `status`.
Rechte eigener Importe: UID/GID **10001:10001**, Modus **0640**; der Server kann sie lesen.
Der Standard liegt in Ubuntu und Docker unter
`/usr/local/share/mumble-funk/audio/radio-noise.wav` (0644), unabhängig von eigenen WAVs.
Die Quelldatei auf dem Host bleibt unverändert. Kein neuer Build ist nötig.

Zurück zur mitgelieferten Aufnahme (nur Dateiauswahl, Lautstärke und Ein/Aus
bleiben erhalten; alte importierte Dateien werden nicht gelöscht):

```bash
bash scripts/docker/audio.sh use-standard-radio-noise
bash scripts/docker/audio.sh enable-radio-noise
```

| Installation | Eigene Rauschdatei |
| --- | --- |
| Ubuntu nativ | `/etc/mumble-funk/sounds/radio-noise-<SHA256>.wav`, siehe [Funkrauschen](radio-noise.md) |
| Docker | `/var/lib/mumble-funk/audio/radio-noise-<SHA256>.wav`, eigenes State-Volume |

Zum Ausschalten, ohne Datei, Lautstärke oder andere Effekte zu ändern:

```bash
bash scripts/docker/audio.sh disable-radio-noise
```

Der JSON-Status enthält neben allen bisherigen Audiofeldern `radionoise`,
`radionoisefile` und `radionoisevolume`. Fehlende Einträge bedeuten `false`, leere
Dateiauswahl und `20`. Der Status zeigt **die gespeicherte Konfiguration**, keinen
Nachweis einer erfolgreich geladenen Datei oder hörbarer Wiedergabe. Er gibt keine
Passwörter aus und stoppt/startet keinen Server.

### Fehler, Sicherung und Updates

Vor Änderungen sichern, wie in Abschnitt 11 beschrieben:

```bash
mkdir -p backups
bash scripts/docker/manage.sh backup "$(pwd)/backups/vor-rauschen-$(date +%Y%m%d-%H%M%S).tar.gz"
```

Ändernde Audio-Befehle stoppen ausschließlich den eigenen Compose-Dienst kurz und
starten ihn nur, wenn er zuvor lief. Verbindungen können dabei getrennt werden.
Eine lokale, begrenzte Momentaufnahme wird read-only eingebunden, mit dem Server
validiert und anschließend atomar samt neuer INI-Auswahl veröffentlicht. Falsche
Formate, Links und zu große Dateien werden abgewiesen; die vorige INI und bisherige
WAV-Dateien bleiben erhalten. Frühere importierte Dateien werden nicht gelöscht.
Scheitert der anschließende Neustart/Healthcheck, versucht die Verwaltung die
vorige INI wiederherzustellen und erneut zu starten. Eine inzwischen anderweitig
veränderte INI wird aus Sicherheitsgründen nicht zurückgesetzt. Bei einer weiterhin
fehlgeschlagenen Wiederherstellung erscheint eine Fehlermeldung: Containerlog
prüfen und keine weiteren parallelen Verwaltungsbefehle ausführen.

Die vorhandene Sicherung enthält INI, SQLite, Zertifikate und das gesamte
`audio/`-Verzeichnis einschließlich eigener Rauschdateien. Backups können
Passwörter und private Aufnahmen enthalten: nur privat aufbewahren. Restore gemäß
Abschnitt 12 ausschließlich in **leere** Zielvolumes; nie bestehende Daten löschen,
um einen Restore zu erzwingen.

Beim Update Abschnitt 9 einschließlich Image-/Konfigurationssicherung befolgen.
Derselbe Projektname, dieselben Volumes und die vorhandene `.env` bleiben erhalten.
Das Image ersetzt keine importierten WAVs, aktiviert Rauschen nicht automatisch und
migriert keine vorhandenen Einstellungen. Ein Rollback auf ein Image vor der
Rauschfunktion kann die Datei behalten, unterstützt aber nicht zwingend deren
Wiedergabe oder Verwaltungsbefehle; dafür die zu diesem Image gesicherte
Konfiguration verwenden. Niemals `down -v` beim Update/Rollback verwenden.

### Hörtest mit Windows-Mumble und Android-Mumla

1. Mit beiden unveränderten Clients den bisherigen Host-Port (normal 64740)
   und denselben Funkkanal verwenden.
2. Erst `status` prüfen. Rauschen einschalten, Open und Close aktiviert lassen.
3. Windows sendet 2–10 Sekunden, Mumla empfängt: Open → Rauschen während Sprache →
   Close hören; dann in umgekehrter Richtung testen.
4. Über 60 Sekunden senden und erneut auf lückenloses Rauschen und Close achten.
   Den synthetischen automatisierten Test nicht als echten Hörtest werten.
5. Gleichzeitig PTT drücken: nur der angenommene Sender ist im Kanal hörbar,
   der abgewiesene bekommt Busy. Schnelle Wechsel und Kanalwechsel ausprobieren.
6. Bei Mumla ohne Ende-Markierung kommt Close erst nach dem konfigurierten
   Timeout, sofern der Timeout-Fallback aktiv ist. Ein Timeout beweist kein
   tatsächliches Loslassen der PTT-Taste.
7. Rauschen ausschalten und vergleichen: Sprache, Busy und normale Squelch-Töne
   müssen weiter funktionieren. Lautstärke 0 und einen eigenen Wert testen.
8. Nach bewusstem Container-Neuerstellen Einstellungen und Datei erneut prüfen;
   vorher sichern. Ergebnis und installierte Git-Revision notieren.

Die Docker-CI führt mit `--radio-noise` ausschließlich temporäre Testprojekte und
die mitgelieferte CC0-Aufnahme sowie eine programmatisch erzeugte eigene WAV aus: Import/Rechte, Ein-/Ausschalten, Lautstärke,
kurze/lange TCP-/verschlüsselte UDP-Streams, decodiertes Close, Busy-Priorität,
Sprachbytes, Sicherung/Restore und Neuerstellen. Ein optionales `--previous-image`
prüft zusätzlich einen echten Wechsel zwischen zwei lokalen Images samt Rollback.
Client-Jitterbuffer und tatsächlicher Klang bleiben Gegenstand der Hörtests.
