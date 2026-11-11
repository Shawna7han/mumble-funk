# Docker-Portstandard: Prüfbericht

Stand: 19. Oktober 2026. Branch `feature/docker-port-default`, Ziel `dev`.
Ausgangspunkt: `47a6f091f9a23b847e99469755b2b31ac038264e`.
Kein Merge, keine Stable-Änderung und keine Veröffentlichung durch diese Aufgabe.

## Änderung und Bestandsschutz

Neue Docker-Installationen verwenden Host **64740 TCP/UDP**, der Container bleibt
auf **64739 TCP/UDP**. Ubuntu nativ bleibt auf **64739 TCP/UDP**.
Nur die beiden Compose-Fallbacks und `.env.example` ändern den Portstandard;
Servercode, Dockerfile, Laufzeitverwaltung, Projekt-/Volume-Namen und Dienste
bleiben unverändert. Keine echte Installation wurde aktualisiert.

Bestehende ausdrücklich gesetzte Ports bleiben erhalten. Alte Installationen ohne
Portzeile verwendeten bislang den Fallback 64739. Vor dem ersten Quellstandwechsel
muss der Betreiber seinen bisherigen tatsächlich veröffentlichten Port ausdrücklich
festhalten. Sonst würde ein späteres Neuerstellen den neuen Fallback anwenden.
Es gibt keine automatische Portmigration und kein automatisches Bearbeiten von
`.env`. [Update, Zuordnungsprüfung und Rollback](docker.md#9-aktualisieren)
beschreiben diesen Sonderfall. Historische Prüfberichte behalten ihre damaligen
Portangaben; der bisherige Stable-Stand bleibt unverändert.

## Durchgeführte Tests

| Prüfung | Ergebnis |
| --- | --- |
| Bestehende Docker-Verwaltungstests | 11/11 bestanden |
| Tatsächliches `docker compose config` ohne `.env` | Host 64740, TCP und UDP nach Container 64739; keine Datei angelegt |
| `.env` ohne Port oder mit leerem Port | Neuer Fallback 64740 für beide Protokolle |
| Explizite Ports 64740, 64739, individuell 65001 | Beibehalten, `.env` bytegleich und Rechte unverändert |
| Neue `.env.example` | Host 64740 für beide Protokolle |
| Historischer/neuer Compose-Fallback, Update und Rollback | Gesamte aufgelöste Konfiguration bei expliziten Ports identisch; fehlende Festlegung als Abweichung erkannt |
| Vollständiger Docker-Integrationstest auf Host 64740 | Bestanden: TCP, verschlüsseltes UDP, Simplex und konkurrierende Sender |
| Bestehendes privates Busy-Feedback | Bestanden, über dieselben Ports, keine Weitergabe in den Kanal, standardmäßig aus |
| Datenpersistenz und Wiederherstellung | Bestanden: Neustart/Neuerstellen, konsistentes Backup/Restore, SQLite, Konten, Kanäle, ACLs, Passwörter und Zertifikat |
| Echter Wechsel zwischen unterschiedlichen Images und Rückkehr | Bestanden, vollständiger Datenbestand unverändert |
| Fremde TCP-/UDP-Portbelegung, native Parallel-Instanz, Beenden | Bestanden; ausschließlich getrennte kurzlebige Testressourcen |
| Bash-Syntax der dokumentierten Befehle und `git diff --check` | Bestanden |

Der Integrationstest verwendet das vorhandene geprüfte Image
`mumble-funk-audio-engine:phase1` (`sha256:3fe5474152fcbec0f25cb7ef70a7c8122bd9cebb9dd4b15da496ae6ea7fa71de`)
und für Update/Rollback das bisherige `mumble-funk-docker:local`
(`sha256:a5015ef61e3866182f9020ac819721249c349d3c38bbacceeec6bdace5495462`).
Serverquellen, Dockerfile und Laufzeitverwaltung sind gegenüber dem bereits
gebauten Audio-Stand unverändert. Der Test verwendet die **neue Compose-Datei**,
keinen überschriebenen Laufzeithelfer. Ein großer erneuter Server-/Image-Build und
die vollständige native Installationssuite wurden daher lokal nicht wiederholt.
Die vorhandenen CI-Workflows bleiben erhalten und prüfen den neuen Commit erneut.

Nicht durchgeführt: Installation/Update auf Betreiber-VMs, echte Android-/Windows-
Praxistests, Router-/Firewalltests oder Host-Neustart. Dieser Portwechsel liefert
keine neue Freigabe der experimentellen Audio-Engine.
