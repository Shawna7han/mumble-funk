# Noise-Lifecycle: Test-Synchronisation

Stand: 7. November 2026. Branch `fix/noise-lifecycle-test-sync`, aus aktuellem
`dev` bei `98bc239e8b8ef8da6dc46faa2830562d5349d224` erstellt.
Nur Testcode und diese Dokumentation; keine Server-/Audioänderung.

## Fehlerbild und Korrektur

Bei [PR #12](https://github.com/Shawna7han/mumble-funk/pull/12) bestand der native
`TestNoiseIntegration` in beiden CI-Versuchen; später scheiterte im Docker-Build
`lifecycle(0)/(1)` beziehungsweise `lifecycle(2)` an der Leerprüfung nach einem
ungeprüften 80-ms-Drain. Die betroffenen Quellen waren zu Dev identisch.

Der bestehende Server entfernt bei Invalidierung den Noise-Zustand. Er kann aber
ein reserviertes Endpaket behalten und erst beim nächsten Timer-Tick senden.
Eine feste Drain-Dauer ist keine Abschlussbestätigung. Die alten CI-Logs enthalten
keine Paketdetails; ihre genaue Paketfolge bleibt rückwirkend unbekannt.

`TestNoiseIntegration::lifecycle()` prüft nun:

- Vor Kanalwechseln eine Ping-Barriere auf dem beobachtenden Empfänger, danach
  Entfernen ausschließlich alter passender `UserState`-Bestätigungen. Erst dann
  wird der neue Wechsel gesendet und die frische Bestätigung abgewartet.
- Bei Disconnect das tatsächliche `UserRemove`, ohne 60-ms-Schlaf. Mute wird über
  die passende Zustandsmeldung, ACL-Änderung über Admin-/Empfänger-Barrieren bestätigt.
- Empfangene Nachrichten bis zur jeweiligen Bestätigung werden in TCP-Reihenfolge
  gesichert. Bereits davor empfangene Audioframes bleiben Teil der Sequenzprüfung;
  nach der Invalidierungsgrenze sind keine normalen Nutzpakete mehr zulässig.
- Genau ein Endpaket bei Senderinvalidierung, höchstens eines bei Empfängerwechsel.
  Beim Empfängerwechsel darf die bestehende Zustellprüfung das best-effort Ende
  verwerfen, wenn der Empfänger beim Timer-Tick im anderen Kanal ist.
- Session, Endmarkierung, 960-Sample-Opus-Dauer, Dekodierbarkeit und durchgängige
  Sequenz in Zweierschritten; kein Paket hinter der Endmarkierung.
- Abschließend unverändert **160 ms strikt leere Audioausgabe**, jetzt nach dem
  Abschluss statt nach einer angenommenen Drain-Dauer. Auch fremde Sessions werden
  in dieser abschließenden Leerprüfung nicht herausgefiltert.

Der Abschluss wird ereignisbasiert mit der bereits vorhandenen 4000-ms-
Empfangsdeadline des Harness erwartet. Das ist eine Fehlergrenze, keine zusätzliche
feste Schlafzeit. Keine globale Wartezeit oder CTest-Timeout wurde erhöht.
`Harness.h` ergänzt optional das Entnehmen der Nachrichten vor einer Bestätigung
und gezieltes Entfernen veralteter Kontrollmeldungen; bestehende Aufrufer behalten
ihr Verhalten.

`TestNoiseScheduler` enthält zusätzlich einen deterministischen Abschlussfall:
Nach Vorbereitung eines echten reservierten Noise-Frames wird invalidiert. Ohne
Callback wird nur die Modelluhr um 160 ms fortgeschaltet. Der nächste Tick muss
exakt diesen Reserveframe mit korrekter Verbindung/Session/Kanal, nächster Sequenz
und Endmarkierung liefern; weitere Modell-Ticks dürfen nichts liefern.
Die Vorbereitung nutzt den vorhandenen Producer-Testaufbau; die verzögerte
Zustellung selbst benötigt keinen realen Schlaf. Der Fall läuft im regulären
Scheduler-Test und separat mit `--lifecycle-end-probe`.

## Lokale Validierung

Wiederverwendeter nativer Build aus der Untersuchung: GCC 14.2.0, Qt 5.15.15,
libopus 1.5.2, CMake 3.31.10 auf Debian 13. Entwicklungsdateien liegen lokal im
Arbeitsverzeichnis. Keine neue Systeminstallation und kein Docker-Build.
Ice/Zeroconf sind lokal deaktiviert; Warnungen aus den lokal eingebundenen
Protobuf-Headern werden nicht als Fehler behandelt. Das verändert keinen Test und
keine versionierte Build-Einstellung; GitHub-CI prüft ihre normale Konfiguration.

Portable Befehle bei bereits konfiguriertem nativen Build:

```sh
cmake --build build --target TestNoiseIntegration TestNoiseScheduler TestSimplexIntegration --parallel 2
build/tests/TestNoiseIntegration lifecycle
build/tests/TestNoiseScheduler --lifecycle-end-probe
ctest --test-dir build -R '^(TestNoiseIntegration|TestNoisePacing|TestNoiseScheduler|TestSimplexIntegration)$' --output-on-failure
```

Vor den vollständigen Tests bestanden alle fünf Lifecycle-Zeilen in sieben
Durchläufen (**35 Fälle**): erster Prüflauf, drei normale Wiederholungen und drei
Wiederholungen auf einem CPU-Kern mit sechs konkurrierenden CPU-Prozessen.
Der separate deterministische Abschlussfall bestand sechsmal.
Vollständiger `TestSimplexIntegration`: bestanden (38,36 s).
Vollständiger `TestNoiseScheduler`, einschließlich neuem Abschlussfall: bestanden
(44,77 s). `TestNoisePacing`: bestanden (8,24 s).
Der erste vollständige `TestNoiseIntegration` bestand alle Lifecycle-Zeilen,
endete aber mit 30 bestandenen und einem fehlgeschlagenen Fall; siehe den
getrennten Befund unten. Auch der isolierte vollständige Wiederholungslauf
blieb rot: 29 bestandene,
zwei fehlgeschlagene Fälle (118,63 s). Alle fünf Lifecycle-Zeilen bestanden erneut.
Es wurden keine weiteren vollständigen Wiederholungen gestartet.
GitHub-CI-Ergebnisse sind am Draft-PR separat zu prüfen.

## Gesonderte offene Close-/Transportbefunde

Im ersten vollständigen Lauf scheiterte `longConfirmed()` an
`closeCorrelation(effects) > .85` mit „Complete Close waveform missing after long
Noise“. Der Noise-Test lief dabei parallel zu `TestSimplexIntegration` und
`TestNoiseScheduler`. Seine übrigen Fälle, insbesondere alle fünf neuen
Lifecycle-Prüfungen, bestanden. `longConfirmed()` ist bytegleich zu Dev und
verwendet keine der neuen optionalen Empfangsfunktionen.

Dieser Befund beweist weder einen neuen Serverfehler noch sicher einen reinen
Testfehler. Die Ursache der Close-Wellenformabweichung wird durch diese eng
begrenzte Korrektur nicht untersucht oder behoben. Keine Prüfgrenze, Wartezeit
oder Audiofunktion wurde deshalb geändert. Der isolierte vollständige
Wiederholungslauf bestätigte denselben
Close-Korrelationsfehler. Zusätzlich scheiterte
`transport(modern-0-udp-0-end-0)` bei `closes`: tatsächlich 2, erwartet 3.
Auch `transport()` ist unverändert.

Gezielter Dev-Vergleich: `TestNoiseIntegration.cpp` und `Harness.h` wurden
bytegleich über `git show 98bc239e:<Pfad>` in einen separaten Arbeitsordner
kopiert und mit denselben Buildflags und demselben unveränderten Server-Binary
gebaut. Ausgeführt wurden nur `transport:modern-0-udp-0-end-0 longConfirmed`.
Der Transportfall bestand; `longConfirmed()` scheiterte erneut an derselben
Close-Korrelation (Originalzeile 238; Laufzeit zusammen 65,11 s).
**Die Close-Abweichung ist somit auch ohne diese Testkorrektur auf Dev
reproduzierbar.** Der einmalige Transportbefund bleibt separat offen.

Damit ist der vollständige lokale Noise-Integrationstest ausdrücklich **nicht
grün**. Die Ursache dieser separaten Close-/Transportbefunde (Server, Test oder
Umgebung) ist offen; sie werden nicht als behoben dargestellt.

## Aussagegrenzen

Die Tests prüfen den Abbruchvertrag und die beobachtete TCP-Paketreihenfolge.
Empfang einer Kontrollmeldung ist kein Transportzeitstempel und kein Beweis über
Client-Jitterbuffer oder hörbare Wiedergabe. Lokale Scheduling-Varianten ersetzen
keinen Nachweis aller möglichen Last-/Netzwerkzustände oder jedes CI-Images.
Die abschließende Leerprüfung ist ein begrenztes Beobachtungsfenster; der
Modelltest ergänzt sie durch gezielte spätere Ticks und leeren Abbruchzustand.

Kein neuer Server-Lifecycle-Fehler ist durch die synchronisierten Lifecycle-Fälle
nachgewiesen. Neue Befunde
mit korrekter Synchronisation sind gesondert zu dokumentieren; sie rechtfertigen
keine spekulative Audioänderung. Keine Merges, Deployments oder Audiointegration.
