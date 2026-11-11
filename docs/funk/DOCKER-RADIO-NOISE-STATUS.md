# Docker-Rauschverwaltung: Prüfbericht

Arbeitsbranch: `feature/radio-noise`, Draft-PR #9 gegen `dev`.
Ausgangspunkt: `61186eeaca5a77eedfddf733f07ff99e77b2492c`.

## Umfang

Die vorhandene native Server-/Open-/Noise-/Close-Implementierung wird auch im
Docker-Image verwendet. Keine Änderung an Scheduler, Simplex, Sprachpaketen,
Busy-Priorität oder Audioassets. Im C++-Startprogramm wird ausschließlich
`--validate-radio-noise-wav DATEI` ergänzt: Es ruft denselben bereits vorhandenen
`AudioSignal::loadWav(..., LengthPolicy::RadioNoise)` wie der Rauschproduzent auf,
prüft die Datei und beendet sich, ohne einen Server zu starten.

Die gemeinsame Audioverwaltung bekommt nur im Container-Modus Rauschbefehle.
`audio.sh` übernimmt die vorhandene Offline-/Neustart-Orchestrierung. WAV-Import
ist auf 8 MiB begrenzt, validiert vor der INI-Änderung, publiziert atomar unter
Prüfsummenname und überschreibt keine abweichende vorhandene Signaldatei. Busy,
Open und Close behalten ihre 128-KiB-Grenzen. Status bleibt eine reine,
passwortfreie INI-Auswertung, kein Nachweis geladener oder hörbarer Audiodaten.

Nach einem erfolgreichen Änderungsbefehl gibt die Containerverwaltung intern nur
die SHA-256-Revision der neuen INI zurück. Scheitert Start/Healthcheck, wird die
vorige INI unter der Instanzsperre wiederhergestellt; die Revision verhindert ein
Zurücksetzen einer inzwischen anderweitig geänderten Konfiguration. Frühere WAVs
bleiben erhalten. Bei einem bereits gestoppten Dienst erfolgt kein Start.
Parallele Verwaltungsbefehle sind weiterhin zu vermeiden; Fehlermeldungen bei
abgelehnter oder fehlgeschlagener Wiederherstellung müssen beachtet werden.

## Persistenz und Bestandsschutz

- Eigene WAVs: `/var/lib/mumble-funk/audio/radio-noise-<SHA256>.wav` im bestehenden
  State-Volume; UID/GID 10001:10001 und Dateimodus 0640.
- Neue INI: ausdrücklich `radionoise=false`, leere Dateiauswahl, Volume 20.
  Bestehende INIs werden weder ergänzt noch migriert. Fehlende Werte bleiben aus.
- Compose, bestehende Volumenamen, Projektname und `.env.example`
  bleiben unverändert: Host 64740 TCP/UDP, Container 64739 TCP/UDP. Individuelle
  Host-Ports und vorhandene `.env` bleiben erhalten.
- Die vorhandenen Backup-/Restore-Funktionen sichern das gesamte State-Volume
  bereits einschließlich eigener WAVs. Keine neue Volume-Struktur.
- Non-root-UID 10001, Read-only-Root, Cap-Drop und vorhandene Archivprüfungen
  bleiben erhalten. Ein rootless Docker-Daemon wird nicht mit einem erfolgreich
  geprüften Non-root-Prozess im Container gleichgesetzt.
- Keine private Aufnahme oder neue Drittanbieter-Abhängigkeit hinzugefügt.
  Docker-Tests erzeugen ihre WAV mit einem festen Pseudozufallsstart synthetisch;
  vorhandene öffentliche CC0-Kurzsignale bleiben bytegleich.

## Lokale Prüfungen

Native Kompilierung erfolgreich. Alle 33 CTest-Einträge haben einen erfolgreichen
Lauf; nach Umgebungskorrektur wurden nur betroffene Tests wiederholt. Der erste
Lauf hatte keine lokalen Socketrechte. Ein Zwischenlauf überlappte mit erneutem
Linken des Testservers. Zwei spätere zeitkritische Busy-Datensätze erhielten unter
paralleler Last nur 22 bzw. 38 statt 61 Pakete. Die vollständige unveränderte
Busy-Suite bestand im gezielten Wiederholungslauf (74,10 s). Kein Test abgeschaltet
und kein Zeitlimit erhöht.

Python-Suiten: insgesamt 130 erfolgreiche Tests aus Installation (5), Assets (8),
Audio-Preview (5), Audioverwaltung (18), Boost (1), Audio-Standards (3), nativer
Rauschinstallation (1), Herkunftsprüfung (29), Setup (25), Audiosicherheit (13) und
Docker-Verwaltung (22). ShellCheck 0.10.0 und `git diff --check` ohne Befund.

Der erste Docker-Build scheiterte nach erfolgreicher Kompilierung an zwei
bestehenden asynchronen Scheduler-Modelltests: `TestNoiseScheduler` fehlte beim
Fall `producer-140ms`/Timeout ein Close; `TestNoiseTiming` erhielt 496 statt
mindestens 499 Pakete und beobachtete 99 ms Abstand. Gleichzeitig liefen native
Tests auf der auf zwei CPUs begrenzten Cloud-Umgebung. Die Modelluhr läuft dabei
20 ms je Tick weiter, während echte Produzenten nur 5 ms Wallclock-Zeit bekommen.
Der isolierte Wiederholungsbuild führt sämtliche Tests unverändert aus; beide
zuvor fehlgeschlagenen Modelltests bestehen dort bereits. Diese Beobachtung
belegt die Empfindlichkeit gegenüber paralleler Ausführung in dieser Umgebung,
keinen nachgewiesenen Fehler der neuen Docker-Verwaltung. Ein erneut auftretender
Fehler ist weiter zu untersuchen, nicht durch pauschale Fristerhöhung zu verdecken.

Der isolierte Docker-Build bestand vollständig: **33/33 CTests und 22/22
Verwaltungstests**. Die anschließende Containerprobe zeigte einen echten
Verpackungsfehler: Der Cloud-Checkout hat für WAVs Modus 0600 und für Hilfsmodule
0600/0700. COPY übernahm diese Rechte, die Dateien gehörten im Image aber root.
UID 10001 konnte keine der drei Standard-WAVs und keines der beiden Audio-
Hilfsmodule lesen (direkt per `os.access(..., os.R_OK)` nachgewiesen). Deshalb
fehlten schon die normalen Open-Pakete vor dem Rauschtest.

Korrektur ausschließlich im Dockerfile: öffentliche WAVs/Module/Lizenz explizit
0644 kopieren und Verzeichnisse traversierbar halten. Die vorhandenen privaten
Volume-Rechte bleiben unangetastet. Der CI-Build entzieht den Quellassets/-modulen
gezielt Gruppen-/Fremdrechte; die echte Integration prüft Lesen als UID 10001 und
0644 vor dem ersten Audiotest. Damit reproduziert die Regression den Fehler auch
bei Runnern, deren Checkout sonst 0644 erzeugt. Kein chmod an privaten Aufnahmen.

Auch der vollständige Build nach Rechtekorrektur besteht mit **33/33 CTests und
22/22 Verwaltungstests**. Lokales Laufzeit-Image:
`sha256:abdb64ae526c54a6c21b437830ba8684cccfd5c2937143f9824c5321e29e4e3c`.

Die veröffentlichte Portprobe mit einer rein synthetischen 2-s-Rausch-WAV
(192044 Bytes) besteht nach der Rechtekorrektur:

| Eingang / Ende | Dauer | Sprachpakete vollständig | Effektpakete |
| --- | --- | --- | --- |
| TCP / bestätigt | 2 s | 101/101 | 108 |
| verschlüsseltes UDP / bestätigt | 2 s | 101/101 | 108 |
| TCP / Timeout | 2 s | 100/100 | 133 |
| verschlüsseltes UDP / Timeout | 62 s | 3100/3100 | 3133 |

Empfang auch über verschlüsseltes UDP getestet; private Effekte bleiben gemäß
bestehender Architektur im TCP-Tunnel. Fortlaufende Effektsequenzen (+2), keine
internen Ende-Markierungen, genau ein finales Ende, echte Opus-Decodierbarkeit
und vollständige Close-Wellenform werden geprüft. Busy unterbricht den privaten
Effektstream des abgewiesenen Senders korrekt, bleibt vollständig und gelangt
nicht zum falschen Empfänger. Der Besitzer erhält keine privaten Fremdeffekte.
Die Cloud-Probe verwendet für exakte Clipvergleiche die libopus-1.4-Bibliothek aus
dem getesteten Ubuntu-Image; der Debian-Host hat sonst Opus 1.5.2. Keine Änderung
an Image-Abhängigkeiten und keine Lockerung der Bytevergleiche.

Bestanden: öffentlich lesbare Standards/Hilfsmodule unter UID 10001, private
importierte WAV 10001:10001/0640, Standard aus, Ein/Aus, Rauschlautstärken 0/20/100,
abgewiesenes Format/Volume 101, unveränderte INI/WAV nach Fehler, erzwungener
Neustart-Healthfehler mit exakter INI-Wiederherstellung, Erhalt bei Neuerstellen
mit aktiviertem und deaktiviertem Rauschen. Nur temporäre eigene Compose-Projekte
werden erzeugt und bereinigt; Operator-Overrides/.env werden nicht übernommen.

Die vollständige Compose-Integration bestand danach auch alle bisherigen
Audio-/Simplex-, Portkonflikt-, Restart-/Recreate- und Parallelserverprüfungen.
Das Backup enthält die eigene synthetische WAV bytegenau; Restore in leere eigene
Testvolumes erhält WAV, Auswahl, deaktiviertes Rauschen, Volume 37 und alle
übrigen Audioeinstellungen. Die belegten Zielvolumes werden weiterhin abgewiesen.
Ein echter Wechsel vom vorherigen lokalen Image `a5015ef61e38` zum neuen Image
und zurück bewahrt die eigene WAV, vollständigen fachlichen SQLite-Zustand
(einschließlich Konten/Kanälen/ACLs), Passwort, Konfiguration und Zertifikat.
Der Altimage-Updatefixture hält Rauschen ausdrücklich ausgeschaltet; ein Image
vor Einführung des Features kann die neue Wiedergabe nicht zusichern.
Es werden keine Volumes automatisch migriert oder ersetzt. Alle nur für diesen
Lauf erzeugten Container, Projektvolumes und Testalias-Tags wurden bereinigt.

Beide Hostprotokolle wurden auf 64740 nach innen 64739 geprüft. Separat löst die
vorhandene Compose-Matrix fehlende, leere, explizite 64740-, historische 64739-
und individuelle 65001-Einstellungen auf und vergleicht Update/Rollback bei
bytegleich erhaltenen `.env`-Dateien. Keine zusätzlichen Audioports.
Die CI führt die vollständige Protokollprobe künftig mit `--radio-noise` aus;
ein echtes Altimage-Rollback bleibt zusätzlich per `--previous-image` testbar.

## GitHub und Praxistests

Der ursprüngliche fehlgeschlagene Push-Job auf `6f976a2` wurde erneut anhand seines
[tatsächlichen Logs](https://github.com/Shawna7han/mumble-funk/actions/runs/39989862699/job/120538531617)
geprüft. Erste relevante Fehlermeldung: `experimental=false` erwartet, tatsächlich
korrekt `experimental=true` für `feature/radio-noise`. Diese bereits im
Ausgangscommit korrigierte Testerwartung bleibt erhalten; betroffene Suite besteht.
Details: [CI-Analyse](CI-STATUS-2026-10-23.md). Kein Retry des alten fehlerhaften
Commits. Nach dem neuen Push werden GitHub-Ergebnisse ausdrücklich nicht abgewartet.

Der Betreiber bestätigte den nativen Hörtest auf `6f976a2`: kontinuierliches
Rauschen, keine kurzen Audioaussetzer, Close auch bei Windows → Mumla. Das ist
keine Docker- oder Stable-Freigabe. Erneuter Docker-Hörtest mit Windows-Mumble und
Mumla bleibt erforderlich, ebenso externer Netzweg, konkrete Rootless-Daemon-
Umgebung und Langzeitbetrieb. Anleitung: [Docker](docker.md#17-eigenes-funkrauschen-einrichten).
