# Kurze Unterbrechungen während laufender Übertragung

Untersuchter Ausgangscommit: `db4f76f3c85519af271ba41fb0aba174259e1320`,
Branch `feature/radio-noise`, Draft-PR #9.

## Praxisbefund und Aussagegrenze

Der Betreiber bestätigt nach der gemeinsamen Open-/Noise-/Close-Umstellung:
Close funktioniert nun auch bei Windows-Mumble → Mumla mit aktivem Rauschen.
Während laufender Übertragung sind aber kurze Unterbrechungen hörbar, ohne Close.
Zum Zeitpunkt der Fehleruntersuchung war unklar, ob auf dem Gerät auch Sprache betroffen war.

**Spätere Betreiber-Rückmeldung:** Nach Installation von
`6f976a22f6511f372ff85ca2403592e5faa8e896` sind die kurzen Aussetzer im bisherigen
Hörtest behoben; Rauschen und Close funktionieren. Der konkrete Android-
Pufferablauf wurde damit weiterhin nicht instrumentiert. Umfang und Grenzen der
Praxisbestätigung stehen in [radio-noise.md](radio-noise.md).

Die damalige Untersuchung wies einen **serverseitigen Timingfehler des privaten
Effektstreams** nach und korrigierte ihn. Ein Mitschnitt der betroffenen VM oder
eine Messung der Android-Audioausgabe lag nicht vor. Deshalb musste zunächst ein
erneuter Hörtest die Zuordnung bestätigen. Die anschließende erfolgreiche
Betreiber-Rückmeldung ist oben ergänzt; die ursprüngliche Analyse bleibt als
Nachweis erhalten. Die reguläre Sprachweiterleitung wurde nicht geändert.

## Reproduzierter Fehler

`tickRadio()` stellte nach jedem gesendeten Frame die nächste Fälligkeit auf
`now + 20000`. Der vorhandene präzise Qt-Timer besitzt dagegen wiederkehrende
Termine. Auch ein `Qt::PreciseTimer` garantiert keine exakt 20.000 Mikrosekunden
zwischen den **tatsächlichen Callback-Eintritten**: Betriebssystemplanung und
unterschiedliche Callback-Laufzeiten ergeben beispielsweise 21/19-ms-Abstände.

Nach einem um 1 ms verspäteten Callback wurde der nächste Frame ebenfalls um
1 ms verschoben. Der folgende Callback kam dadurch vor der neuen Fälligkeit;
der Server übersprang einen vollständigen Durchlauf. Das wiederholt sich:

| Deterministischer Test, gleicher Produzent und gleiche 500 Aufrufe | Effektpakete | Größter Paketabstand |
| --- | ---: | ---: |
| Vorher, Ausgangscode | 250 | 40 ms |
| Nachher | 500 | 21 ms |

Die Frames tragen weiterhin lückenlose +2-Sequenzen und jeweils 20 ms Audio.
Sie werden vorher also **zu langsam**, nicht mit übersprungenen Opus-Nummern
zugestellt. Ein Client kann seinen Vorrat verbrauchen und Ersatzwiedergabe bzw.
Unterbrechungen erzeugen. Die 100-ms-Abbruchgrenze wird bei 40-ms-Abständen nicht
überschritten. Die Simplex-Übertragung bleibt aktiv und Close wird nicht ausgelöst.
Die früheren Paket-/Dekodiertests prüften die Nutzdaten, nicht diese Taktdrift.

## Weitere untersuchte Pfade

- `deliverAudioFeedback()` musste bei fehlgeschlagenem
  `qrwlVoiceThread.tryLockForWrite()` bisher bis zum nächsten regulären
  20-ms-Termin warten. Der Schutz ist notwendig: Identitäts-/Kanalprüfung,
  Squelch-Reservierung und Versand müssen atomar gegenüber UDP-Änderungen bleiben.
  Ob unter normaler Windows-Last auf der echten VM tatsächlich solche Kollisionen
  auftreten, ist nicht gemessen. Ein Test mit echtem zweiten Read-Lock-Thread
  reproduziert den verlorenen Zustelltermin gezielt.
- `RadioNoise::take()` wartet absichtlich nicht. Eine kurzzeitig belegte Mutex
  oder verspätete Produzentenverfügbarkeit konnte ebenfalls einen ganzen
  20-ms-Termin kosten. Tests halten die Producer-Mutexen 60 ms; damit werden
  sowohl Take-Zugriffe als auch Producer-Commits verzögert, ohne Encoderreset.
  Es ist kein Benchmark künstlich verlangsamter Opus-Rechenleistung.
- Die drei Producer-Pakete plus ein Reservepaket bleiben begrenzt. Keine
  Paketwiederholung, kein Ausschneiden aus dem codierten Strom, kein neuer Encoder.
- Das 1024-Byte-Limit für Noise bleibt bestehen. Ein vorübergehender Stau stoppt
  die Entnahme; nach Entlastung kann der Stream seinen Takt wieder erreichen.
  Die endlichen Cue-Grenzen von 64 KiB bleiben ebenfalls unverändert.

## Korrektur

1. `s.due` folgt dem bestehenden Medienraster: pro gesendetem Frame `+= 20000`.
   Es wird nicht mehr an jeden verspäteten Callback neu angehängt.
2. Der Server richtet den nächsten Timeraufruf auf die nächste Fälligkeit aus.
   Die Umrechnung in Millisekunden rundet auf. Bei Produzenten-/Pufferknappheit
   oder bereits überschrittenem Termin gibt es einen neuen Versuch nach 5 ms.
3. Auch nach erfolglosem Voice-Write-Lock wird derselbe Timer nach 5 ms erneut
   gestartet. Kein Warten auf Locks, keine zusätzlichen gestapelten Timer-Callbacks.
4. Es bleibt maximal ein normaler Effektframe je Empfänger und Callback.
   Kurzer Rückstand wird über getrennte, begrenzte Wiederholungsaufrufe abgebaut;
   es gibt keine unbeschränkte Aufholschleife oder zusätzliche Warteschlange.
5. `s.lastPacket` bleibt der tatsächliche letzte Versandzeitpunkt. Die bestehenden
   100-ms-/300-ms-Abbruchpfade und die 300-ms-Close-Startfrist werden nicht erhöht.
   Bei mehr als 100 ms Stillstand außerhalb des vorgesehenen Close-Übergangs wird
   der optionale Effekt wie bisher kontrolliert beendet, ohne einen falschen Close.

Ohne konfigurierten Noise-Produzenten liefert die neue Timerplanung den Wert 0;
der vorhandene 20-ms-Timer für die bisherigen Audiofunktionen bleibt unverändert.
Busy behält Clip, Priorität und Cooldown. Der gemeinsame Encoder und das Schema
Open → Noise → Close → **eine** Ende-Markierung bleiben erhalten.
WAVs, Lautstärken, Sprache, Simplex, Ports, Installer und Konfiguration bleiben
unverändert. Keine neue Bibliothek oder Audioressource.

## Regressionen

```bash
ctest --test-dir build/funk --output-on-failure
build/funk/tests/TestNoiseScheduler --timing-probe
build/funk/tests/TestNoiseIntegration pacing
```

`TestNoiseTiming` umfasst:

- Obige 21/19-ms-Jitterfolge; ideales Raster mit 100 Frames in 2 Sekunden.
- Ausgelassene Timerdurchläufe: 100 Frames trotz neun ausgelassener Termine in
  2 Sekunden, 3100 Frames trotz 309 ausgelassener Termine in 62 Sekunden.
- Einen 20 ms von einem zweiten Thread gehaltenen `QReadWriteLock` mit vier
  erfolglosen Try-Lock-Aufrufen und anschließender begrenzter Wiederholung.
- 60 ms Producer-Contention/verzögerte Commit-Verfügbarkeit und 60 ms simulierten
  TCP-Stau an der unveränderten 1024-Byte-Grenze; danach jeweils vollständiger Takt.
- Einen 120-ms-Stillstand mit erwarteter kontrollierter Beendigung statt
  stillschweigend weiterlaufender, veralteter Effekte.
- Durchgehende Sequenzen, genau eine reguläre Ende-Markierung und vollständigen
  Close innerhalb der Frist in allen vorübergehend gestörten Fällen; tatsächliche
  Opus-Dekodierung. Kein Close im absichtlich abgebrochenen Überlastfall.

`TestNoisePacing` verwendet einen echten lokalen Server und einen Legacy-Protokoll-
Empfänger. Drei Sekunden kontinuierliche 50-Hz-Sprache, einmal TCP und einmal
verschlüsseltes UDP mit zusätzlich etwa 1000 authentifizierten UDP-Pings pro
Sekunde. Die Pings belasten Entschlüsselung/Voice-Lock, ohne das unveränderte
Sprach-Bandbreitenbudget durch künstlich hohe Audioframeraten zu umgehen.

Im isolierten Lauf wurden je 150 Sprachpakete und 149 Effektpakete vor dem Ende
empfangen; der Start benötigt eine kurze Anlaufzeit. Der UDP-Lastfall sendete
2994 zusätzliche Pings. Sämtliche ursprünglichen Sprachframes einschließlich
des Endframes kamen unverändert an. Effekte blieben während der laufenden
Übertragung aktiv, hatten lückenlose Sequenzen und am Ende genau eine Markierung
mit vollständig decodiertem Close. Empfangsabstände wurden getrennt für Sprache
und Effekte erfasst; bei rund 5 ms Polling sind sie keine exakten Server-
Sendezeitpunkte und keine Messung des Android-Jitterbuffers.

Die bestehenden TCP-/UDP-, 62-Sekunden-, Simplex-, Busy-, Squelch-, Timeout-,
Kanal-/Session-/ACL- und Opus-Referenztests bleiben erhalten.

## Erneuter Hörtest und verbleibende Grenzen

Auf der separaten Test-VM gemäß [Installationsanleitung](radio-noise.md)
aktualisieren; keine zusätzliche Einstellung nötig. Originale WAVs verwenden.

1. Windows → Mumla: 10–30 Sekunden gleichmäßig sprechen, Noise an/aus vergleichen.
   Sprache und Rauschen getrennt beurteilen; auch vollständiges Ausschalten von
   Audiofeedback als Vergleich nutzen. Eigene Einstellungen vorher notieren und
   anschließend wiederherstellen.
2. Danach mindestens 62 Sekunden senden und Close genau einmal kontrollieren.
3. Mumla → Windows mit Timeout-Close, kurze PTT-Betätigungen und Busy prüfen.
4. Falls Aussetzer bleiben: notieren, ob gleichzeitig Sprache/Rauschen aussetzen,
   Noise danach fortsetzt oder bis zur nächsten Übertragung ganz ausbleibt und
   ob der Fehler bei deaktiviertem Audiofeedback weiterhin besteht.

Realbetrieb ist nicht hart echtzeitfähig. Dauerhaft zu langsame Opus-Produktion,
ein dauerhaft blockierter TCP-Weg oder erhebliche VM-Planungspausen können
weiterhin Effekte auslassen bzw. beenden. Weder Paketpuffer noch Sicherheits-
grenzen werden dafür aufgeweicht. Ein Client kann Audio auch bei korrekten
Serverpaketen verzögert wiedergeben; das ist durch diese Tests nicht ausgeschlossen.
Keine tatsächlichen neuen Geräte-Hörtests durch Codex. Kein VM-Zugriff/Deployment.

## Ergebnis des abschließenden lokalen Laufs

Der vorhandene Server wurde inkrementell erfolgreich gebaut. Anschließend bestanden
**33/33 CTest-Einträge** in 179,11 Sekunden. Enthalten sind die neuen Timing- und
Netzwerk-Lastprüfungen sowie sämtliche bisherigen Server-/Audio-Regressionen.
Keine Tests entfernt, keine WAV verändert, keine neue Abhängigkeit. Die normale
UDP-/TCP-Sprachweiterleitung und die Simplex-Implementierung sind unverändert.
Die GitHub-CI wurde damals nach dem Push nicht abgewartet. Der anschließend
vom Betreiber erfolgreich abgeschlossene Hörtest ist oben nachgetragen.
