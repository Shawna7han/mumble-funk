> **Historischer Stand `feature/audio-feedback` (alte Preview auf 64740).**
> Diese Anleitung bzw. diese Ergebnisse gelten nicht für `feature/audio-engine`.
> Für die neue, getrennte Phase-1-Preview auf 64741 siehe [Audio-Engine](audio-engine.md)
> und [Prüfbericht](AUDIO-ENGINE-STATUS.md). Alte Installationen nicht damit aktualisieren.

# Prüfstand: experimentelle Audio-Feedback-Engine v0.3

Entwicklungszweig: **`feature/audio-feedback`**. Grundlage: Mumble Server 1.5.939
mit der bisherigen Simplex-Engine v0.2. Dies ist kein stabiles Release.

[Bedienung und Installation](installation-audio-preview.md) ·
[Technische Architektur, WAV-Format und Grenzen](audio-feedback.md)

## Implementierter Umfang

- Optionaler, rein serverseitiger Busy Tone. Keine Änderungen an Clients oder am
  Mumble-Protokoll; keine zusätzliche Client-/Bot-Verbindung.
- Direkte Zustellung nur an die abgewiesene Verbindung, als echte, dekodierbare
  Opus-Audiopakete im bestehenden TCP-Tunnel. Eingehende Sprache kann TCP oder
  verschlüsseltes UDP verwenden; altes und neues Audio-Framing werden berücksichtigt.
- Kurzer synthetischer Doppelton mit Lautstärkeneinstellung. Standardmäßig bleibt
  die Serverfunktion aus; das separate neue Preview-Setup aktiviert sie.
- Eigene PCM-WAV-Datei ohne Neukompilierung: Format-/Größen-/Längenprüfung,
  Umrechnung auf 48 kHz, Pegelbegrenzung, Opus-Vorbereitung beim Start.
- Verwaltungsbefehl `mumble-funk-audio` für Import, Zurücksetzen, Lautstärke und
  Ein-/Ausschalten. Er berührt nur die Preview-Konfiguration und den Preview-Dienst.
- Unabhängig von beiden Diagnosesystemen. Keine Auswertung von JSONL als Auslöser.
- Wiederholungsschutz je erkanntem Versuch plus Mindestabstand und begrenzte
  Warteschlange. Kein unbegrenztes Aufstauen, keine Threads je Benutzer/Ton.
- Kanalwechsel, Verbindungsabbruch und Shutdown werden berücksichtigt; eine neue
  Verbindung mit wiederverwendeter Sitzungsnummer erhält keinen alten Tonauftrag.
- Roger Beep und Squelch sind **nicht implementiert**; die gemeinsame Signaldatei-,
  Erzeugungs-, Ereignis- und Zustellungsschicht ist dafür erweiterbar.

## Simplex und notwendige interne Anpassungen

Die Weiterleitungsentscheidung enthält zusätzlich eine Feedback-Markierung. Ein
separates Merkmal hält fest, ob ein erkannter Versuch schon gemeldet wurde. Damit
werden auch tatsächlich abgewiesene Pakete erkannt, für die die begrenzte Diagnose
kein neues Ereignis ausgibt. Ende-Markierungen ohne Nutzlast lösen keinen Ton aus.

Unverändert bleiben: exklusiver Kanalbesitz, keine automatische Übernahme eines
blockierten Senders, bestehende Timeout-/Wiederholungsgrenzen, gültige und veraltete
Ende-Markierungen, Berechtigungen, Kanalzuordnung und Verbindungsbereinigung.
Die Tonfunktion selbst fordert oder entzieht kein Senderecht.

Die Tests haben außerdem einen seltenen vorhandenen UDP-Fehler eingegrenzt: Das
Prüfen eines verschlüsselten Pakets auf öffentliche Pings kann dessen Nonce-Byte
`0x01` für eine neue Protokollkennung halten. Deshalb wird vor der anschließenden
Dekodierung nun wieder die authentifizierte Clientversion gesetzt. Der eigens
ergänzte Test erzwingt dieses Byte und scheitert nachweislich vor der Korrektur.
Die Protokollversion wird nicht geändert, sondern korrekt beibehalten.

Zusätzlich ist der vorhandene UDP-Verschlüsselungs-Arbeitspuffer jetzt pro Thread
statt global statisch. Gleichzeitige Weiterleitung aus TCP und UDP kann sonst auf
denselben veränderlichen Speicher zugreifen. Dies ändert weder Audioinhalte noch
Protokoll oder Senderechtsregeln. Frühere Integrationsfehler im normalen
Weiterleitungsmodus waren auch mit dem unveränderten Referenz-Build reproduzierbar;
die abschließende vollständige Prüfung war nach der gezielten Korrektur erfolgreich.
Die UDP-Testhilfe wartet nun auf den passenden authentifizierten Ping statt auf
das erste beliebige Datagramm; die eigentlichen Sprach-/Routing-Assertions bleiben
unverändert streng.

## Automatisierte Prüfung

### Entwicklungsumgebung

| Prüfung | Ergebnis |
| --- | --- |
| Server einschließlich Ice, Qt5, OpenSSL und Opus kompilieren | Erfolgreich, separater Debug-Build |
| Alle vorhandenen und neuen CTest-Serverprogramme | **20/20 erfolgreich** |
| Davon bestehende Server-Testprogramme | Alle bisherigen **18** enthalten |
| Neue Audio-Kerntests | Wellenform, Frequenz, Dauer, Lautstärke, Wiederholungsschutz und begrenzte Arbeit |
| Neue reale Server-/Client-Pakettests | TCP/UDP, beide Framing-Versionen, Opus-Dekodierung, nur gewünschter Empfänger |
| Weitere Audio-Pakettests | Eigene WAV-Datei und ungültige-Datei-Fallback, Kanalwechsel, Verbindungswechsel, getrennte Kanäle, alle Ausschaltkombinationen |
| Bestehende Python-Setup-Tests | **16/16 erfolgreich** |
| Bestehende Python-Installationstests | **5/5 erfolgreich** |
| Neue Preview-Installationstests | **5/5 erfolgreich** |
| Neue WAV-/Verwaltungstests | **8/8 erfolgreich**, einschließlich mehrerer Unterfälle |

Die Preview-Installationstests starten auch drei echte Serverprozesse gleichzeitig
auf 64738, 64739 und 64740 mit getrennten temporären Datenbanken und Zertifikaten.
Dabei laufen zu Testzwecken alle drei Prozesse mit dem geprüften Feature-Binary,
aber nur 64740 hat Feedback aktiviert. Das ist kein Test einer vorhandenen VM.

Die Verwaltungstests prüfen den tatsächlichen C++-WAV-Prüfer und -Encoder über den
Serverbefehl `--validate-feedback-wav`, alle vier unterstützten Abtastraten sowie
Längengrenzen, kaputte RIFF-Strukturen, falsche Kanal-/Bitformate, zu große Dateien,
symbolische Links und spezielle Dateien. Ein fehlerhafter Import erhält die aktive
Konfiguration. Import und Zurücksetzen ändern weder Binärdatei noch Build-Zeitstempel.

Die Pakettests prüfen tatsächliche Opus-Nutzdaten, die sich von den blockierten
Mikrofon-Testdaten unterscheiden. Sender und dritte Teilnehmer erhalten keinen
fremden Busy Tone; der Kanalbesitzer bleibt hörbar. Kontinuierlich abgewiesene Daten
übernehmen den Kanal nach dessen Freigabe nicht automatisch. Ein WAV mit anderem
Toninhalt wird tatsächlich vom Server geladen und übertragen.

### Ubuntu-Prüfung

| Prüfung mit echten Ubuntu-Paketen/Servern | Ubuntu 24.04 | Ubuntu 26.04 |
| --- | --- | --- |
| Separater Release-Build, einschließlich Opus | Erfolgreich | Erfolgreich |
| Alle CTest-Programme beim Setup | **20/20** | **20/20** |
| Alle CTest-Programme bei erneutem Setup | **20/20** | **20/20** |
| Belegter Preview-Port wird vor der Installation abgewiesen | Erfolgreich | Erfolgreich |
| Private Dienstdatei mit `systemd-analyze verify` | Erfolgreich | Erfolgreich |
| Tatsächlicher Serverstart auf TCP/UDP 64740 | Erfolgreich | Erfolgreich |
| Eigene WAV-Datei per Verwaltung laden; frische Startmeldung bestätigt die Quelle | Erfolgreich | Erfolgreich |
| Standardton, Lautstärke, Feedback aus/ein | Erfolgreich | Erfolgreich |
| Wiederholtes Setup erhält bestehende Preview-Konfiguration | Erfolgreich | Erfolgreich |
| Dienst entfernen, Preview-Daten behalten | Erfolgreich | Erfolgreich |
| Prüfsummen aller vorhandenen v0.2-Instanzdateien vor/nach dem Ablauf gleich | Erfolgreich | Erfolgreich |

Die Ubuntu-Prüfungen verwenden echte Pakete, Compiler, Binärdateien und
Serverprozesse in wegwerfbaren Containern. **`systemctl` wird dort ausdrücklich durch
eine Testhilfe ersetzt**, die ausschließlich den Preview-Server startet/stoppt;
echte systemd-Prozessüberwachung und automatischer Start nach einem VM-Neustart
sind damit nicht bewiesen. Die Dienstsyntax wird dagegen mit dem echten
`systemd-analyze` geprüft.

Der gemeinsame Cloud-Datenträger lag unter der vorsichtigen Setup-Grenze von
8 GiB freiem Platz. Nur diese freie-Speicher-Auskunft wurde für den Containerlauf
simuliert; Paketinstallation, Kompilierung, CTest, Diensteinstanz, Daten- und
Portprüfungen sind real. Das veröffentlichte Setup behält seine ursprüngliche
Ressourcenprüfung. Auf der Ubuntu-VM werden weiterhin tatsächlich mindestens
8 GiB freier Speicher verlangt.

Für den früheren UDP-Fehler wurde zunächst ein fehlgeschlagener gezielter Test vor
der Korrektur erfasst. Abschließend bestanden sowohl dieser Test als auch der
explizite Test, der eine zuvor getrennte Sitzungsnummer tatsächlich wiederverwendet.
Bloßes erneutes Verbinden mit einer anderen Nummer genügt diesem Test nicht.

## Was damit noch nicht bewiesen ist

- **Noch kein hörbarer Busy-Tone-Test auf echten Android-/Windows-Geräten.** Die
  Benutzer-Praxistests mit Mumla und Windows beziehen sich bisher auf v0.2.
- Keine Zusage, dass jede installierte Client-Version oder Audiohardware während
  gehaltener PTT ausgibt. Der geprüfte Clientquellcode macht dies technisch plausibel;
  die Pakettests belegen die Serverseite. Der Hörtest bleibt erforderlich.
- Keine exakte Erkennung der physischen Taste. Ein Ton bezieht sich auf den vom
  Server erkannten abgewiesenen Versuch. Der Ein-Sekunden-Wiederholungsschutz und
  Überlastschutz können schnelle weitere Töne unterdrücken.
- Keine garantierte geringe Latenz bei TCP-Stau. Bei begrenzten Ressourcen wird
  Feedback ausgelassen; normale Senderechtsregeln bleiben bestehen.
- Noch kein abgeschlossener v0.3-Langzeit-, Großgruppen- oder realer
  Netzwerkstörungstest. Keine Produktionsreife behauptet.
- Noch keine eigenen Geräteprüfungen mit deiner REAPER-/Dexed-Datei: Die Datei
  wurde hier nicht bereitgestellt. Kompatible eigene WAV-Dateien werden allgemein
  unterstützt; sie sind keine fest einkompilierte Ressource.

## Herkunft und Lizenzprüfung

Es wurden keine fremden Audioaufnahmen oder Samples verwendet oder heruntergeladen
und kein charakteristischer Herstellerton kopiert. Der Standardton und alle
Testsignale sind im Projekt erzeugte einfache Sinuswellen. Die mathematische
Erzeugung ist im Quellcode dokumentiert; eigene WAV-Dateien bleiben austauschbar.

Kein neuer fremder Implementierungsquellcode wurde kopiert oder eingebettet.
**Opus ist eine zusätzliche Server-Linkabhängigkeit**: Hauptlizenz BSD-3-Clause,
Komponenten BSD-2-Clause/BSD-3-Clause, anhand offizieller und Paket-Hinweise geprüft.
Die Hinweise sind im Repository und in der Preview-Installation enthalten. Mumbles
bestehende Lizenz und Copyright-Hinweise bleiben erhalten.
[Geprüfte Herkunft und Lizenznachweise](AUDIO-RESOURCE-LICENSES.md).

## Dateien und Git-Sicherung

Die Änderungen betreffen `src/murmur/AudioFeedback.*` und `AudioSignal.*`, deren
Einbindung und Konfiguration in Server/Meta/CMake, ein kleines zusätzliches
Simplex-Entscheidungsmerkmal, neue Tests, eine ergänzte Beispielkonfiguration sowie
die gesonderten Preview-Installations-/Verwaltungsdateien und diese Dokumentation.

Die vollständige Dateiliste lässt sich im Repository anzeigen:

```bash
git diff --name-only 95ac344838c1535f943dccc7d134a280aac6a340..feature/audio-feedback
git log --oneline funk-v0.1..feature/audio-feedback
```

Der erste Implementierungs-Commit ist `50e49900e` (Audio-Engine und Tests).
Die gezielte UDP-Korrektur und die Session-Reuse-Tests stehen in `0ffe71908`.
Getrennte Installation, WAV-Verwaltung und Opus-Lizenzhinweise stehen in `2ebb7ed59`.
Die abschließende Dokumentation wird separat als Feature-Commit gesichert. Es werden keine Release-Tags oder automatischen Merges
erstellt. Bestehende Lizenz- und Copyright-Dateien bleiben unverändert.

Die v0.2-Referenz ist `95ac344838c1535f943dccc7d134a280aac6a340`. Die gewöhnliche
Schnellinstallation verweist weiterhin ausdrücklich auf `funk-v0.1`. Der
GitHub-Standardbranch wird nicht umgestellt.
