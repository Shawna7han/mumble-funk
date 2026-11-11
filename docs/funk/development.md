# Entwicklung und Branch-Übersicht

Bestandsaufnahme: **25. Oktober 2026**. GitHub-Standardbranch ist **`stable`**.
Aktueller geprüfter Ausgangspunkt dieser Dokumentationsüberarbeitung ist `dev`
`a6b63175b7e94e503c410706dba87e23d861f46e`, der Merge von [PR #9](https://github.com/Shawna7han/mumble-funk/pull/9).
Mumble-Basis bleibt **1.5.939**, Simplex-Engine **v0.2**. Der Branchname allein ist
keine unveränderliche Versionskennung; für Berichte die vollständige Revision verwenden.

## Stable und Dev

- **Stable:** `510f90618212511c8bac2c0c4732dca317d1d205`, bisheriger gemeinsamer
  Ubuntu-/Docker-Simplex-Kandidat ohne die neue Audio-Engine. Der Name ist keine
  nachträgliche allgemeine Produktionsfreigabe. Seine historischen Compose-
  Vorgaben unterscheiden sich vom aktuellen Dev-Hoststandard.
- **Dev:** Audio-Engine, Busy, Squelch Open/Close, Timeout-Close, Lautstärken,
  Boost, mitgelieferte CC0-WAVs, eigene WAVs, kontinuierliches Rauschen,
  gemeinsamer Open/Noise/Close-Stream und Audioverwaltung sind integriert.
- **Neue INIs auf Dev:** Simplex und Audioeffekte einschließlich Timeout-Close
  und Rauschen aktiv; Busy/Open/Close 50 %, Boost +8 dB nur für diese drei,
  Rauschen 20. Bestehende INIs und eigene WAVs werden nicht migriert.
- **Installation:** [Dev unter Ubuntu](install-dev.md), [Docker aus Dev](docker.md).
  Ubuntu 64739 TCP/UDP; Docker Host 64740 → Container 64739 TCP/UDP. Keine neuen
  Preview-Dienste oder Audioports. Stable-Anleitungen nur auf Stable anwenden.

## Sämtliche veröffentlichten Branches

Die folgende Tabelle enthält die **16 bei der Bestandsaufnahme veröffentlichten
Branches**. Verlinkte Commit-IDs sind unveränderliche Momentaufnahmen. „In Dev
enthalten“ wurde mit `git merge-base --is-ancestor <Branchspitze> origin/dev`
geprüft; daraus folgt nicht, dass der historische Branch spätere Dev-Funktionen hat.
Ob jemand künftig auf einem Branch weiterarbeitet, lässt sich daraus nicht ableiten.

| Branch | Spitze | Rolle | Tatsächlicher Integrationsstand |
| --- | --- | --- | --- |
| `stable` | [`510f90618212`](https://github.com/Shawna7han/mumble-funk/commit/510f90618212511c8bac2c0c4732dca317d1d205) | Bisheriger Installationskandidat und GitHub-Standardbranch | Simplex v0.2 + Ubuntu/Docker; keine neue Audio-Engine. Vorfahr von Dev. |
| `dev` | [`a6b63175b7e9`](https://github.com/Shawna7han/mumble-funk/commit/a6b63175b7e94e503c410706dba87e23d861f46e) | Aktueller experimenteller Entwicklungsstand | PR #9 integriert; alle unten beschriebenen Audiofunktionen vorhanden. |
| `feature/audio-engine` | [`e3a6130e9a57`](https://github.com/Shawna7han/mumble-funk/commit/e3a6130e9a5710150a54e6fcbc77b85970b2d0bd) | Historischer Audio-Engine-Stand | Spitze in Dev enthalten; PR #2. Private Busy-Engine und WAV-Verwaltung. |
| `feature/audio-feedback` | [`8b295e0cd889`](https://github.com/Shawna7han/mumble-funk/commit/8b295e0cd889933951f7c2e941ad914a5a7c5269) | Erhaltene ursprüngliche Audio-Preview | Spitze nicht in Dev enthalten. Busy-Code später gezielt über Audio-Engine übernommen; kein vollständiger Branch-Merge. |
| `feature/squelch-audio` | [`128ec3ce1d2c`](https://github.com/Shawna7han/mumble-funk/commit/128ec3ce1d2c45b6ddf00df33f1e54d83ff946b0) | Historischer Squelch-Stand | Spitze in Dev enthalten; PR #4. Open/Close, Verwaltung und optionaler Timeout-Close. |
| `feature/audio-volume` | [`f2094b6e8d91`](https://github.com/Shawna7han/mumble-funk/commit/f2094b6e8d91e69f731caab2eeda170b1d741ed2) | Historischer Boost-Stand | Spitze in Dev enthalten; PR #5. Einstellbare 0–12 dB für Busy/Open/Close. |
| `feature/busy-tone-asset` | [`8a8c74d6651c`](https://github.com/Shawna7han/mumble-funk/commit/8a8c74d6651cd207e749d7f93ae91ae23d75e8de) | Historischer Standard-Busy-Stand | Spitze in Dev enthalten; PR #6. Mitgelieferte CC0-Aufnahme und synthetischer Fallback. |
| `feature/audio-defaults` | [`21860948939c`](https://github.com/Shawna7han/mumble-funk/commit/21860948939c34f22612dd230c4aa1a1a86506c1) | Historische Audio-Neuinstallationsvorgaben | Spitze in Dev enthalten; PR #7. Timeout-Close für neue INIs aktiv; später um Rauschen ergänzt. |
| `feature/radio-noise` | [`5cbcc6d746b3`](https://github.com/Shawna7han/mumble-funk/commit/5cbcc6d746b39057f4e54333be87978fbd66f3d2) | Historischer letzter Funkrausch-Featurestand | Spitze in Dev enthalten; PR #9. Gemeinsamer Effektstream, Timingkorrektur, CC0-Standardaufnahme und Docker-Verwaltung. |
| `feature/docker-port-default` | [`c69ce8a181f6`](https://github.com/Shawna7han/mumble-funk/commit/c69ce8a181f65c893bc94f6b2087f60e8a226f40) | Historischer Docker-Portstandard | Spitze in Dev enthalten; echter Merge f82f5aeda. Widersprüchliche PR-#3-Metadaten siehe unten. |
| `feature/experimental-dev-install` | [`cf7b9147ab78`](https://github.com/Shawna7han/mumble-funk/commit/cf7b9147ab788fd2d35c3327fb3bbd63d771c7c1) | Historische Dev-Installer-Erweiterung | Spitze in Dev enthalten; PR #8. Explizites --experimental-dev und Prüfung des live veröffentlichten Dev-Commits. |
| `feature/docker-install` | [`4e30c9aa189a`](https://github.com/Shawna7han/mumble-funk/commit/4e30c9aa189a95171a51a1a15d9931a9fab37049) | Historische ursprüngliche Docker-Basis | Spitze in Dev enthalten; über die gemeinsame Integration von PR #1 übernommen. |
| `integration/stable-v0.2` | [`38dd69f206ee`](https://github.com/Shawna7han/mumble-funk/commit/38dd69f206ee0af2ee4de0f0ccd07f3b93cdd1b1) | Früherer Ubuntu-/Docker-Integrationsstand | Spitze in Dev enthalten; PR #1. Keine aktuelle Audio- oder Dev-Installationsquelle. |
| `funk-v0.1` | [`95ac344838c1`](https://github.com/Shawna7han/mumble-funk/commit/95ac344838c1535f943dccc7d134a280aac6a340) | Erhaltene Ubuntu-/Simplex-v0.2-Referenz | Vorfahr von Dev; bisheriger Simplex-Stand ohne die späteren Audiofunktionen. |
| `master` | [`9ef78aac077e`](https://github.com/Shawna7han/mumble-funk/commit/9ef78aac077e3e1555b0aecf640c941ef1f093f5) | Historische Upstream-Referenz | Spitze nicht Vorfahr der aktuellen 1.5.939-Funklinie; kein aktueller Installationszweig und nicht Standardbranch. |
| `weblate-translation-update` | [`a6f6cc98e8bd`](https://github.com/Shawna7han/mumble-funk/commit/a6f6cc98e8bd5972433af5ccbc09764a1888502e) | Erhaltener Upstream-/Übersetzungszweig | Spitze nicht in Dev enthalten; keine geprüfte Funk-Installationsquelle. |

Der zusätzliche Branch `docs/refresh-dev-overview` ist der Dokumentationsvorschlag
für diesen PR, aus dem genannten Dev-Commit. Er ist keine Installationsquelle.
Keine Branches wurden gelöscht, zurückgesetzt oder umbenannt.

## Merge-Nachweise und Übernahmen

Die GitHub-Abfrage zeigte vor diesem Dokumentations-PR **keine offenen PRs**.
PR #1, #2 und #4–#9 melden einen abgeschlossenen Merge. Die First-Parent-Historie
von Dev enthält die zugehörigen Integrationscommits.

| Änderung | Nachweis |
| --- | --- |
| Gemeinsamer Ubuntu-/Docker-Stand | PR #1, Merge `510f90618212511c8bac2c0c4732dca317d1d205` |
| Audio-Engine mit privatem Busy | PR #2, Merge `47a6f091f9a23b847e99469755b2b31ac038264e` |
| Squelch und Timeout-Close | PR #4, Merge `e8894e5b6f42aaeee4e71262e58fadc471f4bea4` |
| Audio-Boost | PR #5, Merge `6b5322ef0fb404fc726b97eff44b7d213129fd85` |
| Standard-Busy-Aufnahme | PR #6, Merge `35f83e9315945c50cbc3c0dc60c0b6f89ca83bb1` |
| Audio-Defaults | PR #7, Merge `b9e617cdb78bd2b4a96bdbb1a7a40cf422c1bd20` |
| Experimentelle Dev-Installation | PR #8, Merge `a1f8998f5f9bfca176fc768faa6ec123d0c45863` |
| Funkrauschen, gemeinsame Effektzustellung und Standardaufnahme | PR #9, Merge `a6b63175b7e94e503c410706dba87e23d861f46e` |

**Docker-Portstandard / PR #3:** Die PR-API meldet `closed` ohne Mergezeitpunkt
und nennt eine andere Merge-SHA. Der Git-Commitgraph enthält dagegen den echten
Zwei-Eltern-Merge `f82f5aeda5111711b8e0c351e3f8b2400fc4a90a` mit den Eltern
`47a6f091f9a23b847e99469755b2b31ac038264e` und
`c69ce8a181f65c893bc94f6b2087f60e8a226f40`; beide sind Vorfahren von Dev.
Seine Nachricht lautet „Merge pull request #3“. Die Portänderung ist damit
**nachweislich gemergt und enthalten**. Die widersprüchlichen GitHub-Metadaten
bleiben als offener Dokumentationsbefund erhalten; allein aus `closed` wird weder
ein erfolgreicher noch ein fehlender Merge abgeleitet.

**Ursprüngliche Audio-Preview:** `feature/audio-feedback` ist dagegen tatsächlich
kein Vorfahr von Dev. Die vier Dateien `AudioFeedback.cpp/.h` und `AudioSignal.cpp/.h`
waren bei der Übernahme auf `feature/audio-engine` bytegleich zur Preview;
das wurde durch direkten Dateivergleich bestätigt und ist im
[historischen Audio-Engine-Bericht](AUDIO-ENGINE-STATUS.md) dokumentiert.
Die Funktion wurde übernommen, nicht der ganze Preview-Branch. Spätere Audio-
Erweiterungen ändern diese ursprüngliche Herkunft nicht.

## Tests und Praxiserfahrung

Der letzte Feature-Stand vor PR #9 war
`5cbcc6d746b39057f4e54333be87978fbd66f3d2`. Der direkte Vergleich mit dem genannten
Dev-Merge ergibt **keine Unterschiede in Servercode, Skripten, Tests, Dockerdateien
oder Assets**. Die dokumentierten Prüfungen dieses Standes bleiben zuordenbar:

| Kategorie | Tatsächlicher Nachweis / Grenze |
| --- | --- |
| Automatisierte Serverprüfungen | 33/33 CTests nativ und nochmals im vollständigen Docker-Build. |
| Installer und Verwaltung | 131 Python-Prüfungen einschließlich Herkunft, Sicherheit, WAVs, neuen Defaults und Erhalt bestehender Einstellungen. |
| Reale temporäre Compose-Integration | TCP, verschlüsseltes UDP, Simplex/Busy, kurze und 62-sekündige Sendungen, bestätigtes und Timeout-Ende, fortlaufender Effektstream und decodierter Close; unveränderte Sprache. |
| Datenbewahrung | Eigene WAVs, Backup/Restore in leere Volumes, echtes Image-Update/Rollback mit erhaltenen SQLite-Daten, Konfiguration, Passwort und Zertifikat. |
| Ubuntu-Geräteprüfung | Betreiber meldet erfolgreiche native Neuinstallation und Hörtests mit Windows-Mumble und Android-Mumla. Der konkret mit Commit belegte Hörtest von `6f976a22f6511f372ff85ca2403592e5faa8e896` bestätigte behobene Aussetzer und Close auf Mumla beim Windows-Senden. Für die gemeldete Neuinstallation liegt keine separate Commit-Angabe vor. |
| Noch offen | Separater Docker-Gerätehörtest, breitere Client-/Mobilfunk-/Verlusttests, reale Zielhost-/VM-Boot- und Wiederherstellungsabnahme sowie abgeschlossener Langzeit-/Lasttest. |

Beim ersten parallelen lokalen Lauf fehlten in einem bestehenden Busy-Test
Pakete; ohne parallelen Build bestand die Wiederholung. Ein externer Docker-
Paketvergleich benötigte die zum Image passende Opus-Version (1.4 statt der
nativen 1.5.2); danach bestand die gesamte Integration. Tests und Sicherheits-
grenzen wurden dafür nicht abgeschwächt. Siehe auch die Hinweise in der
[Installationstest-Dokumentation](../../tests/funk-install/README.md).

Für diese **reine Dokumentationsänderung** werden Markdown, Links, Referenzen,
Befehle, Defaults und Branch-/PR-Angaben geprüft, keine Server-Neubauten gestartet.
Frühere erfolgreiche Runs sind keine Behauptung eines aktuellen CI-Ergebnisses.
Der Betreiber-Hörtest ist keine allgemeine Stable-Freigabe.

Historische Nachweise bleiben unverändert:
[Simplex](SIMPLEX-STATUS.md), [Stable-Integration](STABLE-INTEGRATION-STATUS.md),
[Audio-Engine](AUDIO-ENGINE-STATUS.md), [Docker-Funkrauschen](DOCKER-RADIO-NOISE-STATUS.md),
[CI-Fehler vom 23. Oktober](CI-STATUS-2026-10-23.md) und
[Audio-Timing](AUDIO-TIMING-ANALYSIS.md). Ihre Aussagen beziehen sich auf den
jeweils genannten früheren Prüfstand, nicht automatisch auf das heutige Dev.

## Befunde in historischen READMEs — nur Vorschläge

Die Einleitungen von `README.md` **und** `README.de.md` wurden auf sämtlichen
Feature-Branchspitzen gelesen. Historische Branches bleiben unverändert.

| Branch(es) | Befund | Separat vorgeschlagene Dokumentationskorrektur |
| --- | --- | --- |
| `feature/audio-volume` | Nennt sich im Kopf `feature/squelch-audio` / Draft-PR #4 statt Audio-Boost. | Eigenen Branch und Boost 0–12 dB nennen; damalige deaktivierte Effekte und fehlendes späteres Rauschen korrekt beibehalten. |
| `feature/experimental-dev-install` | Übernommener Kopf nennt `feature/audio-defaults`, nicht den Dev-Installer-Branch. | Als Installer-Erweiterung kennzeichnen; Audiofunktionen nur im damaligen Umfang beschreiben. |
| `feature/audio-feedback`, `feature/docker-install` | Einstieg betont Simplex und stellt die jeweilige Preview-/Docker-Erweiterung nicht klar heraus. | Kurzen historischen Scope-Hinweis und Link zum heutigen Dev ergänzen; spätere Funktionen nicht nachtragen. |
| `feature/squelch-audio` | Phasenname ist passend, bezeichnet den inzwischen gemergten PR #4 noch als Draft. | PR-Zustand als historische Angabe kennzeichnen und heutigen Dev nur verlinken. |
| `feature/audio-engine`, `feature/docker-port-default`, `feature/busy-tone-asset`, `feature/audio-defaults` | Branchbezogene Köpfe vorhanden; spätere Merge-/Defaultbranch-Angaben im übrigen Text sind teilweise überholt. | Historischen Umfang erhalten, heutigen Standardbranch und Nachfolger in einem klar getrennten Hinweis nennen. |
| `feature/radio-noise` | Kopf beschreibt seinen letzten Feature-Stand korrekt; enthält noch die ursprüngliche Feature-Installation. | Optional auf den Merge von PR #9 und die normale Dev-Installation hinweisen. Nicht den alten Branch als aktuellen Dev ausgeben. |

Auch `integration/stable-v0.2` und die alten Prüfberichte sind historische
Momentaufnahmen; keine automatische Inhaltsangleichung an Dev.

## Vorschlag für die Stable-Startseite — nicht angewendet

Die auf GitHub angezeigte Stable-README behauptet noch, `master` bleibe vorerst
Standardbranch. Tatsächlich ist es bereits `stable`. Außerdem ist der neuere
Audio-Stand nur indirekt auffindbar. In einem **separat freizugebenden Stable-Doku-PR**
könnte die veraltete Standardbranch-Aussage ersetzt und dieser Hinweis ergänzt werden:

> **Newer experimental development:** `dev` contains the integrated audio engine,
> Squelch and continuous radio noise. These features are not part of this Stable
> candidate. See the [Dev overview](https://github.com/Shawna7han/mumble-funk/tree/dev)
> before deliberately selecting a test installation. GitHub's default branch is Stable.

> **Neuer experimenteller Entwicklungsstand:** `dev` enthält Audio-Engine,
> Squelch und kontinuierliches Funkrauschen. Diese Funktionen gehören nicht zu
> diesem Stable-Kandidaten. Vor einer bewusst gewählten Testinstallation die
> [Dev-Übersicht](https://github.com/Shawna7han/mumble-funk/tree/dev) lesen.
> GitHub-Standardbranch ist Stable.

Stable bleibt bei `510f90618212511c8bac2c0c4732dca317d1d205`. Es erfolgt weder
ein Merge nach Stable noch eine Änderung des Standardbranches oder von Tags.

## Weiterentwickeln und installieren

Neue Arbeit aus aktuellem `origin/dev` auf einen eigenen Feature-/Dokumentationsbranch
legen, per PR prüfen und erst nach Freigabe integrieren. Für eine normale
Testinstallation immer den **veröffentlichten Dev-Commit** und `--experimental-dev`
verwenden; ein Dokumentationsbranch oder lokaler Zusatzcommit wird dadurch nicht
zur erlaubten Installationsquelle. Herkunfts-/Versionsprüfungen bleiben erhalten.

[Dev-Installation](install-dev.md) · [Ubuntu-Befehle](ubuntu-commands.md) ·
[Docker](docker.md) · [Releaseplan](release-plan.md) ·
[Historische Stable-Migration](docker-stable-migration.md)

Entwickler können ohne Installation weiterhin nach der vorhandenen
[Buildoptionen](../dev/build-instructions/cmake_options.md) arbeiten.
Docker-Testprojekte dürfen nur ihre eigenen Wegwerf-Volumes aufräumen;
für echte Installationen gilt weiterhin **niemals `down -v`** bei Updates.
