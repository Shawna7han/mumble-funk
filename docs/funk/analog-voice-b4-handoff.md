# Übergabe für einen neuen Codex-Chat: nach B3

Stand: 7. November 2026. **B3 umgesetzt; B4 nicht begonnen.** Diese Datei ist
keine automatische Beauftragung. B4 nur nach separatem ausdrücklichem Auftrag.

## Branch und Quellstand

- Repository: <https://github.com/Shawna7han/mumble-funk>.
- Ausschließlich `feature/analog-voice-live`, bestehender
  [Draft-PR #15](https://github.com/Shawna7han/mumble-funk/pull/15) gegen `dev`.
  Keinen neuen Feature-Branch oder PR eröffnen, nicht mergen oder aktivieren.
- B3-Basis: `21717037e6f5dfcf21177dc4f56f08eb72e43ae9`.
- B3-Code-/Test-/Architekturstand: `3a4a439707a41ce0cae648c78074315bc2830c86`.
  Diese Übergabe folgt als eigener Dokumentationscommit; aktuellen HEAD/CI auf
  GitHub zusätzlich prüfen. B1/B2-Dateien und bisherige Tests sind erhalten.
- `dev`/`stable`, Installer, Docker und produktiver Sprachweg unverändert.

Zuerst [Integrationsplan](analog-voice-integration-plan.md), [B3](analog-voice-b3.md),
[B2-API](analog-voice-b2.md) und [Sperrregeln](../dev/MurmurLocking.md) lesen.
Keine neue Gesamtanalyse oder neue Filterentwicklung notwendig.

## Neue Schnittstellen

`tools/analog-voice/include/AnalogVoiceService.h`:

- Konstruktion mit Parameters/Wake/optional Clock; vier Codecs, ein Worker,
  begrenzte Arrays. Alternativ vier injizierte `Processor` für Tests.
- `connect`/`update`/`disconnect`: Lebensdauer und Kanal-/Sender-Routingepochen.
  `setReceive` für reine Empfangsänderungen (z. B. Deaf/SelfDeaf): streicht die
  Identität aus allen wartenden Empfängerschnittmengen, erhält eigene Sendungen.
- `open(sender,bypass)` -> generationgebundenes `Handle`; Ressourcenmangel ohne
  Codecplatz latcht Raw-Bypass, ohne Ticketplatz explizite fehlgeschlagene Annahme.
- `submit(handle,Input)` kopiert Payload und Empfänger, liefert Receipt/Ticket.
  Input umfasst echte Paketdauer in 48-kHz-Samples, Quellframe und Endeflag.
  `Admission` muss ausgewertet werden; keine stillschweigende direkte Weiterleitung.
- `finish` reserviert technischen Abschluss unabhängig von Datenkapazität.
  `cancel`/`invalidateChannel` verwerfen auch laufende Ergebnisse.
- Wake nur queued, niemals synchron zurück in Server. Hauptthread:
  `acknowledgeNotification()` einmal, dann `take(Batch&)` bis false.
  Ergebnis enthält B2-Pakete, Quelltickets, effektive Empfängerschnittmenge,
  Bypass-/Fehler-/Terminalstatus. Auch leere Batches tragen Kontrollinformation.
- `statistics` und `fixedStorageBytes`; `shutdown` außerhalb aller Serverlocks,
  idempotent, ein Lifecycle-Owner. Callbackkontext bis zum Join am Leben halten.

`SimplexController::Transmission::started` ist ein additives Bit genau an der
bestehenden Annahmeentscheidung Idle→Owning. Es ist unabhängig vom Logger und von
Frame 0. Auch eine Ein-Paket-Sendung kann `allowed && started` zurückgeben, obwohl
Simplex ihren Kanal beim Marker schon freigibt. Regeln und Guard-Lebensdauer des
produktiven Pfads sind unverändert. Nicht aus Ereignislogs oder Frame-Resets neue
Übertragungsgenerationen ableiten.

`AnalogVoiceStream::abort(Result&)` ist die additive externe Fault-Schnittstelle:
nur alte Reserve einmal als Fault, ohne Codec/DSP/Nachlauf; kein physischer Marker.
Für echte Invalidierung weiterhin vollständigen Verwurf statt Fehlerabschluss.

## Konkrete B4-Voraussetzungen und offene Grenzen

1. Dienstbesitz/Start/Stop pro Server, experimenteller standardmäßig ausgeschalteter
   Schalter und ein explizites Budget für virtuelle Server planen. Limits heute:
   4 Codecs, 16 Übertragungen, 128 Verbindungen, 64 Empfänger; etwa 866 KiB feste
   Dienstpuffer plus Codecs/Stacks. Keine Behauptung gemessener VPS-Leistung.
2. Erst nach bisherigen Auth-/Rechte-/Simplex-Prüfungen und Empfängerauswahl
   `started` auswerten. Noch unter Voice-Lock und gültigem Guard `open`/`submit`
   atomar in die Kanalordnung übernehmen. Wenn ein gerade geöffnetes Handle kein
   Ticket annimmt, den leeren Versuch kontrolliert schließen/canceln. Nicht den
   Guard verkürzen, bevor der alternative Vertrag für den konkreten Serverzweig
   getestet ist. Ungefilterte Nachfolger müssen ebenfalls hinter wartende Barrieren.
3. Alle Kontrollpfade auf echte Senderechts-/Routing- und Empfangsänderungen prüfen:
   Kanalwechsel, Disconnect, Mute/Suppress/ACL, Deaf/SelfDeaf und Kanalentfernung.
   Senderänderungen invalidieren; reine Empfangsänderungen mit `setReceive`
   dürfen keine zusätzliche Sendesperre erzeugen. Zustände unter Voice-Schutz
   synchronisieren. Lebensdauerkennungen dürfen nicht durch Sessions ersetzt werden.
4. Release-Callback beim bestätigten Marker darf den noch anzunehmenden letzten
   Payload nicht invalidieren. Timeout dagegen genau einmal als `finish` unter
   bestehender Simplex-Ordnung reservieren; dort keinen Voice-Lock erwerben.
   Service-Fault weder als bestätigtes Quellende noch als neuen Simplex-Release
   zurückmelden. Das nötige Verbindung→Handle-Mapping ist begrenzt zu halten.
5. Kontextgebundene koaleszierte Qt-Notification anschließen. Unter angemessenem
   Voice-/Lebensdauerschutz Batch übernehmen, Empfänger neu auflösen und Rechte
   unmittelbar vor lokaler Netzwerkübergabe erneut prüfen. Dienstmutex vorher
   freigeben. Reentranz, Lockkonflikt/Wiederaufwecken und Shutdown-Kontext testen.
6. Positions-/Volume-/Protokollmetadaten am ursprünglichen Ticket ergänzen und am
   ersten beitragenden Quellanteil verankern. Bestehende Verpackung/Versandhelfer
   gemeinsam benutzen, kein zweites Routing. Aktuell keine echte Zustellung und
   kein Nachweis von Protokoll-/Metadatenidentität im Server.
7. Die kumulative Empfängerschnittmenge ist absichtlich strenger als reine
   Halbframe-Schnittmengen: keine neuen Empfänger mitten in einer Generation.
   Vor Betriebsfreigabe diesen Verfügbarkeitsnachteil sowie die sichtbare
   Faulted-/Paketverlustpolitik bewerten. Keine stillschweigende Erweiterung.
8. Ausgangsuhr pro Verbindungslebensdauer erhalten. Nicht Quellenframes umschreiben.
   Bypass bleibt unverändertes Original-Opus; dessen Quellreset ist keine Garantie
   einer monotonen Raw-Uhr. Ungefilterter/gefilteter Encoderwechsel und reale
   Client-Jitterbuffer müssen im späteren Live-Test bewertet werden.
9. Echte Serverintegration zusätzlich testen: deaktivierter/fehlender Schalter
   byteidentisch, UDP/TCP/Transportwechsel, Verschlüsselung, tatsächliche Qt-Wakes,
   langsame Empfänger, Kontrolländerungen/Shutdown und bestehende Serversuite.
   B3 beweist den isolierten Dienst und den echten Guard-Vertrag, keine komplette
   Server-Sperrhierarchie mit echten Netzwerkempfängern.

B5-Effekt-Lifecycle nicht vorziehen. Quelltickets können in mehreren Ausgangsspans
erscheinen; künftige Squelch-Beobachtungen deduplizieren und nie PLC/Padding als
Originalpaket zählen. B6-Hör-/VPS-Tests und separate Betriebsfreigabe bleiben offen.

## Reproduzierbare lokale Prüfung

Lokal vorhandene Abhängigkeiten via `/workspace/.cloud-deps/activate.sh`; diesen
Pfad in einem neuen Chat zuerst prüfen. C++17, CMake, Ninja, libopus-dev, Python 3
für Offline-Tests, optional Qt5/Qt6-Core für den Guard-Test. Keine neuen
System-/Docker-Installationsverfahren. Buildverzeichnisse hier unter
`/workspace/work/b3/{build,asan,tsan,root,simplex}`.

```sh
cmake -S tools/analog-voice -B work/b3-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=OFF \
  -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_SERVICE=ON \
  -DVOICE_BUILD_SIMPLEX_TESTS=ON -DVOICE_BUILD_PREVIEW=ON
cmake --build work/b3-release --parallel 2
ctest --test-dir work/b3-release --output-on-failure

cmake -S tools/analog-voice -B work/b3-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_UNITY_BUILD=OFF \
  -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_SERVICE=ON \
  -DVOICE_BUILD_SIMPLEX_TESTS=ON -DVOICE_BUILD_PREVIEW=ON -DVOICE_SANITIZERS=ON
cmake --build work/b3-asan --parallel 2
ctest --test-dir work/b3-asan --output-on-failure

cmake -S tools/analog-voice -B work/b3-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_UNITY_BUILD=OFF \
  -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_SERVICE=ON \
  -DVOICE_BUILD_SIMPLEX_TESTS=ON -DVOICE_THREAD_SANITIZER=ON
cmake --build work/b3-tsan --target voice-service-test voice-service-simplex-test --parallel 2
ctest --test-dir work/b3-tsan -R VoiceService --output-on-failure

cmake -S . -B work/b3-root -G Ninja -Dclient=OFF -Dserver=OFF -Dtests=ON \
  -Dplugins=OFF -Doverlay=OFF -Dlto=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=OFF -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_SERVICE=ON
cmake --build work/b3-root --target voice-dsp-test voice-stream-test voice-service-test --parallel 2
ctest --test-dir work/b3-root -R '^Voice(Dsp|Stream|Service)$' --output-on-failure
python3 tests/funk-install/test_source.py
(cd tools/analog-voice && sha256sum -c SHA256SUMS)
git diff --check
```

Ergebnisse: Release **6/6**, ASan/UBSan **6/6**, TSan **2/2**, Haupt-CMake **3/3**,
Source-Guards **29/29**, beide unveränderten DSP-Hashes. Zusätzlich vorhandener
`TestSimplex` unverändert mit Controller/Logger separat unter Qt6/AUTOMOC gebaut
und bestanden; kein vollständiger Murmur-Build. Keine final fehlgeschlagenen
lokalen Tests. Externe libopus/Qt nicht sanitizer-instrumentiert. CMake meldet die
bestehende Deprecation der `FindPythonInterpreter`-Submodule-Minimumversion;
keine unterdrückten C++-Compilerwarnungen.

`.github/workflows/analog-voice.yml` prüft Release, ASan/UBSan und TSan ohne
Docker. Bestehende Server-CI bleibt separat. Deren aktuellen Status auf PR #15
prüfen; insbesondere alte Noise-/Squelch-Timingfehler nicht mit B3 gleichsetzen
oder durch pauschal erhöhte Wartezeiten verstecken.

**Nach B3 anhalten. Kein automatischer B4-Start, Merge, Deployment oder Release.**
