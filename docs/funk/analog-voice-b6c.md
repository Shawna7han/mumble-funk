# B6C: begrenzte Paketbursts und Fehlerdiagnose

Ausgangspunkt `02c9c59f59dcf001b1e597bff89f010872f934ce`, ausschließlich
`feature/analog-voice-live`, bestehender Draft-PR #15. Experimentell. Keine
Betriebsfreigabe, kein Merge und keine Installation auf Proxmox/VPS.

## Belegter Betreiberfehler und offene Herkunft des Bursts

Im Proxmox-Lauf stehen `inputFull=3`, `faulted=358`, `codecErrors=1`, aber
`expired=0`, `noLane=0` und `transportDropped=0`. `faulted` zählt abgewiesene
Eingabeversuche, nicht Aussendungen. Die Ablaufhypothese aus B6B erklärt diesen
konkreten Lauf nicht.

Generation 474: Tickets 475–478 nehmen 20-ms-Pakete an; Ticket 479 meldet
`input_full`, danach folgen `faulted`-Versuche auch über TCP. Fünf Submits liegen
innerhalb von 51 µs; Quellframes steigen korrekt um zwei 10-ms-Einheiten.
Damit ist der Kapazitätsfehler mit anschließend gelatchter Generation für
diesen Fall bewiesen. Nicht jeder gehörte Ausfall ist ihm eindeutig zugeordnet.

Diese Zeitstempel beschreiben **serverseitige Übergaben**, weder Android-
Sendezeiten noch Kernel-Ankunftszeiten. Ohne zugeordnete Ankunftsdaten lassen
sich Android-Bursting, Netzjitter, VM-Scheduling und Socket-Rückstau nicht
unterscheiden. Keine dieser Möglichkeiten wird zur bewiesenen Betreiberursache
erklärt. Der Codecfehler ist ebenfalls keiner Generation/Ursache zugeordnet.

## Warum ein schneller Worker allein nicht genügt

`Server::run()` liest je Durchlauf ein authentifiziertes UDP-Datagramm. Liegen
weitere Datagramme bereit, kehrt das nächste `poll()` sofort zurück. Der
Voice-Thread kann somit vorhandenen Rückstau ohne Medienpause abarbeiten;
Scheduling oder ein vorangegangener Voice-Lock-Konflikt kann Bündelung begünstigen.
Es gibt keinen zusätzlichen UDP-Audiosammelpuffer und keinen 20-ms-Mindestabstand
zwischen Submits.

Für TCP leert `Connection::socketRead()` in einer Schleife alle vollständigen
TLS-Nachrichten. Deren `message` führt auf dem Hauptthread in den Sprachpfad.
Die geordnete Analogzustellung ist dagegen ein queued Hauptthread-Aufruf und
kann während desselben Read-Callbacks nicht laufen. Ein einziger TLS-
Schreibvorgang des neuen Tests reproduziert diese Bündelung gezielt.

Die Eingabe wird unter kurzem Dienstmutex kopiert und der vorhandene Worker
über die Condition Variable geweckt. Arbeitsauswahl und atomare Freigabe beim
Warten verwenden denselben Mutex; ein verlorener Wakeup ist nicht nachgewiesen.
Codec/DSP laufen außerhalb der Server-/Simplex-/Dienstsperren. Der Scheduler
muss den Worker nicht innerhalb von 51 µs ausführen.

Zusätzlich hält **ein noch nicht abgeholter Ergebnisbatch die weitere Arbeit
dieser Lane an**. Selbst ein sofort laufender Worker kann dann keine weiteren
Queueplätze freimachen. Diese bewusste Ergebnis-Backpressure bleibt erhalten.

Auch der Nachfolger derselben Verbindung wartet im Worker auf die vollständige
Abwicklung der älteren Generation, damit deren Ausgangsmedienuhr erhalten bleibt.
Ein zusätzlicher Sockettest bündelt einen bestätigten Ein-Paket-Vorgänger und
fünf Pakete seines Nachfolgers in **denselben** TLS-Schreibvorgang. Der queued
Dispatcher kann dazwischen nicht laufen. Beide Generationen werden empfangen,
dekodiert und mit altem Ende vor neuem Anfang geprüft. Der Diensttest untersucht
diesen Fall zusätzlich mit eingefrorenem Testclock und vollem Samplebudget.

Der neue Diensttest hält einen ersten echten Codec-Ergebnisbatch unkonsumiert,
reicht gültige Pakete nach und friert ausschließlich den injizierten Testclock
ein. Auf dem unveränderten B6B-Dienst scheitert deterministisch das fünfte
wartende 20-ms-Paket: `samples=960 queued=5 inputFull=1`. Damit ist die Kapazität
unabhängig von Scheduling und 40-ms-Ablauf reproduziert. Nach der Korrektur
bestehen derselbe Test und tatsächliche Socket-Empfangs-/Dekodiertests.

## Hergeleitete Kapazitätskorrektur

Vorher galten vier Pakete **und** 5760 Samples als Grenze. Vier 20-ms-Pakete
belegen nur 3840 Samples / 80 ms; der fünfte Versuch scheiterte also trotz noch
ausreichendem Samplebudget. Bei 10-ms-Paketen waren nur 40 ms nutzbar.

Jetzt wird die Zahl der festen Plätze aus dem vorhandenen Budget berechnet:

```cpp
InputSamples = 5760; // unverändert: 120 ms bei 48 kHz
InputPackets = InputSamples / (FrameSamples / 2) + 1; // 12 x 10 ms + leerer Marker
```

Die kleinste DSP-unterstützte Eingabe dauert 10 ms / 480 Samples. Zwölf solche
Pakete müssen dieselbe Medienkapazität nutzen können wie ein 120-ms-Paket.
Ein leerer bestätigter Terminator braucht zusätzlich einen Jobplatz, aber keine
Samples. Es sind deshalb **13 feste Plätze**, keine beliebig gewählte oder
dynamisch wachsende Queue. Die separate Originalreserve vor erster Kodierung
bleibt bei vier Jobs; Source-Historie, Codecplätze und Ausgang bleiben unverändert.

Die Sampleprüfung gilt weiter bei jeder Annahme: Ein siebtes wartendes 20-ms-
Paket überschreitet 5760 Samples und erzeugt echten `InputFull`. Für nicht
DSP-unterstützte 2,5-/5-ms-Pakete begrenzt zusätzlich die feste Paketanzahl den
Metadatenaufwand. Eine neue DSP-Unterstützung wird ihnen nicht zugesagt.

### Speicher und Latenz

Im verwendeten x86_64-Build steigt `fixedStorageBytes()` von **1727864** auf
**2261464 Bytes**: **533600 Bytes** zusätzlich je Dienst, höchstens **2134400**
zusätzliche Bytes bei der unveränderten Prozessgrenze von vier Diensten.
`sizeof(Batch)` steigt durch die Fehlerklassifikation von 67136 auf 67144 Bytes.
Codecobjekte, Bibliotheken und Stacks kommen separat hinzu; kein garantierter RSS.
Die zusätzlichen Wertfelder der optionalen Logqueue kommen ebenfalls getrennt
hinzu; deren bestehende Grenze von 4096 Einträgen bleibt erhalten.

Es bleiben ein Worker, vier Codecplätze, 16 Ticketlanes, 64 Empfänger je Snapshot,
1024 Bytes je Eingabepayload und ein Ergebnisbatch je Lane. Keine gespeicherten
Gesamtaussendungen, zusätzlichen Encoder oder unbegrenzten Lifecycle-Metadaten.

Ein lokaler Release-Mikrobenchmark verarbeitete vor der Reparatur je Profil und
Paketdauer 200-mal 120 ms gültiges Opus plus technischen Abschluss: drei Profile,
10/20/40/60/120 ms, 3000 Batches. Die Mediane lagen bei 0,99–1,22 ms,
95.-Perzentile bei 1,15–2,27 ms, das größte Wallclockmaximum bei 35,53 ms.
Dies ist ein Cloudbefund mit libopus 1.5.2 und zwei CPU-Kernen als cgroup-Quota,
kein VPS-Benchmark oder Scheduler-Versprechen. Qt, Netzwerk und Noise sind darin
nicht enthalten; mehrere aktive Kanäle teilen weiterhin denselben Worker.

Das Medienbudget ist **keine Wartezeit**: Der Worker wartet nicht auf 120 ms
Audio. Ein Burst kann schneller als seine nominelle Spieldauer verarbeitet
werden. Der **40-ms-Altersvertrag bleibt unverändert**, vor/nach Arbeit und
vor normaler Abholung. Dauerhafter Dispatcher-/Worker-Stall, stärkere Last oder
größere Bursts können weiterhin zu dokumentierten Verlusten führen.

## Fault, Ende und physische PTT-Wechsel

`InputFull` bleibt eine gescheiterte geordnete Annahme. Ein stiller Neustart
oder Midstream-Raw-Fallback würde Decoderzustand, Tickets, Encoderreserve und
Lifecycle nicht mehr lückenlos zuordnen. Deshalb bleiben Fehler gelatcht.
Wartende Daten und normale Ergebnisse werden verworfen; ein laufender Job
wird danach nicht als normale Sprache veröffentlicht. Eine bereits vorhandene
B2-Reserve darf weiterhin einmal geordnet mit `End::Fault` ausgegeben werden.

Der Fehlerbericht gibt Simplex und Kanalbarriere nicht frei. Weitere Eingaben
liefern `Faulted`. Ein zulässiger bestätigter Marker reserviert auch bei Fault
das Ende; ein echter Simplex-Timeout reserviert einen technischen Abschluss.
Erst nach dem geordneten terminalen Batch darf der Nachfolger Sprache übergeben.
Echte Invalidierung cancelt alte Identitäten.

Ohne gültigen Marker kann eine fortlaufende Aussendung die faulted Generation
weiterverwenden. Simplex erneuert seine Zulassung anhand eingehender Pakete;
Service-Annahme ist nachgelagert. Eine kurze physische PTT-Pause unterhalb von
Timeout/Quiet-Rearm erzeugt keine sichere neue Grenze. Ein Queuefehler kann
deshalb zahlreiche weitere Versuche ohne Busy verstummen lassen. Die Reparatur
verhindert den belegten unnötigen Kapazitätsfehler **vor** diesem Fault und
setzt keine Generation anhand vermuteter Tastbetätigungen zurück.

Auch eine gesunde Generation kann ohne bestätigte Grenze mehrere physische
Tastendrücke umfassen. Dies ist vom vollständigen Verstummen zu unterscheiden.
Die bereits in B4 dokumentierte moderne Protokollgrenze bleibt bestehen:
`UDPDecoder` lehnt leeres `opus_data` auch bei Terminatoren ab. Legacy-Eingänge
mit leerem Marker und moderne Payloadmarker werden getrennt getestet. Ein
zusätzlicher Test zeigt die unveränderte moderne Ablehnung. Das beweist nicht,
welche Marker Mumla tatsächlich sendete, und erklärt den A/B-Befund nicht allein.

## Separater Codecfehler und Diagnose

Der bestehende Sammelzähler `codecErrors` umfasst nicht nur negative libopus-
Ergebnisse, sondern auch Faulted-Streamzustand, `Ignored` oder fehlende Source-
Zuordnung. Ein monotoner, aber überlappender Quellframe kann `Ignored` liefern,
obwohl Simplex ihn zulässt und libopus keinen Fehler meldet.

Neue feste Unterzähler unterscheiden `codecBegin`, `codecStream`, `codecIgnored`
und `codecSourceMap`. `analog_result` ergänzt `codec_stage`, `stream_error`
und numerischen `codec_error`; die B2-Fehlerwerte bleiben erhalten. Ein späterer
leerer terminaler Faultbatch behauptet keine neue Codecoperation und kann
deshalb `codec_stage=none` enthalten.

Gezielte Tests unterscheiden gültigen Dauerheader mit ungültiger Struktur
(`fa ff`: `invalid_payload`, `OPUS_INVALID_PACKET` -4) von überlappenden Frames
(`Ignored`, Code 0), injiziertem Encoderfehler sowie Start-/Source-Map-Fehlern.
Bestehende B2-Tests injizieren außerdem echte Decoder-/Encoderaufruffehler.
Queuefehler mit nachfolgenden Faulted-Versuchen erzeugen im gezielten Test
keinen zusätzlichen Codecfehler. **Die Ursache des einen alten Proxmox-
Codecfehlers bleibt unbekannt; keine Gleichsetzung mit InputFull.**

`analog_submit` ergänzt Queuebelegung nach Annahme bzw. **vor** Fault/Clear bei
`input_full`: `queued_packets`, `queued_samples`. Neue Summen unterscheiden
Paket-/Samplegrenze und Hochwasser. Worker-Wartezyklen und maximale Jobalter
bei Arbeitsbeginn, Arbeitsende und normaler Abholung helfen bei künftigen
Scheduling-/Dispatcherbefunden. Das sind Wallclockalter inklusive Scheduling
und Sperrverzug, keine CPU-Zeiten oder generationgenau zugeordneten Maxima.

Alle Werte verwenden vorhandene kurze Sperren. Nur Annahmepfad/Dispatcher bauen
optionale Logereignisse; kein Worker-Dateizugriff, keine Sprachinhalte oder
Zugangsdaten, keine neue Queue und keine synchronen Serverrückrufe. Die bounded
Try-Lock-Logqueue darf weiterhin Einträge verlieren (`diagnostic_loss`). Lokale
Transportübergabe bleibt von tatsächlichem Empfang und Wiedergabe unterschieden.

## Tests und verbleibende Grenzen

`VoiceService` / `VoiceServiceSimplex` prüfen die deterministische Kapazität mit
echtem Codec und gefrorenem Testclock, beide Überlastgrenzen, gelatchte Folgen,
Marker/technische Recovery, Kanalbarriere, Identitäten und getrennte Fehlergründe.
`voice-service-test --burst-only` isoliert die neue Kapazitätsregression.

`TestAnalogVoiceBurst` ergänzt 64 Burst-Datenfälle: alle Profile/Paketdauern und
vier Transportvarianten, darin vier Analog-aus-Vergleiche; je zwei Aussendungen
an Legacy-UDP- und moderne TCP-Empfänger. Empfangene Sprache wird dekodiert und
bei aktivem Filter vollständig mit B2 verglichen, einschließlich Frames und Ende.
Hinzu kommen 18 PTT-Pausenfälle mit je vier Aussendungen (50–1000 ms; Marker,
unmarkiert, ausdrücklich Timeout), acht deterministische Socket-Überlast-/
Recoveryfälle, zwei Codecursachenfälle, die moderne Markergrenze und den
unmittelbaren Nachfolger bei noch wartendem Vorgängernachlauf.

Die B6B-Rapid-Matrix mit 10/50 Aussendungen und alle bisherigen Tests bleiben
erhalten. Neue Sockettests senden Paketgruppen ohne Medienpausen und prüfen
tatsächlichen Empfang/Dekodierung. Ein erster Testentwurf verwendete moderne
leere Marker entgegen dem Decodervertrag und deutete terminale Faultbatches
als neue Codecoperationen. Diese Fixture-Annahmen wurden korrigiert; keine
produktive Decoder- oder Fehlerregel wurde dafür geändert.

Tatsächliche lokale Abnahme:

| Prüfung | Ergebnis |
| --- | --- |
| Vollständiger Non-Unity-Release-Serverbuild | erfolgreich |
| Vollständige native CTest-Suite | **41/41**, 687,88 s; einschließlich B6B-Rapid und B6C-Burst |
| Isolierte B1–B3-Suite nach erweitertem Vorgängertest | **6/6**, 1,60 s |
| Neue Burst-Sockets | vollständiger Lauf 69,13 s und im Gesamtlauf 69,54 s bestanden; zusätzlicher gleicher-Verbindungs-Nachlauf bestanden |
| Striktes ASan/LSan/UBSan, sieben relevante Serverziele | **7/7**, 357,11 s; darin Rapid 182,40 s und vollständiger erweiterter Bursttest 105,91 s |
| Strikte isolierte ASan/LSan/UBSan-Suite | **6/6**, 3,37 s |
| Relevantes TSan | **2/2**, 0,65 s |
| Wiederholte Diensttests | je 50 Wiederholungen von VoiceService und VoiceServiceSimplex, **100 Ausführungen bestanden** |
| Sockettests auf einer CPU | **77/77 Qt-Fälle**, 31,62 s; Burstkapazität, wartender Vorgänger, Fault-Recovery und Codecgründe |
| Installer-/Audiokonfiguration | **44/44**, einschließlich AudioDefaults **3/3** (188,86 s), fehlender/deaktivierter/eigener INI-Werte und Zustandsübernahme |
| Source-/Setup-/Security-/GHCR-Guards | **31 + 28 + 13 + 3 bestanden** |
| Docker-Python | **38/38**, 4,21 s |
| DSP/WAVs/Standardkonfiguration | SHA-256 gegenüber B6B unverändert; `git diff --check` erfolgreich |

Keine gemeldeten Speicher-/UB-/Race-Befunde in diesen Sanitizerläufen und keine
Unterdrückungen. Ein erneuter vollständiger **41-Ziele-ASan-Lauf** ist damit
nicht behauptet. Externe Qt/OpenSSL/libopus sind nicht selbst instrumentiert;
die historischen B6A-Befunde bleiben mit ihrer dort dokumentierten Prüfabdeckung
bewertet. Die neue Routine unterdrückt weder Codecfehler noch echte Überlast.

Für den GHCR-Guard fehlte zunächst PyYAML; nach lokaler Installation bestand
er unverändert. Keine Testabschaltung oder produktive Abhängigkeitsänderung.
Docker und GitHub-CI werden anschließend am veröffentlichten Commit geprüft. Die nach dem Commit
entstehenden Ergebnisse und der aktuelle Status stehen im bestehenden
[Draft-PR #15](https://github.com/Shawna7han/mumble-funk/pull/15); ausstehende
Prüfungen gelten bis zum tatsächlichen Nachweis nicht als bestanden.

Simplex, Busy-Priorität, Squelch/300-ms-Frist/Timeout-Close, Noise, Boost, WAVs,
DSP-Profile, Neuinstallationsvorlagen und Bestandsschutz bleiben erhalten.
Fehlendes/false Analog Voice nutzt den Direktpfad ohne Worker. Reale Hör-/VM-/
VPS-Nachweise, Herkunft der UDP-Bündelung und alter Codecfehler bleiben offen.

## Nächster praktischer Test erst nach Freigabe

Die [Testinstallationsanleitung](install-analog-voice-test.md) verwendet weiterhin
`FUNK_BUILD_JOBS=1` und erhält Konfiguration/Zustände. Nach Betreiberfreigabe
dieselben Android-/Windows-Versionen mit Analog aus/an vergleichen: je 10/50
PTT-Wechsel, Pausen 50–1000 ms, UDP/TCP/Wechsel, Anfang/Ende sowie durchgehende
Anzeige und vollständigen Sprachverlust getrennt notieren.

Bei Verlust Generation/Ticket/Frame/Transport, Queuewerte, Codecdetails,
`diagnostic_loss` und geordnete Shutdownsummen privat sichern. Interessant ist,
ob Samplebudget, Jobalter oder ein konkreter Codec-/Sequenzfehler greift.
Keine Sprachaufnahmen oder INI-Passwörter veröffentlichen. Betriebsfreigabe
bleibt beim Betreiber; hier findet kein externer Test statt.
