# Übergabe nach B4: B5 noch nicht begonnen

Stand: 8. November 2026. Ausschließlich `feature/analog-voice-live`, bestehender
[Draft-PR #15](https://github.com/Shawna7han/mumble-funk/pull/15). Nicht mergen,
nicht produktiv aktivieren, nicht auf dem VPS installieren. B5 erst nach neuem
Auftrag. B6 bleibt die separate Hardware-/Hör-/VPS-Abnahme.

Zuerst [B4](analog-voice-b4.md), [B3](analog-voice-b3.md),
[B2](analog-voice-b2.md) und [Locking](../dev/MurmurLocking.md) lesen.
Die DSP-Dateien und ihre Checksummen sind unverändert.

## Vorhandene Schnittstellen

- `ServerAnalogVoice.cpp`: pro Server ein begrenzter Dienst; pro Prozess höchstens
  vier aktivierte Dienste. `initializeAnalogVoice`/`shutdownAnalogVoice` besitzen
  den Worker. Fehlende/false Konfiguration belässt den Direktpfad.
- `syncAnalogVoice`: Verbindungsidentitäten bei Kanaleintritt, Rechtewechsel und
  Empfangswechsel synchronisieren. Disconnect invalidiert ausdrücklich.
- `submitAnalogVoice`: Ticketannahme nach vorhandenen Prüfungen und Empfängerauswahl,
  noch unter Transmission-Guard; Start nur über `Transmission::started`.
- `releaseAnalogVoice`: unter Simplex, ohne Userpointer/Voice-Lock; Timeout reserviert
  technisches `finish`; echte Invalidierung cancelt. Marker schließt beim Input.
- `deliverAnalogVoice`: QObject-gebundene koaleszierte Aktivierung. Voice-Write-
  Trylock vor Acknowledge/Take; 1-ms-Retry bei Konflikt. Auch leere Kontrollbatches
  konsumieren. Vor Netzwerkübergabe Identität und aktuelle Rechte prüfen.
- `sendVoicePacket`: gemeinsamer Verpackungshelfer für direkt und gefiltert.
  Gefiltertes TCP verwendet unmittelbaren Hauptthread-Tunnelversand mit
  65536-Byte-Backloggrenze; UDP verwendet vorhandene Verschlüsselung.
- B3 `Input` und `Ticket` tragen additive kopierte `Metadata` für Position und deren
  ursprüngliche Empfangsaudienz. `sources[i][0]` ist der erste beitragende Anker;
  `completed` ist kein Ersatz für die Quellen eines Ausgabepakets.

## B5-Auftrag vorbereiten, nicht vorziehen

Aktiver Analogfilter unterdrückt derzeit **Squelch und kontinuierliches Rauschen**.
Busy bleibt unverändert. Deshalb existiert dort kein SquelchPlan und keine
Squelch-Beobachtung der Original- oder Ausgangspakete. `analogvoice=false` bzw.
fehlender Schalter verwenden weiterhin die bestehende Effektintegration.

B5 muss die Unterdrückung erst nach folgenden Nachweisen aufheben:

1. Quellereignisse genau einmal je angenommenem Originalticket beobachten.
   Halbframes/Lookahead können dasselbe Ticket in mehreren Spans referenzieren;
   PLC/Padding/EncoderDelay und technische/Fault-Enden sind keine neuen Originale.
2. Open an tatsächlich beginnende Zustellung binden; Close erst nach letzter
   Sprachreserve/Nachlauf und vor Kanalnachfolger ordnen. Nicht Simplex-Besitz als
   Gültigkeitsprüfung alter angenommener Endpakete verwenden.
3. Timeout, bestätigten Marker und Fehlerabschluss unterscheiden. Die vorhandene
   300-ms-Close-Frist, kontinuierliches Rauschen und private Effektstreams mit
   vorhandenen Mechanismen erhalten. Keine neue Effektarchitektur entwickeln.
4. Empfängerschnittmenge, Identitäts-/Routinggenerationen, Empfangsänderungen und
   TLS-Verwurf berücksichtigen; ein lokaler Send-Aufruf bestätigt keine Wiedergabe.
5. Release-Callback nie um Voice-Lock oder Codec-Arbeit erweitern. Dispatcher hält
   keinen Dienstmutex beim Versand. Leere terminale Batches tragen Lifecycle.

## Tests und verbleibende Abnahme

Die abschließenden B4-Testergebnisse und Sanitizer-Grenzen sind in B4 dokumentiert.
`TestAnalogVoiceIntegration` führt echte Serverprozesse/Opus/TCP/verschlüsseltes
UDP aus. Linux-Testbuilds besitzen einen Linker-Codec-Gate für Überlast und
Shutdown bei aktivem Worker. Der Hook ist bei `tests=OFF` nicht enthalten.
Fünf-Sender-Einrichtung benötigt ein explizites Testbudget für Steuernachrichten
und localhost-Loginrate; keine Produktgrenzen oder pauschalen Sleeps erhöhen.

Noch nicht vollständig durch echte Sockets nachgewiesen: forcierte Sessionnummer-
Wiederverwendung, saturierter langsamer Empfänger, deterministisch erzwungener
Qt-Voice-Lockkonflikt, alle Codec-/Ausgangsqueue-Fehler und prozessweites
Vier-Server-Budget. B3 besitzt dafür teilweise deterministische isolierte Tests.
Sanitizer sind nur für Projektcode instrumentiert, nicht externe Qt/libopus.
VPS-Leistungsfähigkeit, reale Client-Jitterbuffer, subjektiver Klang und
Gesamtlatenz bleiben B6. Die experimentelle Integration ist keine Betriebsfreigabe.

Der vorhandene moderne UDPDecoder akzeptiert keine leeren Opus-Pakete. Gefilterte
B2-Nachlaufabschlüsse enthalten Payload; leere Raw-Bypass-Enden behalten die
bestehende Decodergrenze. Vor Freigabe gesondert bewerten, keine künstlichen
Payloads oder geänderten Quellenframes zur Umgehung erzeugen.

B4-Sanitizerrestpunkte vor Freigabe bearbeiten: 224-Byte-ACL-Cache-Leak beim
Serverabbau, bestehende CryptographicHash-Lecks und OCB2-Ausrichtungs-UBSan-Befund.
Der Shutdown-Test bleibt unter LeakSanitizer rot; keine Tests oder Checks wurden
zur Freigabe abgeschaltet. Ein separater Fix behebt den bestehenden temporären
QStringBuilder-Pfad im AudioFeedback-Test. Release-Serverintegration und
isolierte Release-/TSan-Tests bestehen; genaue Zählweise steht im B4-Bericht.

B4-CI-Nachkorrektur: Der Unix-SIGTERM-Notifier wird in main.cpp nicht länger mit
asynchronem cleanup überschrieben. Shutdown erfolgt dadurch im Qt-Hauptthread;
Unix-SIGTERM endet regulär mit Exit 0. CodecGate liefert seine Linux-Thread-ID für
einen deterministischen Worker-SIGTERM-Regressionstest. Der Überlasttest verlangt
weiterhin exakt inputFull=1, zeigt bei Fehlern Serverstatistik und Exitstatus und
prüft einen anderen, noch vor Workerfreigabe angenommenen Nachfolgesender.
Dies ändert keine Überlastgrenze und behebt nicht die bekannten Sanitizer-Befunde.
