# B6B: kritische PTT-Analyse und Korrektur der Image-Migrationsprüfung

Ausgangspunkt: `0c5daee790a688a023c0cea0214aaaf86d4fd607`, ausschließlich
`feature/analog-voice-live`, bestehender Draft-PR #15. **Der Praxisfehler ist
nicht als behoben nachgewiesen. Keine Betriebs- oder praktische Testfreigabe.**
Diese Untersuchung verwendet lokale Serverprozesse und Container. Auf Proxmox
oder dem VPS wurde nichts ausgeführt.

## Nachgewiesener CI-Fehler

Die Runs `42866288350` und `42866278628` bestanden nativen Build/Tests,
Docker-Build und den ersten Containerlauf. Der zweite Containerlauf scheiterte
bei `probe(old['serverpassword'])` nach dem Image-/Compose-Wechsel.

Die neue Ausgangskonfiguration enthält `analogvoice=true`. Das Migrationsfixture
schaltet seine optionalen Effekte aus, erhält diesen Analogschalter aber korrekt.
`information(upgrade) == old`, Zertifikat-, Volume-, Port- und SQLite-Prüfungen
bleiben bestehen. Der bisherige Probe verwendete dagegen
`SIMPLEX_OPAQUE_AUDIO_CONTENT` aus dem Direktpfad-Harness und verlangte exakt
diese unveränderten Bytes bei genau einem Empfängerpaket. Unter libopus 1.4
liefern sowohl `opus_packet_get_nb_samples` als auch `opus_decode_float` dafür
**-4 (`OPUS_INVALID_PACKET`)**. Ablehnung im Analogpfad ist korrekt.

Das ist lokal mit identischer INI reproduziert: alter Probe fehlgeschlagen,
neuer Probe erfolgreich, Analogfilter durchgehend aktiv, INI bytegleich.
Der neue Modus `--analog-forwarding <Profil>` sendet gültiges 20-ms-Opus,
prüft TCP und verschlüsseltes UDP, konkurrierende Sender, vier schnelle
Sprecher-/Transportwechsel, Medienuhren, Endmarkierungen und Encoder-Nachlauf.
Die empfangene Sprache muss dekodierbar, gefiltert und mit der bestehenden
B2-Profilreferenz vergleichbar sein. Vor konkurrierenden Versuchen sichern
TCP-/UDP-Barrieren die tatsächliche Zulassungsreihenfolge; die Sendeaufrufe
verschiedener Verbindungen allein garantieren keine Reihenfolge.

Der Modus wird anhand der **erhaltenen** INI nach Image-Wechsel und Rollback
gewählt. Bei fehlendem/false Analogschalter bleibt der alte Direktpfad-Probe
mit seinen unveränderten Byteprüfungen aktiv. Die Analogsimulation wird für die
Migration weder ausgeschaltet noch durch Raw-Opus ersetzt.

## Praxisfehler: belegter Mechanismus, offene Zuordnung

Die übermittelten Simplex-Ausschnitte belegen Zulassung und Timeout-Freigaben,
aber keine eindeutig zugeordnete Empfängerzustellung. Ein kausaler Nachweis auf
dem echten Mumla-/Mumble-System liegt noch nicht vor.

Eine komplette verlorene Testaussendung trat im parallelen ASan-/UBSan-Lauf
auf: TCP, CB-AM, 50 Aussendungen, Aussendung 44, 120-ms-Pakete. Die relevanten
Abschlusszähler waren:

```text
accepted=133 inputFull=0 expired=1 codecErrors=0 noLane=0 bypassStarts=0
openBusy=0 openInvalid=0 stale=0 invalid=0 closed=0 stopped=0 faulted=2
processed=132 published=359 taken=359 handoffPackets=359 handoffRecipients=718
senderRejected=0 noRecipients=0 transportDropped=0
```

Damit ist dieser konkrete Verlust als Überschreitung des bestehenden
40-ms-Jobalters mit anschließenden `Faulted`-Annahmen eingegrenzt. Kein Hinweis
auf Codecfehler, Lane-Erschöpfung, falsche Empfängeridentität oder TCP-Verwurf
in diesem Fall. Ob Worker-Wartezeit, Ausführung oder Dispatcher-Verzug den
Grenzwert überschritten, und ob derselbe Auslöser auf Proxmox vorliegt, ist
damit noch nicht bewiesen.

Ein deterministischer Codec-Gate-Test hält den Worker länger als 40 ms an und
belegt unabhängig davon:

1. Der unveränderte Dienst faultet die betroffene Generation.
2. Weitere zulässige Sprachpakete können von Simplex akzeptiert werden, während
   `submit` für diese Generation nur noch `Faulted` meldet und keine neue
   Sprache beim Testempfänger ankommt. Dafür ist kein Simplex-Busy nötig.
3. Ohne bestätigten Endmarker sind schnelle physische PTT-Wechsel unterhalb
   der 500-ms-Ruhegrenze für den Server keine verlässlichen neuen Grenzen.
4. Erst eine gültige Grenze/Wiederfreigabe ermöglicht eine frische Generation;
   deren Sprache kommt anschließend wieder über den normalen Analogpfad an.

Das passt zum beschriebenen Fehlerbild, ist aber **kein Nachweis seiner Ursache
auf dem Betreiberhost und keine Behebung**. Ein automatisches Wiederöffnen einer
faulted Generation anhand vermuteter PTT-Pausen würde den B3-Vertrag verändern.
Es wurde nicht eingeführt. Jobalter, Queuegrenzen, Kanalbarrieren, Simplex-Regeln,
DSP-Profile und Squelch-Fristen bleiben unverändert.

Der CI-Fehler hat eine andere belegte Ursache: ungültige Testdaten und eine
Direktpfad-Erwartung bei aktivem Analogfilter. Ein gemeinsamer Race-Condition-
Nachweis liegt nicht vor. Der frühere parallele 62-s-Befund aus den
[CB-Vorlagentests](audio-defaults.md) wird ebenfalls nicht rückwirkend als geklärt
oder behoben bezeichnet.

## Datenfluss und neue Diagnose

`processMsg` prüft Simplex/Berechtigung und wählt die ursprüngliche Audienz.
Unter dem vorhandenen Transmission-Guard öffnet `submitAnalogVoice` nur bei
`started` eine Lane und kopiert vollständige Verbindungsidentitäten, Metadaten
und Tickets. Fehlerannahmen umgehen den Dienst nicht. Timeout reserviert ein
technisches `finish`; echte Invalidierung cancelt. Bestätigte Marker schließen
über das angenommene Originalticket. Der Worker behält vier Codecplätze und
bounded Queues; geordnete terminale Batches halten die Kanalbarriere aufrecht.

`deliverAnalogVoice` nimmt unter Voice-Write-Schutz geordnete Batches, prüft
Senderidentität/-rechte und die ursprünglichen Empfänger erneut und übergibt
erst dann an `sendVoicePacket`. Der begrenzte TCP-Backlog und verschlüsseltes
UDP bleiben unverändert. Die B5-Lifecycle-Beobachtung verwendet weiterhin
Originaltickets/Source-Spans einmalig; Open und Close werden nicht aus der
bloßen Annahme abgeleitet. Sprache und private Effekte bleiben getrennt.

Bei vorhandenem `simplexdiagnostics=true` schreibt der **bestehende**
`SimplexLogger` zusätzlich:

| Ereignis | Bedeutung |
| --- | --- |
| `analog_submit` | `accepted`, `faulted`, `input_full`, `stale`, `closed`, `invalid`, `stopped`, fehlende Generation, abgewiesenes Open oder ungültiger Codec |
| `analog_finish` | Status des reservierten technischen Timeout-Abschlusses |
| `analog_result` | Vom Dienst zurückgegebenes Ergebnis, einschließlich leerer Kontrollbatches und konkreter Fehlerklasse |
| `analog_discard` | Sender fehlt, Identität veraltet oder aktuelle Sendeberechtigung nicht mehr gültig |
| `analog_handoff` | Tatsächliche lokale Übergabezahl oder Empfänger-/Transport-Verwerfungsgrund |

Die Felder `connection`, `session`, `channel`, `generation`, `routing_epoch`,
`ticket`, `frame`, `samples`, `output_packets`, `recipients` und `local_handoffs`
ermöglichen die Zuordnung. `output_frame` am Handoff bezeichnet die tatsächliche
Ausgangsmedienuhr. `ticket`/`frame` beim Ergebnis und Handoff stammen aus
`Batch.completed`; sie sind **keine** Behauptung einer 1:1-Zuordnung zu allen
beitragenden Originalen. Die Squelch-Zuordnung über Source-Spans wird nicht geändert.
`transport=server` markiert nachgelagerte Stufen, ohne einen gemischten Versand
fälschlich insgesamt TCP oder UDP zu nennen.

Neue Dienst-/Abschlusszähler unterscheiden Open-Konflikte, ungültige Identitäten,
Admission-Fehler, verarbeitete Workerjobs, veröffentlichte und genommene
Ausgangspakete, lokale Übergaben und Sender-/Empfänger-/Transport-Verwerfungen.
Der Worker erledigt keine Logdatei- oder Netzwerkarbeit. Die vorhandene
4096-Einträge-Try-Lock-Queue und Dateirotation bleiben bestehen. Ohne Logger
werden keine Diagnoseereignisse gebaut. Es werden keine Opus-/PCM-Inhalte,
Passwörter oder Zugangsdaten protokolliert.

Der Logger darf Einträge verlieren; `diagnostic_loss`/`dropped_total` bleiben
maßgeblich. Exakte Annahmesummen werden deshalb aus Dienstzählern geprüft,
nicht aus der Zahl der Logzeilen. Eine lokale Transportübergabe beweist keine
Wiedergabe durch den echten Client; die Sockettests prüfen zusätzlich Empfang
und Opus-Dekodierung ihrer Testclients.

## Regressionen und Prüfumfang

`TestAnalogVoiceRapid` ergänzt 18 Datenfälle mit insgesamt **420** simulierten
Aussendungen pro vollständigem Durchlauf, dazu den deterministischen Fault-Test:

- 10 und 50 aufeinanderfolgende Aussendungen; Pausen 50/100/200/300/500 ms.
- Analog an/aus, CB-AM, TCP, verschlüsseltes UDP und wechselnder Ingress.
- Ein Sender/ein Empfänger, mehrere Empfänger mit Legacy-/modernem Protokoll,
  sowie schnelle Wechsel zwischen zwei Sendern.
- 10/20/40/60/120-ms-Opus, Marker- und unmarkierte/Timeout-Verläufe.
- Aktives Squelch/Busy/Noise mit den CB-Pegeln; pro Aussendung muss der passende
  Testempfänger tatsächliche, dekodierbare, nicht stumme Sprache erhalten.
- Exakte Dienstannahmen und Fehlerzähler, monotone Ausgangsuhren, Diagnosefelder;
  Fault-Generation ohne Raw-Bypass und Wiederaufnahme nach gültiger Grenze.

Unmarkierte 30-ms-Bursts dürfen nach B2 noch ausschließlich in Halbframe,
Lookahead und Reserve liegen. Die fortlaufenden Tests verwenden deshalb dort
50 ms gültiges Quellaudio; sie verlangen keine erfundene Ausgabe vor einem Ende.
Bestehende Kurz-/Einpaket-/Halbframe-/Nachlaufprüfungen bleiben erhalten.
`TestAnalogVoiceEffects::blocked(0)` prüft weiterhin explizit einen neuen
Sender bei noch blockiertem Vorgänger und die geordnete Sprachübergabe vor
optionalem Close. Andere Kanäle, Berechtigungsentzug, Disconnect, Fehler und
Shutdown bleiben durch die bestehenden B1–B5-Tests abgedeckt.

Ergebnisse des ersten Abnahmestands (Parallelbetrieb in der Cloud):

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Vollständiger nativer Release-Build | erfolgreich, Non-Unity |
| Native CTest-Suite | 37/40 im ersten Lauf; neue Diagnose-Vollständigkeitsannahme korrigiert; NoisePacing und AudioFeedbackIntegration bestanden anschließend einzeln |
| Neue Rapid-Matrix | Docker: vollständig bestanden; Host: einzelne `expired`-Fälle bei parallelem Release-/Sanitizerbetrieb; gezielte 50-TCP-Wiederholung bestanden |
| TSan VoiceService / VoiceServiceSimplex | 2/2 bestanden |
| Striktes ASan/LeakSanitizer/UBSan | 4/5 ausgewählte Ziele bestanden; Rapid meldete Deadline-/Empfangsfehler, keinen Sanitizer-Speicherbefund |
| Installer-/Audio-Prüfungen | 41 Tests außerhalb AudioDefaults bestanden; AudioDefaults 2/3, fehlender vollständiger Close im alten 62-s-Direktpfad-Noise-Szenario unter Parallelbetrieb |
| Source-/Setup-/Security-/GHCR-Guards | 31 + 28 + 13 + 3 bestanden |
| Vollständiger Dockerfile-Build | Kompilation erfolgreich, 38/40 CTest; NoiseScheduler und NoiseTiming fehlgeschlagen; deshalb kein erfolgreich erzeugtes B6B-Abnahmeimage aus diesem Lauf |
| Imagewechsel-Ursachenvergleich | Alter Probe scheitert, korrigierter Probe empfängt/dekodiert bei identischer aktiver Analog-INI |

NoiseTiming meldete im Dockerlauf eine 40-ms-Lücke bei unveränderter 21-ms-
Prüfgrenze. NoiseScheduler verlor in seiner absichtlich verzögerten Producer-
Variante den erwarteten Close-Beginn. Native Zwischenläufe verloren außerdem
optionale Effektteile bzw. erreichten die Noise-Pacing-Untergrenze nicht. Diese
Beobachtungen werden nicht als behoben bezeichnet und nicht allein aus ihrer
zeitlichen Nähe kausal dem Parallelbetrieb zugeschrieben. Kein Test wurde
deaktiviert und keine Frist zur Stabilisierung erhöht.

Weitere isolierte Wiederholungen und der aktuelle GitHub-Status werden im
[bestehenden Draft-PR #15](https://github.com/Shawna7han/mumble-funk/pull/15)
mit den tatsächlichen Ergebnissen ergänzt. Weder ein unvollständiger lokaler
Dockerlauf noch fehlende reale Hör-/VPS-Nachweise gelten dadurch als bestanden.

## Nächster praktischer Test erst nach Betreiberfreigabe

Nach Freigabe auf der eigenen Testinstanz dieselben PTT-Folgen zuerst mit
Analogfilter aus und dann an wiederholen. Mumla-/Mumble-Version, Paketdauer,
Transport und Zeitpunkt jeder fehlenden Aussendung notieren. Insbesondere
50/100/200/300/500-ms-Pausen, Marker versus Timeout und schnelle Sprecherwechsel
prüfen. Auch erster/letzter Sprachanteil und Open/Noise/Close bleiben Hörtests.

Bei einem Verlust die zugehörigen `granted`-, `analog_submit`-, `analog_result`-
und `analog_handoff`-Ereignisse aus demselben `run`/derselben Generation sowie
`diagnostic_loss` sichern. Nach geordnetem Testende die Abschlusszähler aus dem
Serverlog notieren. Keine INI mit Passwort und keine Sprachaufnahmen veröffentlichen.
Die [Testinstallationsanleitung](install-analog-voice-test.md) beschreibt den
datenbewahrenden Updateweg mit `FUNK_BUILD_JOBS=1` für wenig Arbeitsspeicher.
