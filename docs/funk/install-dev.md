# Experimentellen Dev-Stand unter Ubuntu testen

`dev` ist eine **experimentelle Entwicklungsversion**, keine Stable-Freigabe.
Diese Anleitung verwendet den veröffentlichten `origin/dev` mit dem ausdrücklichen
Schalter `--experimental-dev`. Seit dem Merge von PR #9 enthält Dev auch
kontinuierliches [Funkrauschen](radio-noise.md) und den gemeinsamen
Open/Noise/Close-Effektstream. Ein Wechsel auf einen Feature-Branch ist dafür
nicht erforderlich. Historische Feature-Branches behalten ihre eigenen Opt-ins.

Verwende eine eigene Ubuntu-24.04- oder Ubuntu-26.04-LTS-Test-VM mit systemd und
einem normalen Benutzer mit sudo-Rechten. Empfohlen: 4 GiB RAM, 2 CPU-Kerne,
10 GiB freier Speicher und Internetzugang. Weitere Voraussetzungen stehen in
[setup.md](setup.md#voraussetzungen). Einen sauberen Proxmox-Snapshot vorab selbst
anlegen; die Skripte setzen keine VM zurück.

## Frische Test-VM installieren

Als normaler Benutzer, nacheinander ausführen und bei Fehlern anhalten:

```bash
sudo apt update && sudo apt install -y git

git clone --branch dev --single-branch --recurse-submodules \
  https://github.com/Shawna7han/mumble-funk.git mumble-funk-dev
cd mumble-funk-dev

bash scripts/funk/setup.sh --experimental-dev

sudo mumble-funk-info
```

Nicht in ein bereits vorhandenes Quellverzeichnis klonen. Das Setup installiert
Abhängigkeiten, baut den Server, führt die Servertests aus und installiert erst
bei Erfolg. Der normale Dienst heißt **`mumble-funk`**, der Port ist **64739 TCP/UDP**.
Es gibt keinen zusätzlichen Preview-Dienst oder Preview-Port. Die vorhandenen
Audio-Standardeinstellungen werden nur beim Erzeugen einer fehlenden INI
gesetzt: Simplex, AudioFeedback, Busy, Open/Close, Timeout-Close und Funkrauschen
aktiv; Busy/Open/Close bei 50 %, deren Boost +8 dB und Rauschen bei 20.
Alle vier CC0-WAVs werden unter `/usr/local/share/mumble-funk/audio/` installiert.
Vorhandene INIs einschließlich fehlender Schlüssel, eigene WAVs und bewusst
deaktivierte Effekte bleiben erhalten. [Genaue Vorgaben](audio-defaults.md).
Zur Verbindung mit Windows-Mumble und
Android-Mumla siehe [Zugangsdaten und Verbindung](setup.md#zugangsdaten-und-verbindung).
Das Passwort aus `mumble-funk-info` privat aufbewahren.

**Achtung:** Auf einer VM mit vorhandenem `mumble-funk` aktualisiert das Setup
diesen Dienst nach erfolgreichem Build und Tests; es erstellt keine parallele
Instanz. Auf anderen VMs oder laufenden Produktivservern diese Befehle nicht für
einen unbeabsichtigten Test ausführen. Firewall und Router werden nicht verändert.

## Welche Schutzprüfungen erhalten bleiben

- Mumble-1.5.939-Abstammung und unveränderte versionierte Dateien.
- `origin` muss das offizielle Repository `Shawna7han/mumble-funk` bezeichnen.
- Der ausgecheckte Branch muss `dev` heißen; nur `--experimental-dev` erlaubt ihn.
- Die vollständige lokale Commit-ID muss sowohl `refs/remotes/origin/dev` als auch
  dem **live abgefragten** GitHub-Branch `refs/heads/dev` entsprechen.
- Fehlende Referenzen, lokale Zusatzcommits, ein veralteter Dev-Stand und ein
  fehlgeschlagener GitHub-Zugriff führen zum Abbruch. Ein anderer Opt-in-Schalter
  erlaubt `dev` nicht. Keine falsche Stable-Kennzeichnung und kein automatisches Pull.
- Ubuntu-Version, Ressourcen, Ports, vorhandene Datenpfade und Dienstzuordnung
  werden weiterhin geprüft. Stable, `funk-v0.1`, Versionstags und die bekannten
  Feature-Opt-ins behalten ihre bisherigen Regeln.

Wenn `dev` während des Builds weiterentwickelt wird, kann die erneute Prüfung vor
der Installation bewusst abbrechen. Den Quellstand dann prüfen und aktualisieren,
statt die Prüfung zu umgehen. Die Herkunftsprüfung ist eine Bedienungssicherung,
keine kryptografische Signaturprüfung.

Die Ausgabe nennt `EXPERIMENTELL` und die Quellrevision. Nach der Installation
steht in `/opt/mumble-funk/current/BUILD-INFO` unter anderem `branch=dev` und
`experimental=true`.

## Später auf den aktuellen Dev-Stand aktualisieren

Im oben angelegten Dev-Verzeichnis:

```bash
cd ~/mumble-funk-dev
git status --short --branch
```

Nur fortfahren, wenn der Branch `dev` ist und keine eigenen Änderungen angezeigt
werden. Eigene Änderungen zuerst separat sichern; kein `reset --hard` oder `clean`.
Vor einem Update den VM-Snapshot bzw. eine externe Sicherung selbst erstellen.

```bash
git fetch origin refs/heads/dev:refs/remotes/origin/dev
git merge --ff-only origin/dev
bash scripts/funk/setup.sh --experimental-dev
```

Die explizite Fetch-Referenz aktualisiert auch den benötigten lokalen Remote-Stand.
Das Setup erhält bestehende INI-Einstellungen einschließlich fehlender Einträge,
Passwörter, Datenbanken, Zertifikate und eigene WAV-Auswahl. Es migriert keine
Audio-Einstellungen. Vor dem Austausch werden Konfiguration und Datenbankordner
unter `/var/backups/mumble-funk/` gesichert; nur der eigene Funkdienst wird bei der
Bereitstellung kurz gestoppt. Sicherungen enthalten Geheimnisse und gehören nicht
ins Repository. [Sicherung und Rückkehr zur vorherigen Version](setup.md#updates).

Diese Anleitung wechselt eine bestehende Stable-Installation nicht automatisch
auf `dev`. Für den ersten Versuch die separate Snapshot-Test-VM verwenden.

## Manueller Build und Installation

Falls die Abhängigkeiten bereits eingerichtet sind, akzeptieren auch die einzelnen
Skripte denselben Schalter und prüfen die Quelle erneut:

```bash
bash scripts/funk/build.sh --experimental-dev
sudo bash scripts/funk/install.sh --experimental-dev
```

Für den normalen vollständigen Ablauf einschließlich Voraussetzungen und Startprüfung
weiterhin `setup.sh --experimental-dev` verwenden. Keine Audioarchitektur oder
Simplex-Regel wird durch diesen Schalter geändert.

## Prüfung nach der Installation

Die gemeinsame [Übersicht der Status- und Verwaltungsbefehle](ubuntu-commands.md)
erklärt Verbindungsinformationen mit/ohne Passwort, Audio-Konfiguration, Dienst,
installierten Git-Stand und Serverlog. Sie gilt auch für die normale Installation
auf der Snapshot-Test-VM. `sudo mumble-funk-info` zeigt das Zugangspasswort;
für eine Ausgabe ohne Passwort `sudo mumble-funk-info --hide-password` verwenden.

Diese Anleitung installiert `dev` einschließlich Funkrauschen; dessen
[Audioverwaltung und Hörtest](radio-noise.md) verwenden dieselbe normale Installation.
Für Docker gilt die [Dev-Docker-Anleitung](docker.md), ebenfalls ohne neue Audioports.

Mit Windows-Mumble und Android-Mumla Sprache, Simplex, Busy, Open, Rauschen, Close
und Timeout-Close testen. Ein erfolgreiches Ende mit Marker darf keinen zweiten
Timeout-Close auslösen. Für Status, WAV-Auswahl und Lautstärken die
[gemeinsame Befehlsübersicht](ubuntu-commands.md) verwenden.
Automatisierte Installer-Prüfungen ersetzen diese Geräte-Hörtests nicht.
