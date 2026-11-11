# CB-Standardkonfiguration für Neuinstallationen — experimentell

Diese Vorgaben gelten zunächst für **`feature/analog-voice-live` in Draft-PR #15**
und erst nach einer gesonderten Übernahme auch für neue experimentelle
Dev-Installationen. Der veröffentlichte `dev` wird durch diesen Feature-Commit
nicht verändert. Stable und historische Branches bleiben unverändert.
[Branch- und Merge-Übersicht](development.md).

## Neue Ubuntu- und Docker-Konfigurationen

Bei einer vollständig neuen Installation verwenden das normale Ubuntu-Setup und
Docker dieselben Audio-Vorgaben. Eine fehlende INI bei vorhandenen Zustandsdaten
berechtigt nicht zur Neuinitialisierung; die bisherigen Schutzprüfungen bleiben:

```ini
simplex=true
analogvoice=true
analogvoiceprofile=cb-am
audiofeedback=true
busytone=true
busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav
busytonevolume=100
squelch=true
squelchopen=true
squelchclose=true
squelchopenfile=/usr/local/share/mumble-funk/audio/squelch-open.wav
squelchclosefile=/usr/local/share/mumble-funk/audio/squelch-close.wav
squelchopenvolume=80
squelchclosevolume=80
feedbackboostdb=8
squelchclosetimeoutfallback=true
radionoise=true
radionoisefile=/usr/local/share/mumble-funk/audio/radio-noise.wav
radionoisevolume=20
```

Quelle: `scripts/funk/ubuntu/server.ini` und die nur bei Neuinitialisierung
verwendete INI-Vorlage in `scripts/docker/runtime.py`. Das automatische native
Setup und Docker setzen außerdem 500 ms für Simplex-Timeout und Retry-Paketruhe.
Die Kern-Ersatzwerte bei fehlenden Einträgen sind davon getrennt.

Die vier Dateien werden automatisch unter `/usr/local/share/mumble-funk/audio/`
bereitgestellt. Sie sind für den Dienstbenutzer bzw. Container-UID 10001 lesbar.
Herkunft und [CC0-1.0-Freigabe](../../assets/audio/LICENSE.md) sind dokumentiert.
Die Rauschaufnahme bleibt vollständig bytegleich; es wird keine Stille eingefügt.

Der gemeinsame Boost beträgt **+8 dB ausschließlich für Busy, Open und Close**.
Rauschen hat eine unabhängige Lautstärke von **20** und wird nicht mitgeboostet.
Lautstärke 0 bleibt stumm. Der aktivierte Analogfilter dekodiert Sprache, wendet
das unveränderte Profil `cb-am` an und kodiert sie erneut. `business-fm` und
`narrowband` bleiben wählbar. Bei fehlendem `analogvoice` oder ausdrücklichem
`analogvoice=false` bleibt der bestehende Direktpfad ohne diesen Worker aktiv.
Mit aktivem Rauschen folgen Open → Rauschen → Close in einem gemeinsamen
privaten Effektstream aufeinander, ohne innere Ende-Markierungen oder Mixing.

Der Timeout-Close-Fallback ist für neue INIs aktiv. Ohne bestätigte Ende-Markierung
kann nach dem bestehenden Paket-Timeout einmal Close folgen. Bei bestätigtem Ende
gibt es keinen zusätzlichen Timeout-Close. Ein Timeout beweist kein physisches
Loslassen der PTT-Taste. [Verhalten und Grenzen](radio-noise.md).

## Bestehende Installation: keine automatische Umstellung

Eine vorhandene INI wird weder ergänzt noch migriert. Das gilt auch für fehlende
Schlüssel, ausgeschaltetes Simplex/Audio/Squelch/Rauschen, Volume 0, Boost 0,
eigene WAVs und einen leeren Busy-Pfad für den synthetischen Ton. Beispielhafte
unveränderte Server-/Status-Ersatzwerte bei fehlenden älteren Einträgen:

| Fehlender Eintrag | Ersatzwert, keine neue INI-Zeile |
| --- | --- |
| `analogvoice` | `false`; Direktpfad |
| `audiofeedback`, `squelch` | `false` |
| `squelchclosetimeoutfallback` | `false` |
| `feedbackboostdb` | `0` |
| `busytonefile` | leer; synthetischer Busy |
| `radionoise` | `false` |
| `radionoisefile` | leer; keine Rauschdatei |
| `radionoisevolume` | `20` |

Auch ein ausdrücklich gespeichertes `false` bleibt erhalten. Es gibt keinen
`--apply-cb-preset`-Schalter und keine automatische Übernahme des CB-Standards bei
einem Update. Fehlende Schlüssel werden auch dann nicht ergänzt, wenn eine neue
Vorlage sie enthält. Die Core-Ersatzwerte wurden nicht geändert; gewöhnliche
Mumble-Installationen ohne Simplex erhalten keine automatische Filteraktivierung.
Datenbanken, Konten, Kanäle, Passwörter, Zertifikate und eigene Dateien werden
nicht durch die neuen Vorgaben ersetzt. Docker behält `.env`, Projektnamen und
Volumes. Ein abweichendes
Operator-Audio am neu eingeführten nativen Paketpfad wird nicht überschrieben;
der Installer weist den Konflikt vor dem Austausch ab.

## Installieren, auswählen und testen

Für die aktuelle Installation nur die gemeinsamen Anleitungen verwenden:

- [Dev unter Ubuntu mit `--experimental-dev`](install-dev.md).
- [Docker aus Dev, Backup/Restore und Updates](docker.md).
- [Status und Verwaltung](ubuntu-commands.md).
- [Eigene Rausch-WAV und Rückkehr zur Standardaufnahme](radio-noise.md#d-eigene-aufnahme-und-rückkehr-zum-standard).

Unabhängige manuelle Verwaltungsbefehle bleiben unverändert verfügbar. Sie sind
kein Teil eines Updates und kein neues Preset. Die Verwaltung bietet unter anderem
`boost 8`, `volume`, `open-volume`, `close-volume`, `use-standard-busy-tone`,
`enable-squelch`, `enable-open`,
`enable-close`, `enable-timeout-close`, `use-standard-radio-noise`,
`radio-noise-volume 20` und `enable-radio-noise`. Die jeweiligen Voraussetzungen
bleiben aktiv: Rauschen benötigt bereits aktiviertes Simplex und Audio-Feedback.
Ändernde Befehle können den eigenen Dienst kurz neu starten; `status` nicht.

`reset-busy-tone` wählt weiterhin den mathematisch erzeugten Doppelton. Eine
ungültige direkt konfigurierte Busy-Datei führt zum synthetischen Fallback;
fehlerhafte Importe werden vor der Auswahl abgewiesen. Eigene WAVs benötigen
keine Neukompilierung und werden beim Wechsel auf Standarddateien nicht gelöscht.

Ubuntu bleibt bei **64739 TCP/UDP**. Aktuelles Dev-Docker verwendet
**Host 64740 TCP/UDP → Container 64739 TCP/UDP**; bestehende ausdrücklich gewählte
andere Host-Ports behalten ihre Gültigkeit. Historische Stable-Vorgaben werden
nicht nachträglich geändert. [Port-Bestandsschutz](docker.md#9-aktualisieren).

Automatisierte Ergebnisse und die vom Betreiber gemeldeten Ubuntu-Hörtests sind
im [Entwicklungsüberblick](development.md#tests-und-praxiserfahrung) voneinander
getrennt. Für neue Tests mit Windows-Mumble und Android-Mumla erste/letzte Silben,
Busy, schnellen PTT-Wechsel, Open/Rauschen/Close sowie bestätigtes und Timeout-Ende
vergleichen. Keine allgemeine Stable- oder Langzeitfreigabe.

## Prüfung der CB-Vorlagenänderung am 10. November 2026

- Sieben native Installations-/Audiotestprogramme: **44/44** Tests. Die
  Audio-Defaults-Prüfung startet echte isolierte Server mit den erzeugten nativen
  und Docker-INIs. CB-AM, Busy 100, Open/Close 80, Noise 20 und Boost 8 werden
  geprüft; TCP, verschlüsseltes UDP, bestätigtes Ende und Timeout sind enthalten.
- Source-Guards **31/31**, Setup **28/28**, Audio-Security **13/13** und
  GHCR-Guards **3/3**. Bestehende INIs mit fehlenden, deaktivierten und eigenen
  Werten bleiben bytegleich; eigene WAVs, Datenbanken und Zertifikatsdateien
  bleiben erhalten. Vorhandene Backup-/Restore- und Rollback-Prüfungen bleiben aktiv.
- Docker-Python-Suite **38/38**, relevante Server-CTest-Ziele **7/7**:
  `TestSimplex`, `TestAnalogVoiceIntegration`, `TestAnalogVoiceLifecycle`,
  `TestAnalogVoiceEffects`, `TestAudioFeedback`, `TestRadioNoise` und
  `TestSquelchFoundation`. Die vorhandenen Profiltests prüfen weiterhin alle drei
  Profile sowie fehlendes/ausgeschaltetes Analog Voice.
- Test-Probe im Release-Build neu gebaut; Murmur-Quellen, DSP-Dateien und alle
  vier WAVs einschließlich Lizenzdatei bytegleich zum Ausgangscommit
  `396356c09a2b58af3668d6247b8a4265d2ca3a57`. Core-Fallbacks, Installerlogik,
  Queuegrenzen und Timeouts wurden nicht geändert.
- Echter lokaler Docker-/Compose-Lauf erfolgreich: CB-Erstinitialisierung,
  TCP/verschlüsseltes UDP, Busy-Priorität, bestätigtes und Timeout-Ende,
  62-Sekunden-Noise-Regressionslauf, Neustart/Neuerstellung, Rückkehr zur alten
  INI bei injiziertem Healthcheck-Fehler, WAV-Import, Backup/Restore,
  Portkonflikte, paralleler nativer Server und sauberer Shutdown. Getestet wurde
  ein lokales Testimage aus dem vorhandenen B6A-Image mit ausschließlich der
  neuen `runtime.py`; der komplette produktive Dockerfile-Build wurde hierfür
  nicht erneut lokal ausgeführt. UID 10001 und die bestehende Containerisolation
  wurden geprüft, kein externer Docker-/VPS-Host verwendet. Die neue GitHub-CI
  führt den vollständigen Build separat aus.

Der neue CB-Sockettest prüft vier kurze Transport-/Ende-Szenarien mit dem
bestehenden B2-Codecvertrag. Medienuhren, Paketanzahl und Endmarkierungen werden
exakt geprüft; die Audioausgabe wird nach dem Dekodieren verglichen. Native und
Container-Builds müssen keine bitidentischen Opus-Payloads erzeugen. Hier wurden
libopus **1.5.2 auf dem Host** und **1.4 im vorhandenen Docker-Image** nachgewiesen;
der neue dekodierte CB-Vergleich bestand auch zwischen diesen Versionen. Die
bestehenden Direktpfadtests behalten ihre exakten Original-Opus-/Effektprüfungen
und den 62-Sekunden-Noise-Test mit ausdrücklich alten Testeinstellungen. Für die
historischen Byteprüfungen gegen den Container wird dessen libopus 1.4 auch auf
der Prüfseite verwendet; die Assertions werden nicht abgeschwächt.

Ein zusätzlicher paralleler 62-Sekunden-CB-Vorversuch meldete **2601 statt 3101**
Sprachpakete. Die Ursache ist damit nicht bewiesen; dieser Befund wird nicht als
behoben bezeichnet. Der neue Neuinstallations-Test ist keine Lastabnahme und
ändert keine produktive Überlastgrenze. Die bereits dokumentierten B6A-Risiken
und die spätere B6B-Last-/Hörabnahme bleiben offen. Die abschließenden obigen
Prüfungen sind von diesem zusätzlichen Vorversuch zu unterscheiden.
