# Bestehende Ubuntu-Testinstallation auf Analog Voice aktualisieren

Nur für die eigene Proxmox-Test-VM (Ubuntu 24.04/26.04 LTS, normaler Benutzer mit
sudo). Experimentell, keine Produktivfreigabe. B6B führt der Betreiber später
selbst durch. Diese Anleitung wurde hier nicht auf einem externen Server ausgeführt.

## Vorbereiten und Quellen wechseln

Vorab in Proxmox selbst einen konsistenten Snapshot und zusätzlich eine externe
Sicherung der Konfiguration, Datenbank und eigenen WAVs erstellen. Sicherungen
enthalten Passwörter und Zertifikate; privat aufbewahren. Mindestens 8 GiB freien
Platz für den Build vorsehen (10 GiB empfohlen). Die bestehende Installation
verwendet weiterhin Dienst `mumble-funk`, gewöhnlich Port 64739 TCP/UDP.

Im bisherigen sauberen Dev-Quellverzeichnis, ohne vorangestelltes sudo:

```bash
git status --short --branch
git remote -v
```

Bei eigenen Änderungen oder unbekannten unversionierten Dateien anhalten und diese
separat sichern. Kein reset/clean verwenden. `origin` muss das offizielle
`https://github.com/Shawna7han/mumble-funk.git` sein. Dann:

```bash
git fetch origin refs/heads/feature/analog-voice-live:refs/remotes/origin/feature/analog-voice-live
git switch --no-track -c feature/analog-voice-live origin/feature/analog-voice-live
```

Falls der lokale Feature-Branch schon existiert, stattdessen
`git switch feature/analog-voice-live` und anschließend
`git merge --ff-only origin/feature/analog-voice-live` verwenden. Keine neue lokale
Entwicklung oder Zusatzcommits auf dem Installationscheckout durchführen.

```bash
git status --short --branch
git rev-parse HEAD
git rev-parse origin/feature/analog-voice-live
bash scripts/funk/setup.sh --experimental-analog-voice
```

Der explizite Fetch funktioniert auch aus einem früheren `--single-branch`-Clone.
Das Setup prüft erneut offiziellen Origin, exakten Branch, saubere Quellen und
Gleichheit von HEAD, lokalem Remote-Ref und live veröffentlichtem Commit. Jeder
Fehler beendet das Setup. Bei zwischenzeitlichem Branchfortschritt erst prüfen
und erneut fetchen; die Schutzprüfung nicht umgehen.

Alternativ bei bereits vorbereiteten Abhängigkeiten:

```bash
bash scripts/funk/build.sh --experimental-analog-voice
sudo bash scripts/funk/install.sh --experimental-analog-voice
```

Nur nach erfolgreichem Build und CTest den zweiten Befehl ausführen. Das normale
Setup automatisiert diese Reihenfolge. Nicht `sudo setup.sh` verwenden.

## Was beim Update erhalten bleibt

Es ist ein Update desselben Dienstes, keine zusätzliche Instanz. Die vorhandene
INI `/etc/mumble-funk/server.ini`, SQLite-Zustände unter `/var/lib/mumble-funk`,
Benutzer/Kanäle/ACLs, Passwörter, Zertifikate, Port, eigene WAVs und Audioeinstellungen
werden nicht neu initialisiert. Bestehende Konfigurationen werden nicht migriert.

Vor dem Umschalten hält der Installer ausschließlich den eigenen Dienst an und
sichert Konfiguration und den vollständigen Zustandsordner unter
`/var/backups/mumble-funk/<Zeitstempel>-<PID>/config` bzw. `state`. Den ausgegebenen
Sicherungspfad notieren. Bei abweichender eigener Datei auf einem neu paketierten
Standard-WAV-Pfad bricht der bestehende Schutz ab; Aufnahme separat sichern und
Konflikt bewusst klären. Keine WAV-Datei zur Umgehung löschen.

Programmversionen liegen unter `/opt/mumble-funk/releases/<Binary-SHA256>/`.
`current` bezeichnet die aktive, `previous` die vorherige unterschiedliche
Binary-Version. `BUILD-INFO` nennt Revision, Branch und `experimental=true`.
Build-/Testfehler führen nicht zum Austausch der laufenden Installation.

## Version prüfen und Filter bewusst aktivieren

Die Installation schaltet Analog Voice nicht selbst ein. Ein fehlender Schlüssel
oder `analogvoice=false` behält den Direktpfad. Zuerst prüfen:

```bash
sudo systemctl --no-pager --full status mumble-funk
sudo mumble-funk-info --hide-password
sudo cat /opt/mumble-funk/current/BUILD-INFO
sudo readlink -f /opt/mumble-funk/current/mumble-server
sudo readlink -f /proc/$(systemctl show -p MainPID --value mumble-funk)/exe
```

Installierte Revision mit `git rev-parse HEAD` vergleichen; der laufende
`/proc/.../exe` muss zur gewählten Release-Datei passen. Keine Passwörter/INI-Inhalte
in öffentliche Fehlerberichte kopieren.

Für den bewussten Test die INI zuerst privat sichern und mit dem vorhandenen Editor
bearbeiten. Vorhandene Schlüssel ändern, keine widersprüchlichen Duplikate anhängen:

```bash
sudo cp -a /etc/mumble-funk/server.ini /etc/mumble-funk/server.ini.before-analog-test
sudoedit /etc/mumble-funk/server.ini
```

```ini
simplex=true
analogvoice=true
analogvoiceprofile=cb-am
```

Profile: `cb-am`, `business-fm`, `narrowband`. Je Änderung Neustart erforderlich.
Bestehende Squelch-/Busy-/Noise-Schalter, WAV-Pfade und Lautstärken bewusst prüfen
und beibehalten; der Installer setzt sie beim Update nicht zurück.

```bash
sudo systemctl restart mumble-funk
sudo systemctl --no-pager --full status mumble-funk
sudo journalctl -u mumble-funk -b --no-pager -n 80
sudo tail -n 80 /var/log/mumble-funk/server.log
sudo mumble-funk-audio status
```

Im Log die erfolgreiche experimentelle Analog-Initialisierung mit dem gewählten
Profil prüfen. Eine Warnung `Analog voice disabled` bedeutet keinen aktiven Filter.
Anschließend führt der Betreiber selbst die B6B-Hörtests mit Mumble/Mumla durch:
Sprache, Open/Noise/Close, Busy, rascher PTT-Wechsel und Timeout. Serverseitige
Übergabe allein beweist keine hörbare Client-Wiedergabe.

## Rückkehr bei Problemen

Zum Abschalten des Filters `analogvoice=false` setzen und den eigenen Dienst
neu starten. Für einen Programmrollback zuerst beide Release-Ziele prüfen:

```bash
sudo readlink -f /opt/mumble-funk/current
sudo readlink -f /opt/mumble-funk/previous
```

Nur fortfahren, wenn `previous` eine erhaltene und ausführbare Version unter
`/opt/mumble-funk/releases/` bezeichnet. Den bisherigen Current-Pfad notieren.

```bash
sudo systemctl stop mumble-funk
sudo bash -eu -c 'cd /opt/mumble-funk; target=$(readlink previous); test -x "$target/mumble-server"; ln -sfn -- "$target" current.rollback; mv -Tf current.rollback current'
sudoedit /etc/mumble-funk/server.ini
```

Den Analogschalter deaktivieren oder die eigene vor dem Test gesicherte INI
bewusst zurückspielen. Datenbank nicht löschen oder neu anlegen. B6A ändert kein
Datenbankschema. Eine notwendige vollständige Wiederherstellung der aufgezeichneten
Installer-Sicherung erfolgt ausschließlich bei gestopptem Dienst und nach erneuter
privater Sicherung des aktuellen Zustands; keine alten Daten blind überschreiben.

```bash
sudo systemctl start mumble-funk
sudo systemctl --no-pager --full status mumble-funk
sudo cat /opt/mumble-funk/current/BUILD-INFO
sudo mumble-funk-info --hide-password
```

Bei Fehlern angehalten lassen und Logs prüfen. Keine Neuinstallation, Portmigration
oder Passwortneuvergabe zur Fehlerumgehung durchführen.
