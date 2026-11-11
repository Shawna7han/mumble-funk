# Fertiges experimentelles Dev-Image aus GHCR

**Vorbereitung, noch keine Veröffentlichung:** Dieser Feature-Stand richtet den
Veröffentlichungsweg ein. In diesem Auftrag wird kein Image zu GHCR gepusht und
kein Veröffentlichungs-Tag erzeugt. Die folgenden Installationsbefehle sind erst
nach separater Betreiberfreigabe, Merge des Workflows nach Dev, erfolgreichem
Veröffentlichungslauf und Freischaltung des Packages auf **Public** verwendbar.
Ein derzeit nicht vorhandenes Image ist kein Installationsfehler deiner VM.

Vorgesehener Name: **`ghcr.io/shawna7han/mumble-funk`**, zunächst ausschließlich
**Linux/AMD64**. Es bleibt experimentell; kein `latest`, `stable` oder Release-Tag.
Der [lokale Docker-Build](docker.md) mit `compose.yaml` bleibt vollständig erhalten.
`compose.ghcr.yaml` ist eine **eigenständige** Alternative ohne Build-Abschnitt,
kein zusätzliches Override über `compose.yaml`.

## Was unverändert bleibt

- Derselbe Servercode und dasselbe Multi-Stage-Dockerfile; keine zweite Audio-Engine.
- Simplex, Busy, Squelch Open/Close, Timeout-Close, Funkrauschen und alle vier
  CC0-Aufnahmen einschließlich eigener WAV-Verwaltung.
- [Aktuelle Dev-Defaults](audio-defaults.md): neue INIs aktivieren Effekte,
  Busy/Open/Close bei 50 %, deren Boost +8 dB, Rauschen bei 20 ohne Boost.
- Bestehende INIs und fehlende alte Schlüssel werden nicht migriert. Passwörter,
  SQLite, Zertifikate, WAVs und ausgeschaltete Effekte bleiben erhalten.
- Projektname standardmäßig `mumble-funk-docker`, Dienst `mumble-funk`, Volumes
  `funk_config`, `funk_state`, `funk_logs` mit denselben Zielverzeichnissen.
- Host **64740 TCP/UDP → Container 64739 TCP/UDP**, eigene Host-Ports erlaubt.
- UID/GID **10001:10001**, read-only Root-Dateisystem, keine Capabilities,
  `no-new-privileges`, bisheriges tmpfs, Healthcheck und `unless-stopped`.
  UID 10001 allein bedeutet nicht, dass der Docker-Daemon selbst rootless läuft;
  der konkrete Rootless-Daemon bleibt ein eigener Zielhosttest.

## 1. Versionen und Digest verstehen

| Bezeichnung | Bedeutung |
| --- | --- |
| Dev-Commit | Vollständige Git-ID der freigegebenen Quellrevision, nicht nur „Mumble 1.5.939“. |
| Git-Freigabetag | Annotiertes `ghcr-dev-<40-stellige Commit-ID>`; kontrollierter Auslöser, kein offizieller Release-Tag. |
| Image-Tag | `ghcr.io/shawna7han/mumble-funk:dev-<40-stellige Commit-ID>`. Identifiziert den Quellstand. |
| Image-Digest | `ghcr.io/shawna7han/mumble-funk@sha256:<64 Hexzeichen>`. Unveränderliche Identität des tatsächlich gebauten Manifests. |
| Spätere Stable-Version | Braucht eine gesonderte Abnahme, Versionsentscheidung und Veröffentlichung. |

Der Workflow lehnt das Überschreiben eines bereits vorhandenen Dev-Image-Tags ab.
Registry-Tags sind technisch trotzdem verschiebbare Namen; **für reproduzierbare
Installationen den verifizierten Digest verwenden**. Unterschiedliche Bibliotheken
oder Basisimage-Aktualisierungen können aus demselben Quellcommit unterschiedliche
Binärdateien erzeugen. Der Digest beschreibt genau das veröffentlichte Ergebnis.

OCI-Labels enthalten Repository-URL, Quellcommit, `dev-<SHA>`, UTC-Bauzeit und
`BSD-3-Clause AND CC0-1.0` für Projektquellen und Audio. Abhängigkeiten behalten ihre
eigenen Lizenzhinweise im Image. Die WAV-Dateien werden nicht umkodiert.

## 2. Neuinstallation nach tatsächlicher Veröffentlichung

Docker Engine, Compose v2, Git und ein freier TCP-/UDP-Host-Port werden vorausgesetzt;
[Docker-Voraussetzungen](docker.md#voraussetzungen). Ein Compiler ist auf dem
Betreiberhost nicht nötig. Auf einer passenden Linux/AMD64-Testinstanz beginnen:

```bash
git clone --branch dev --single-branch https://github.com/Shawna7han/mumble-funk.git mumble-funk-docker
cd mumble-funk-docker
# Nur bei einer wirklich neuen Installation; vorhandene .env nicht ersetzen:
if [ ! -e .env ]; then cp -n .env.example .env; fi
nano .env
```

Die vorhandenen Zeilen, insbesondere Port und Projektname, beibehalten. Für eine
neue Installation zwei Zeilen ergänzen, mit dem **wirklich veröffentlichten** Digest
statt des folgenden Platzhalters:

```dotenv
COMPOSE_FILE=compose.ghcr.yaml
FUNK_IMAGE=ghcr.io/shawna7han/mumble-funk@sha256:HIER_DEN_VERIFIZIERTEN_DIGEST_EINTRAGEN
```

In nano: Strg+O, Eingabetaste, Strg+X. Kein `latest` verwenden. Wenn `FUNK_IMAGE`
fehlt/leer ist, bricht Compose ab. Einen eigenen Host-Port weiterhin nur über
`FUNK_HOST_PORT` festlegen; intern bleibt 64739 TCP/UDP.

```bash
docker compose -f compose.ghcr.yaml config --quiet
docker compose -f compose.ghcr.yaml pull
docker compose -f compose.ghcr.yaml up -d --no-build
docker compose -f compose.ghcr.yaml ps
docker compose -f compose.ghcr.yaml exec -T mumble-funk funk-container health
```

Der Healthcheck muss erfolgreich sein. Windows-Mumble und Android-Mumla verbinden
sich zum **Host-Port**, normalerweise 64740. Für die Zugangsdaten lokal:

```bash
docker compose -f compose.ghcr.yaml exec -T mumble-funk funk-container info
```

**`info` zeigt das Zugangspasswort.** Nicht in Screenshots/Logs veröffentlichen.
Docker-`info` hat derzeit keinen Passwort-Ausblendungsschalter. Für eine
weitergebbare Audio-Konfiguration stattdessen `bash scripts/docker/audio.sh status`
verwenden; die native Ubuntu-Info mit `--hide-password` ist ein anderer Befehl.

## 3. Bestehende lokale Docker-Installation sicher umstellen

Nicht in einem neuen Projekt mit ungeprüften Standardnamen starten. Im bisherigen
Projektverzeichnis beginnen und dieselben Compose-Optionen wie bisher verwenden.
Bei eigenen Override-Dateien deren Einstellungen bewusst in der Zielkonfiguration
erhalten; nicht blind durch die Standarddatei ersetzen.

### Bestehende Instanz und Port identifizieren

```bash
docker compose ps
container=$(docker compose ps -q mumble-funk)
test -n "$container" || { echo 'Keine laufende eigene Instanz gefunden; anhalten.'; false; }
docker inspect "$container" --format 'Projekt={{index .Config.Labels "com.docker.compose.project"}} Dateien={{index .Config.Labels "com.docker.compose.project.config_files"}}'
docker compose port --protocol tcp mumble-funk 64739
docker compose port --protocol udp mumble-funk 64739
```

Bei Fehlern anhalten. Den tatsächlichen Projektnamen in `COMPOSE_PROJECT_NAME`
beibehalten, auch wenn er vom Standard abweicht. Verwendete Optionen wie `-p`,
`--env-file` und zusätzliche Compose-Dateien nicht verlieren. Stimmen TCP-/UDP-
Ports nicht überein, erst klären. Eine fehlende oder leere Portzeile kann bei alten
Installationen 64739 bedeutet haben: Den **tatsächlich benutzten** Port vor dem
Wechsel bewusst festhalten, keine automatische Portmigration.
[Ausführlicher Bestandsschutz](docker.md#9-aktualisieren).

### Backup und bisheriges Image sichern

```bash
umask 077
backup_dir="$(pwd)/backups/vor-ghcr-$(date -u +%Y%m%dT%H%M%S)"
mkdir -p "$backup_dir"
git status --short --branch
git rev-parse HEAD > "$backup_dir/previous-commit.txt"
if test -f .env; then cp -p .env "$backup_dir/previous.env"; fi
docker compose config --format json > "$backup_dir/compose-before.json"
rollback_image="mumble-funk-before-ghcr:$(date -u +%Y%m%dT%H%M%S)"
docker image tag "$(docker inspect "$container" --format '{{.Image}}')" "$rollback_image"
printf '%s\n' "$rollback_image" > "$backup_dir/previous-image.txt"
docker image save "$rollback_image" | gzip > "$backup_dir/previous-image.tar.gz"
bash scripts/docker/manage.sh backup "$backup_dir/server.tar.gz"
```

Nur bei sauberem, bekanntem Quellstand fortfahren. Die Sicherung enthält Geheimnisse
und eigene WAVs: extern und privat aufbewahren. Das Backup stoppt nur den eigenen
Dienst kurz und startet ihn wieder, wenn er zuvor lief. SQLite, INI, Zertifikate,
Audio und Logs sind Teil des vorhandenen geprüften Sicherungsverfahrens.

### Dev-Dateien und freigegebenes Image auswählen

Falls die neue Compose-Datei noch fehlt, im selben Quellordner nach Sicherung auf
den veröffentlichten Dev-Stand wechseln. Das funktioniert auch mit älteren
Single-Branch-Klonen; bei lokalen Änderungen oder Konflikten anhalten:

```bash
git fetch origin refs/heads/dev:refs/remotes/origin/dev
if git show-ref --verify --quiet refs/heads/dev; then
  git switch --no-guess dev
else
  git switch --no-track -c dev refs/remotes/origin/dev
fi
git merge --ff-only origin/dev
nano .env
```

Nur die neue Image-Auswahl und den Compose-Dateipfad bewusst setzen. Vorhandene
Ports, Projektnamen und weitere Einstellungen nicht überschreiben. Wenn bereits
`COMPOSE_FILE` mit eigenen Overrides vorhanden ist, diese zuerst abgleichen.
Für diesen Terminalablauf sicherstellen, dass keine alten exportierten Shellwerte
die beabsichtigte Auswahl übersteuern. Ein Git-Wechsel ändert keinen laufenden Container.

Vor jedem Neuerstellen die tatsächlich aufgelöste Konfiguration vergleichen:

```bash
docker compose -f compose.ghcr.yaml config --format json > "$backup_dir/compose-after.json"
python3 - "$backup_dir" <<'CHECK'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
a = json.loads((root/'compose-before.json').read_text())
b = json.loads((root/'compose-after.json').read_text())
for value in (a, b):
    service = value['services']['mumble-funk']
    for key in ('image', 'build', 'platform'):
        service.pop(key, None)
assert a == b, 'Projekt, Volumes, Ports oder andere Einstellungen unterscheiden sich: anhalten.'
print('Projekt, Volumes, Ports und verbleibende Einstellungen unverändert.')
CHECK
docker compose -f compose.ghcr.yaml pull
docker compose -f compose.ghcr.yaml up -d --no-build
docker compose -f compose.ghcr.yaml exec -T mumble-funk funk-container health
bash scripts/docker/audio.sh status
```

Nach erfolgreicher Prüfung Anmeldung, Konten, Kanäle, Zertifikat, Passwort, eigene
WAVs und Audioeinstellungen kontrollieren. Windows-Mumble/Mumla-Hörtest durchführen.
Bei einem Konfigurationsunterschied den Vergleich nicht einfach entfernen.
**Niemals `docker compose down -v`** für eine bestehende Installation verwenden.

## 4. Spätere Updates und Rollback

Bei jedem Update vorher wie oben sichern, die bisherige Image-Auswahl notieren,
einen neuen freigegebenen Digest in der bestehenden `.env` auswählen, Konfiguration
vergleichen und erst dann `pull` und `up -d --no-build` ausführen. Eigene Ports,
Projektname und Volumes bleiben gleich. Ein Pull allein startet keinen Container neu.

Für einen kompatiblen Rollback das gespeicherte vorherige Image verwenden:

```bash
rollback_image=$(cat "$backup_dir/previous-image.txt")
# Falls das Image lokal fehlt, aus der privaten Sicherung laden:
# gzip -dc "$backup_dir/previous-image.tar.gz" | docker image load
FUNK_IMAGE="$rollback_image" docker compose -f compose.ghcr.yaml up -d --no-build --pull never
FUNK_IMAGE="$rollback_image" docker compose -f compose.ghcr.yaml exec -T mumble-funk funk-container health
```

Danach dieselbe Auswahl bewusst in `.env` festhalten, damit spätere Befehle nicht
wieder das neuere Image wählen. Bei Rückkehr zur ursprünglichen lokalen Bauweise
stattdessen deren ursprüngliche Compose-Auswahl verwenden; Daten und Volume-Namen
bleiben dieselben. Kein alter Datenbankstand wird automatisch eingespielt.

Ein älteres Image muss mit dem **aktuellen** SQLite-/Konfigurationsstand kompatibel
sein. Die hier ergänzte Verpackung verändert deren Schema nicht. Für zukünftige
Schemaänderungen gibt es keine pauschale Downgrade-Zusage: Bei Inkompatibilität die
vorherige Sicherung getrennt in **leere** Zielvolumes wiederherstellen und prüfen,
nicht die laufende Datenbank überschreiben. [Vorhandener Restoreweg](docker.md#12-wiederherstellen-ohne-vorhandene-daten-zu-überschreiben).

## 5. Audioverwaltung

Mit `COMPOSE_FILE=compose.ghcr.yaml` und der Image-Auswahl in der bestehenden `.env`
verwenden die **unveränderten** Verwaltungsskripte dieselben Daten wie der Server:

```bash
bash scripts/docker/audio.sh status
bash scripts/docker/audio.sh disable-radio-noise
bash scripts/docker/audio.sh enable-radio-noise
bash scripts/docker/audio.sh radio-noise-volume 20
bash scripts/docker/audio.sh set-radio-noise "$HOME/meine-sounds/rauschen.wav"
bash scripts/docker/audio.sh use-standard-radio-noise
```

Ändernde Befehle stoppen nur den eigenen Compose-Dienst kurz und starten ihn nur,
wenn er zuvor lief; der vorhandene Healthcheck-/INI-Rollback bleibt aktiv. Vor dem
Import prüft derselbe Servervalidator Format und Größe. Eigene Dateien liegen im
persistenten State-Volume unter `/var/lib/mumble-funk/audio/`, werden nicht ins Image
eingebaut und bleiben in Backup/Restore enthalten. Die Standardaufnahme liegt
unter `/usr/local/share/mumble-funk/audio/radio-noise.wav`.

`status` enthält keine Passwörter, schreibt nichts und startet nichts neu. Er zeigt
die INI-Auswahl, keinen bestätigten WAV-Ladevorgang. Busy-, Squelch-, Timeout-Close-
und Boost-Befehle bleiben erhalten: [Docker-Audioverwaltung](docker.md#16-audioassets-verwaltung-und-lizenz).

## 6. Image-Version prüfen

```bash
container=$(docker compose -f compose.ghcr.yaml ps -q mumble-funk)
docker inspect "$container" --format '{{index .Config.Labels "org.opencontainers.image.source"}} {{index .Config.Labels "org.opencontainers.image.revision"}} {{index .Config.Labels "org.opencontainers.image.version"}}'
docker image inspect "$(docker inspect "$container" --format '{{.Image}}')" --format '{{json .RepoDigests}}'
```

Quellcommit, Image-Digest und Clientversionen im Testbericht notieren. Keine
Passwörter oder privaten WAVs veröffentlichen.

## 7. Veröffentlichung — nur nach separater Freigabe

Die Workflowdatei muss zunächst regulär per PR nach Dev integriert sein. Stable
bleibt Standardbranch und wird dafür **nicht** verändert. Es gibt absichtlich
keinen neuen `workflow_dispatch`: Ein Tag-Push verwendet den Workflow im getaggten
Commit und benötigt seine Datei nicht auf dem Standardbranch.

Ausschließlich nach ausdrücklicher Betreiberfreigabe auf einer sauberen eigenen
Git-Kopie den konkreten Dev-Commit wählen und einen **neuen annotierten** Auslöser
setzen. Die folgenden Befehle werden durch diesen Entwicklungsauftrag nicht ausgeführt:

```bash
git fetch origin refs/heads/dev:refs/remotes/origin/dev
SOURCE_SHA=$(git rev-parse origin/dev)
git cat-file -e "$SOURCE_SHA:.github/workflows/publish-dev-image.yml"
git tag -a "ghcr-dev-$SOURCE_SHA" "$SOURCE_SHA" -m "Approved experimental Dev image $SOURCE_SHA"
git push origin "refs/tags/ghcr-dev-$SOURCE_SHA"
```

Bei bestehendem Tag anhalten; weder löschen noch verschieben oder erzwingen.
Die Workflowprüfung verlangt vollständige SHA, identischen annotierten Tag und
Checkout, saubere versionierte Dateien und Mitgliedschaft im live geholten Dev.
Ein Feature-Nachfolger von Dev reicht nicht aus. Normale Branch-Pushes, PRs und
Forks veröffentlichen nichts; `latest`, `stable` und Release-Tags sind ausgeschlossen.

Der Ablauf trennt Berechtigungen:

1. **Guard:** nur `contents: read`; Identität und Dev-Mitgliedschaft prüfen.
2. **Build/Test:** nur lesende Repositoryrechte; vorhandenes Dockerfile, AMD64,
   vollständige Server-/Python-Prüfungen und Compose-Integration einschließlich
   lokaler Compose → fertiges Image → Rückkehr. In CI kann der Vorgänger dasselbe
   getestete Image sein; verschiedene Image-Versionen werden zusätzlich lokal geprüft.
3. **Publish:** erst nach Erfolg; `contents: read`, `packages: write`. Dasselbe
   getestete Image wird als Artefakt geladen, seine Image-ID/OCI-Metadaten erneut
   geprüft und ohne Neubuild gepusht. Nur `GITHUB_TOKEN`, kein persönlicher Token.
4. Der Workflow verweigert vorhandene Image-Tags. Netzwerk-/Berechtigungsfehler
   werden nicht als „Tag fehlt“ gewertet. Nach dem Push wird der Registry-Digest
   mit dem lokalen gepushten Image verglichen. Summary und Artefakt
   `published-dev-image-identity` enthalten Quellcommit, Tag und Digest.

Scheitert die Prüfung **nach** einem Push, kann das Image bereits vorhanden sein,
obwohl der Job fehlschlägt: erst Registry/Logs prüfen, nicht blind überschreiben.
Registry-Authentifizierung und die erste Veröffentlichung werden in diesem Auftrag
nur mit Test-Doubles geprüft; echter GHCR-Upload und anonymer Download bleiben bis
zur Freigabe offen.

## 8. GitHub-Einstellungen nach Freigabe

- Actions im Repository aktivieren und die verwendeten offiziellen
  `actions/checkout`, `actions/upload-artifact`, `actions/download-artifact` erlauben.
  Nicht pauschal alle Workflows mit Schreibrechten ausstatten; nur der Publish-Job
  fordert `packages: write` an.
- Veröffentlichungstags `ghcr-dev-*` nur kontrolliert erzeugen lassen; ein passender
  Tag-Ruleset mit eingeschränkten Erstellern ist empfehlenswert und wird hier nicht
  automatisch eingerichtet. Ein solcher Tag-Push ist die ausdrückliche Auslösung.
- Nach der ersten genehmigten Veröffentlichung: GitHub-Profil → **Packages** →
  **mumble-funk** → **Package settings** → **Change visibility** → **Public**.
  Die Repository-Sichtbarkeit dabei nicht verändern.
- Repository-Verknüpfung und unter **Manage Actions access** den Zugriff für
  `Shawna7han/mumble-funk` prüfen, falls GHCR einen Push verweigert.
- Den erfolgreichen Workflow und Digest dokumentieren und erst danach einen
  anonymen `docker pull`/Hörtest auf der separaten Betreiber-Testinstanz freigeben.

Bis dahin: **implementierter Veröffentlichungsweg, lokal geprüfte Images und Tests,
aber keine behauptete GHCR-Veröffentlichung, kein Release und kein Deployment**.
