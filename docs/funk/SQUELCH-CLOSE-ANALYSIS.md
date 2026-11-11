# Squelch Close nach kontinuierlichem Rauschen

Repository: `Shawna7han/mumble-funk`, Branch `feature/radio-noise`, Draft-PR #9.
Ausgangsstand der Untersuchung: `dc74ff8cabfaa0cb7323adf8956fbfa10f180cd0`.

## Befund und Beweisgrenze

Der Projektinhaber hat einen erfolgreichen A/B-Hörtest auf Android gemeldet:
Windows-Mumble → Mumla verliert Close mit aktiviertem Rauschen; 200 ms vor dem
Close-Signal in einer diagnostischen WAV machen es hörbar. Das gilt für kurze
und über 60 Sekunden lange Übertragungen. Die originale WAV wird nicht verändert.
Ohne Rauschen tritt dieser Hörfehler nicht auf.

Die normale alte Server-Paketfolge war in lokalen Netzwerktests vollständig:
Noise-Ende, 60 ms Abstand, vier Close-Pakete, letzte Ende-Markierung. Opus war
dekodierbar. Zusätzlich wurden drei serverseitige Abbruchpfade ohne Noise-Ende
nachgewiesen: die 1024-Byte-TCP-Grenze im gemeinsamen Gültigkeits-Gate, ein leerer
Produzent mit mehr als 100 ms Rückstand und eine 120 ms verspätete Zustellung.
Der ältere Sequenzrücksprung nach 60 Sekunden war ein separater Fehler.

Ein unabhängig geschriebenes Modell mit dem vorhandenen BSD-Speex-Jitterbuffer
reproduziert den Verlust: Das Empfangsobjekt erhält Close noch während der
Wiedergabe des gepufferten alten Noise-Endes. Beim anschließenden Entfernen des
Objekts gehen bereits eingereihte Close-Pakete verloren. Relevante Parameter:
100 ms Empfangsreserve und Ausgabeblöcke von 10 bis 120 ms. Das Modell bildet auch
Anlaufpufferung und den über Objektwechsel behaltenen Verfügbarkeitsmittelwert ab.
Es ist kein Android-, JNI- oder AudioTrack-Test.

Zur Parameterprüfung wurden ausschließlich lesend untersucht:

- Mumla 3.7.3: Commit `c179d8ff5a38e8e758cb84011e3845764b6f6976`.
- Zugehöriger Humla-Stand: `4df8e7f3f45c9300864cca514e868032448c2baa`.
- Dortiger nativer Speex-Stand: `a6d05eb5ff9d5062852cdf7df574bec728921ef9`.

Es wurde kein fremder Java-/JNI-Code übernommen. Modell und Erweiterung verwenden
bereits vorhandene Opus-/Speex-Abhängigkeiten. Keine neue Audioressource,
Bibliothek oder private WAV wird veröffentlicht.

## Weshalb keine feste zusätzliche Pause?

200 ms Stille **im Signal** können einen neuen Empfangspuffer bereits vor dem
hörbaren Ton aufbauen. Eine reine Zustellpause tut dies nicht. Im Modell verliert
ein 120-ms-Ausgabeblock nach 180 ms Pause weiterhin Close; erst etwa 300 ms nach
dem Noise-Ende wird Close dort vollständig wiedergegeben. Das lässt keine
zuverlässige Reserve zur vorhandenen 300-ms-Frist ab dem Übertragungsende.
Die Frist wird deshalb nicht erhöht und die WAV nicht mit Stille aufgefüllt.
Der genaue Ablauf auf dem physischen Mumla-Gerät bleibt eine gestützte Hypothese.

## Beschlossene Lösung: ein privater Effektstream

Mit aktivem Rauschen erzeugt derselbe vorbereitete Worker pro Empfänger:

1. Open-PCM, unmittelbar gefolgt von Noise-PCM, auch innerhalb eines 20-ms-Frames.
2. Samplegenau gelooptes Rauschen bis zur angenommenen Ende-Anforderung.
3. Close-PCM, anschließend nur den notwendigen Opus-Lookahead zum Codecabschluss.
4. Genau eine Ende-Markierung auf dem letzten Close-Paket.

Zwischen Open/Noise/Close gibt es weder Ende-Bits noch künstliche Sequenzsprünge,
Encoderresets oder 60-ms-Pausen. Der Original-Open mit 18,6875 ms benötigt damit
kein zusätzliches vollständiges Nachlaufframe. Der Worker behält seinen Encoder
und generiert sequenziell PCM; es findet keine Überlagerung statt. Sprache läuft
unverändert in ihrer eigenen normalen Weiterleitung. Private Effekte benutzen
weiter die existierende Empfänger-Session, keinen Bot und keine neue Session.

Ohne Rauschen bleibt der bisherige Open-/Close-Pfad erhalten. Deaktiviertes Open
wird ausgelassen, deaktiviertes Close endet mit einem regulären Codecabschluss.
Timeout-Close benötigt weiterhin den bestehenden Fallback-Schalter; ein Timeout
ist kein Nachweis des physischen Loslassens der PTT-Taste. Der Server erkennt
einen angenommenen Sprachbeginn, nicht die physische Taste.

## Begrenzungen und Abbrüche

- Unverändert maximal 16 beim Start erzeugte Worker, 64 Empfängerzustände,
  128 ausstehende Anforderungen und drei codierte Pakete pro Worker.
- Zusätzlich genau ein ungesendetes Reservepaket pro aktivem Effektstream.
  Das erlaubt eine gültige Abschlussmarkierung ohne leeres Opus-Paket,
  Wiederholung eines schon gesendeten Frames oder Warten auf neue Kodierung.
- Producer-Kommandos sind Try-Lock-Operationen. Bereits codierte und ein gerade
  bearbeitetes Paket werden geordnet ausgeliefert, nicht aus dem laufenden
  Encoderstrom herausgeschnitten. Der Phasenwechsel kann daher einige Frames
  benötigen. Keine unbeschränkten Paketbursts. Die spätere Timingkorrektur erlaubt
  begrenztes Aufholen über getrennte Timeraufrufe; siehe
  [Audio-Timing](AUDIO-TIMING-ANALYSIS.md).
- Close wird genau einmal reserviert. Sein erstes Paket muss weiterhin innerhalb
  von 300 ms nach der Ende-Anforderung zugestellt werden; die Frist wird auch
  nach einer frühen Reservierung geprüft. Veraltetes Close entfällt.
- Vorübergehende Produzentenknappheit oder eine 120-ms-Ausführungslücke während
  des Übergangs werden innerhalb dieser Frist toleriert. Sonst beendet die
  vorhandene 100-ms-Rückstandsgrenze den optionalen Effekt mit der Reserve.
  Auch nach begonnenem Close ist ein Produzentenstillstand zeitlich begrenzt.
- Identität/Generation/Berechtigung und TCP-Kapazität werden getrennt geprüft.
  Noise/Noise-Ende behalten das 1024-Byte-Limit, endliche Open-/Close-Cues das
  bestehende 64-KiB-Limit. TCP-Druck überspringt keine codierten Pakete.
- Bei dauerhaft blockiertem/defektem Transport kann kein Endpaket garantiert
  werden. Nach spätestens 300 ms ohne Zustellung wird optionales Audio verworfen,
  statt Puffergrenzen zu lockern oder eine unbegrenzte Warteschlange zu führen.
  Danach wird kein veralteter Close nachgesendet; der Client muss auslaufen.
- Busy behält Vorrang, Clipbytes und Cooldown. Die Reserve beendet den aktuellen
  Effekt, dann gelten die bisherigen unabhängigen Busy-Stream-Grenzen. Wenn die
  Übertragung noch läuft, setzt Noise nach Busy an der nächsten PCM-Position mit
  einem neuen Encodersegment fort; Open wird nicht wiederholt. Kein Close wird
  hinter Busy für eine inzwischen beendete Übertragung nachgeholt.
- Neue Übertragungen, Kanalwechsel, Disconnect, ACL-Verlust und Session-Reuse
  invalidieren alte Close-Anforderungen. Ein vorhandenes ungesendetes Noise-
  Reservepaket darf als reine Abschlussmarkierung an einen noch gültigen Empfänger
  gehen; niemals ein neuer Close. Die Zustellung prüft die Verbindung erneut.
  Schon empfangene Clientpakete können selbstverständlich nicht zurückgerufen werden.
- Bei ausgeschöpftem Worker-Pool kann der gesamte optionale Effekt für weitere
  Empfänger entfallen. Sprache und Simplex bleiben unabhängig davon funktionsfähig.

## Regressionen und Wiederholung

```bash
ctest --test-dir build/funk --output-on-failure
build/funk/tests/TestNoiseScheduler --short-close-probe
bash tests/funk-analysis/run_noise_close_playout.sh
```

`TestRadioNoise` vergleicht alle codierten Bytes über Open/Noise/Close mit einem
unabhängigen, einmal initialisierten Referenz-Encoder; kurze Übertragungen und
alle Open-/Close-Schalterkombinationen sind enthalten. Zusätzlich tatsächliche
Opus-Dekodierung, Lookahead, Loopkontinuität und feste Queue-/Worker-Grenzen.

`TestNoiseScheduler` prüft kurze und 62-sekündige Übertragungen, bestätigtes und
Timeout-Ende, alle Close-Pakete, kontinuierliche +2-Sequenzen, keine interne
Ende-Markierung, 20-ms-Paketabstand am normalen Phasenwechsel und 300-ms-Frist.
Die drei früheren Störfälle sind Pflichtregressionen: TCP-Druck für einen Tick,
Produzenten-Zugriff 80/140 ms blockiert und 120 ms verspäteter Scheduler.
Die Testinjektion blockiert nur Producer-Try-Locks und setzt keinen Encoder zurück.
Weitere Fälle: dauerhafte Knappheit, verzögerter Tick, früh beendetes Open,
deaktivierte Signale, Busy, schnelle PTT-Wechsel, Kanal/Disconnect/Session-Reuse.

`TestNoiseIntegration` empfängt echte lokale Serverpakete über TCP und
verschlüsseltes UDP: 2/10 Sekunden und parallel beide Transporte über 62 Sekunden.
Legacy-Protokoll-Empfänger, vollständige unveränderte Sprachpakete, zugehörige
Freigabeursachen, fortlaufende Effektnummern, genau eine Abschlussmarkierung und
vollständige decodierte Close-Wellenform werden geprüft. Bytegleichheit mit einem
separat zurückgesetzten Close-Encoder wäre hier ein falsches Kriterium; deshalb
werden nach dem Encoder-Referenztest zusätzlich reale Dekodierung und die
Korrelation der vollständigen hinteren drei Close-Frames geprüft.

Das Puffermodell zeigt den alten Verlust und gibt beim zusammenhängenden Stream
für 2 und 62 Sekunden sowie alle fünf Ausgabeblockgrößen sämtliche vier
Close-Pakete wieder. Es ist ausdrücklich kein Beweis für Gerätewiedergabe.

## Erforderlicher erneuter Hörtest

Auf der vom Betreiber verwalteten Snapshot-Test-VM gemäß
[radio-noise.md](radio-noise.md) aktualisieren. Original-Open und Original-Close
verwenden, **keine** diagnostisch verlängerte Close-WAV. Ausdrücklich prüfen:

- Windows → Mumla: 2, 10 und mindestens 62 Sekunden, Noise an/aus im Vergleich.
- Mumla → Windows: bestätigtes Ende soweit vorhanden, sonst Timeout-Fallback.
- Sehr kurzes PTT während Open, schnelle Wechsel, Busy bei belegtem Kanal.
- Einzelnes Open/Close ausschalten, Kanalwechsel und Verbindung neu aufbauen.
- Erste/letzte Sprachsilbe vollständig, Close genau einmal, keine hängenden Effekte.

Codex verändert keine vorhandene VM/Serverinstallation. Bis zu diesem Hörtest
bleibt der Stand experimentell. Draft-PR #9 bleibt offen; kein Merge/Release.

## Lokal ausgeführte Prüfungen dieses Änderungsstands

- Inkrementeller Server-Build mit den vorhandenen Release-/ICE-Einstellungen
  erfolgreich, keine neue Entwicklungsumgebung oder Serverinstallation.
- Vollständiger CTest-Lauf: 31/31 Einträge erfolgreich. Nach den abschließenden
  Absicherungen erneut alle neun betroffenen Audio-/Noise-/Simplex-Einträge,
  einschließlich echtem TCP-/UDP-Langtest, mit der privaten WAV erfolgreich.
- Zusätzlicher privater WAV-Test: Originalprüfsumme unverändert, mehr als drei
  vollständige Loops, unabhängige Encoderreferenz und tatsächliche Dekodierung.
- Python: acht Audioasset-/Verwaltungstests, ein Boost-Konfigurationstest,
  ein Radio-Noise-Staging-/Installationstest, 13 Audiosicherheitstests und
  18 Docker-Verwaltungs-/Backup-/Restore-Tests erfolgreich.
- Alle drei öffentlichen Standard-WAVs mit ihren Git-Blob-IDs verglichen:
  unverändert. Keine private WAV, neue Drittanbieterressource oder Abhängigkeit.

Die Netzwerktests benötigen lokale Socket-Berechtigungen der Entwicklungsumgebung;
Sandbox-Läufe ohne diese Berechtigung konnten keine Testports öffnen und wurden
mit freigegebenen lokalen Netzwerkzugriffen erfolgreich wiederholt. Docker-Tests
hier betreffen die vorhandene Verwaltungslogik; kein erneuter Docker-Image-Build,
keine Ubuntu-/Proxmox-Installation und keine tatsächlichen Geräte-Hörtests.
GitHub-CI wird nach dem Push ausdrücklich nicht abgewartet.
Die anschließend zusätzlich erweiterte Scheduler-Matrix (beide Endearten bei
allen Übergangsstörungen sowie bestätigtes/Timeout-Ende nach jeweils 62 Sekunden)
bestand ebenfalls vollständig. Die abschließende Scheduler-Prüfung dauerte 43,82 s.

## Spätere Praxisrückmeldung

Der Betreiber bestätigt Close auf Windows → Mumla nach der Installation von
`db4f76f3c85519af271ba41fb0aba174259e1320`. Neu gemeldete kurze Unterbrechungen
während laufender Übertragung wurden separat untersucht: Ein zurückgesetzter
Fälligkeitstermin konnte trotz intakter Sequenzen die Effektversorgung halbieren.
Siehe [Timinganalyse und Korrektur](AUDIO-TIMING-ANALYSIS.md). Die gemeinsame
Open-/Noise-/Close-Architektur und die Original-WAVs bleiben bestehen.
