> **Diese technische Anleitung bezieht sich auf den bisherigen Stable-Kandidaten.**
> Für den aktuellen experimentellen Dev-Stand mit integrierter Audio-Engine,
> Busy, Squelch, Boost und Funkrauschen die [Dev-Anleitung](install-dev.md)
> mit `setup.sh --experimental-dev` verwenden.
> [Audio-Defaults](audio-defaults.md) · [Funkrauschen und WAV-Verwaltung](radio-noise.md).
> Bestehende Einstellungen und eigene Dateien bleiben bei Updates erhalten.

# Mumble-Funk parallel unter Ubuntu Server 24.04 oder 26.04 installieren

Diese Anleitung installiert unseren gemeinsamen Kandidaten `stable` auf Basis von
Mumble 1.5.939. Der vorhandene Standardserver auf **64738** bleibt bestehen.
Der zusätzliche Server verwendet **64739 für TCP und UDP**. Beim hier beschriebenen manuellen Weg ist die optionale
Simplex-Sendesteuerung standardmäßig ausgeschaltet. Aktivierung, Update und
Praxistest stehen in der [Simplex-Anleitung](simplex.md). Mumla und Windows-Mumble
bleiben unverändert.

**Einfachster Einstieg:** Die neue [automatische Setup-Anleitung](setup.md) erledigt
Abhängigkeiten, Build, Tests und Einrichtung ohne manuelles Bearbeiten der INI.
Bei einer Neuinstallation aktiviert sie Simplex mit 500/500 ms. Diese ausführliche
Anleitung bleibt als manueller und technischer Weg erhalten.

Die folgenden Befehle führst du **später auf deiner Ubuntu-VM** aus, beispielsweise
nach der Anmeldung per SSH. Diese Vorbereitung installiert dort noch nichts.
Führe die Blöcke der Reihe nach aus. Bei einer Fehlermeldung nicht mit dem nächsten
Schritt fortfahren, sondern die Meldung aufbewahren.

## Voraussetzungen

- Ubuntu Server **24.04 LTS oder 26.04 LTS**, einschließlich ihrer Punktversionen
  wie 26.04.1, mit systemd als normalem Dienstmanager. Die Prüfung verwendet
  `ID=ubuntu` und `VERSION_ID=24.04` bzw. `26.04` aus `/etc/os-release`.
  Andere Distributionen und Ubuntu-Versionen bleiben gesperrt.
- Ein normaler Benutzer mit `sudo`-Rechten; kein dauerhaftes Arbeiten als root.
- Internetzugang zu Ubuntu-Paketquellen und GitHub, einschließlich der Git-Submodule.
- Zum Bauen empfohlen: 2 CPU-Kerne, 4 GB RAM und mindestens 10 GB freier Speicher.
  Mit weniger RAM kann `FUNK_BUILD_JOBS=1 bash scripts/funk/build.sh` helfen.
- Port 64739 ist frei. TCP **und** UDP müssen zwischen Endgeräten und VM erlaubt sein.
- Die Paketquelle **universe** ist aktiviert; sie ist bei Ubuntu Server normalerweise
  vorhanden. Falls sie fehlt, siehe den Zusatz unter Schritt 2.
- Du kennst die IP-Adresse deiner Ubuntu-VM. Für den ersten Versuch sollten die
  Android-/Windows-Geräte im selben Netz sein.

Vor größeren Änderungen an der VM ist ein Proxmox-Backup sinnvoll. Die Skripte
installieren kein Ubuntu-Paket `mumble-server` und verwenden nicht `cmake --install`.
Sie ersetzen keine Dateien oder Dienste des Standardservers.

| Zweck | Standardserver | Neuer Funkserver |
| --- | --- | --- |
| Port | 64738, unverändert | 64739 TCP und UDP |
| Dienst | vorhandener Dienst, unverändert | `mumble-funk.service` |
| Programm | bisherige Installation | `/opt/mumble-funk/current/mumble-server` |
| Konfiguration | bisherige Datei | `/etc/mumble-funk/server.ini` |
| Datenbank und Zertifikat | bisherige Datenbank | `/var/lib/mumble-funk/server.sqlite` |
| Normales Serverlog | bisherige Logdatei | `/var/log/mumble-funk/server.log` |
| Diagnose | bisherige Einstellung | `/var/log/mumble-funk/voice.1.jsonl` |
| Systembenutzer | bisheriger Benutzer | `mumble-funk`, ohne interaktive Anmeldung |

Die beiden Server besitzen getrennte Benutzerkonten, Kanäle und Berechtigungen.
Ein Benutzerkonto des Standardservers wird nicht automatisch übernommen. Auch das
Serverzertifikat wird separat erzeugt. Eine neue Zertifikatsabfrage im Client ist
deshalb beim ersten Verbinden zu erwarten.

## Wenn nur die bisherige Ubuntu-Sperre aufgetreten ist

Du musst weder Ubuntu neu einrichten noch das Projekt erneut herunterladen.
Wenn der Server schon gebaut wurde und nur die Installation wegen Ubuntu 26.04
abgebrochen ist, aktualisiere dein vorhandenes Quellverzeichnis:

```bash
cd "$HOME/src/mumble-funk"
git status --short --branch
```

Verwende gegebenenfalls den Ordner, in den du das Projekt tatsächlich heruntergeladen
hast. Wenn unter der Zweigzeile eigene geänderte Dateien erscheinen, diese zuerst
prüfen und aufbewahren. Keine Dateien zurücksetzen oder löschen.

Anschließend diese Befehle nacheinander ausführen; bei einer Fehlermeldung anhalten:

```bash
git switch stable
git pull --ff-only origin stable
git submodule update --init --recursive
bash scripts/funk/build.sh
sudo bash scripts/funk/install.sh
sudo systemctl is-enabled mumble-funk
sudo systemctl is-active mumble-funk
```

Der Build verwendet vorhandene Ergebnisse unter `build/funk` weiter und führt
die 18 Servertestprogramme aus. Zusätzliche Bibliotheken musst du bei einem
bereits erfolgreichen Build normalerweise nicht nachinstallieren. Die bestehenden
Dateien unter `/etc/mumble-funk` und `/var/lib/mumble-funk` bleiben erhalten.
Zum Abschluss sollten `enabled` und `active` erscheinen. Der Entwicklungsserver
verwendet weiterhin 64739; der Standardserver bleibt auf 64738.

## 1. Ausgangslage prüfen

```bash
cat /etc/os-release
df -h /
free -h
sudo ss -lntup | grep -E ':64738\b|:64739\b' || true
```

64738 darf bereits belegt sein. **64739 muss frei sein.** Falls dort etwas läuft,
zunächst klären, was es ist; keinen bestehenden Dienst beenden.
Prüfe auch, dass der Standardserver mit Mumla weiterhin erreichbar ist.

## 2. Benötigte Werkzeuge installieren

Das sind Compiler, Bibliotheken und Hilfsprogramme, kein zweiter Ubuntu-Mumble-Dienst.
Für Ubuntu 24.04 und 26.04 gilt dieselbe Paketliste.
Ubuntu kann dabei gemeinsame Bibliotheken mit regulären Paketaktualisierungen
versorgen. Der bestehende Mumble-Dienst wird von unseren Skripten nicht angefasst.

```bash
sudo apt update
sudo apt install git build-essential cmake ninja-build pkg-config \
  qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libboost-dev \
  libssl-dev libprotobuf-dev protobuf-compiler libprotoc-dev libcap-dev \
  libavahi-compat-libdnssd-dev libzeroc-ice-dev openssl logrotate python3
```

Nur falls dabei Pakete aus `universe` nicht gefunden werden:

```bash
sudo apt install software-properties-common
sudo add-apt-repository --yes universe
sudo apt update
```

Danach den obigen `sudo apt install ...`-Befehl wiederholen.

## 3. Entwicklungszweig herunterladen und bauen

```bash
mkdir -p "$HOME/src"
cd "$HOME/src"
git clone --branch stable --recurse-submodules \
  https://github.com/Shawna7han/mumble-funk.git
cd "$HOME/src/mumble-funk"
git branch --show-current
bash scripts/funk/build.sh
```

Der angezeigte Zweig muss `stable` sein. Falls das Verzeichnis schon existiert,
nicht löschen oder darüber klonen: Verwende stattdessen die Update-Anleitung unten.
Das Bauen kann einige Minuten dauern. Das Skript prüft den Zweig und die Abstammung
vom offiziellen Release und führt anschließend die vorhandenen 18 Testprogramme aus.
Am Ende muss `100% tests passed` stehen. Es arbeitet ohne `sudo` und startet noch
keinen dauerhaften Server.

## 4. Eigenen Dienst installieren

```bash
cd "$HOME/src/mumble-funk"
sudo bash scripts/funk/install.sh
sudo systemctl is-enabled mumble-funk
sudo systemctl is-active mumble-funk
sudo ss -lntup | grep -E ':64738\b|:64739\b'
```

Erwartet werden `enabled` und `active`; beide Ports sollten sichtbar sein.
Der neue Dienst startet künftig automatisch beim Start der VM.
Das Skript legt nur die oben genannten eigenen Verzeichnisse sowie
`/etc/systemd/system/mumble-funk.service` und `/etc/logrotate.d/mumble-funk` an.
Beim Wiederholen erhält es deine vorhandene Konfiguration und Datenbank.
Die mitgelieferte systemd-Dienstdatei und die eigene Logrotate-Regel werden bei
Updates erneuert. Persönliche Servereinstellungen gehören in `server.ini`.

Für die erste Installation erzeugt es ein zufälliges Zugangspasswort. Anzeigen:

```bash
sudo grep '^serverpassword=' /etc/mumble-funk/server.ini
hostname -I
```

Das Passwort steht hinter dem Gleichheitszeichen. Verwahre es privat.
Dieses Passwort erlaubt die Verbindung, verleiht aber keine Administratorrechte.
Es lässt sich später mit `sudo nano /etc/mumble-funk/server.ini` ändern;
anschließend `sudo systemctl restart mumble-funk` ausführen.

## 5. Netzwerk und Clientverbindung

Falls du UFW bereits verwendest, prüfe zuerst dessen Zustand:

```bash
sudo ufw status
```

Nur bei **aktivem UFW** die beiden Regeln ergänzen:

```bash
sudo ufw allow 64739/tcp comment 'Mumble-Funk'
sudo ufw allow 64739/udp comment 'Mumble-Funk'
```

Aktiviere UFW nicht allein für diese Anleitung: Das könnte deinen SSH-Zugang
beeinträchtigen. Falls `ufw` nicht installiert ist, ist dieser Schritt nicht nötig;
prüfe stattdessen vorhandene Firewall-Regeln. Eine aktive Proxmox-Firewall muss
64739 ebenfalls für TCP und UDP erlauben. Für Zugriff aus dem Internet braucht
dein Router zusätzlich entsprechende Weiterleitungen zur IP der VM.

Lege in Mumla bzw. Windows-Mumble einen **zusätzlichen** Servereintrag an:

- Adresse: IP-Adresse deiner Ubuntu-VM.
- Port: **64739**.
- Benutzername: frei wählen.
- Passwort: das oben angezeigte Zugangspasswort.

Der bisherige Eintrag mit Port 64738 bleibt erhalten. Prüfe mit zwei Geräten, dass
beide sich am Funkserver hören und der Standardserver weiterhin erreichbar ist.

## 6. PTT-Diagnose einschalten

Die Diagnose ist zunächst ausgeschaltet. Einschalten:

```bash
sudo sed -i 's/^voicediagnostics=false$/voicediagnostics=true/' /etc/mumble-funk/server.ini
sudo systemctl restart mumble-funk
sudo grep '^voicediagnostics' /etc/mumble-funk/server.ini
sudo tail -f /var/log/mumble-funk/voice.1.jsonl
```

Nun mit Mumla verbinden, PTT drücken, sprechen, während gedrückter Taste schweigen
und PTT loslassen. Mit `Strg+C` beendest du nur die Live-Anzeige, nicht den Server.
Die Diagnose zeichnet Metadaten und Benutzernamen auf, keine Sprachinhalte.
Ein Timeout bedeutet ein **vermutetes** Ende; nur eine empfangene Ende-Markierung
bestätigt das Ende des beobachteten Sprachstroms. Sie beweist keinen physischen
Tastendruck. Ausführliche Interpretation und Gerätetests: [Diagnose-Anleitung](diagnose.md).

Die Einstellung `voicediagnosticstimeout=500` bedeutet 500 Millisekunden ohne
Sprachpaket. Für erste Vergleichstests den Wert unverändert lassen.
Für den ersten virtuellen Server lautet die Datei `voice.1.jsonl`; zusätzliche
virtuelle Server erhalten eine andere Nummer. Pro Datei wird bei 10 MiB rotiert,
mit einer Sicherung `voice.1.jsonl.1`.

Ausschalten, ohne gespeicherte Ergebnisse zu löschen:

```bash
sudo sed -i 's/^voicediagnostics=true$/voicediagnostics=false/' /etc/mumble-funk/server.ini
sudo systemctl restart mumble-funk
```

## 7. Spätere Updates

Führe diesen Block im vorhandenen Quellverzeichnis aus:

```bash
cd "$HOME/src/mumble-funk"
git status --short --branch
git switch stable
git pull --ff-only origin stable
git submodule update --init --recursive
bash scripts/funk/build.sh
sudo bash scripts/funk/install.sh
```

Bei eigenen Änderungen oder einem Git-Fehler zuerst anhalten; nichts mit `reset --hard`
oder `clean` löschen. Änderungen an `/etc/mumble-funk/server.ini` sind unabhängig
vom Git-Quellverzeichnis und bleiben erhalten.

Während Download, Build und Tests läuft der bisherige Funkserver weiter. Erst beim
Installieren wird **nur `mumble-funk`** kurz gestoppt und neu gestartet. Der Standardserver
bleibt in Betrieb. Vor dem Austausch wird der komplette Datenbankordner samt
Konfiguration unter `/var/backups/mumble-funk/DATUM-PROZESSNUMMER/` gesichert.
Die Sicherungen enthalten auch das Serverzertifikat und Passwörter; sie sind nur
für root lesbar. Sie werden nicht automatisch gelöscht und benötigen auf Dauer
Speicherplatz.

Die Programmversionen liegen getrennt unter `/opt/mumble-funk/releases/`, benannt
nach der SHA-256-Prüfsumme des Programms. `current` zeigt auf die aktive Version,
`previous` nach einem tatsächlichen Versionswechsel auf die vorherige.
Wiederholtes Installieren derselben Version überschreibt das laufende Programm nicht.

Falls die letzte neue Programmversion Probleme macht, kannst du auf die vorherige
zurückschalten; das stellt **nur das Programm**, keine alte Datenbank wieder her:

```bash
sudo test -x /opt/mumble-funk/previous/mumble-server && \
sudo bash -c 'set -e; systemctl stop mumble-funk; ln -sfn "$(readlink /opt/mumble-funk/previous)" /opt/mumble-funk/current.rollback; mv -Tf /opt/mumble-funk/current.rollback /opt/mumble-funk/current; systemctl start mumble-funk'
```

Falls keine vorherige Version existiert, tut der Befehl nichts. Er ist für Updates
innerhalb unserer jetzigen Mumble-Basis gedacht. Bei künftigen Änderungen am
Datenbankformat muss eine Wiederherstellung gesondert geplant werden.

## Betrieb und Fehlersuche

```bash
# Zustand und Fehler beim Start
sudo systemctl --no-pager --full status mumble-funk
sudo journalctl -u mumble-funk -b --no-pager -n 50

# Normales Serverlog, einschließlich Meldungen zur Diagnose
sudo tail -n 80 /var/log/mumble-funk/server.log

# Nur unseren Funkserver stoppen bzw. wieder starten
sudo systemctl stop mumble-funk
sudo systemctl start mumble-funk
```

Normale Servermeldungen werden von systemd in `server.log` geschrieben. Logrotate
bewahrt vier ältere Dateien auf; die Rotation erfolgt wöchentlich oder bei der
nächsten Prüfung oberhalb 10 MiB. Wegen `copytruncate` können im kurzen Moment
einer Rotation einzelne normale Logzeilen verloren gehen. Diagnosedateien rotieren
unabhängig davon innerhalb der bereits vorhandenen Diagnosefunktion.

`systemctl is-active` bestätigt einen laufenden Prozess. Ob Geräte den Server
über alle Firewalls erreichen und Sprachpakete austauschen, zeigt erst der
Verbindungstest mit deinen Endgeräten. Ein vollständiger Neustarttest der VM
bleibt nach der späteren Installation sinnvoll.

## Frühere Prüfung der Installationslösung (vor Simplex v0.2)

Die folgenden Ergebnisse betreffen den Stand vor Simplex. Der neue Prüfstand
mit 18 Server-Testprogrammen ist in [SIMPLEX-STATUS.md](SIMPLEX-STATUS.md) dokumentiert.

Am 17. Oktober 2026 wurde die Lösung in frischen Containern für **Ubuntu 24.04
LTS und Ubuntu 26.04.1 LTS (amd64)** mit den oben genannten Build-Abhängigkeiten
geprüft. Die Paketnamen gelten für beide Versionen; zusätzliche Paketquellen
außer den regulären Ubuntu-Quellen einschließlich `universe` sind nicht nötig.

| Testumgebung | CMake | Qt | systemd |
| --- | --- | --- | --- |
| Ubuntu 24.04 | 3.28.3 | 5.15.13 | 255.4 |
| Ubuntu 26.04.1 | 4.2.3 | 5.15.18 | 259.5 |

In **beiden** Umgebungen erfolgreich:

- Vollständiger Neubau mit `scripts/funk/build.sh`; alle **16 Servertestprogramme**
  erfolgreich.
- Alle **fünf Installationstests** erfolgreich, einschließlich paralleler
  Serverprozesse auf 64738 und 64739, getrennten Datenbanken und Diagnoseausgaben.
- Betriebssystemprüfung mit zehn Kombinationen: ausschließlich Ubuntu 24.04 und
  26.04 zugelassen; andere bzw. fehlende Kennungen abgewiesen. Das tatsächliche
  `/etc/os-release` im 26.04.1-Container enthält wie erwartet `VERSION_ID=26.04`.
- Shell-Syntax und ShellCheck ohne Befund.
- Tatsächliche Dateianlage in den vorgesehenen Systemverzeichnissen der Container,
  separater Systembenutzer und erfolgreicher Serverstart unter diesem Benutzer.
- Wiederholte Installation erhält die Konfiguration und sichert die vorhandene
  Datenbank; der Verweis auf eine vorherige Programmversion wird korrekt erhalten.
- Dienstdatei mit `systemd-analyze verify` und Logrotate-Regel im Prüfmodus geprüft.
  Die Dienstdatei, die INI-Vorlage und die Logrotate-Regel wurden inhaltlich nicht
  geändert; sie liegen im Quellcode jetzt gemeinsam unter `scripts/funk/ubuntu`.

In den Containern lief systemd nicht als Betriebssystem-Dienstmanager. Die
`systemctl`-Aufrufe wurden dort für den Test durch protokollierende Platzhalter
ersetzt. Ein tatsächliches Aktivieren, Stoppen und Starten durch systemd sowie
der Start beim Hochfahren der VM sind damit **noch nicht praktisch nachgewiesen**.
Das muss nach der späteren Installation auf deiner VM überprüft werden.
Dein Proxmox-Server und deine Endgeräte wurden bei diesen Tests nicht verwendet.
Der Server-Funktionscode wurde für diese Installationslösung nicht geändert.
