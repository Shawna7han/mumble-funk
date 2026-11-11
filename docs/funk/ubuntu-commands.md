# Status und Verwaltung der Ubuntu-Testinstallation

Für die normale native Mumble-Funk-Installation auf der eigenen Ubuntu-Test-VM:
Dienst `mumble-funk`, Konfiguration `/etc/mumble-funk/server.ini`, Standardport
**64739 TCP/UDP**. Diese Übersicht beschreibt den integrierten `dev`-Stand nach PR #9;
sie ist keine Stable-Freigabe. Docker verwendet andere Verwaltungsbefehle:
[Docker-Anleitung](docker.md).

Alle folgenden Abfragen sind **nur lesend**: Sie ändern keine Konfiguration und
lösen keinen Serverneustart aus. Befehle einzeln im Terminal der Test-VM eingeben.

| Zweck | Befehl | Was die Ausgabe bedeutet |
| --- | --- | --- |
| Verbindungsinformationen **mit Passwort** | `sudo mumble-funk-info` | Adresse, Port, Dienst-/Versionsinformationen und konfiguriertes Zugangspasswort. **Diese Ausgabe privat halten.** |
| Verbindungsinformationen ohne Passwort | `sudo mumble-funk-info --hide-password` | Gleiche Serverinformationen, aber ohne Anzeige des Zugangspassworts. Adressen bleiben sichtbar. |
| Audio-Konfiguration | `sudo mumble-funk-audio status` | JSON mit Busy-, Squelch-Open-/Close-, Timeout-Close-, Boost- und Funkrausch-Einstellungen; keine anderen geheimen INI-Einträge. |
| Laufender Dienst | `sudo systemctl status mumble-funk` | `active (running)` bedeutet, dass der Dienst läuft; `inactive` oder `failed` weist auf einen gestoppten bzw. fehlgeschlagenen Dienst hin. Enthält auch letzte Dienstmeldungen. Mit `q` die Seitenansicht verlassen. |
| Installierte Version | `cat /opt/mumble-funk/current/BUILD-INFO` | Unter anderem `revision` (vollständiger Git-Commit), `branch` und `experimental`. Das beschreibt die installierte Version, nicht einen eventuell neueren Quellcode-Checkout. |
| Letzte Servermeldungen | `sudo tail -n 60 /var/log/mumble-funk/server.log` | Die letzten 60 Zeilen, beispielsweise Start- und WAV-Ladefehler. Keine gezielte Passwortabfrage; Logausgaben vor dem Weitergeben prüfen. |

`mumble-funk-info` ohne `--hide-password` ist der hier genannte Befehl, der das
Zugangspasswort ausdrücklich ausgibt. Für Screenshots oder Supportausgaben die
Variante mit `--hide-password` verwenden. Dienststatus und Logansicht sind keine
gefilterten Audio-Statusausgaben.

## Funkrauschen in der Audio-Statusausgabe

Die drei zusätzlichen Felder stehen im selben JSON-Objekt wie die bisherigen
Einstellungen. Ein Ausschnitt einer neuen Dev-Installation sieht so aus:

```json
{
  "radionoise": "true",
  "radionoisefile": "/usr/local/share/mumble-funk/audio/radio-noise.wav",
  "radionoisevolume": "20"
}
```

Die Werte bleiben wie bisher **Zeichenketten**. Die tatsächlich konfigurierte
Dateiangabe wird vollständig angezeigt, auch wenn Rauschen deaktiviert ist.
Fehlende INI-Einträge erscheinen mit den Server-Standardwerten:

| Feld | Standard bei fehlendem INI-Eintrag |
| --- | --- |
| `radionoise` | `"false"` |
| `radionoisefile` | `""` (keine Datei angegeben) |
| `radionoisevolume` | `"20"` |

Die Abfrage liest ausschließlich die INI. Sie prüft **nicht**, ob die Datei
existiert, lesbar ist oder beim letzten Serverstart erfolgreich geladen wurde.
Auch `radionoise=true` beweist keine laufende Audioausgabe. Ein Dienst kann noch
mit Einstellungen vor der letzten INI-Änderung laufen; außerdem können andere
Voraussetzungen oder datenbankseitige Einstellungen die Wirkung beeinflussen.
Den tatsächlichen Betrieb über Dienststatus, Serverlog und Hörtest prüfen.
Die Statusabfrage validiert keine WAV, migriert keine Einstellungen und übernimmt
keine fehlenden Werte in die Datei.

## Einstellungen ändern

Die bestehenden Verwaltungsbefehle bleiben unverändert:

- [Busy Tone und eigene WAV-Dateien](audio-engine.md).
- [Squelch Open/Close, getrennte Schalter und Lautstärken](squelch-ubuntu.md).
- [Globaler Audio-Boost](audio-volume.md).
- [Funkrauschen konfigurieren und testen](radio-noise.md).

Anders als `status` können ändernde Audio-Verwaltungsbefehle den eigenen Dienst
neu starten. Für Funkrauschen stehen `set-radio-noise DATEI`,
`use-standard-radio-noise`, `enable-radio-noise`, `disable-radio-noise` und
`radio-noise-volume WERT` zur Verfügung; Schritte und Bestandsschutz siehe
[Funkrauschverwaltung](radio-noise.md#d-eigene-aufnahme-und-rückkehr-zum-standard).
Neue INIs verwenden die mitgelieferte CC0-Aufnahme bei Lautstärke 20; fehlende
Schlüssel in bestehenden INIs behalten dagegen die oben genannten Laufzeitwerte.
Eigene WAV-Dateien und bestehende Konfigurationen werden durch Abfragen nicht verändert.
