# Analoge Sprachfilter: B3 – asynchroner Dienst und geordnete Tickets

Stand: 7. November 2026. Branch `feature/analog-voice-live`, Ausgangspunkt
`21717037e6f5dfcf21177dc4f56f08eb72e43ae9` (B2-Übergabe), fortbestehender
[Draft-PR #15](https://github.com/Shawna7han/mumble-funk/pull/15) gegen `dev`.
**Keine produktive Integration, Konfiguration, Aktivierung oder Betriebsfreigabe.**
Server, Simplex-Entscheidungen, Clients, Busy/Squelch/Noise, Installer und Docker
bleiben unverändert. Keine Weiterarbeit an B4–B6 durch diese Dokumentation.

## Bausteine und Besitz

`tools/analog-voice/include/AnalogVoiceService.h` und
`tools/analog-voice/src/AnalogVoiceService.cpp` bilden eine Qt-freie C++17-Bibliothek
`funk-voice-service`. Optional `VOICE_BUILD_SERVICE=ON`, benötigt
`VOICE_BUILD_LIVE=ON`; beides standardmäßig OFF. Keine Verlinkung in Murmur.
Eine Dienstinstanz ist für einen virtuellen Server bestimmt: ein Workerthread,
vier vorab konstruierte/recycelte `AnalogVoiceStream`-Paare, fester Speicher.
Initfehler verhindern die Konstruktion, bevor Annahme möglich wird.

Nur der Worker ruft Codecs auf. Produzenten und Hauptthread tauschen Werte über
einen kurzen Dienstmutex aus. Alle Decode-/DSP-/Encode-/Reset-/Abort-Aufrufe sowie
der Wake-Callback finden außerhalb dieses Mutex statt. Keine Serverobjekte,
Sockets, lebenden `AudioReceiverBuffer` oder geliehenen Netzwerk-Spans im Worker.
`submit` prüft Größen und TOC-Dauer (keine Dekodierung) und kopiert höchstens
1024 Bytes. Der spätere Server muss zusätzlich das Limit inklusive Mumble-Hülle
prüfen. Ein Test verändert den Originalpuffer während eines angehaltenen Workers.

`Processor` ist die schmale injizierbare Worker-Schnittstelle. Im normalen
Konstruktor kapselt sie ausschließlich den B2-Adapter; Tests injizieren Gates,
Maximalbatches und Fehler. Der unveränderte DSP und die drei Profile bleiben
bytegleich. Die additive B2-Methode `abort(Result&)` gibt bei Überlast nur die
vorhandene Reserve als `End::Fault` frei, ohne Codec-Arbeit; eigener Regressionstest.

## Identitäten, Tickets und Annahme

- `connect`: Verbindung + Session + Kanal bekommen eine neue Lebensdauerkennung
  und Routingepoch. Session allein genügt nie. Belegt ein begrenztes Register.
- `update`: neue Epoch auch bei Rückkehr in denselben Kanal; erhält die
  Ausgangsmedienuhr dieser Verbindung und invalidiert eigene wartende Generationen.
  `maySend` und `mayReceive` bleiben getrennt. `disconnect` entwertet die Identität.
- `setReceive`: für reine Deaf-/Empfangsrechtswechsel; streicht diese Identität
  unwiderruflich aus allen wartenden Empfängerschnittmengen, ohne ihre eigene
  Sendegeneration abzubrechen. Die Rückkehr kann keine alten Empfänger reaktivieren.
  Hier ersetzt die monotone Streichung einen zusätzlichen Empfangsepochenzähler;
  Kanal-/Verbindungswechsel verwenden weiterhin neue Identitätsepochen.
- `open`: nur nach externer legitimer Startentscheidung. Liefert `Handle` aus
  Speicherplatz und monotoner Übertragungsgeneration. Ein noch nicht geschlossener
  Vorgänger im selben Kanal verhindert einen neuen Start. Kein Simplex-Ersatz.
- `submit`: Ticket mit Senderidentität, Generation, Routingepoch, Kanalnummer,
  monotoner Kanal-Ticketnummer, Quellframe, tatsächlicher Sampledauer, Endeflag und
  monotoner Annahmezeit. Der Job besitzt Payload und Empfängersnapshot als Werte.
- `finish`: eigener reservierter technischer Abschluss hinter allen Datenjobs;
  kein behauptetes physisches PTT-Ende. Wiederholung liefert `Closed`.
- `cancel`/`invalidateChannel`: generationgebundener Verwurf ohne Ausgabe,
  funktioniert auch bei vollen Puffern und während Codec-Arbeit.

Ein dienstweiter 64-bit-Zähler erzeugt Epochen und Tickets. Die Teilfolge jedes
Kanals ist strikt steigend, aber bewusst nicht lückenlos. Bei Zählererschöpfung
keine Wiederverwendung. Voller Verbindungs-/Übertragungspool verweigert die
Registrierung/Annahme; kein unbeschränktes Mapping und kein versteckter Raw-Versand.

## Reihenfolge und Kanalbarrieren

Jede Übertragung hat eine begrenzte FIFO und einen Ergebnisbatch. Der Worker
bearbeitet runnable Plätze round-robin. Pro Kanal kann der Dispatcher nur die
älteste noch vorhandene Generation abholen. Erst ihr abgeholter terminaler Batch
oder ihre vollständig verworfene Arbeit räumt die Barriere. Deshalb bleibt
A-Ende vor B-Anfang, auch wenn B Bypass ist oder A langsam verarbeitet wird.
Andere Kanäle dürfen passieren; auch die Ergebnisabholung ist round-robin.
Ein einzelner Codec-Aufruf belegt naturgemäß den einen Worker; keine Zusage
paralleler Codec-Bearbeitung verschiedener Kanäle.

Ein angenommenes letztes Paket bleibt gültig, nachdem Simplex den Kanal normal
freigegeben hat. `take` prüft Identität/Rechte, **nicht** den aktuellen
Simplex-Kanalbesitzer. Kanalwechsel, Disconnect oder echter Rechteentzug
invalidieren dagegen alle alten Resultate. Laufende Codec-Arbeit wird nicht
gewaltsam unterbrochen: ihr Ergebnis wird nach Rückkehr verworfen. Platzrecycling
und Codecreset erfolgen erst danach im Worker. Alte Handles können keinen neuen
Platz treffen.

Pro Verbindung bleiben Generationen auch über Kanalwechsel/Slotwechsel seriell.
Der Worker persistiert `nextOutputFrame()` einschließlich verworfener Kodierungen
in der weiterhin existierenden Verbindungslebensdauer und gibt es als
`outputFrameFloor` an `begin` weiter. Quellenframes werden nicht verfälscht.
Unveränderte Bypass-Pakete behalten ihre originalen Quellenframes; deren Ende
hebt ebenfalls das Minimum für spätere gefilterte Sendungen an. Ein vom Client
zurückgesetzter Raw-Quellenzähler wird nicht umnummeriert; die Monotoniegarantie
bezieht sich auf neu kodierte Ausgaben, nicht auf unverändert durchgereichte Bytes.

## Dispatcher, Empfänger und Quellzuordnung

`Wake` stellt ausschließlich eine zusammengefasste Queued-Benachrichtigung bereit.
Der Dienst setzt ein Pending-Bit; der Hauptthread ruft einmal am Callback-Eingang
`acknowledgeNotification()` und dann `take(Batch&)` bis false auf. Höchstens ein
weiterer Wake kann während eines laufenden Callbacks ausstehen. Ein langsamer
Verbraucher blockiert keine Codec-/Netzwerkoperation: sein Ergebnisplatz bleibt
belegt, neue Arbeit dieses Platzes wartet begrenzt. Keine Events pro Paket.
Der optionale Qt-Anschluss und sein Lebensdauerkontext gehören ausdrücklich B4.

`take` entfernt ungültige Empfänger anhand von Verbindung, Lebensdauer, Session,
Kanal, Epoch und Empfangsrecht. **B4 muss anschließend unter Voice-Schutz die
Identitäten auf aktuelle ServerUser auflösen und unmittelbar beim Versand erneut
prüfen.** Der Dienst liefert Werte, keine gegen spätere Serveränderungen gesperrte
Versandgarantie. Empfänger dürfen dabei nur entfallen, niemals hinzukommen.

Bewusste konservative Abweichung vom minimalen Quellspan-Snapshot: Der Dienst
verwendet die kumulative **Schnittmenge der ganzen Generation**, einschließlich
bereits angenommener wartender Pakete. Ein später hinzugekommener Empfänger hört
erst eine neue Generation; ein entfallener Empfänger kann auch noch wartende
ältere Anteile verlieren. Das ist strenger als nur die Schnittmenge der tatsächlich
zusammengefassten Halbframes, verhindert aber Erweiterung durch DSP-/Codecgedächtnis
und hält die Zuordnung begrenzt. Kein Reblocking über Sender-Routingepochen.

Acht rollierende Quelltickets pro Platz erhalten die Zuordnung über Halbframe,
Codec-Lookahead und Abschlussreserve. `Batch.sources` referenziert die Source-Spans
jedes Ausgangspakets; PLC, Padding und EncoderDelay erfinden keine Eingangstickets.
Fehlende Zuordnung wird als Fehler behandelt. `Batch.completed` meldet auch einen
angenommenen Job ohne Audioausgabe, etwa einen ersten Halbframe. B4/B5 dürfen
Quelltickets in mehreren Ausgangsspans nicht mehrfach als Squelch-Ereignis zählen.
Positions-/Volume-/Protokollmetadaten und deren Anker sind noch Aufgaben von B4.

## Feste Grenzen und Fehlervertrag

| Ressource | Grenze |
| --- | --- |
| Worker / Codecpaare | 1 / 4 pro Dienst |
| Übertragungsplätze | 16 einschließlich Bypass und wartender Nachfolger |
| Registrierte Verbindungen / Empfänger je Snapshot | 128 / 64 |
| Wartender Eingang je Platz | 4 Pakete und 5760 Samples = 120 ms |
| Laufender Aufruf | zusätzlich höchstens ein Job im einzigen Worker |
| Ausgang je Platz | ein vorab reservierter Batch mit 10 Paketen; kein weiterer Job, bis er abgeholt ist |
| Bypass-Originalreserve | 4 Pakete vor erster Kodierung; B2 benötigt tatsächlich höchstens einen offenen Halbframe |
| Kontrolle | eigener Endejob plus Cancel-/Fault-/Shutdown-Zustand je Platz |
| Jobalter | 40 ms vor/nach Verarbeitung und vor normaler Ergebnisabholung |

Lokal x86_64/GCC 14: `fixedStorageBytes()` **887080 Bytes**, `sizeof(Batch)`
23704 Bytes. Hinzu kommen vier Streamobjekte (je 29240 Bytes), vier Opus-
Decoder/Encoderpaare, feste temporäre Worker-/Aufruferpuffer und Threadstack sowie
Bibliotheken. Kein Ziel-VPS-Benchmark oder garantierter RSS-Grenzwert. Keine
Laufzeitvergrößerung der Arrays; Konstruktion/Threadstart dürfen allokieren.

- Kein Codecplatz: die **ganze** neue Generation ist Bypass, solange ein eigener
  Ticketplatz verfügbar ist. Auch diese Originalbytes laufen durch die Barriere.
- B2 meldet vor erstem Encode Bypass: gesicherte Originale einschließlich des
  aktuellen Pakets werden geordnet zurückgegeben; danach bleibt Bypass gelatcht.
- Datenqueue voll: `InputFull`, ganze Generation Faulted, wartende Ergebnisse und
  Daten entwertet. Kein späterer Codecneustart und kein Midstream-Raw-Fallback.
- Ausgang belegt: vorerst keine weitere Verarbeitung; begrenzter Eingang und
  40-ms-Altersprüfung führen bei anhaltendem Druck zum gleichen Fehlervertrag.
- Abgelaufene Arbeit: `Expired` vor/nach Codec oder im Dispatcher. Auch spät
  zurückgekehrte Ergebnisse werden nicht als normale Sprache ausgegeben.
- Codecfehler: `Codec`, B2-Faultreserve wenn vorhanden, sonst leere Fehlermeldung.
- Externer Fault: `abort` holt bestmöglich **nur** die ungesendete Reserve mit
  `End::Fault`. Diese darf als Fehlerabschluss trotz abgelaufenem Jobalter passieren,
  jedoch nur an weiterhin gültige Empfänger. Kein künstlich kodierter Abschluss.
- Echte Invalidierung/Shutdown: keine Reservezustellung, vollständiger Verwurf.
- Ende bei voller Datenqueue: Endezustand wird trotzdem gespeichert; Payloadverlust
  ist ein expliziter Fehler, der Abschluss kann nicht still aus der Queue fallen.
- Fault bleibt bis zur externen Ende-/Abbruchentscheidung gelatcht. Ein Faultbericht
  allein gibt den Kanal nicht frei; reserviertes Ende räumt die Barriere geordnet.

`Statistics` zählt Annahmen, Eingangsüberlast, Ablauf, Codecfehler, verspätete
Ergebnisse, Cancel, Bypass-Starts und fehlende Ticketplätze. Keine Audiodatenlogs.
`Admission::Invalid` ist vor Ticketannahme abgelehnte fehlerhafte Eingabe; der
Server muss seine bestehende Validierung/Endlogik behalten. `open` ohne nachfolgenden
Job muss bei einem abgebrochenen Annahmeversuch explizit gecancelt werden.

## Sperren und Shutdown

Vertrag: Voice-Lock -> Simplex-Guard -> Dienstmutex. Kein Rückweg in Simplex,
kein Codec unter einer vom ausführenden Thread gehaltenen Server-/Simplex-Sperre,
kein Dienstmutex über Netzwerk oder Callback. Kontrolländerungen erfolgen später
unter Voice-Schutz. Shutdown: Produzenten und Qt-Kontext anhalten/entwerten,
Dienst stoppen/joinen außerhalb dieser Sperren, erst dann Besitzer zerstören.
Shutdown ist idempotent; mehrere gleichzeitige Shutdown-Aufrufer sind kein
unterstützter Vertrag. Siehe [MurmurLocking](../dev/MurmurLocking.md).

## Tatsächlich ausgeführte Prüfungen

Die finalen Befehle und Ergebnisse stehen auch in der [B4-Übergabe](analog-voice-b4-handoff.md).
Release **6/6**, ASan/UBSan **6/6**, TSan **2/2**, Haupt-CMake-Analogtests **3/3**.
Alle geänderten C++-Dateien separat ohne Unity und mit Warnungen als Fehler gebaut;
clang-format 10.0.1. GCC 14.2.0, CMake 3.31.6, libopus 1.5.2, lokal Qt 6.8.2 für den
isolierten echten Controller; CI nutzt Qt5. Externe Opus-/Qt-Bibliotheken sind
nicht selbst sanitizer-instrumentiert. DSP-Prüfsummen und Source-Guards (29/29) geprüft. Der vorhandene
`TestSimplex` wurde zusätzlich unverändert mit Controller/Logger in einem
separaten Qt6/AUTOMOC-Testbuild gebaut und bestanden.

Deterministische Tests verwenden Condition-Variable-Handshakes und eine injizierte
Uhr; keine zufälligen Sleeps. Fünf-Sekunden-Watchdogs brechen hängende Tests ab,
verändern aber nicht das Produktzeitlimit. Geprüft werden normale Reihenfolge,
A-Ende/B-Anfang mit verzögertem Worker und Raw-Nachfolger, unabhängige Kanäle,
Wertkopie, Empfänger raus/zurück/Rechteverlust/Sessionreuse, Senderwechsel,
Disconnect, Kanalinvalidierung, Cancel, alte Handles, Slotreuse und Medienuhr
auch nach Routingwechsel, Paket-/Medien-/Ausgangskapazität, reservierte Kontrolle,
vor/nach Verarbeitung und Ausgabe veraltete Arbeit, Bypass/Fault-Latch,
echte Reserve nach Überlast, 12 Start-/Stopzyklen sowie Shutdown bei aktivem Worker.

`VoiceServiceSimplex` benutzt unverändert den echten `Transmission`-Guard:
Ticketannahme noch unter Guard, sofortiger Nachfolger nach Release, gleichzeitig
ankommendes TCP-/UDP-Duplikat desselben Senders, nur eine zugelassene Übergabe.
Das ist ein Schnittstellentest, **kein Test echter Server-TCP-/UDP-Sockets**.
Echte Opus-Tests prüfen 10-ms-Zusammenfassung, Dekodierbarkeit, Endmarkierung,
Snapshot-Schnittmenge und vollständigen Bypass nach einem offenen Halbframe.
Das additive `Transmission::started` markiert genau die vorhandene
Idle→Owning-Entscheidung, auch bei Ein-Paket-Sendungen. Keine Änderung von
Berechtigung, Retry, Busy oder Release. Der B1/B2-Bestand deckt weiterhin alle Profile/Dauern, PLC, Nachlauf, Fehler und
lange Medienuhren ab.

Keine vollständige Murmur-/Noise-/Squelch-Serversuite lokal, keine Docker-Builds,
Gerätehörtests oder B6-Lasttests. Der zusätzliche GitHub-Workflow baut nur diese
isolierten Tests; bestehende Server-/Dockerverfahren bleiben unverändert.
GitHub-CI-Status nach Veröffentlichung separat berichten, nicht aus lokalen Tests
ableiten. Verbliebene Integrations- und Freigaberisiken stehen in der Übergabe.
