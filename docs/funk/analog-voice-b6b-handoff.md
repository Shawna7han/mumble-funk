# B6B-Übergabe: reale Hör- und Leistungsabnahme noch offen

**Aktueller Nachtrag:** B6B wurde inzwischen zur kritischen Fehleranalyse
beauftragt. [Untersuchung und tatsächliche Ergebnisse](analog-voice-b6b.md).
Der folgende Text ist die historische B6A-Übergabe; die Praxisfreigabe bleibt offen.

B6A stabilisiert die vorhandene Architektur und bereitet den expliziten
`--experimental-analog-voice`-Installer für `feature/analog-voice-live` vor.
[Technische Abnahme](analog-voice-b6a.md) und
[Testinstallationsanleitung](install-analog-voice-test.md) zuerst lesen.
Kein externes System wurde installiert; der Betreiber übernimmt die spätere
Proxmox-Testinstallation selbst. Keine Produktivfreigabe und kein Merge.

B6B erst nach eigenem Auftrag:

- Echte Windows-Mumble- und Android-Mumla-Versionen, Paketdauern, Mikrofonpegel
  und AGC-Einstellungen dokumentieren. Sprachverständlichkeit und alle Profile
  cb-am/business-fm/narrowband mit/ohne Rauschen beurteilen.
- Sehr kurze und lange Sendungen, Open/Noise/Close, Busy, schnelle PTT-Wechsel,
  Sprecherwechsel, bestätigte und vermutete Timeout-Enden prüfen. Physisches
  PTT-Ende nicht aus einem technischen Terminator ableiten.
- Ende-zu-Ende-Latenz, erste/letzte Silben, Pegel und Codec-Transienten messen.
  Getrennte Sprach-/Effektstreams und Client-Jitterbuffer können hörbare
  Reihenfolgen ändern; lokale Sendaufrufe sind kein Wiedergabenachweis.
- CPU/RAM, Queue-/Codeczeiten und mehrere parallele Funkkanäle auf dem späteren
  2-GB-VPS untersuchen; 120-ms-Pakete, Lastspitzen, lange Laufzeit, Paketverlust
  und langsame TCP-Empfänger einschließen. Keine Lastzusage aus Cloudtests ableiten.
- Aktuelle Sanitizer- und Empfangsfehlerbefunde aus B6A übernehmen. Nicht
  reproduzierte Fehler gelten nicht als behoben. Vollständige Sessionreuse-/
  TLS-Sättigungs-/Socket-Fehlermatrix bleibt ergänzender Nachweis.
- Die dokumentierte moderne Decodergrenze bei leerem Raw-Opus und mögliche
  Paketverluste bei B3-Überlast bewerten. Keine Queue-/Fristvergrößerung oder
  stillen Midstream-Raw-Bypass zur Verschleierung verwenden.

Eine spätere Betriebsfreigabe verlangt dokumentierte Hör-/Lastbefunde, geklärte
Restfehler und ausdrückliche Zustimmung des Betreibers. B6B wurde hier nicht begonnen.
