> **Geltungsbereich:** Diese Anleitung beschreibt den bisherigen Stable-Stand,
> dessen Host-Fallback 64739 war. Der neue Entwicklungsstand
> `feature/docker-port-default` verwendet 64740. Vor einem späteren Wechsel auf
> diesen Stand den tatsächlichen bisherigen Port ausdrücklich festhalten; siehe
> [Bestandsschutz beim Update](docker.md#9-aktualisieren). Der Vergleich unten
> muss bei einer unerwarteten Portänderung anhalten. Keine `.env` ersetzen.

# Bestehenden Docker-Server auf Stable umstellen

Diese Schritte führt der Kollege **selbst auf seinem Docker-Host** aus. Sie werden
nicht automatisch ausgeführt. Der Stable-Kandidat enthält Simplex v0.2 auf Mumble
1.5.939; Audio-Feedback ist nicht enthalten. Die abschließende Praxistest-Freigabe
steht aus. Bei jeder Fehlermeldung anhalten und die Ausgabe aufbewahren.

## 1. Im bisherigen Verzeichnis bleiben und prüfen

Den bisherigen Ordner verwenden, nicht eine zweite Installation daneben anlegen:

```bash
cd /pfad/zum/bisherigen/mumble-funk-docker
git status --short --branch
docker compose ps
docker compose port --protocol tcp mumble-funk 64739
docker compose port --protocol udp mumble-funk 64739
```

Der Pfad ist durch den tatsächlichen Ordner zu ersetzen. Vor dem Wechsel dürfen
keine eigenen Änderungen an versionierten Dateien vorliegen. `.env` ist absichtlich
nicht versioniert und bleibt erhalten. Die bisherigen Veröffentlichungen sollten
für den Kollegen **64740 TCP und UDP** anzeigen; intern bleibt 64739.

Behalte `.env`, eventuell separat gesetztes `COMPOSE_PROJECT_NAME`, alle `-p`,
`--env-file` und `-f`-Optionen genau bei. Wer bisher `sudo docker` benutzt, benötigt
dies auch hier; dann das Sicherungsskript mit `sudo bash` starten. Nicht zwischen
verschiedenen Docker-Daemons oder Compose-Projekten wechseln.

Die unveränderten Namen sind: Dienst `mumble-funk`, Projekt standardmäßig
`mumble-funk-docker`, Volumes `funk_config`, `funk_state`, `funk_logs` (mit dem
bisherigen Projektpräfix). Ein anderer Projektname zeigt auf andere Volumes und
kann wie ein leerer Server erscheinen. **Niemals `docker compose down -v`, Volume-
Löschung, `git reset --hard`, `git clean` oder ein neues `.env` über das alte kopieren.**

## 2. Sicherung und Rückkehrpunkt anlegen

Die Sicherung enthält Passwörter/Zertifikate und gehört nicht ins öffentliche Git.
Die Bash-Variable `backup_dir` in derselben Sitzung für alle Schritte beibehalten:

```bash
umask 077
backup_dir="$HOME/mumble-funk-backups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$backup_dir"
git rev-parse HEAD > "$backup_dir/previous-commit.txt"
docker compose config --format json > "$backup_dir/compose-before.json"
docker inspect "$(docker compose ps -q mumble-funk)" \
  --format '{{.Image}}' > "$backup_dir/previous-image.txt"
docker image tag "$(cat "$backup_dir/previous-image.txt")" mumble-funk-rollback:before-stable
if test -f .env; then cp -p .env "$backup_dir/previous.env"; fi
bash scripts/docker/manage.sh backup "$backup_dir/server.tar.gz"
tar -tzf "$backup_dir/server.tar.gz"
docker compose ps
```

Für eine erneute Migration einen anderen Rollback-Tag wählen, wenn der obige schon
zu einer früheren Sicherung gehört. Der Sicherungsbefehl stoppt nur diese Instanz
kurz und startet sie danach wieder, sofern sie vorher lief. Das Archiv muss unter
anderem `config/server.ini` und die tatsächliche SQLite-Datei unter `state/`
enthalten. Die drei Volumes werden zusammen gesichert. Archiv und Metadaten auf
zusätzlichem geschütztem Speicher aufbewahren; kein Passwort hier veröffentlichen.

## 3. Stable holen und Compose-Zuordnung vergleichen

```bash
git fetch origin
git switch --track origin/stable
git rev-parse HEAD
docker compose config --quiet
docker compose config --format json > "$backup_dir/compose-stable.json"
python3 - "$backup_dir" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
a = json.loads((root/'compose-before.json').read_text())
b = json.loads((root/'compose-stable.json').read_text())
assert a['name'] == b['name'], 'Projektname verändert: anhalten'
assert set(a['services']) == set(b['services']), 'Dienste verändert: anhalten'
assert a['volumes'] == b['volumes'], 'Volume-Zuordnung verändert: anhalten'
for key in ('ports', 'volumes', 'image'):
    assert a['services']['mumble-funk'][key] == b['services']['mumble-funk'][key], key+' verändert: anhalten'
print('Projekt, Dienst, Ports, Image-Name und Datenvolumes stimmen überein.')
PY
```

Falls `stable` lokal schon existiert: statt `git switch --track` die beiden Befehle
`git switch stable` und `git pull --ff-only origin stable` verwenden. Die ausgegebene
Commit-ID muss dem freigegebenen Testkandidaten entsprechen. Sonst erst klären, ob
inzwischen ein neuerer Stand eingetroffen ist. Noch läuft der alte Container.

## 4. Bauen, bewusst neu starten und prüfen

```bash
docker compose build
docker compose up -d --no-build
docker compose ps
docker compose logs --tail 80 mumble-funk
docker compose exec mumble-funk funk-container health
docker compose exec mumble-funk funk-container info
```

Erst `up` tauscht den Container aus; die Datenvolumes bleiben erhalten. Der Build
muss seine Server- und Hilfsprogrammtests bestehen. Zugangspasswort, SuperUser-
Passwort, Benutzer, Kanäle, ACLs und Zertifikat mit dem bisherigen Stand vergleichen.
Es sollte kein neues Zertifikat erscheinen. Mit unverändertem Mumla/Windows-Mumble
am **Host-Port 64740** testen: Sprache in beiden Richtungen, zwei gleichzeitige
Sendeversuche, keine automatische Übernahme eines blockierten Senders, erneuter
PTT-Versuch, Kanalwechsel und Wiederverbindung. Beide Transportwege prüfen.
Ein `healthy`-Status allein beweist noch keine hörbare Sprache oder UDP-Erreichbarkeit.
Ergebnis und vollständige Commit-ID zur Abnahme melden.

## 5. Rückkehr zum alten Image

Bei einem Problem mit dem Kandidaten im selben Verzeichnis und Compose-Projekt:

```bash
docker compose stop mumble-funk
docker image tag mumble-funk-rollback:before-stable \
  "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["services"]["mumble-funk"]["image"])' "$backup_dir/compose-before.json")"
git switch feature/docker-install
docker compose up -d --no-build
docker compose ps
```

Dabei **nicht neu bauen**: Es wird das gesicherte frühere Image mit denselben
Volumes gestartet. Der historische Feature-Branch bleibt remote unverändert.
Die Integration verändert das Datenbankschema nicht; der automatisierte Test
prüft auch Rückkehr zum vorherigen Image. Falls eigene Änderungen im Quellordner
vorliegen oder der lokale Feature-Branch von `previous-commit.txt` abweicht, vor
`git switch` anhalten und den gespeicherten Stand klären.

## 6. Archiv wiederherstellen, falls Daten beschädigt sind

`restore` überschreibt absichtlich **keine gefüllten Volumes**. Für einen nötigen
Wiederherstellungstest ein neues, ausdrücklich benanntes Testprojekt und einen
freien Host-Port verwenden; das ursprüngliche Projekt samt Volumes bleibt erhalten:

```bash
COMPOSE_PROJECT_NAME=mumble-funk-recovery docker image tag \
  mumble-funk-rollback:before-stable mumble-funk-recovery:local
COMPOSE_PROJECT_NAME=mumble-funk-recovery FUNK_HOST_PORT=64741 \
  bash scripts/docker/manage.sh restore "$backup_dir/server.tar.gz"
COMPOSE_PROJECT_NAME=mumble-funk-recovery FUNK_HOST_PORT=64741 \
  docker compose up -d --no-build
```

64741 muss für TCP/UDP frei sein; `mumble-funk-recovery` darf noch keine Daten
enthalten. Für den eigentlichen Betrieb zuerst die wiederhergestellten Konten,
Kanäle, Passwörter und Zertifikate prüfen, dann die gewünschte Port-/Projektzuordnung
bewusst planen. Originalvolumes nicht zur Wiederherstellung löschen.
