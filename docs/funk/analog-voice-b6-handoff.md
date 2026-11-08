# Übergabe nach B5: B6 noch nicht begonnen

Ausschließlich `feature/analog-voice-live`, bestehender Draft-PR #15. Keine
Betriebsfreigabe, kein Merge nach dev/stable und kein VPS-Deployment. B6 erst nach
einem neuen Auftrag. [B5](analog-voice-b5.md) beschreibt Implementierung,
Tests und Grenzen; B1–B4 bleiben verbindliche Grundlage.

B5 verbindet Originaltickets mit dem vorhandenen SquelchPlan im Dispatcher.
Sprachübergabe, Source-Beobachtung und terminaler Abschluss bleiben geordnet;
Noise nutzt unverändert den gemeinsamen privaten Open/Noise/Close-Effektstream.
Der Release-Callback reserviert lediglich technische Endaufträge. Keine
Codec-/DSP-Arbeit unter Serverlocks und keine Workerzugriffe auf Serverobjekte.
Die ursprüngliche Endzeit und 300-ms-Close-Frist müssen erhalten bleiben.

Für B6 ausdrücklich noch durchzuführen:

- Reale Windows-Mumble- und Android-Mumla-Hörtests mit dokumentierten Versionen,
  Paketdauern, Mikrofonpegeln/AGC; alle Profile jeweils mit/ohne Funkrauschen.
- Sehr kurze, lange (>60 s) und rasch aufeinanderfolgende PTT-Sendungen;
  bestätigte Enden, vermutete Timeout-Enden, schnelle Sprecherwechsel, Busy und
  wechselnde Transporte. Hörbare Reihenfolge und Aussetzer separat bewerten.
- Ende-zu-Ende-Latenz, erste/letzte Silben, Lautheit, Headroom, Codec-Transienten
  und Pegel gegenüber den freigegebenen Offlineproben bewerten. Kein versprochener
  RMS-Abgleich oder pauschaler Client-Lookahead-Abzug.
- Auf dem späteren ungefähr 2-GB-VPS CPU/RSS und Queue-/Codeczeiten unter mehreren
  parallelen Funkkanälen plus Noise messen; p50/p95/p99, 120-ms-Pakete,
  Start-/Endbursts, Langzeitbetrieb, TCP-Stau und Paketverlust einbeziehen.
  Ein Worker, vier Codecplätze und höchstens vier Dienste pro Prozess sind
  Ressourcenlimits, keine nachgewiesene VPS-Leistungszusage.
- Reale langsame TLS-Empfänger, Sessionnummer-Wiederverwendung und vollständige
  Codec-/Ausgangsqueue-Fehler über Sockets ergänzen. Der aktuelle Adaptertest
  beweist keine Client-Wiedergabe oder komplette Netzwerkfehlermatrix.
- Offene B4-Sanitizerbefunde gezielt bearbeiten: ACL-Cache-Leak,
  CryptographicHash-Lecks, OCB2-Ausrichtungs-UB. Keine globale Unterdrückung.
- Moderne Decodergrenze bei leerem Raw-Opus und B3-Überlastpolitik bewerten:
  Faulted kann Sprache verlieren; kein sicherer Midstream-Raw-Fallback.
  Optionale alte Close-Cues dürfen ausfallen, Sprache bleibt priorisiert.

Vor einer späteren Betriebsfreigabe müssen Testnachweise, Hörbefunde, Ressourcen-
und Latenzbudget sowie Fehlerpolitik gemeinsam bewertet und verbleibende Blocker
behoben sein. Danach ist die ausdrückliche Freigabe des Betreibers erforderlich.
Installer/Docker dürfen den Filter weiterhin nicht automatisch aktivieren.

Weiterer in der B5-Abnahme sichtbar gewordener Altbefund: UBSan meldet den
unaligned quint32-Store in `Server::tcpTransmitData` beim TCP-Tunnelheader (`uc+2`).
Der Cast existiert bereits im geprüften Ausgangscommit. Vor Freigabe gezielt
beheben und gemeinsam mit den übrigen Sanitizerbefunden erneut prüfen.
