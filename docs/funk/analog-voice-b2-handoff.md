# Übergabe für einen neuen Codex-Chat: nach B1

Stand: 7. November 2026. **B1 ist umgesetzt; B2 ist nicht freigegeben.**
Zuerst den neuen ausdrücklichen Auftrag lesen und aktuellen GitHub-Stand prüfen.
Diese Übergabe erteilt keinen Auftrag für weitere Implementierung.

## GitHub und Quellbasis

- Repository: <https://github.com/Shawna7han/mumble-funk>
- B1-Branch: [feature/analog-voice-b1-dsp](https://github.com/Shawna7han/mumble-funk/tree/feature/analog-voice-b1-dsp),
  Draft-PR gegen `dev`; den zugehörigen PR und aktuellen Commit auf GitHub prüfen.
- B1 basiert direkt auf Dev-Commit `98bc239e8b8ef8da6dc46faa2830562d5349d224`.
  Keine Änderung/Merge von `dev`, `stable` oder Phase-2A-Branch durch B1.
- Phase-1-Referenz: `86071c3771b9976d3a1a359fa04fd5e3770f81da`.
  Der freigegebene Originalcode wurde aus dem vom Betreiber bereitgestellten
  Quellarchiv übernommen und ist jetzt im B1-Branch versioniert; alte Cloud-Dateien
  und privates Archiv sind für die Weiterarbeit nicht mehr nötig.
- [Phase-2A-Plan](https://github.com/Shawna7han/mumble-funk/blob/feature/analog-voice-integration/docs/funk/analog-voice-integration-plan.md),
  [Draft-PR #12](https://github.com/Shawna7han/mumble-funk/pull/12).
  Für B2 insbesondere Abschnitt 11/B2 sowie die unmittelbaren Codec-Voraussetzungen
  aus Abschnitt 6 berücksichtigen; Architektur nicht neu entwickeln.

## Relevante Dateien und B1-Ergebnis

- `tools/analog-voice/include/VoiceProcessor.h`, `src/VoiceProcessor.cpp`:
  unveränderter DSP und Profile `cb-am`, `business-fm`, `narrowband`;
  Mono-float 48 kHz, höchstens 960 Samples pro `process()`, Zustand pro Instanz.
- `tools/analog-voice/SHA256SUMS`: freigegebene Header-Prüfsumme
  `e33e0101f3a201f2a80b1aa0766664d1e68d8c887e8e1cba7a4ba4fe7367b13c`,
  CPP-Prüfsumme `a3ad1bfaa668ea0b7d0e14ecd1803e24e7fdcc25d0abb25c4818b5cb52ca526f`.
- `tools/analog-voice/CMakeLists.txt`: einziges statisches DSP-Target
  `funk-voice-dsp`, C++17, keine Qt-/Opus-/Python-Abhängigkeit.
  Haupt-`CMakeLists.txt` registriert es bei `server OR tests`.
  Murmur verlinkt/nutzt den DSP noch nicht.
- `tools/analog-voice/offline/CMakeLists.txt`, `src/{OpusCodec,Preview,WaveIO}.cpp`,
  `src/main.cpp`: erhaltene Offline-Werkzeuge, nur bei `VOICE_BUILD_PREVIEW=ON`.
  `OpusCodec::process()` ist ein eigener Encode/Decode-Roundtrip, kein Live-Adapter;
  `Preview.cpp` enthält den dateiweiten RMS-Abgleich. Beides nicht in Live übernehmen.
- `tools/analog-voice/tests/{dsp_test.cpp,codec_test.cpp,cli_test.py}`:
  unveränderter Phase-1-Testbestand.
- [B1-Dokumentation mit Buildbefehlen und lokalen Ergebnissen](analog-voice-b1.md).

Lokal bestanden: reiner DSP-Build, Haupt-CMake-DSP-Build, `VoiceDsp` auch mit
ASan/UBSan, Offline-CTest **3/3** (`VoiceDsp`, `VoiceCodec`, `VoiceCli`),
Hash-/Bytevergleich und Diff-Prüfung des unveränderten Serverpfads.
GCC 14.2.0, CMake 3.31.10, libopus 1.5.2, Python 3.12.14.
Kein lokaler Murmur-Vollbuild, keine Serverinstallation, kein Live-Hör-/Lasttest.
GitHub-CI separat beim aktuellen PR prüfen. Keine Aufnahmen/Hörproben veröffentlicht.

## Offene Arbeit ausschließlich nach ausdrücklicher B2-Freigabe

Einen reinen `AnalogVoiceStream` außerhalb von `Server` erstellen: externe
Opus-Payloads dekodieren, unveränderten DSP verwenden, einmal neu kodieren.
Noch keine Worker, Serverpaket-, Routing-, Simplex- oder Squelch-Änderungen.
Kein Deployment, kein produktiver Schalter, keine Profil-/Pegeländerung oder AGC.
Auf neuem Feature-Branch arbeiten; B1/Phase-2A nicht als Integrationsbranch umnutzen.

Offene B2-Nachweise laut Plan:

- Fester 5760-Sample-Decoderpuffer für bis zu 120 ms, DSP-Stücke <= 960,
  Reblocking auf 20-ms-Ausgaben; 10/20/40/60/120-ms-Pakete testen.
- Nicht unterstützte 2,5-/5-ms-/Kanalmodi vor erster Ausgabe erkennen;
  Restdaten erst am echten Ende auffüllen, nicht zwischen Paketen.
- Quell-/Ausgangssequenzen in 10-ms-Einheiten, 20-ms-Ausgabe erhöht um 2;
  keine Rücksprünge, auch über 60 s und kurze Sendungen hinweg.
- Ein-Paket-Sendungen, leere Terminatoren und Payload+Terminator;
  eigene Encoder-Lookahead-Abfrage, EOF-Nachlauf und echte Abschlussreserve,
  vollständige Dekodierbarkeit sowie erste/letzte Silben mit Testsignalen.
- PLC/Lücken, Duplikate und Diskontinuitäten gezielt prüfen. Keine pauschalen
  312 Samples vom fremden Decoder-Eingang abschneiden.
- Kein dateiweiter RMS-Abgleich; Live-Pegel/Qualität benötigen spätere eigene
  Freigabe. B1 beweist noch keine Klangidentität zur Offline-Gesamtkette.

Asynchroner Dienst/Ordnungsvertrag (B3), Serveranbindung (B4), Effekt-Lifecycle
(B5) und Betriebsfreigabe (B6) bleiben spätere getrennte Schritte.
