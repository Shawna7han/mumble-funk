# Releaseplan: Stable-Kandidat und integrierte Audioentwicklung

Stand: **25. Oktober 2026**. GitHub-Standardbranch ist bereits **`stable`**.
Diese Dokumentationsüberarbeitung ändert weder Standardbranch noch Branchschutz,
Versionstags, Releases, Registry-Images oder laufende Installationen.

## Zwei getrennte Quellstände

| Stand | Revision bei dieser Bestandsaufnahme | Bedeutung |
| --- | --- | --- |
| Bisheriger Stable-Kandidat | `510f90618212511c8bac2c0c4732dca317d1d205` | Gemeinsame Ubuntu-/Docker-Basis, Simplex v0.2, ohne neue Audio-Engine. |
| Aktueller Dev nach PR #9 | `a6b63175b7e94e503c410706dba87e23d861f46e` | Integrierte Audiofunktionen einschließlich Funkrauschen, CC0-Aufnahmen, gemeinsamer Effektstream und Verwaltung. Weiterhin experimentell. |

Die Audiointegration in Dev ist **keine Übernahme nach Stable**. Eine Freigabe
muss den konkreten Quellcommit, den Funktionsumfang und beide Installationswege
benennen. Stable wird nicht rückwirkend als Audioversion beschrieben.
[Branch- und Merge-Nachweise](development.md).

## Versionsidentität

Mumble-Basis bleibt **1.5.939**, Simplex-Engine **v0.2**. Ubuntu und Docker müssen
für dieselbe Veröffentlichung denselben vollständigen Git-Commit bauen:

```bash
git rev-parse HEAD
```

Nativ steht die installierte Revision zusätzlich in
`/opt/mumble-funk/current/BUILD-INFO`. Unterschiedliche Compiler oder Bibliotheken
dürfen unterschiedliche Binärdateien erzeugen; entscheidend ist die Quellrevision.
Der bewegliche Branchname und „Mumble 1.5.939“ allein reichen nicht aus.

Das annotierte `funk-v0.2.0` bleibt unverändert auf
`95ac344838c1535f943dccc7d134a280aac6a340` und markiert den früheren Simplex-Stand
noch ohne Docker-Verpackung. `funk-v0.2.1` war der Vorschlag für die frühere
Ubuntu-/Docker-Zusammenführung; ein entsprechender veröffentlichter Versionstag
war bei der Bestandsaufnahme nicht vorhanden. Die Versionsnummer einer künftigen
Audioveröffentlichung ist separat festzulegen, nicht stillschweigend aus diesem
alten Vorschlag abzuleiten. Hier wird kein Tag oder Release erzeugt.

## Nachgewiesene Entwicklung und offene Freigabeschritte

Die [Entwicklungsübersicht](development.md#tests-und-praxiserfahrung) trennt
implementierte Funktionen, automatisierte Ergebnisse und gemeldete Gerätetests.
Der Betreiber meldet eine erfolgreiche native Ubuntu-Neuinstallation und
Windows-/Mumla-Hörtests. Die frühere Behauptung, alle Audiofunktionen seien noch
außerhalb von Dev, ist seit den abgeschlossenen Integrationen überholt.

Vor einer Veröffentlichung der Audio-Version bleiben insbesondere:

- Den konkreten Dev-Commit samt relevantem CI-Ergebnis und Quellidentität festlegen.
- Docker mit Windows-Mumble und Android-Mumla praktisch hören: kurze/lange
  Sendungen, Open/Rauschen/Close, Timeout-Close, Busy und vollständige Sprache.
- Größere Client-/Netzabdeckung, schnelle PTT-Wechsel, Paketverluste, Mobilfunk,
  Kanalwechsel, Disconnect und Wiederverbindung untersuchen.
- Auf dem tatsächlichen Zielhost Start/Neustart, native systemd-VM-Starts und
  Docker-Betrieb prüfen. Eine Cloud-Simulation ist kein Zielhost-Bootnachweis.
- Backup, Wiederherstellung und Rollback mit der konkreten Betreiberkonfiguration
  und bestehenden Daten nachvollziehen; automatisierte Wegwerf-Tests sind dokumentiert.
- Einen längeren Stabilitäts-/Lastbetrieb dokumentieren. Kein abgeschlossener
  Langzeittest oder allgemeine ARM-/Clientfreigabe wird behauptet.
- Ausdrückliche Freigabe des Inhabers für die vollständige Commit-ID und den
  gewünschten Umfang einer Stable-Übernahme bzw. Veröffentlichung einholen.

Erst danach können eine Stable-Integration per PR, neue Versionstags und
Release-Unterlagen gesondert beauftragt werden. `master`, historische Feature-
Branches und Archiv-Tags bleiben erhalten; kein automatischer Merge oder Cleanup.

## Workflows und spätere Registry-Veröffentlichung

Dev enthält serverbezogene Build-, Installer- und CodeQL-Prüfungen einschließlich
Docker-Integration. Diese werden durch eine reine Dokumentationsänderung nicht
verändert oder deaktiviert. Ein definierter Workflow und alte erfolgreiche Runs
beweisen kein aktuelles CI-Ergebnis; nach dem Doku-Push wird nicht auf CI gewartet.

Die frühere Stable-Integration entfernte alte Upstream-Veröffentlichungsaktionen
für fremde Docker-Repositories, Mumble-WinGet und ungeeignete Client-Builds.
Der heutige Dockerweg baut lokal; `docker compose pull` ist für das `:local`-Image
kein Updateweg. Es wird keine GHCR-Veröffentlichung oder automatisches Deployment
eingerichtet. Ein späterer Registryweg braucht eigene Freigabe, konkrete Source-ID,
Version und Digest sowie einen nachvollziehbaren Rollback.

Historische Nachweise bleiben unverändert:
[Stable-Integration](STABLE-INTEGRATION-STATUS.md),
[ursprünglicher Schutzvorschlag](github-collaboration.md) und
[Docker-Migration](docker-stable-migration.md). Deren damalige Aussagen über
Standardbranch, Ports und noch nicht integrierte Audioentwicklung sind zeitgebunden.
Die aktuelle Rollenverteilung steht ausschließlich in [development.md](development.md).
