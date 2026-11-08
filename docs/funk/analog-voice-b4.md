# B4: experimentelle Murmur-Sprachintegration

Stand: 8. November 2026; `feature/analog-voice-live`, bestehender Draft-PR #15.
B4 verbindet die vorhandene B1–B3-Engine erstmals mit dem Server. Keine
Betriebsfreigabe; B5 und B6 bleiben eigenständige, noch nicht ausgeführte Phasen.

## Aktivierung und Grenzen

```ini
simplex=true
analogvoice=true
analogvoiceprofile=cb-am
```

Neustart erforderlich. `analogvoice` fehlt standardmäßig bzw. ist `false`.
Dann entstehen weder Dienst, Worker noch Codecinstanzen; direkte Sprache und
Effektbeobachtung bleiben im bisherigen Pfad. `false`/`0` und `true`/`1` werden
akzeptiert. Ungültiger Schalter, unbekanntes Profil, fehlendes Simplex oder ein
Initialisierungsfehler deaktivieren den Filter mit einer Serverwarnung.
`cb-am`, `business-fm`, `narrowband` verwenden unveränderte freigegebene Presets.
B2 unterstützt keine konfigurierbare Encoderbitrate oder separate Headroom-API;
`analogvoicebitrate` und `analogvoiceheadroomdb` wurden deshalb nicht eingeführt.
Es gelten unverändert B2s 40000 bit/s und Codecparameter.

**Bis B5 werden bei tatsächlich aktiviertem Dienst Squelch und kontinuierliches
Rauschen unterdrückt**, unabhängig von deren konfigurierten Schaltern. Busy bleibt
verfügbar. Es wird kein SquelchPlan für gefilterte Sprache gestartet und kein
bestätigter Close vor wartenden Sprachpaketen erzeugt. Dies ist eine bewusst
begrenzte experimentelle Betriebsart, keine vollständige Effektintegration.
Mit deaktiviertem/fehlgeschlagenem Filter gilt die bisherige Effektkonfiguration.
Installer und Docker aktivieren den Filter nicht automatisch.

## Serverpfad und Lebensdauer

`ServerAnalogVoice.cpp` enthält Initialisierung, Registrierung, Ticketübergabe,
Release-Behandlung und Qt-Dispatcher. Ein Dienst gehört einem virtuellen Server;
maximal vier Dienste pro Prozess verhindern unbemerkte Ressourcenvervielfachung.
Ein weiterer angeforderter Dienst bleibt mit Warnung deaktiviert. Je Dienst:
ein Worker, vier Codecpaare, 16 Übertragungen, 128 Verbindungen, maximal 64
Empfänger je Snapshot; sonstige B3-Queue- und Zeitlimits bleiben erhalten.

`Server::processMsg()` führt unverändert Authentifizierung, Mute/Suppress,
Bandbreitenprüfung, Simplex und die bestehende Empfängerauswahl aus. Noch unter
Voice-Read-Lock und gültigem Transmission-Guard öffnet `started` die Generation;
`submitAnalogVoice` kopiert das Original-Opus samt Quellframe und Metadaten in
B3s geordnete Annahme. Frame 0 und Logs erzeugen keine neuen Generationen.
Codec/DSP-Arbeit findet ausschließlich im Worker außerhalb der Serverlocks statt.
Pro Eingang entsteht eine Verarbeitung, deren Payload alle zugelassenen
Empfänger gemeinsam erhalten.

Verbindungen werden beim Kanaleintritt registriert. ServerUser hält die vollständige
Dienstidentität einschließlich Lebensdauer und Routingepoch. Kanal-/Senderechtswechsel
rufen `update`, reine Empfangsänderungen `setReceive`, Disconnect `disconnect` auf.
Deaf/SelfDeaf-Pruning kann keine alten Empfänger reaktivieren. Mumble koppelt manche
Deaf-Nachrichten selbst an Mute; eine damit verbundene Sendesperre bleibt bestehen.
Der bestätigte Marker schließt über das angenommene Paket. Timeout reserviert
`finish` unter dem Simplex-Mutex; andere Releases canceln. Der Callback löst keine
Benutzerpointer auf und nimmt keinen Voice-Lock. Codecfehler ändern Simplex nicht.

Beim Shutdown stoppen UDP und Simplex-Timer zuerst. Simplex invalidiert seine
Generationen; danach wird der Worker außerhalb der Serverlocks gestoppt/gejoint.
Der QObject-Kontext bleibt bis zum Join erhalten. Später aufgerufene queued
Callbacks prüfen den Dienstpointer; QObject-Zerstörung entfernt übrige Events.

## Qt, Rechte und Versand

B3s koaleszierter Wake ruft ausschließlich ein kontextgebundenes queued
`invokeMethod` auf. Der Hauptthread versucht Voice-Write-Lock; bei Konflikt bleibt
die Notification unbestätigt und genau ein kontextgebundener 1-ms-Retry wird geplant.
Erst nach Lockgewinn folgen `acknowledgeNotification` und `take` bis leer.
Auch leere Kontroll-/Terminalbatches werden konsumiert. Kein gepopptes Ergebnis
wird wegen eines Lockkonflikts verloren.

Der Dispatcher prüft Senderidentität, Authentifizierung, Kanal, Mute/Suppress und
aktuelles Speak-Recht. Empfänger werden über Session gefunden, aber nur bei
vollständiger Identitätsgleichheit akzeptiert; neue Empfänger werden niemals
hinzugefügt. B3s konservative kumulative Empfängerschnittmenge bleibt bestehen.
`AudioReceiverBuffer::addReceiver` prüft zusätzlich Deaf/SelfDeaf und Positionskontext.
Die Lebensdauer-/Rechteprüfung bleibt bis zur Netzwerkübergabe durch Voice-Write
abgesichert. Normaler Simplex-Release entwertet keine bereits angenommene Sprache.

`sendVoicePacket` ist der gemeinsame ursprüngliche Verpackungs-/Versandhelfer.
Legacy/modernes Protokoll, Positionsdaten, Kontext und empfängerabhängige
VolumeAdjustment-Gruppierung bleiben dort. Im Simplex-Modus gibt es weiterhin
keine Listener-/Whisper-Routen und deren individuellen Lautstärken; reguläre
Empfänger verwenden Faktor 1. Sender bleibt die echte Session. UDP nutzt die
bestehende Verschlüsselung. Gefiltertes TCP wird im Hauptthread unmittelbar über
`tcpTransmitData` übergeben, damit kein zweites session-only Qt-Event eine alte
Identität später ansprechen kann. Bei TLS-Pufferständen ab 65536 Bytes werden
Pakete für den langsamen Empfänger verworfen; es gibt keine neue Warteschlange.
Der deaktivierte Direktpfad verwendet weiterhin den ursprünglichen TCP-Signalweg.

## Metadaten, Fehler und Ressourcen

B3 Input/Ticket besitzen additive kopierte `Metadata`: Position und die ursprünglich
für Positionen zugelassenen Verbindungskennungen. B2/B3-Quellspans tragen diese
über Halbframes, Reserve und Lookahead. Anker ist `sources[i][0]`, der erste tatsächlich
beitragende Quellanteil; niemals das eventuell spätere `completed`-Ticket. Reine
PLC-/Padding-/Delay-Pakete erfinden keine Position oder Effektbeobachtung.
Neue Positionskontext-Übereinstimmung erweitert die ursprüngliche Positionsaudienz
nicht. Quellenframes bleiben unverändert, gefilterte Ausgangsuhren werden durch
B3 pro Verbindungslebensdauer auch über Slotwechsel erhalten.

B3 regelt Raw-Bypass vor erstem Encode, Kanalbarrieren, Faulted-Latch, reservierte
Abschlüsse, 40-ms-Jobalter und begrenzte Queues. Kein freier Ticketplatz bedeutet
Paketverlust, niemals direkten Versand um eine Barriere. Ungültige Annahme,
fehlerhafte TOC-Dauer oder zu großer Empfängersnapshot canceln den Versuch.
`InputFull`/`Faulted` bleiben bis zum externen Ende geordnet gelatcht.
Nicht-Opus gehört nicht zum aktivierten experimentellen Pfad. Unterstützte
2,5-/5-ms-Opus-Pakete folgen B2s vollständigem geordnetem Bypass, ohne Rundung.
Beim Stop werden aggregierte Dienstzähler ohne Audioinhalte geloggt.

Lokal x86_64: `fixedStorageBytes()` 1727528 Bytes, `sizeof(Batch)` 67120 Bytes.
Dazu kommen vier B2-Streamobjekte/Opuspaare, temporäre Puffer, Threadstack und
Bibliotheken. Die Metadaten erhöhen B3s festen Speicher. Dies ist keine RSS- oder
VPS-Leistungsmessung; B6 bleibt offen. Keine Sprachaufnahmen oder neuen Ports.

## Validierung

Release-Vollbuild und Debug-Vollbuild mit ASan/UBSan wurden ohne Unity gebaut;
Warnungen als Fehler. Die tatsächlichen abschließenden Ergebnisse stehen unten.
Die bestehenden DSP-Prüfsummen sind unverändert. Der neue Sockettest umfasst:
fehlenden/false/ungültigen Schalter, ungültiges Profil, alle drei Profile mit
10/20/40/60/120 ms, dekodierbare bytegleiche B2-Referenz, zwei Protokollversionen,
Positionen, echte Senderidentität, verschlüsseltes UDP und TCP-Wechsel,
Halbframe-Anker, neue/wechselnde/deafe Empfänger, Sender-Kanal-/Mute-/ACL-Verlust,
Disconnect, Paketlücken, leere Marker, Timeout, fünf unabhängige Kanäle und
Codecmangel. Ein eigener Test prüft die bestehende Bandbreitensperre.

Linux-Testbuilds linken `CodecGate.cpp` via `--wrap=opus_decode_float` ausschließlich
bei `tests=ON`. FIFOs halten gezielt den ersten Workeraufruf: Mainthread-Roundtrips,
Queueüberlast, Fehlerbarriere, Disconnect und Shutdown werden so geprüft.
Produktionsbuilds enthalten diesen Testhook nicht. B3s deterministische Gate-/Clock-
Tests bleiben erhalten. Der Fünfsendertest benötigte `messageburst=20` für seine
Kanal-Einrichtung: vorher verwarf das normale Burstlimit die fünfte Kanalanforderung.
Keine Produktlimits oder Wartezeiten wurden dafür verändert.

Offene Nachweise: erzwungene Sessionnummer-Wiederverwendung und alle Queue-/Codec-
Fehler sind überwiegend im isolierten B3-Test bewiesen; keine vollständige
Socketmatrix aller Fehlerfälle. Kein deterministisch erzwungener Qt-Voice-Lockkonflikt,
kein saturierter realer langsamer TCP-Empfänger, keine prozessweite Vier-Server-
Budget-Abnahme. Externe Qt/libopus sind nicht sanitizer-instrumentiert. Keine
Hör-/Hardware-/VPS-Abnahme und keine Aussage zur produktiven Latenz.

Weitere bestehende Protokollgrenze: `UDPDecoder` verwirft im modernen Format
Pakete mit leerem `opus_data`, auch wenn sie Terminatoren sind. Gefilterte B2-
Abschlüsse enthalten Payload und funktionieren in beiden Varianten. Ein leerer
Raw-Bypass-Abschluss behält dagegen das bisherige Verhalten; der Fünfsendertest
prüft diesen Fall mit Legacy-Empfänger. Diese Einschränkung wurde nicht durch
Protokolländerungen oder erfundene Opus-Payloads verdeckt.

### Tatsächliche Ergebnisse dieser Sitzung

- Vollständige Release-Serversuite: zunächst 36/37 CTest-Targets erfolgreich;
  das neue B4-Target wurde anschließend korrigiert und separat vollständig
  erfolgreich wiederholt (37 QtTest-Fälle inklusive Init/Cleanup). Der korrigierte
  bestehende AudioFeedback-Integrationstest wurde ebenfalls erneut bestanden.
  Dies ist ein Gesamtbefund aus Vollsuite und gezielten Wiederholungen, kein
  behaupteter zweiter kompletter grüner CTest-Lauf.
- Isolierte B1/B2/B3-Release-Suite: 6/6; ThreadSanitizer-Service/echter
  Simplex-Handoff: 2/2 ohne Befunde.
- Debug-Vollbuild mit ASan/UBSan erfolgreich. Ausgewählte 18 CTest-Targets:
  nach gezielter Wiederholung 16 erfolgreich, 2 weiterhin fehlgeschlagen:
  `TestAnalogVoiceIntegration` beim Shutdown (36/37 QtTest-Fälle bestanden)
  und `TestCryptographicHash` beim Leakcheck. Kein sauberer Sanitizer-Abschluss.
- Der Shutdown meldet 224 geleakte Bytes aus `ChanACL::effectivePermissions`,
  neu angelegt über `Server::~Server` -> `clearACLCache` ->
  `flushClientPermissionCache`. LeakSanitizer ändert den erwarteten SIGTERM-
  Exitcode 15 auf 1. Der Test bleibt rot, keine Leakunterdrückung.
- `TestCryptographicHash` meldet Lecks aus bestehenden Konstruktorallokationen.
  UBSan meldet außerdem misaligned Load/Store in `CryptStateOCB2.cpp:249` bei
  verschlüsseltem UDP. Diese bestehenden Pfade wurden durch B4 nicht geändert;
  sie bleiben ausdrücklich offene Sanitizer-Befunde, auch wo CTest Exit 0 meldet.
- ASan fand zusätzlich einen bestehenden QStringBuilder-Lebensdauerfehler in
  `TestAudioFeedbackIntegration::standardAsset`. Separater Fix materialisiert
  den Pfad als QString; Release und ASan-Wiederholung bestanden danach.
- Source-Guards 29/29, Setup 25/25, Audio-Security 13/13,
  Docker-Python-Guards 38/38, GHCR-Workflow-Guards 3/3 bestanden.
- DSP-SHA256-Dateien unverändert; `git diff --check` bestanden;
  geänderte C++-Abschnitte mit clang-format 10 formatiert, Non-Unity-Builds.

Reproduktion in dieser Cloud mit vorhandenen Abhängigkeiten aus
`/workspace/.cloud-deps/activate.sh` (Qt5-Plugins für Haupttests auswählen):

```sh
cmake -S . -B work/b4-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_NUMBER=939 -Dclient=OFF -Dserver=ON -Dtests=ON -Dplugins=OFF \
  -Doverlay=OFF -Dlto=OFF -Dice=OFF -Dzeroconf=OFF -DCMAKE_UNITY_BUILD=OFF
cmake --build work/b4-release --parallel 2
ctest --test-dir work/b4-release --output-on-failure

cmake -S . -B work/b4-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -Dclient=OFF -Dserver=ON -Dtests=ON -Dplugins=OFF -Doverlay=OFF -Dlto=OFF \
  -Dice=OFF -Dzeroconf=OFF -DCMAKE_UNITY_BUILD=OFF \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie'
cmake --build work/b4-asan --parallel 2
ctest --test-dir work/b4-asan \
  -R 'Voice|Simplex|Squelch|AudioReceiver|AudioFeedback|Crypt' --output-on-failure
```

`BUILD_NUMBER=939` entspricht dem bestehenden Installer-Testvertrag. Kein Docker-
Vollbuild, keine Ice-/Zeroconf-Laufzeitabnahme, keine Installeraktivierung auf
externen Rechnern. GitHub-CI wird nach Push separat geprüft; lokale Befunde sind
keine Aussage über einen erfolgreichen entfernten CI-Lauf.

Native Installer-Stagingtests nach vollständigem Release-Neubuild mit
`BUILD_NUMBER=939`: **5/5 bestanden**. Der erste Versuch mit leerer Buildnummer
wurde korrekt vom bestehenden Versionsguard abgewiesen; weder Installer noch
Versionsprüfung wurden angepasst.

## CI-Nachkorrektur: fehlende Überlaststatistik beim Shutdown

Der wiederholte Fehler bei `blockedWorkerOverload` war lokal reproduzierbar:
14 erfolgreiche Läufe, dann fehlte `inputFull=1`. Die vollständige Ausgabe endete
bei `Ending voice thread`; nach dem unveränderten 3-s-Watchdog wurde der Server
gekillt. Ein zusätzlicher strace-Lauf reproduzierte den Fehler sofort und zeigte,
dass SIGTERM im Codec-Worker ankam (derselbe Thread hatte zuvor die Gate-FIFOs
geöffnet). `cleanup()` führte dort den Serverabbau aus; Qt meldete ausdrücklich
`requested database does not belong to the calling thread`.

Die abschließende Statistik fehlte vollständig. Das war kein Nachweis eines
abweichenden Überlastzählers oder eines vorherigen Expired/Codec-Fehlers. B3 lässt
bei angehaltenem Worker vier weitere Inputs zu; der nächste Input latcht einmal
InputFull. Weitere Inputs bleiben Faulted, der Endmarker reserviert den Abschluss.
Die FIFO-Handshakes ordnen diese Annahme vor der Workerfreigabe. Der erste Fehler
bleibt gelatcht; der Test verlangt weiterhin genau einen InputFull-Eintrag.

Ursache: `UnixMurmur` installiert bereits einen SIGTERM-Socket/Qt-Notifier, aber
`main.cpp` überschrieb ihn später mit `signal(SIGTERM, cleanup)`. Dadurch konnte
Shutdown aus einem beliebigen unterbrochenen Thread bzw. Lockabschnitt erfolgen.
Die kleine Produktkorrektur lässt den vorhandenen Unix-Notifier bestehen; nur
Windows behält die direkte Registrierung. Der Hauptthread verlässt danach die
Ereignisschleife und führt `cleanup(0)` geordnet aus. SIGTERM endet unter Unix damit
regulär mit Exitcode 0. SIGINT und sonstige Signalpfade wurden nicht verändert.

Testkorrektur und Regression:

- Ein fehlerhafter Statistik-/Shutdown-Befund gibt nun die vollständige
  Serverausgabe mit vorhandenen Zählerständen sowie Exitstatus/-code aus.
  Ein fehlender Abschluss wird nicht als alternativer Überlastfehler akzeptiert.
- Die Überlast- und Raw-Bypass-Prüfungen bleiben erhalten. Ein anderer Sender wird
  noch bei blockiertem Worker über authentifiziertes UDP angenommen; vor Freigabe
  darf nichts beim Empfänger ankommen. Nachfolgersprache bleibt hinter dem alten
  Fehlerabschluss; nach Beginn der Nachfolgersprache darf keine alte Ausgabe folgen.
  Der UDP-Ping dient als Annahmebarriere ohne TCP-Nagle-Wartezeit im 40-ms-Jobbudget.
- Der ausschließlich in Linux-Testbuilds vorhandene Codec-Gate meldet seine
  Thread-ID. Der Shutdown-Test sendet SIGTERM mittels tgkill gezielt an diesen
  angehaltenen Worker und verlangt geordneten Exit 0. Damit wird der ursprüngliche
  Fehler nicht nur von der zufälligen Signalauswahl des Kernels abhängig geprüft.
- Keine produktiven Queue-/Zeit-/Codecgrenzen wurden verändert, keine Prüfung
  entfernt und kein Timeout erhöht. Die bekannten ASan/UBSan-Befunde bleiben offen.

Gezielte Wiederholungen nach der Signalwegkorrektur: beide Worker-Tests gemeinsam
in 50 normalen Läufen, 20 Läufen unter `strace -f` und 30 auf eine CPU gebundenen
Läufen bestanden (100/100 Paare). Der exakte Zählervergleich berücksichtigt nun
auch Wortgrenzen, damit `inputFull=10` nicht als `inputFull=1` gelten könnte.

Nachkorrektur, native Gesamtabnahme: vollständiger Non-Unity-Release-Serverbuild
und **37/37 CTest-Targets bestanden**, einschließlich aller bestehenden Server-,
Simplex-, Squelch- und Noise-Tests; native Installer-Stagingtests **5/5**.

Docker-Nachprüfung: vollständiger Dockerfile-Build unter Ubuntu 24.04 erfolgreich,
inklusive **37/37 CTest-Targets** und **38/38 Docker-Python-Tests**. Damit wurde die
Korrektur zusätzlich mit den dortigen Qt5-/libopus-Versionen geprüft.

Umgebungsdetail der Container-Laufzeitprüfung: Die Cloud-Native-Probe war gegen
libopus 1.5.2 gebaut, das Ubuntu-Image enthält 1.4. Der erste zusätzliche
Laufzeitlauf scheiterte deshalb beim bytegenauen Busy-Vergleich (`Busy priority
or bytes changed`). Für die Wiederholung wurde die unveränderte Probe über
LD_LIBRARY_PATH mit exakt der libopus.so.0 aus dem fertigen Image ausgeführt
(Version per opus_get_version_string und Auswahl per ldd bestätigt). Weder
Paketvergleich noch Testskript oder Image wurden dafür verändert. Im GitHub-
Ubuntu-Workflow verwenden Probe und Image ohnehin dieselbe Opus-Version.

Die angeglichene Container-Laufzeitprüfung bestand vollständig:
`integration.py --audio-feedback --radio-noise --host-port 64740`, einschließlich
TCP/verschlüsseltem UDP, bestätigten/Timeout-Abschlüssen mit 62-s-Noise-Fall,
Busy und unveränderter Sprache, Rootless-Isolation, Neustart/Recreate,
Portkonflikten, parallelem nativem Server, konsistentem Backup/Restore und
sauberem Shutdown. Die bekannten ASan-/UBSan-Befunde wurden dabei nicht bearbeitet.
