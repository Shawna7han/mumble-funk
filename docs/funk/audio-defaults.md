# Standard-Audio für neue Dev-Installationen — experimentell

Diese Vorgaben gelten für den aktuellen **`dev` nach PR #9**. Sie setzen die
frühere Arbeit aus `feature/audio-defaults` fort und enthalten jetzt auch die
mitgelieferte CC0-Rauschaufnahme. Stable und historische Branches erhalten dadurch
keine neuen Funktionen. [Branch- und Merge-Übersicht](development.md).

## Neue Ubuntu- und Docker-Konfigurationen

Beim Erzeugen einer fehlenden INI verwenden das normale Ubuntu-Setup und Docker:

```ini
simplex=true
audiofeedback=true
busytone=true
busytonefile=/usr/local/share/mumble-funk/audio/busy-tone.wav
busytonevolume=50
squelch=true
squelchopen=true
squelchclose=true
squelchopenfile=/usr/local/share/mumble-funk/audio/squelch-open.wav
squelchclosefile=/usr/local/share/mumble-funk/audio/squelch-close.wav
squelchopenvolume=50
squelchclosevolume=50
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
Lautstärke 0 bleibt stumm. Sprache wird nicht dekodiert oder neu kodiert.
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
| `audiofeedback`, `squelch` | `false` |
| `squelchclosetimeoutfallback` | `false` |
| `feedbackboostdb` | `0` |
| `busytonefile` | leer; synthetischer Busy |
| `radionoise` | `false` |
| `radionoisefile` | leer; keine Rauschdatei |
| `radionoisevolume` | `20` |

Auch ein ausdrücklich gespeichertes `false` bleibt erhalten. Datenbanken, Konten,
Kanäle, Passwörter, Zertifikate und eigene Dateien werden nicht durch die neuen
Vorgaben ersetzt. Docker behält `.env`, Projektnamen und Volumes. Ein abweichendes
Operator-Audio am neu eingeführten nativen Paketpfad wird nicht überschrieben;
der Installer weist den Konflikt vor dem Austausch ab.

## Installieren, auswählen und testen

Für die aktuelle Installation nur die gemeinsamen Anleitungen verwenden:

- [Dev unter Ubuntu mit `--experimental-dev`](install-dev.md).
- [Docker aus Dev, Backup/Restore und Updates](docker.md).
- [Status und Verwaltung](ubuntu-commands.md).
- [Eigene Rausch-WAV und Rückkehr zur Standardaufnahme](radio-noise.md#d-eigene-aufnahme-und-rückkehr-zum-standard).

Bestehende Installationen übernehmen neue Werte nur auf ausdrücklichen Wunsch.
Die Verwaltung bietet unter anderem `boost 8`, `volume 50`, `open-volume 50`,
`close-volume 50`, `use-standard-busy-tone`, `enable-squelch`, `enable-open`,
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
