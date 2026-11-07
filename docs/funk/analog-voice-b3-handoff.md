# Übergabe für einen neuen Codex-Chat: nach B2

Stand: 7. November 2026. **B2 ist implementiert und lokal geprüft. B3 wird hier
nicht begonnen.** Diese Datei erteilt keinen Auftrag für weitere Schritte.
Vor Weiterarbeit den neuen ausdrücklichen Auftrag und aktuellen GitHub-Stand lesen.

## Branch und geprüfte Quellbasis

- Repository: <https://github.com/Shawna7han/mumble-funk>.
- Fortbestehender Feature-Branch:
  [feature/analog-voice-live](https://github.com/Shawna7han/mumble-funk/tree/feature/analog-voice-live).
  Auch B3–B6 sollen nach separatem Auftrag auf diesem Branch stattfinden.
- Ausgangs-`dev`: `e080e9beca404900c737c048293d0c77a43cd1aa`, enthält PRs #12–#14.
- B2-Adapter, Tests und Dokumentation: Commit
  `9d007a82e273349d9eb361ed14950d9d8230b075`.
- Ergänzung des externen Ausgangsuhr-Minimums und zugehöriger Test: Commit
  `039f3b32516e8fef69d0e569f2e2cc13ec94f8ae` (**geprüfter B2-Codestand**).
  Diese Übergabe folgt als eigener Dokumentationscommit; aktuellen Branch-HEAD
  und den offenen Draft-PR gegen `dev` zusätzlich prüfen.
- `dev` und `stable` bleiben unverändert. Kein Merge, Release, Deployment oder
  produktiver Schalter. Das Gesamtfeature ist unvollständig und nicht freigegeben.

## Maßgebliche Dokumentation

- [Integrationsplan](analog-voice-integration-plan.md), insbesondere Abschnitte
  6/8 und B3 in Abschnitt 11. Keine vollständige Neuanalyse der Serverarchitektur.
- [B2 API, Medienuhr, Fehlervertrag, Grenzen und Testbefehle](analog-voice-b2.md).
- [B1-Dokumentation](analog-voice-b1.md) und [B1→B2-Übergabe](analog-voice-b2-handoff.md)
  bleiben historische Nachweise; B2 ist nun durch den separaten Auftrag umgesetzt.

## Implementierte Bausteine

`tools/analog-voice/include/AnalogVoiceStream.h` und
`tools/analog-voice/src/AnalogVoiceStream.cpp`: isolierte C++17-Klasse im Namespace
`funkvoice`, keine Qt-/Socket-/Thread-Abhängigkeit. Genau ein Decoder und ein neuer
Encoder pro Instanz; keine Offline-Roundtrips oder dateiweise Pegelanpassung.
Der vorhandene `VoiceProcessor` und alle drei Profile sind unverändert.

`tools/analog-voice/CMakeLists.txt` stellt optional `funk-voice-stream` bereit:
`VOICE_BUILD_LIVE=ON` aktivieren, libopus-Entwicklungsdateien bereitstellen.
Standard OFF; keine Verlinkung in Murmur. `funk-voice-dsp` bleibt eine einzige,
Opus-/Qt-freie DSP-Bibliothek. `VOICE_BUILD_PREVIEW` ist unabhängig optional.

Wichtige API:

- Konstruktion mit `Parameters` allokiert Codecs; Initfehler werfen Exceptions.
- `begin(sourceFrame, outputFrameFloor = 0)` startet eine neue Sendung und setzt
  DSP/Decoder/Encoder zurück. `Active` kann nicht dadurch überschrieben werden.
  Ausgangsstart ist das Maximum aus internem Cursor, Quellenstart und externem
  Minimum. B3 muss die Ausgangsuhr je Verbindung über Codecslotwechsel hinweg
  erhalten und über das Minimum weitergeben; Quellzahlen dafür nicht verfälschen.
- `process(Input, Result&)`: geliehene externe Opus-Bytes, Quellframezahl in
  10-ms-Einheiten und optionaler bestätigter Endmarker. Unterstützt Mono, 48 kHz,
  ganze 10-ms-Gesamtdauern bis 120 ms. 2,5/5 ms und Stereo werden abgelehnt.
- `Result`: festes Array mit zehn Ausgangsplätzen, `count`, `Disposition`,
  `Error`, libopus-Fehlercode, PLC-/Restlückenlängen. Result vor Wiederverwendung
  verbrauchen; Eingang darf dessen Speicher nicht überlappen.
- `Packet`: 20-ms-Payload mit eigener Framezahl, `End` und bis zu acht nominalen
  Sample-Spans (`Source`, `Concealment`, `EncoderDelay`, `EndPadding`).
  `sourceFrame` und `sourceOffset` identifizieren echte Eingangsanteile.
- `finish(Result&)` schließt technisch, ohne physischen PTT-Release zu behaupten.
  `reset()` verwirft eine Generation; Ausgangsuhr bleibt erhalten.

## Verbindliche Eigenschaften für den späteren Dienst

5760er-Decoderpuffer → DSP-Stücke ≤960 → Reblocking auf 960 → 20-ms-Encode.
Ein halber 10-ms-Block wartet auf echte Folgedaten. Ein kodiertes Paket bleibt
als ungesendete Reserve erhalten. EOF verarbeitet zuerst Payload, dann den
abgefragten **eigenen** Lookahead (lokal 312 Samples) und nötiges Framepadding.
Nur das echte letzte Paket erhält einen Endmarker. Keine eingangsseitige
Entfernung eines angenommenen Client-Lookaheads.

Quellfortschritt ist die tatsächliche Eingangsdauer; Ausgangsfortschritt jeweils
2. Keine Uhr-Modulo-Grenze bei 60 Sekunden. Stale/überlappende/duplizierte Payloads
werden ohne Zustandsänderung ignoriert. Leerer Terminator ist am letzten
Payloadstart oder am nächsten Quellcursor erlaubt, aber nicht an einem neuen
unbekannten Vorwärtssprung. Keine Sprachpayload bedeutet keine erfundene Ausgabe.

Lücken: maximal 40 ms Decoder-PLC/DSP; bei Halbframe und langer Lücke 30 ms,
um vor dem Zeitsprung den offenen Block zu vervollständigen. Restlücke erscheint
als Ausgangsframe-Sprung. Kein unbegrenztes Aufholen und keine Stille mitten in
der Sendung. Zustand bleibt während dieser Generation erhalten.

Fehler latchen vor erstem erfolgreichen Encode `Bypass`, danach `Faulted`.
Es gibt keine automatische Original-Opus-Weiterleitung und keinen Midstream-
Codecwechsel. Ein fehlerhafter Aufruf verwirft seine gesamte neue Ausgabe;
höchstens seine **vor dem Aufruf** vorhandene Reserve wird einmal als `End::Fault`
zurückgegeben. Auch bei `Rejected` deshalb `Result.count` berücksichtigen.
Ein Fault-Abschluss ist kein bestätigtes Quellende und darf keinen Simplex-Release
auslösen. Erst eine extern legitimierte neue Sendung darf `begin()` auslösen.

B3 muss begrenzten Ausgangsplatz vor der Verarbeitung reservieren und Input-
Originale für eine vollständige Bypass-Entscheidung vorhalten. Bei Fehlern müssen
Generation/Ergebnisse nach dem Plan entwertet werden. Bereits vorher ausgelieferte
Ergebnisse kann der synchrone Adapter nicht zurückrufen.

Die Spans enthalten den nominalen eigenen Encoder-Lookahead. Zuordnungen daher
bis in das Nachlaufpaket erhalten. Das sind keine Berechtigungen und keine exakten
Grenzen des DSP-/Codec-Gedächtnisses. Nicht über Sender-/Routingepochen reblocken.
B3/B4 benötigen weiterhin konservative Snapshot-Schnittmengen, Positionsanker am
ersten echten Quellanteil und erneute Berechtigungsprüfung vor Zustellung.
PLC-/Padding-Spans sind keine neu empfangenen Quellpakete und dürfen keine
zusätzlichen Squelch-Beobachtungen erfinden. Die späteren Tickets, Barrieren und
genau einmal auszuführenden Beobachtungen sind ausdrücklich noch nicht implementiert.

## Prüfung und technische Voraussetzungen

Ausgeführte Befehle vollständig in [B2](analog-voice-b2.md). Lokal GCC 14.2.0,
CMake 3.31.6, libopus 1.5.2, Python 3.13.5 und clang-format 10.0.1.
Vorhandene Cloud-Abhängigkeiten via `/workspace/.cloud-deps/activate.sh`,
Builds unter `/workspace/work/b2-{release,sanitized,root,dsp-only}`.
In einem neuen Chat diese Pfade nicht als gegeben voraussetzen.

- Release: **4/4** (`VoiceDsp`, `VoiceStream`, `VoiceCodec`, `VoiceCli`).
- ASan/UBSan-Debug: **4/4**, keine Befunde im instrumentierten Projektcode;
  die vorhandene dynamische libopus ist selbst nicht instrumentiert.
- Haupt-CMake, Analog-Voice-Targets ohne Unity: **2/2**.
- DSP-only-Library gebaut, beide freigegebenen SHA-256-Werte bestätigt.
- Reproduzierbare synthetische Tests: alle Profile, 10/20/40/60/120 ms, Referenz-
  Payloadgleichheit und Dekodierbarkeit, erste/letzte Energie, EOF/Reserve,
  Quellzuordnung, leerer/Payload-Terminator, PLC/Restlücken, Duplikate/Überlappung,
  Maximalbatch, 62,02 Sekunden Audio, Reset, Instanztrennung, externe Ausgangsuhr.
- Linux-Tests injizieren Decoder-/Encoderfehler per Linker-Wrapper, einschließlich
  Batch- und EOF-Fehler. Keine Testhooks im Adapter. C++-Allokationszähler in Tests;
  kein vollständiger C-malloc-Nachweis für die externe Bibliothek.

Keine abschließend fehlgeschlagenen B2-/B1-Tests. Kein Murmur-Vollbuild,
Server-/Gerätehörtest, VPS-Lasttest oder Produktionsnachweis. Keine Aussage über
die vollständige Serversuite/GitHub-CI. Reale Clientpaketierung, Qualität bei
Lücken, zusätzliche Latenz, Routingwechsel und Reihenfolge gegenüber Effekten
müssen später geprüft werden. Keine Klanggleichheitszusage zur RMS-angepassten
Offline-Vorschau. B3 ist der asynchrone Dienst/Ordnungsvertrag; Serverintegration,
Effekt-Lifecycle und Betriebsfreigabe bleiben B4/B5/B6.

**Nach dieser Übergabe anhalten. Kein automatischer B3-Start oder Merge nach `dev`.**
