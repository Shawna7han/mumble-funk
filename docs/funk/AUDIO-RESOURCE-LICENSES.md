# Herkunft und Lizenzen der Audio-Ergänzung v0.3

Geprüfter Umfang: die aus `feature/audio-feedback` nach `feature/audio-engine` übernommenen
Audioressourcen, Audio-Erzeugung und die zusätzliche Server-Abhängigkeit. Diese
Prüfung ergänzt die unverändert erhaltenen Lizenzhinweise des Mumble-Projekts.

## Ergebnis

**Es werden keine fremden Audioaufnahmen, Samples oder Soundeffekte eingebunden.**
Es wurden keine Audiodateien aus dem Internet heruntergeladen. Das Repository
enthält für diese Funktion keine neu hinzugefügten WAV-, MP3-, OGG- oder ähnlichen
Aufnahmen. Eigene WAV-Dateien werden erst lokal vom Administrator importiert.

Der Fallback ist ein für dieses Projekt programmierter, generischer Sinus-Doppelton.
Er bildet keinen charakteristischen Hersteller-/Funkgeräte-Ton nach. Seine
mathematische Definition und Zeitabschnitte stehen direkt in
`src/murmur/AudioSignal.cpp`, Funktion `syntheticBusy()`, sowie in der
[Audio-Dokumentation](audio-feedback.md#the-signal-and-its-provenance).

| Bestandteil | Herkunft | Lizenz / Behandlung |
| --- | --- | --- |
| Integrierter Fallback | Eigene programmatische Sinuswellen, keine Aufnahme | Projekt-BSD; neue Dateien tragen Mumble-Funk-Contributors-Hinweis |
| WAV-Prüfer, Umrechnung, Hüllkurve, Zustellungslogik | Im Feature implementierter Projektcode | Bestehende Projekt-BSD-Lizenz; keine übernommenen fremden Implementierungsfragmente |
| Temporäre Testsignale | In den Tests aus Sinusberechnungen erzeugt | Projektcode; keine externen Audiodateien |
| Benutzerdefinierte WAV-Dateien | Vom Administrator lokal bereitgestellt | Keine automatische Veröffentlichung im Repository; Rechte an der eigenen Datei liegen außerhalb des Fallbacks |
| Opus (`libopus-dev` / `libopus0`) | Offizielles Ubuntu-/Distributionspaket; im Mumble-Client bereits verwendeter Codec, jetzt auch Server-Linkabhängigkeit | Hauptlizenz BSD-3-Clause, Komponenten BSD-2-Clause/BSD-3-Clause; Hinweise erhalten |
| Qt5 und übrige bestehende Server-Abhängigkeiten | Bereits bestehende Mumble-Buildgrundlage | Bestehende Hinweise unverändert; keine neue fremde Audiobibliothek hierfür hinzugefügt |
| Optionale lokale Konvertierung mit `ffmpeg` | Nur ein dokumentierter Benutzerbefehl | Keine Build-/Laufzeitabhängigkeit des Servers; keine FFmpeg-Binärdatei/-Quelle wird eingebunden |

**Neuer Drittanbieter-Code:** Es wurde kein fremder Quellcode und kein Codec-Quellbaum
in das Projekt kopiert oder als neue Submodule hinzugefügt. Die Server-Binärdatei
verwendet jedoch die zusätzliche dynamisch verknüpfte **Opus-Bibliothek**. Das ist
Drittanbieter-Software und wird ausdrücklich nicht als Eigenentwicklung ausgegeben.

## Geprüfte Codec-Nachweise

- Offizielles [Opus-COPYING, Tag v1.5.2](https://github.com/xiph/opus/blob/v1.5.2/COPYING),
  einschließlich Copyright-Liste, vollständiger BSD-Bedingungen und der Verweise auf
  die ergänzenden lizenzgebührenfreien Patentlizenz-Erklärungen. Die Bedingungen
  verlangen Erhalt der Hinweise und erlauben keine Darstellung der Rechteinhaber
  als Unterstützer des Forks.
- Copyright-/Komponentenübersicht des im Cloud-Build verwendeten `libopus-dev`
  1.5.2 unter `/usr/share/doc/libopus-dev/copyright`. Die aufgeführten
  Komponentenlizenzen sind BSD-2-Clause und BSD-3-Clause.
- Zusätzlich direkt geprüft: Ubuntu 24.04 `libopus-dev`/`libopus0` **1.4-1build1**
  und Ubuntu 26.04 **1.6.1-1**. Beide Paket-Copyright-Inventare nennen ausschließlich
  BSD-2-Clause/BSD-3-Clause für die aufgeführten Komponenten. Die dort verwendete
  API wurde beim tatsächlichen Release-Build und den Opus-Pakettests geprüft.
- Die zusammengeführten Hinweise stehen in
  [`3rdPartyLicenses/libopus_license.txt`](../../3rdPartyLicenses/libopus_license.txt).
  Sie werden zusätzlich in Mumbles bestehende Drittanbieter-Lizenzanzeige eingebunden.
- Das Preview-Setup installiert die Projektlizenz und Opus-Hinweise unter
  `/opt/mumble-funk-audio-engine/current/licenses/`. Die tatsächlich installierten
  Ubuntu-Pakete behalten außerdem ihre versionsspezifischen Hinweise unter
  `/usr/share/doc/libopus0/` bzw. `/usr/share/doc/libopus-dev/`.

Es wurden zur Prüfung Quelltexte der unveränderten Clients und der offizielle
Lizenztext angesehen; daraus wurden keine Audioaufnahmen oder fremden
Implementierungen in die neue Engine übernommen. Bestehende Mumble-Schnittstellen
werden regulär weiterverwendet. `LICENSE` und vorhandene Copyright-Dateien wurden
nicht entfernt oder umgeschrieben.

## Eigene REAPER-/Dexed-Datei

Dein Export ist eine austauschbare lokale Ressource und wird weder fest in den
Server kompiliert noch von uns hochgeladen. Der [Verwaltungsbefehl](audio-engine.md#aktivierung-und-eigene-wav)
prüft technische Kompatibilität; er ermittelt keine Urheber- oder Nutzungsrechte
beliebiger Benutzerdateien. Für den integrierten Fallback bestehen keine
Abhängigkeiten von diesem Export oder von externen Aufnahmen.
