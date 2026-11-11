# Analoge Sprachfilter: B2 – isolierter Live-Opusadapter

Stand: 7. November 2026. Ausgangspunkt `dev` bei
`e080e9bec` (PRs #12–#14 enthalten), Arbeitsbranch `feature/analog-voice-live`.
Ausschließlich B2; keine Serverintegration oder produktive Aktivierung.

## Dateien und Datenfluss

- `tools/analog-voice/include/AnalogVoiceStream.h`: öffentliche, Qt-freie API.
- `tools/analog-voice/src/AnalogVoiceStream.cpp`: ein Decoder und ein Encoder je
  Instanz, fester PCM-Puffer, unveränderter `VoiceProcessor`, Reblocking, Reserve.
- `tools/analog-voice/tests/stream_test.cpp`: synthetischer externer Opus-Sender,
  unabhängige Referenzkette, Zeit-/EOF-/Fehler- und Lebenszyklustests.
- `tools/analog-voice/CMakeLists.txt`: optionales statisches Target
  `funk-voice-stream`, verlinkt `funk-voice-dsp` und vorhandene libopus.
  Gemeinsame Opus-Suche für Live und Offline im übergeordneten CMake.

Externe Opus-Payload → Mono-Decoder bei 48 kHz (maximal 5760 Samples) → DSP-Aufrufe
mit höchstens 960 Samples → Sammlung in 960er-Blöcken → neuer Opus-Encoder →
20-ms-Ausgangspakete. Kein Offline-Opus-Roundtrip, RMS-Abgleich oder zusätzliche AGC.
Die freigegebenen Profile und beide DSP-Dateien bleiben bytegleich; beide
SHA-256-Prüfsummen in `tools/analog-voice/SHA256SUMS` sind bestätigt.

Encoder: 48 kHz Mono, `OPUS_APPLICATION_VOIP`, 40000 bit/s, Komplexität 5,
kein DTX oder Inband-FEC; übrige libopus-Standardwerte. Der Konstruktor fragt
`OPUS_GET_LOOKAHEAD` ab. Unterstützt sind 0…480 Samples eigener Lookahead, damit
der Abschluss höchstens ein zusätzliches Paket benötigt. Ungeeignete Codec-
Initialisierung wirft eine Exception; der spätere Dienst muss vor Annahme einer
Generation gesunde Instanzen reservieren. Kein eigener Opus-Bibliotheksimport.

## API und Verantwortung des Aufrufers

`AnalogVoiceStream(parameters)` initialisiert DSP und Codecs. Nur die Konstruktion
allokiert Codec-Speicher. `begin(firstSourceFrame, outputFrameFloor = 0)` setzt DSP und beide Codecs für
eine neue Sendung zurück; während `Active` lehnt es einen Neustart ab.
`reset()` verwirft eine Generation ohne Ausgabe und stellt `Idle` her; der nächste
`begin()` initialisiert den Codec-Zustand erneut. Ausgangsframezahlen bleiben
auch bei Abort erhalten. Für einen neuen Verbindungslebenszyklus eine neue Instanz
verwenden. Aufrufe pro Instanz müssen seriell erfolgen; keine internen Locks/Threads.

`process(Input, Result&)` nimmt eine geliehene Payload nur für diesen Aufruf an:

- `payload`, `bytes`: nacktes Opus ohne Mumble-Hülle, maximal 1024 Bytes.
  Der gesamte spätere Mumble-Datagrammrahmen muss zusätzlich das bestehende
  1024-Byte-Limit erfüllen; dessen Prüfung gehört zum späteren Serveradapter.
- `frame`: Quellstart in 10-ms-Einheiten, `uint64_t`.
- `end`: bestätigte Endmarkierung; Payload wird vor dem Abschluss verarbeitet.

Unterstützt: gültige Mono-Pakete mit Gesamtdauer in ganzen 10-ms-Einheiten bis
120 ms. Die Tests verwenden 10/20/40/60 ms sowie echte 120-ms-Mehrfachpakete.
Gültige 2,5-/5-ms-Gesamtdauern und Stereo werden ausdrücklich abgelehnt.
`opus_packet_parse` prüft auch die Framing-Struktur, nicht nur die TOC-Dauer.
Kein stilles Downmixing oder Runden von Medienzeit.

`Result` ist ein vom Aufrufer vorgehaltener fester Batch. `count` enthält die
verwendbaren Pakete, auch beim Fehlerabschluss; Einträge darüber sind unbeachtlich.
Payloads und Metadaten gehören dem Batch und müssen vor Wiederverwendung kopiert
oder verbraucht werden. Eingangsbytes dürfen den Result-Speicher nicht überlappen.
`Disposition` unterscheidet Annahme, Ignorieren und Ablehnung; `error` und
`codecError` liefern eigene Fehlerklasse beziehungsweise libopus-Rückgabewert.
Bei gelatchtem Zustand werden weitere Eingaben ohne Ausgabe ignoriert.

`finish(Result&)` ist ein expliziter technischer Abschluss, etwa auf eine später
vom Controller bestätigte Timeout-Anforderung. Er führt denselben EOF-Drain aus,
kennzeichnet ihn aber als `End::Technical`. Wiederholte Abschlüsse erzeugen nichts.
Keine Uhr und keine selbst erfundene PTT-/Timeout-Entscheidung im Adapter.

## Sequenzen, Lücken und Quellzuordnung

Quell- und Ausgangscursor sind getrennt. Eingangsfortschritt ist Paketdauer / 10 ms,
Ausgangsfortschritt je 20-ms-Paket genau 2. Ein neuer Start wählt mindestens den
bisher nächsten Ausgangsframe und mindestens den Quellenstart sowie den optional
extern mitgeführten `outputFrameFloor`. Damit kann B3 den Ausgangscursor einer
Verbindung beim Wechsel einer Codecinstanz erhalten, ohne die Quellnummer zu
verfälschen. Keine 60-Sekunden-
Modulo-Uhr oder Rücksetzung auf 0 zwischen kurzen Sendungen. Additionen werden auf
Überlauf geprüft; ein ungültiger Start verschiebt den Ausgangscursor nicht.

Veraltete, überlappende und doppelte Payloadpakete werden ignoriert, ohne Decoder,
DSP, Cursor oder Endmarkierung zu verändern. Kein Reordering-Puffer. Ein leerer
Terminator darf am Startframe des letzten angenommenen Payloadpakets oder am
nächsten Quellcursor liegen. Ein leerer Terminator mit Vorwärtssprung wird
abgelehnt: seine fehlende Sprachdauer kann B2 nicht bestimmen. Ein Stream ganz
ohne Sprachpayload endet ohne erfundenes Opus-Paket.

Ein Vorwärtssprung vor einer Payload erzeugt höchstens vier 10-ms-PLC-Blöcke über
den vorhandenen Decoder und DSP. Bei größeren Lücken und einem offenen Halbframe
werden drei PLC-Blöcke verwendet, damit vor der übersprungenen Zeit kein halber
PCM-Block offen bleibt. Die restliche Lücke verschiebt den Ausgangscursor sichtbar;
sie wird nicht mit Nullsamples aufgefüllt oder durch Neunummerierung verborgen.
`concealedFrames` und `skippedFrames` nennen beide Teile der Quelllücke. Codecs und
DSP werden innerhalb dieser Generation nicht neu gestartet. Qualität nach großen
Lücken ist damit nicht garantiert; PLC erfindet keinen nachgewiesenen Originalinhalt.

Jedes Ausgangspaket enthält bis zu acht `Span`s über seine nominale dekodierte
960-Sample-Zeitachse: `Origin`, `sourceFrame`, `sourceOffset`, `outputOffset`,
`samples`. `Source` verweist auf ein angenommenes Quellpaket und dessen dekodierte
Sampleposition. `Concealment` verweist auf fehlende 10-ms-Quellframes.
`EncoderDelay` und `EndPadding` kennzeichnen technische Anteile.
Der eigene Encoder-Lookahead ist in dieser Zuordnung bereits berücksichtigt:
Quellanteile können daher erst in einem späteren Ausgangspaket erscheinen.

Diese Spans sind **keine Sicherheitsgrenze für akustischen Einfluss**: DSP- und
Codec-Gedächtnis kann über die nominalen Intervalle hinausreichen. Für B3/B4 gelten
weiterhin Sender-/Routinggenerationen, Berechtigungssnapshots, Schnittmenge statt
Vereinigung beim Zusammenfassen und Prüfung unmittelbar vor Zustellung gemäß Plan.
Kein Zusammenfassen über Routingepochen hinweg. Der Aufrufer ordnet `sourceFrame`
seinen externen Tickets/Metadaten zu und muss diese bis zum letzten zugehörigen
Span einschließlich Lookahead aufbewahren. B2 enthält selbst keine Tickets,
Empfänger, Squelch-Beobachtungen oder Kanalbarrieren.

## EOF, Reserve und Fehlervertrag

Unter 960 echte Samples warten auf Folgepakete; kein Zwischenpadding. Genau ein
bereits kodiertes Paket bleibt intern ungesendet. Erst der nächste erfolgreiche
Encode gibt diese Reserve frei. Bei EOF werden zuerst Nutzdaten verarbeitet,
danach Nullsamples für den tatsächlichen eigenen Encoder-Lookahead und nötiges
Framepadding angehängt. Nur das endgültige letzte Paket erhält
`End::InputMarker` beziehungsweise `Technical`. Keine doppelte Endmarkierung,
kein wiederverwendetes altes Paket als normaler EOF und kein eingangsseitiger
312-Sample-Abzug. Der unbekannte Client-Lookahead bleibt erhalten.
Der eigene Lookahead und die bis zu 20 ms Reserve sind reale Zusatzlatenz.

Zustände: `Idle` → `Active` → `Ended`; Ablehnungen latchen `Bypass` vor dem ersten
erfolgreichen neuen Encode, danach `Faulted`. Das ist konservativer als die
Planfreigabe vor erstem Versand. Ein bereits kodierter, noch ungesendeter Stream
wird ebenfalls nicht zu Original-Opus zurückgeschaltet.

Ein fehlgeschlagener Aufruf veröffentlicht **keine Teilausgabe dieses Aufrufs**.
Nur seine schon vorher vorhandene Reserve wird gegebenenfalls einmal mit
`End::Fault` ausgegeben. Fehlt diese, gibt es keinen künstlichen Fehlerterminator.
Bereits früher übergebene Pakete können nicht zurückgenommen werden. Verworfene
Kodierungen können eine sichtbare Lücke vor der nächsten Generation hinterlassen;
die Ausgangsuhr läuft niemals zurück. `Fault` ist kein bestätigtes PTT-Ende.
Neustart erst durch explizites `begin()` einer neuen, extern legitimierten Sendung.
Auch Bypass leitet selbst keine Originalpakete weiter; ein späterer Dienst muss
dafür die Originale vorhalten und seine Generation insgesamt entscheiden.

Feste Grenzen je Instanz/Aufruf:

| Ressource | Grenze |
| --- | --- |
| Decoder-PCM | 5760 float-Samples |
| Reblock-PCM | 960 float-Samples; zwischen Aufrufen höchstens 480 echte Rest-Samples |
| PLC je Lücke | maximal 1920 Samples, vier 10-ms-Aufrufe |
| Abschlussreserve | ein kodiertes Paket |
| Ausgangs-Payloadspeicher | 1275 Bytes je Paket; tatsächliche Länge separat |
| Result | zehn Pakete einschließlich Reserve und EOF |
| Quellmetadaten | acht Spans je Paket plus feste PCM-/Lookahead-Metadaten |

Der Maximalbatch ist getestet: vorhandene Reserve + Halbframe + 40 ms PLC +
120-ms-Eingang + EOF. Keine internen Queues, Heap-Allokationen im Verarbeitungscode,
Threads, Netzwerkzugriffe oder dynamisches Pufferwachstum. B3 muss seinen eigenen
Ausgangsplatz **vor** dem Aufruf reservieren, damit ein voller Verbraucher nicht
nachträglich Pakete aus einer laufenden Generation verliert.

## Build und tatsächlich ausgeführte Prüfung

Voraussetzungen: C++17, CMake ≥ 3.15, libopus-Header/-Bibliothek. Python 3 nur für
die optionalen ursprünglichen CLI-Tests. `VOICE_BUILD_LIVE=OFF` und
`VOICE_BUILD_PREVIEW=OFF` bleiben Standard; DSP-only bleibt Opus-/Python-/Qt-frei.
Murmur wird mit dem neuen Target nicht verlinkt.

```sh
git submodule update --init --recursive
cmake -S tools/analog-voice -B work/b2-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=OFF \
  -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_PREVIEW=ON
cmake --build work/b2-release --parallel 2
ctest --test-dir work/b2-release --output-on-failure

cmake -S tools/analog-voice -B work/b2-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_UNITY_BUILD=OFF \
  -DVOICE_BUILD_LIVE=ON -DVOICE_BUILD_PREVIEW=ON -DVOICE_SANITIZERS=ON
cmake --build work/b2-sanitized --parallel 2
ctest --test-dir work/b2-sanitized --output-on-failure

cmake -S . -B work/b2-root -G Ninja -Dclient=OFF -Dserver=OFF -Dtests=ON \
  -Dplugins=OFF -Doverlay=OFF -Dlto=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=OFF -DVOICE_BUILD_LIVE=ON
cmake --build work/b2-root --target funk-voice-stream voice-stream-test voice-dsp-test --parallel 2
ctest --test-dir work/b2-root -R '^Voice(Dsp|Stream)$' --output-on-failure

cmake -S tools/analog-voice -B work/b2-dsp-only -G Ninja -DVOICE_BUILD_TESTS=OFF
cmake --build work/b2-dsp-only --parallel 2
(cd tools/analog-voice && sha256sum -c SHA256SUMS)
git diff --check
```

Lokal: GCC 14.2.0, CMake 3.31.6, libopus 1.5.2, Python 3.13.5,
Formatierung mit clang-format 10.0.1. Vorhandene Abhängigkeiten aus
`/workspace/.cloud-deps/activate.sh`; Buildverzeichnisse unter `/workspace/work/`.
Keine Systempaket-/Docker-/Installationsänderung.

Gemessener eigener Lookahead: 312 Samples (6,5 ms). Auf dieser x86_64-Umgebung
belegt die Stream-Klasse 29240 Bytes zuzüglich der beiden Opus-Zustände;
ein vorgehaltener Result-Batch belegt 15080 Bytes. Temporäre feste Stackpuffer
kommen während eines Aufrufs hinzu; diese Werte sind keine plattformunabhängigen ABI-Garantien.

- Release einschließlich Offline-Bestand: **4/4** bestanden (`VoiceDsp`,
  `VoiceStream`, `VoiceCodec`, `VoiceCli`).
- ASan/UBSan-Debug einschließlich Offline-Bestand: **4/4** bestanden,
  keine Sanitizer-Befunde im instrumentierten Projektcode. Die vorhandene
  dynamische libopus ist selbst nicht sanitizer-instrumentiert.
- Haupt-CMake, gezielte Analog-Voice-Targets: **2/2** bestanden.
- Reiner DSP-Library-Build erfolgreich; DSP-Hashes unverändert.
- Live-Test: alle Profile/Dauern, gemischte Halbframes, vollständig dekodierbare
  Outputs, bytegenaue unabhängige Referenz, erste/letzte Signalenergie, nominale
  Samplezuordnung, Payload-/Leerterminator, technischer Abschluss, Reserve,
  Maximalbatch, PLC und sichtbare Restlücken, Duplikate/Überlappung, 62,02 Sekunden
  fortlaufende Medienzeit, Wiederholungsstarts und isolierte Instanzen.
- Fehler: malformed Framing, falsche Puffer, 2,5/5 ms, Stereo, Sequenzüberlauf,
  gelatchte Zustände, Reset. Unter Linux/GNU-kompatiblem Linker zusätzlich echte
  libopus-Aufrufe via `--wrap` gezielt mit Encoder-/Decoderfehlern ersetzt;
  Fehler beim EOF und nach mehreren Encodes eines Batches geprüft.
  Kein Fehler-Injektionsschalter im Produktivcode. C++-Allokationszähler prüft
  normale `process()`- und Neustartpfade; kein vollständiger C-malloc-Interposer.

Während der Entwicklung gefunden und korrigiert: zu kleiner Repacketizer-Puffer
im Testgenerator (nun libopus-eigene RAII-Allokation) und vergifteter Ausgangscursor
nach abgelehntem Start nahe `uint64_t`-Überlauf. Der GCC-14-Fehlalarm beim
eingebetteten Test-new/delete-Paar wird durch nicht-inline Allokationshooks
vermieden; Compilerwarnungen werden nicht abgeschaltet.

Keine abschließend fehlgeschlagenen lokalen B2-/B1-Tests. Kein Murmur-Vollbuild,
keine Aussage über die gesamte Serversuite oder GitHub-CI, keine Geräte-/Live-
Hörtests oder VPS-Lastmessung. Synthetische Referenzgleichheit beweist keine
Klanggleichheit zur Offline-Kette mit deren RMS-Abgleich. Lange Lücken, reale
Clientpaketierung, Latenz, Empfängerwechsel und Effekt-Reihenfolge bleiben spätere
Integrations-/Hörtestthemen. Weiterarbeit ausschließlich nach neuem Auftrag:
[B3-Übergabe](analog-voice-b3-handoff.md).

## Additive B3-Ergänzung: externer Fehlerabschluss

B3 ergänzt `abort(Result&)` und `Error::Aborted`: Bei `Active` wird nur eine
bereits kodierte, noch ungesendete Reserve einmal mit `End::Fault` zurückgegeben.
Kein Decoder/DSP/Encoder, kein Padding, kein Fortschalten der Medienuhr und kein
bestätigtes Quellende. Ohne vorheriges Encode entsteht `Bypass`, sonst `Faulted`;
weitere Abort-Aufrufe sind ohne Ausgabe. `reset()` bleibt der vollständige
Verwurf ohne Abschluss, insbesondere für echte Identitäts-/Rechteinvalidierung.
Der zusätzliche Test `externalAbort()` prüft leeren Start, Halbframe, vorhandene
Reserve, Uhr und Wiederholung; der bisherige B2-Testbestand bleibt erhalten.
