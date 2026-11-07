# Analoge Sprachfilter: B1 – unveränderten DSP ins Buildsystem übernehmen

Stand: 7. November 2026. Ausschließlich Quellübernahme und Build-Anbindung;
keine serverseitige Audioverarbeitung oder Betriebsfreigabe.

## Grundlage und übernommene Dateien

- Repository: <https://github.com/Shawna7han/mumble-funk>
- Tatsächlich geprüfter Ausgangspunkt: `dev` bei
  `98bc239e8b8ef8da6dc46faa2830562d5349d224`.
- Neuer Branch: `feature/analog-voice-b1-dsp`, direkt von diesem Dev-Stand.
- Phase-1-Referenz: `86071c3771b9976d3a1a359fa04fd5e3770f81da`;
  tatsächliche Quelle ist das bereitgestellte Archiv
  `ANALOG-VOICE-SOURCE-2026-11-07.zip`, Unterordner
  `mumble-funk-analog-voice/tools/analog-voice/`.
- [Geprüfter Phase-2A-Plan, insbesondere Abschnitt 11/B1](https://github.com/Shawna7han/mumble-funk/blob/feature/analog-voice-integration/docs/funk/analog-voice-integration-plan.md),
  [Draft-PR #12](https://github.com/Shawna7han/mumble-funk/pull/12).

Unter `tools/analog-voice/` unverändert aus dem Archiv kopiert und byteweise
gegen die Archivdateien verglichen:

- DSP: `include/VoiceProcessor.h`, `src/VoiceProcessor.cpp`.
- Optionaler Offline-Adapter: `include/{OpusCodec,Preview,WaveIO}.h`,
  `src/{OpusCodec,Preview,WaveIO}.cpp`, `src/main.cpp`.
- Bestehende Tests: `tests/dsp_test.cpp`, `tests/codec_test.cpp`, `tests/cli_test.py`.

Neu sind die CMake-Anbindung, README, Prüfsummenliste und diese B1-/B2-Dokumentation.
Nicht übernommen: Benchmark, Archiv, Sicherungspatch, Restaurierungsanweisungen
und alte Statusberichte. Die Phase-1-Implementierung wurde nicht rekonstruiert.

## Unveränderte freigegebene Prüfsummen

Vor dem Kopieren im entpackten Archiv und nach Übernahme/Tests im Zielbaum geprüft:

```text
VoiceProcessor.h   e33e0101f3a201f2a80b1aa0766664d1e68d8c887e8e1cba7a4ba4fe7367b13c
VoiceProcessor.cpp a3ad1bfaa668ea0b7d0e14ecd1803e24e7fdcc25d0abb25c4818b5cb52ca526f
```

Wiederholbar vom Repository-Wurzelverzeichnis:

```sh
(cd tools/analog-voice && sha256sum -c SHA256SUMS)
```

`VoiceProcessor`, `Parameters`, `profile()`, `process()` und `reset()` bleiben
erhalten: Mono-float, 48 kHz, in-place, höchstens 960 Samples je Aufruf.
`cb-am`, `business-fm` und `narrowband` einschließlich aller Filter-, Kompressor-,
Sättigungs- und Pegelparameter sind unverändert. Keine neue AGC.

## Targets und Abhängigkeiten

| Target | Inhalt / Abhängigkeiten |
| --- | --- |
| `funk-voice-dsp` | Statische Bibliothek, ausschließlich `VoiceProcessor.cpp`, öffentlicher Include-Pfad und `cxx_std_17`; nur C++-Standardbibliothek. |
| `voice-dsp-test` | Unveränderter DSP-Test, nur `funk-voice-dsp`. |
| `funk-voice-preview-support` | Optional, `OpusCodec.cpp`, `WaveIO.cpp`, `Preview.cpp`; verlinkt denselben DSP und vorhandene libopus. |
| `funk-voice-preview` | Optionales Offline-CLI, nur Preview-Support. |
| `voice-codec-test` / `VoiceCli` | Optional, ursprüngliche Codec-/WAV-/CLI-Tests; Python nur für `VoiceCli`. |

Das Haupt-CMake fügt `tools/analog-voice` bei `server OR tests` hinzu.
Der Server wird noch nicht mit dem DSP verlinkt. Client-Builds ohne Tests erhalten
kein neues Target. `VOICE_BUILD_PREVIEW` ist standardmäßig OFF;
`VOICE_BUILD_TESTS` übernimmt eingebettet `tests`, standalone ist es ON.
Compilerwarnungen und optionale Sanitizer gelten nur für die Analog-Voice-Targets.
Der C++-Standard des Hauptprojekts wird nicht global verändert.

Der DSP hat keinen WAV-Zugriff, keinen Opus-Adapter und keine Python-, Qt-,
Socket-, Thread- oder Murmur-Abhängigkeit. Das Hauptprojekt benötigt bereits
unabhängig davon Python für seine bisherigen Generierungsschritte.

`Preview.cpp` enthält den dateiweiten RMS-Abgleich und die Offline-Roundtrips.
`OpusCodec` kodiert und dekodiert eigene 960-Sample-Frames. Diese Dateien sind
ausschließlich Offline-Werkzeuge, keine Grundlage zum direkten Einhängen in den
Live-Sprachpfad. Es entsteht nur eine DSP-Bibliothek, keine zweite Opus-Kopie.

## Ausgeführte Verifikation

Werkzeuge: GCC 14.2.0, CMake 3.31.10, Python 3.12.14, vorhandene libopus 1.5.2.
CMake wurde in einer lokalen Arbeits-venv bereitgestellt. Für den Offline-Test
wurden nur die offiziellen Opus-1.5.2-Header lokal geladen und die bereits vorhandene
`/lib/x86_64-linux-gnu/libopus.so.0` verwendet. Keine Systempaket-, Docker- oder
Ubuntu-Installationsänderung.

Folgende Befehle wurden mit außerhalb des Repositorys liegenden Buildverzeichnissen
ausgeführt; hier sind portable relative Verzeichnisse angegeben:

```sh
# DSP inklusive unverändertem Phase-1-Test
cmake -S tools/analog-voice -B build/b1-dsp -DCMAKE_BUILD_TYPE=Release
cmake --build build/b1-dsp --parallel 2
ctest --test-dir build/b1-dsp -V

# Reine Bibliothek, ohne Tests oder Offline-Abhängigkeiten
cmake -S tools/analog-voice -B build/b1-only \
  -DVOICE_BUILD_TESTS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/b1-only --target funk-voice-dsp --parallel 2

# Tatsächliche Einbindung über das Haupt-CMake, ohne Murmur-Vollbuild
git submodule update --init 3rdparty/FindPythonInterpreter 3rdparty/cmake-compiler-flags
cmake -S . -B build/b1-root -Dclient=OFF -Dserver=OFF -Dtests=ON \
  -Dplugins=OFF -Doverlay=OFF -Dlto=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/b1-root --target funk-voice-dsp voice-dsp-test --parallel 2
ctest --test-dir build/b1-root -R '^VoiceDsp$' --output-on-failure

# Optionaler Offline-Bestand; bei lokal abgelegtem Opus zusätzlich
# -DOPUS_INCLUDE_DIR=<Headerverzeichnis> -DOPUS_LIBRARY=<libopus-Pfad> angeben
cmake -S tools/analog-voice -B build/b1-offline \
  -DCMAKE_BUILD_TYPE=Release -DVOICE_BUILD_PREVIEW=ON
cmake --build build/b1-offline --parallel 2
ctest --test-dir build/b1-offline --output-on-failure

# DSP mit ASan/UBSan
cmake -S tools/analog-voice -B build/b1-sanitized \
  -DCMAKE_BUILD_TYPE=Debug -DVOICE_SANITIZERS=ON
cmake --build build/b1-sanitized --parallel 2
ctest --test-dir build/b1-sanitized --output-on-failure
```

Ergebnisse:

- DSP allein und über Haupt-CMake: erfolgreich kompiliert; `VoiceDsp` bestanden.
- DSP mit ASan/UBSan: `VoiceDsp` bestanden, keine Sanitizer-Befunde.
- Offline-Build: **3/3** CTests bestanden (`VoiceDsp`, `VoiceCodec`, `VoiceCli`).
  WAV-/Codec-Tests erzeugen ausschließlich temporäre synthetische Fixtures und
  entfernen diese wieder; keine Sprachaufnahme oder neue Hörprobe.
- Der unveränderte DSP-Test prüft alle drei Profile, Reset, Instanzisolation,
  blockgrenzenunabhängige Ausgabe, Allokationsfreiheit bei Verarbeitung,
  Frequenzgang, Kompression, Spitzenbegrenzung, Stille, Bypass und Fehlerverträge.
- DSP-only-Compile-Daten: genau `VoiceProcessor.cpp`, keine Opus-/Qt-/Python-
  Eingaben und keine Preview-Artefakte. `ldd voice-dsp-test` zeigt nur die
  C++-/C-/Mathematik-Laufzeitbibliotheken.
- Alle zwölf übernommenen Quell-/Testdateien bytegleich zum Archiv;
  beide freigegebenen SHA-256-Werte nach den Tests erneut bestätigt.
- Diff gegen Ausgangs-Dev für `src/`, `cmake/`, `.github/`, `scripts/` und
  Installations-/Docker-Dateien leer. `Server.cpp/.h`, Opus-Forwarding,
  Simplex, Squelch, Busy und Rauschen bleiben unverändert. `git diff --check` sauber.

## Grenzen und Voraussetzungen für B2

Kein vollständiger Murmur-/Client-Build, keine Serverinstallation, keine
Live-/Gerätehörtests und keine VPS-Lastmessung in dieser lokalen B1-Verifikation.
Die bestehende GitHub-CI kann nach Veröffentlichung zusätzlich laufen; ihre
Ergebnisse sind getrennt von diesen tatsächlich lokal ausgeführten Tests zu lesen.
Kein Deployment, Merge oder Eingriff in laufende Server.

Die Quellen- und Buildnachweise erfüllen B1. Ein Live-Adapter, Worker,
Paketänderungen und produktive Aktivierung sind noch nicht implementiert.
Für B2: separat freigegebener Auftrag, aktueller Dev-/B1-Stand und verfügbare
libopus-Entwicklungsdateien; dann einen netzwerkfreien Adapter mit externem
Decoder-Eingang und genau einem neuen Encoder testen. Kein Offline-RMS-Abgleich
oder zusätzliche AGC. Details und offene Tests: [B2-Übergabe](analog-voice-b2-handoff.md).
