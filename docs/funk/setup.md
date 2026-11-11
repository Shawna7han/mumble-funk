> **Stable oder Dev?** Die Befehle unten installieren den bisherigen
> `stable`-Simplex-Kandidaten ohne neue Audiofunktionen. Für den aktuellen
> experimentellen Dev-Stand einschließlich Busy, Squelch und Funkrauschen gilt
> die [Dev-Anleitung](install-dev.md) mit `--experimental-dev`.
> [Aktuelle Audio-Defaults und Bestandsschutz](audio-defaults.md).

# Mumble-Funk mit wenigen Befehlen einrichten

Diese Anleitung richtet den **experimentellen Simplex-v0.2-Funkserver** unter
Ubuntu Server **24.04 LTS oder 26.04 LTS** ein. Ein vorhandener Standard-Mumble-Server
auf 64738 bleibt getrennt. Unser Dienst verwendet standardmäßig **64739 TCP und UDP**.
Mumla auf Android und Mumble auf Windows bleiben unverändert.

Das Setup installiert benötigte Werkzeuge, kompiliert, testet und verwendet danach
die vorhandene Installation mit systemd. Du musst keine Datei unter `/etc` öffnen.
Die [ausführliche technische Installationsanleitung](installation-ubuntu.md) bleibt
für den manuellen Ablauf und die Verzeichnisstruktur erhalten.

## Voraussetzungen

- Ubuntu Server 24.04 oder 26.04, einschließlich Punktversionen, mit laufendem systemd.
- Anmeldung als **normaler Benutzer mit sudo-Rechten**, beispielsweise per SSH.
  Nicht als root kompilieren und kein `sudo` vor `setup.sh` setzen. Das Setup
  fragt selbst nach dem sudo-Passwort, wenn Administratorrechte nötig sind.
- Internetzugang zu Ubuntu-Paketquellen und GitHub, auch für die Git-Submodule.
  Die Ubuntu-Paketquelle `universe` muss vorhanden sein. Bei Netzwerkproxys muss
  der Paketmanager auch bei Ausführung mit sudo darauf zugreifen können.
- Mindestens **2 GiB RAM einschließlich Swap** und **8 GiB freier Speicher** im
  Quellverzeichnis. Empfohlen: mindestens 4 GiB RAM, 2 CPU-Kerne und 10 GiB freier
  Speicher. Bei weniger als 4 GiB RAM baut das Setup standardmäßig mit einem Prozess.
- Der gewünschte Funkserver-Port ist frei, sofern dort nicht bereits eindeutig
  unser eigener Dienst läuft. Port 64738 bleibt für Standard-Mumble reserviert.

Das Kompilieren kann je nach VM mehrere Minuten dauern. Während der ersten
Installation ist noch kein Funkdienst vorhanden; bei einem Update läuft die
bisherige Version während Build und Tests weiter.

## Schnellinstallation auf einem neuen Server

Führe diese Befehle nacheinander aus. Bei Fehlern anhalten und die Meldung aufbewahren:

```bash
sudo apt update && sudo apt install -y git

git clone --branch stable --recurse-submodules \
  https://github.com/Shawna7han/mumble-funk.git

cd mumble-funk

bash scripts/funk/setup.sh
```

Existiert das Quellverzeichnis bereits, nicht darüber klonen oder es löschen.
Verwende den Abschnitt „Updates“ unten.

Das Setup führt folgende Schritte durch:

1. Ubuntu-Version, Git-Zweig, Ressourcen und systemd prüfen.
2. Fehlende Grundwerkzeuge installieren; vorhandene Installation und Port prüfen.
3. Fehlende Build-Abhängigkeiten über APT installieren und Submodule initialisieren.
4. Mit dem vorhandenen `build.sh` kompilieren und **alle 18 Servertestprogramme** ausführen.
5. Nur nach erfolgreichen Tests mit `install.sh` unseren eigenen Dienst einrichten
   beziehungsweise aktualisieren. Vor dem Austausch einer bestehenden Installation
   werden ihr Datenbankordner und ihre Konfiguration gesichert.
6. Dienststart, automatischen Start beim Hochfahren, TCP-/UDP-Listener des eigenen
   Prozesses und neue Simplex-Startmeldungen prüfen.
7. Verbindungsinformationen anzeigen. Das Passwort erscheint erst beim lokalen Abruf.

APT installiert kein Ubuntu-Paket `mumble-server`. Gemeinsame Bibliotheken können
regulär aktualisiert werden; automatische Neustarts durch `needrestart` werden für
den APT-Aufruf unterdrückt. Unsere Skripte stoppen ausschließlich `mumble-funk`.

### Wenn 64739 bereits belegt ist

Das Setup beendet keinen fremden Prozess und bricht ab. Für eine **neue**
Installation kannst du ausdrücklich einen anderen freien Port wählen:

```bash
bash scripts/funk/setup.sh --port 64740
```

Erlaubt sind 1024 bis 65535 außer 64738. Beide Protokolle verwenden denselben Port.
Eine bestehende Installation behält ihren bisherigen Port; ein abweichendes
`--port` wird abgewiesen und verändert die Konfiguration nicht.

## Zugangsdaten und Verbindung

Nach erfolgreichem Setup erscheint `MUMBLE-FUNK INSTALLATION ERFOLGREICH`.
Die Zusammenfassung zeigt Zustand, Simplex-Einstellung, lokale IP-Adressen und Port.
Alle Verbindungsdaten einschließlich Zugangspasswort kannst du jederzeit lokal abrufen:

```bash
sudo mumble-funk-info
```

Das Passwort ist bei Neuinstallationen zufällig erzeugt (128 Bit Zufall, 32
Hexadezimalzeichen). Es gibt kein fest eingebautes Standardpasswort. Es liegt nur
in der lokalen Serverkonfiguration und ihren geschützten Sicherungen, nicht im
Repository. Bewahre die Ausgabe privat auf. Bei Updates bleibt das Passwort erhalten.

Lege in Mumla oder Windows-Mumble eine neue Serververbindung an:

| Feld im Client | Eintrag |
| --- | --- |
| Name | Beispielsweise `Mumble-Funk` |
| Adresse | Vom Endgerät erreichbare IP-Adresse deiner Ubuntu-VM |
| Port | Angezeigter Port, normalerweise `64739` |
| Benutzername | Eigener frei gewählter Name |
| Passwort | Angezeigtes Zugangspasswort |

Das Zugangspasswort ist kein SuperUser-/Administratorkennwort. Die Installation
legt keinen neuen Administratorzugang mit festem Passwort an.
Ein neues selbstsigniertes Serverzertifikat kann beim ersten Verbinden eine
Clientnachfrage auslösen. Bestehende Zertifikate werden bei Updates erhalten.

Es können mehrere lokale IP-Adressen angezeigt werden. Wähle die Adresse, die von
deinem Smartphone oder PC erreichbar ist. Es wird keine öffentliche IP-Adresse
über einen externen Dienst ermittelt. Für Internetzugriff können eine öffentliche
Adresse oder ein DNS-Name und Routerweiterleitungen nötig sein.

**Das Setup öffnet keine Firewall und keinen Router.** Prüfe bei Bedarf Ubuntu-UFW,
Proxmox-Firewall und Router für den tatsächlich gewählten **TCP- und UDP-Port**.
Ein laufender Dienst bestätigt keine Erreichbarkeit durch externe Firewalls.

## Simplex-Einstellungen bei Neuinstallationen

Nur eine **neue Konfiguration über `setup.sh`** erhält automatisch:

| Einstellung | Wert |
| --- | --- |
| Simplex | aktiviert |
| Belegungstimeout `simplextimeout` | 500 ms |
| Paketruhe für neuen Versuch `simplexretrytimeout` | 500 ms |
| Simplex-Diagnose | aktiviert, alle drei Ereignisgruppen |
| Simplex-Log | `/var/log/mumble-funk/simplex.1.jsonl`, 10 MiB je Datei plus eine Rotation |
| Vorhandene PTT-/Sprachpaketdiagnose | ausgeschaltet |

Die Simplex-Engine selbst behält ihren bisherigen Standardwert von 1000 ms für
`simplexretrytimeout`, falls kein eigener Wert konfiguriert ist. Die manuelle
[Simplex-Anleitung](simplex.md) beschreibt weiterhin diese konservativere Einstellung.

**Bestehende Installationen werden nicht umkonfiguriert.** Ein bisher ausgeschalteter
Simplex-Modus bleibt aus; die Zusammenfassung weist darauf hin. Auch persönliche
Timeouts, Port, Passwort, Benutzer, Kanäle und Zertifikat bleiben erhalten.

Der Server kennt die physische PTT-Taste nicht. Bei 500 ms Paketruhe kann auch eine
Netzunterbrechung wie ein neuer Versuch aussehen. Für erste Gerätetests PTT loslassen,
zwei Sekunden warten und erneut drücken. [Verhalten und Grenzen](simplex.md#4-verhalten-und-bewusste-grenzen)
sowie der [Praxistestplan](simplex.md#6-praxistest-mit-android-und-windows) erklären dies genauer.

## Updates

Wechsle in dein vorhandenes Quellverzeichnis und prüfe zuerst den Zustand:

```bash
cd mumble-funk
git status --short --branch
```

Der Pfad hängt davon ab, wo du das Projekt heruntergeladen hast. Der Zweig muss
`stable` sein. Der bisherige Branch `funk-v0.1` bleibt ebenfalls unterstützt. Werden eigene Änderungen angezeigt, diese zuerst aufbewahren und
prüfen. Nichts mit `reset --hard` oder `clean` löschen.

Ohne lokale Änderungen:

```bash
git pull --ff-only origin stable
bash scripts/funk/setup.sh
```

`setup.sh` selbst führt kein Git-Pull durch und verwendet den vorhandenen Quellstand.
Es initialisiert fehlende Submodule, baut erneut und prüft alle Tests. Einstellungen
unter `/etc/mumble-funk` gehören nicht zum Git-Verzeichnis und werden erhalten.
Bei Fehlern vor der Bereitstellung wird die laufende Programmversion nicht ausgetauscht.
Erst die Installation stoppt kurz ausschließlich unseren Funkdienst.

Sicherungen liegen unter `/var/backups/mumble-funk/DATUM-PROZESSNUMMER/`, die
Programmversionen unter `/opt/mumble-funk/releases/`. Sicherungen enthalten Passwörter
und Zertifikate; sie sind nur für root lesbar und werden nicht automatisch gelöscht.
Die [Anleitung zur vorherigen Version](simplex.md#7-abschalten-oder-vorherige-version-wiederherstellen)
gilt weiterhin. Ein Fehler beim Start nach dem Austausch wird gemeldet; es gibt
keine automatische Wiederherstellung von Programm oder Datenbank.

## Fehlerbehebung

| Meldung / Problem | Nächster Schritt |
| --- | --- |
| Betriebssystem nicht unterstützt | `/etc/os-release` prüfen; Ubuntu 24.04 und 26.04 werden gezielt zugelassen. |
| Als root gestartet | Mit deinem normalen sudo-Benutzer anmelden und Setup ohne vorangestelltes sudo starten. |
| Speicher reicht nicht | VM-RAM/Swap oder freien Datenträgerplatz erweitern. Optional mit `FUNK_BUILD_JOBS=1 bash scripts/funk/setup.sh` bauen. |
| APT findet Pakete nicht | Internet, Ubuntu-Paketquellen und `universe` prüfen; ausführliche Ubuntu-Anleitung verwenden. |
| Paket-/Git-Download scheitert | Netzwerk/Proxy prüfen und Setup anschließend wiederholen. Keine erfolgreiche Installation wird behauptet. |
| Build oder Tests fehlschlagen | Angezeigte Fehlermeldung aufbewahren; die Installation wird nicht ausgeführt. |
| Port durch fremden Prozess belegt | Ursache klären oder bei Neuinstallation ausdrücklich einen freien Alternativport wählen. Keinen fremden Dienst beenden. |
| Bestehender Port soll verändert werden | Setup führt keine Portmigration einer bestehenden Installation durch. |
| Simplex wird als deaktiviert angezeigt | Eine bestehende Einstellung wurde erhalten. Die separate Simplex-Anleitung beschreibt die bewusste Aktivierung. |
| Verbindung von außen scheitert | Richtige VM-Adresse, gewählten Port, TCP und UDP sowie alle Firewalls/Router prüfen. |

Für Startprobleme:

```bash
sudo systemctl --no-pager --full status mumble-funk
sudo journalctl -u mumble-funk -b --no-pager -n 50
sudo tail -n 80 /var/log/mumble-funk/server.log
```

Zur Simplex-Diagnose:

```bash
sudo tail -F /var/log/mumble-funk/simplex.1.jsonl
```

`Strg+C` beendet nur die Anzeige. Die Diagnose enthält Benutzernamen und andere
Metadaten, aber keine Sprachinhalte. Auszüge vor dem Teilen auf persönliche Daten prüfen.

## Dienst sicher entfernen, Daten behalten

Diese Befehle erst verwenden, wenn du unseren Funkdienst entfernen möchtest.
Der Standard-Mumble-Dienst wird nicht angesprochen:

```bash
sudo systemctl disable --now mumble-funk.service
sudo bash -c 'umask 077; tar -czf /var/backups/mumble-funk-removal-$(date -u +%Y%m%dT%H%M%S).tar.gz -C / etc/mumble-funk var/lib/mumble-funk'
sudo rm -- /etc/systemd/system/mumble-funk.service /etc/logrotate.d/mumble-funk /usr/local/sbin/mumble-funk-info
sudo systemctl daemon-reload
```

Bei einer Fehlermeldung anhalten. Konfiguration, Datenbank, Logs, Programmversionen,
Sicherungen und Systembenutzer `mumble-funk` bleiben absichtlich erhalten. So sind
eine spätere Neuinstallation und das Wiederverwenden der Daten möglich.
Entferne diese Daten erst nach externer Sicherung und nur, wenn du sie endgültig
nicht mehr benötigst. Gemeinsame APT-Abhängigkeiten nicht pauschal deinstallieren.

## Grenzen und Prüfstand

Mumble-Funk bleibt ein experimenteller Alpha-Prototyp. Das Setup kann lokale
Voraussetzungen und den gestarteten Prozess prüfen, aber weder die Verbindung
jedes Endgeräts noch einen Systemstart deiner VM vorwegnehmen. Nach Installation
ist ein Gerätetest und später ein Neustarttest sinnvoll.

Automatische Portmigration, externe Datenbanken, fremde Datenbankpfade und
ungewöhnliche mehrdeutige INI-Darstellungen werden nicht automatisch verarbeitet.
Das Setup bricht in solchen Fällen ab, statt bestehende Daten zu verändern.

Die tatsächlich ausgeführten Prüfungen einschließlich der Grenzen der
Containerumgebung stehen im [Setup-Prüfbericht](SETUP-STATUS.md).

## Unterstützte Quellstände und Wechsel auf Stable

Das neue Setup akzeptiert unveränderte Checkouts von `stable` oder `funk-v0.1`,
deren HEAD dem jeweiligen lokalen `origin`-Referenzstand entspricht. Vorher `git fetch origin` verwenden. `origin` muss auf `Shawna7han/mumble-funk` zeigen. `dev` benötigt ausdrücklich `--experimental-dev` und einen mit dem lokalen
`origin/dev` sowie dem live veröffentlichten GitHub-Stand identischen Commit.
Die bekannten Audio-Feature-Branches benötigen weiterhin ihre jeweiligen
Opt-in-Schalter. Andere Feature-Branches, lokale Zusatzcommits und unbekannte
Quellstände bleiben gesperrt. Das ist
eine Bedienungsschutzprüfung, kein Ersatz für eine kryptografische Signatur.

Bei einer vorhandenen Installation zuerst die eigene Konfiguration/Daten sichern
(Details in der [Ubuntu-Anleitung](installation-ubuntu.md)). Im sauberen bisherigen
Quellverzeichnis, als normaler Benutzer:

```bash
git status --short --branch
git fetch origin
git switch --track origin/stable
bash scripts/funk/setup.sh
sudo mumble-funk-info
```

Falls `stable` lokal bereits existiert, stattdessen `git switch stable` und
`git pull --ff-only origin stable` verwenden. Bei eigenen Änderungen oder Fehlern
anhalten. Das Setup baut und testet vor dem bewussten Austausch des eigenen
Funkdienstes; Dienstname und alle bisherigen Datenpfade bleiben gleich. Bestehende
Werte werden nicht auf die Neuinstallationswerte zurückgesetzt.

Für künftige freigegebene Versionen unterstützt **dieser neue Installer** auch
`git switch --detach funk-vX.Y.Z`: Er verlangt einen annotierten Versionstag am
HEAD, der auf GitHub veröffentlicht ist und dort denselben Commit bezeichnet.
Dazu ist beim Setup Internetzugang nötig. Unveröffentlichte, leichte und Archiv-Tags
werden abgewiesen. Jetzt wird kein neuer Tag erstellt; `funk-v0.2.0` enthält noch
den alten Installer und keine Docker-Dateien. Diese neue Auswahlfunktion wird
nicht nachträglich in den alten Tag eingebaut.
