# B5: geordnete Squelch- und Noise-Anbindung

Experimenteller Stand auf `feature/analog-voice-live`, bestehender Draft-PR #15.
Ausgangspunkt: `5b1473b6c1d3ab85e06152df17b35ce20bc4e20f`. Der ausdrücklich unfertige
Sicherungscommit `b4133e3e0` erhält den unterbrochenen Arbeitsstand. B6 und eine
Betriebsfreigabe bleiben ausstehend. Kein Merge, Deployment oder automatisches
Aktivieren durch Installer/Docker.

## Zustellung und Originaltickets

`ServerAnalogVoice.cpp` verwendet weiterhin den B4-Dispatcher, Voice-Write-Trylock,
koaleszierte Qt-Notification und B3-Kanalbarriere. `AnalogVoiceLifecycle` ist ein
schmaler Hauptthreadadapter mit genau 16 Lifecycle-Plätzen, passend zu den
bestehenden Dienstlanes. `Batch.handle` identifiziert Lane und Generation; eine
neue Generation ersetzt deren alten Metadatenzustand. Es entsteht keine zusätzliche
Audioqueue, kein Worker und kein Renderer.

Nach jedem Sprachpaket liefert der gemeinsame Versandhelfer die tatsächlich lokal
übergebenen vollständigen Empfängeridentitäten zurück. UDP zählt nur bei erfolgreichem
Datagramm-Systemaufruf; unmittelbares TCP zählt erst nach der bestehenden
TLS-Backlogprüfung und Übergabe an den TCP-Tunnel. Fehlende Crypto-Initialisierung,
fehlgeschlagener UDP-Versand oder ausgeschöpftes TLS-Budget erzeugen keine erfolgreiche
Empfängerzustellung im Lifecycle. Das ist keine Bestätigung von Netzwerkempfang
oder hörbarer Wiedergabe.

Der Adapter liest ausschließlich `Batch.sources` für das tatsächlich übergebene
Paket. Ein monotoner Originalticket-Wasserstand je Generation dedupliziert mehrere
Source-Spans und wiederkehrende Lookahead-Anteile. Die vom Dienst garantierte
Medien-/Ticketreihenfolge ist dafür Voraussetzung. Beobachtet werden ursprüngliche
Quellframezahlen; `completed` dient ausschließlich dem Abschluss, niemals als
Ersatzquelle für ein Ausgangspaket. Ein erster 10-ms-Halbframe wartet dadurch bis
zu seiner späteren echten Zustellung. PLC, Padding und EncoderDelay erzeugen keine
neuen Originalbeobachtungen.

Open entsteht frühestens nach lokaler Übergabe eines tatsächlich beitragenden
Sprachpakets an einen zulässigen Empfänger. Reine Annahme oder Jobfertigstellung
erzeugen keine Effekte. Auch beim Payload+Endmarker wird das Original zuerst mit
`last=false` beobachtet; sein Ende folgt separat nach dem vollständigen Nachlauf.
Dies verhindert vorzeitiges Close bei mehreren Ausgabepaketen desselben Originals.

## Empfänger, Rechte und Effektstream

B4 prüft weiterhin die vollständige Identität mit Verbindung, Lebensdauer, Session,
Kanal und Routingepoch unter Voice-Schutz. Der Dienst liefert die konservative
Schnittmenge seiner Originalsnapshots. Der Adapter darf diese nur weiter verkleinern:
bei später gescheiterter lokaler Zustellung entfernt `retainRecipients` den Empfänger
unwiderruflich aus der laufenden Squelchgeneration. Neue Empfänger bekommen keine
alten Sprach- oder Effektteile. Kanalwechsel, Disconnect und Senderrechteverlust
invalidieren den Bestand. Reine Empfangssperren verwenden `invalidateRecipient`,
um die eigene Sendung nicht zusätzlich abzubrechen. Mumble kann Deaf-Nachrichten
weiterhin selbst mit Mute koppeln; ein echter Senderechteverlust gilt unverändert.
Die Effektzustellung prüft bei aktiviertem Analogpfad zusätzlich aktuelles Speak.

Die vorhandenen WAVs, Lautstärken, AudioFeedback-Queues, Noise-Produzenten,
Encoder und Timer bleiben bestehen. Der Adapter ruft nur deren bestehende
`requestSquelch`-/`requestNoise`-Schnittstellen auf. Open, kontinuierliches Rauschen
und Close bleiben ein gemeinsamer privater Effektstream je Empfänger; Sprache
behält den echten Sender und ihren getrennten Stream. Kein PCM-Mixing, zusätzlicher
Encoder oder Bot. Busy verwendet unverändert die bestehende Priorität.
Bei `squelchopen=false` wird der unterdrückte Open intern bestätigt, sodass
Close-only und Noise-only weiter funktionieren.

## Ende, Frist und Kanalnachfolger

Ein bestätigtes Payloadende fordert Close erst nach Abarbeitung aller Pakete des
terminalen Batches an. Ein leerer Originalmarker ohne Source-Span wird genau einmal
im terminalen Batch beobachtet; ohne vorausgegangene Sprachübergabe kann er keinen
Open erzeugen. Auch leere Kontrollbatches werden abgearbeitet.

Tickets erhalten bei Annahme ihre ursprüngliche monotone Zeit. Der verzögerte
Close übernimmt diese Endzeit. `SquelchPlan::end` beendet eine bereits beobachtete
Payload ohne erneute Originalbeobachtung. Der zusätzliche optionale Endzeitparameter
von `observe` gilt für leere Originalmarker; bisherige direkte Aufrufer behalten
ihre Zeitsemantik. `MaxCloseCueAgeUs` bleibt **300000**. Ein im Dispatcher oder
Renderer bereits überfälliger Close darf ausfallen; die Frist wird nicht neu gestartet.

Bei tatsächlichem Simplex-Timeout reserviert der Release-Callback ausschließlich
`finish(handle, event.frame, event.timeUs)`. Er nimmt keinen Voice-Lock, verwendet
keine Userpointer und führt keine Codec-/Netzwerkarbeit aus. Der Dispatcher beendet
den Lifecycle nach dem technischen Drain. Close wird nur bei aktiviertem
`squelchclosetimeoutfallback` angefordert. Die tatsächliche Timeoutzeit bleibt die
Fristbasis. Ein technischer Sprachterminator ist keine Bestätigung physischen
PTT-Loslassens; Simplex/Diagnosen werden nicht zurückgespeist.

B3 garantiert weiterhin letzte Sprachübergabe A vor erster Sprachübergabe B.
Der Dispatcher bearbeitet A-Abschluss vor dem nächsten Batch von B. Dessen neue
Squelchgeneration entwertet optional noch wartende alte Cues. Sprache wartet nie
auf einen optionalen Ton. Andere Kanäle bleiben unabhängig.

## Fehler und Konfiguration

Codecfehler, Queueüberlast, Ablauf und Sequenzfehler latchen den Lifecycle als
fehlgeschlagen und invalidieren bereits laufende optionale Effekte. Eine zulässige
B3-Fehlerreserve darf weiterhin als Fault-Sprachabschluss übergeben werden; sie
erzeugt weder neuen Open noch bestätigten Close. Keine Midstream-Raw-Umschaltung,
keine Barrierenumgehung, kein künstlicher Simplex-Release. Echte Invalidierungen
und Shutdown verwerfen alte Arbeit. Die B4-Unix-SIGTERM-Korrektur bleibt erhalten.

```ini
simplex=true
analogvoice=true
analogvoiceprofile=cb-am
audiofeedback=true
busytone=true
squelch=true
squelchopen=true
squelchclose=true
squelchclosetimeoutfallback=true
radionoise=true
```

Nur ein Konfigurationsbeispiel, keine Aktivierungsanweisung. Bestehende WAV-Pfade
und Lautstärkeparameter gelten weiterhin. Fehlendes/false `analogvoice` verwendet
den bestehenden Direktpfad einschließlich originalem Opus und Effektverhalten,
ohne zusätzlichen Worker. Profile und DSP-Prüfsummen sind unverändert.

## Prüfungen und Grenzen

Die tatsächlichen Abnahmeergebnisse werden unten festgehalten. Die neuen
`TestAnalogVoiceEffects`-Sockettests prüfen alle drei Profile und 10/20/40/60/120 ms,
Legacy/modern, bestätigte Payload- und Leerenden, Timeout und abgeschalteten
Fallback, Close-only, Open-only, Noise-only, 5-ms-Bypass, gemeinsame kontinuierliche
Effektsequenzen, Busy, TCP/verschlüsseltes UDP und Transportwechsel, Halbframes,
Empfänger raus/zurück/deaf/später hinzu sowie Sprecherwechsel und parallele Kanäle.
Der Codec-Gate prüft fehlende Effekte vor Workerfreigabe, Disconnect, Kanalwechsel,
Mute und echten Timeout bei blockiertem Worker.

`TestAnalogVoiceLifecycle` prüft deterministisch Source-Deduplizierung,
abweichendes completed-Ticket, Source-freie technische Anteile, leere terminale
Batches, bestätigte und technische Abschlüsse, Fristgrenzen 299999/300000/300001 us,
alle Fehlerklassen, fehlgeschlagene Zustellung, irreversible Empfängerentfernung
und alte Cues beim Generationenwechsel. B4-Überlast-/Shutdown- und B3-Fehlertests
bleiben erhalten. Socket-Gate-Timeouts dürfen eine zulässige Faultreserve liefern;
der Test unterscheidet diese von Erfolgseffekten. Der Nachfolger verwendet einen
regulären, vorab für UDP eingerichteten Client.

Keine Gerätehörtests oder VPS-Messungen. Zwei Transportwege und Client-Jitterbuffer
können Wiedergabereihenfolgen verändern. Deterministische fehlgeschlagene Zustellung
wird am Adapter geprüft; ein real saturierter TLS-Empfänger, forcierte Sessionreuse
und alle Codecfehler im vollständigen Socketpfad bleiben ergänzende Nachweise.
Die bestehende moderne Decodergrenze für leeres Raw-Opus bleibt bestehen.
B3s 40-ms-Jobalter und begrenzte Ressourcen erlauben Paketverluste unter Last.
EOF-Reserve und Encoder-Lookahead bleiben zusätzliche Sprachlatenz.

Bekannte B4-Sanitizerrestpunkte bleiben bis zu einem nachgewiesenen eigenen Fix
offen: ACL-Cache-Leak beim Serverabbau, CryptographicHash-Lecks sowie unaligned
OCB2-Zugriffe. Externe Qt-/libopus-Bibliotheken sind lokal nicht instrumentiert.

## Tatsächliche Abnahme

- Vollständiger Non-Unity-Release-Serverbuild erfolgreich.
- Erneute vollständige native CTest-Suite: **39/39 bestanden**, 445,12 s.
- Gezielte abschließende Analog-Suite: **3/3 bestanden**; zusätzlich fünf
  gemeinsame Wiederholungen der neuen Gate- und Sprecherwechseltests erfolgreich.
- Isolierte B1–B3-Release-Suite: **6/6**, relevante TSan-Tests **2/2** bestanden.
- ASan-/UBSan-Vollserverbuild erfolgreich. Ausgewählte vier CTest-Targets:
  **2/4 bestanden** (die beiden neuen B5-Targets); B4-Analog-Integration und
  CryptographicHash wegen offener Leakbefunde fehlgeschlagen. Der B4-Test meldet
  die Shutdown-Exitcodeabweichung durch LeakSanitizer. Kein sauberer Sanitizerlauf.
  Gesondert mit Server-Reportdateien ausgeführte B5-Profil-/Noise-/Gate-Fälle
  bestanden; der ACL-Leak in Kindprozessen bleibt dabei nachgewiesen.
- Native Installer-/Audiokonfigurationsprüfungen: **42 Tests bestanden** über
  install/assets/preview/squelch-admin/boost/radio-noise/defaults.
- Source-Guards **29/29**, Setup **25/25**, Audio-Security **13/13**, GHCR-Guards
  **3/3** bestanden. GHCR wurde mit dem vorhandenen System-Python/PyYAML geprüft;
  der erste Cloud-Python-Aufruf hatte das Modul nicht installiert.
- DSP-SHA256 unverändert, `git diff --check` bestanden; C++ mit clang-format 10.
- Vollständiger Dockerfile-Build erfolgreich. Wiederholte Ubuntu-Container-Suite:
  **39/39 CTest-Targets**, 488,23 s, und **38/38 Python-Tests** bestanden.
  Fertiges lokales Image: `sha256:1f779c6386fdd0b6ed8fd95f7a56558bc1f230f47ad33beb30362ab3f75fa99b`.
  Laufzeitabnahme des fertigen Images mit `integration.py --audio-feedback
  --radio-noise --host-port 64740` vollständig bestanden: TCP/verschlüsseltes
  UDP, bestätigte und Timeout-Enden einschließlich 62-s-Noise-Übertragung,
  Busy und unveränderte Sprache, Rootless-Isolation, Neustart/Recreate,
  Portkonflikte, paralleler nativer Server, Backup/Restore, WAV-/INI-Persistenz
  und sauberer Shutdown. Die native Probe verwendete exakt die libopus.so.0
  aus dem Image; die Cloud-Standardversion 1.5.2 unterscheidet sich von Ubuntus 1.4.
  Auch die zweite Prebuilt-Compose-/Migrations-/Rollback-Prüfung bestand
  vollständig. Beide Skriptläufe endeten mit Exit 0; ihre Container wurden
  aufgeräumt. Die lokale Previous-Image-Fixture verwendet ausdrücklich dasselbe
  Image und beweist Compose-Migration/Rollback, keinen Versionsupgrade.

Während paralleler Build-/Sanitizerlast scheiterten zunächst ein B4-Überlast-
Empfangstest im Gesamtlauf (38/39) und ein 120-ms-Profilfall in einer gezielten
Wiederholung. Es wurden keine Limits erhöht oder Prüfungen entfernt. Danach
bestanden alle drei Analog-Targets gemeinsam erneut. Die gesonderten
Gate-/Sprecherwechseltests bestanden in fünf weiteren gemeinsamen Läufen.
Die erste fehlgeschlagene Empfangsprüfung hatte noch keine abschließende
Serverstatistik; ihre genaue Ursache ist damit nicht nachgewiesen. Die
Profilsuite gibt bei erneutem Empfangsfehler nun die Serverausgabe aus.

Zusätzlicher bestehender UBSan-Befund während dieser Abnahme: der unaligned
quint32-Store beim TCP-Tunnelheader (`Server::tcpTransmitData`, aktuell
Server.cpp:2205). Der identische reinterpret_cast auf `uc + 2` ist bereits im
Ausgangscommit 5b1473b6 vorhanden. Er bleibt als weiterer Altbefund offen;
B5 behauptet keinen bereinigten Sanitizer-Gesamtstand. Ein separat protokollierter
B5-Serverlauf bestätigt zudem weiterhin den 224-Byte-ACL-Leak, auch wenn der
übergeordnete Sockettest regulär endet.

Lokales Docker-Umgebungsproblem: Die erste vollständige Container-Teststufe
bestand mit 39/39 CTest-Targets und 38/38 Python-Tests. Erst eine nachfolgende
Runtime-COPY-Schicht scheiterte mit `no space left on device`. Der lokale Daemon
verwendet vfs. Nach Entfernen erzeugter ASan-Buildartefakte, Paketarchive sowie des
alten lokalen B4-Testimages und seiner unbenutzten Build-Caches waren wieder rund
2 GB frei. Testberichte und ASan-CMakeCache wurden behalten; keine Quelländerung
oder Git-Historie wurde verworfen. Die anschließende Wiederholung bestand vollständig einschließlich Image-Verpackung.

GitHub-Abnahme des Implementierungsstands `3ca2d6c8ef6472bafb4ddceeb7e783c86df5ffd1`:
alle vier abgefragten Push-Workflows erfolgreich (`Server build and integration`,
`Analog voice isolated tests`, `Installer checks`, `Server code scanning`). Der
Serverworkflow bestand ausdrücklich auch beide Container-Laufzeitphasen inklusive
Prebuilt-Compose, Migration und Rollback. Spätere Abschlussänderungen betreffen
nur Dokumentation; deren eigener CI-Status wird im Abschlussbericht getrennt genannt.
