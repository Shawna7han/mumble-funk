> **Historische Phase-2C-Installation.** Squelch ist inzwischen in Dev integriert.
> Für die aktuelle normale Ubuntu-Installation [Dev installieren](install-dev.md)
> verwenden, nicht den unten erhaltenen Feature-Checkout. Aktuelle Vorgaben:
> [Audio-Defaults](audio-defaults.md), [Verwaltung](ubuntu-commands.md),
> [Open/Noise/Close](radio-noise.md). Die alten Phasenangaben bleiben zuordenbar.

# Squelch auf einer frischen Ubuntu-Snapshot-VM testen (Phase 2C)

**Neuer Nachfolgestand:** [Standard-Audioeinstellungen](audio-defaults.md) auf
`feature/audio-defaults` aktiviert Funk-Audio bei Neuinstallationen mit +8 dB.
Die branchspezifischen Defaults und Installationsoptionen unten dokumentieren den
vorherigen Entwicklungsstand; bestehende Installationen werden nicht migriert.

Für den experimentellen **Audio Boost (0–12 dB)** aus `feature/audio-volume`
siehe [Audio-Boost-Anleitung mit Update und Hörtest](audio-volume.md).
Die bisherigen branchspezifischen Anleitungen unten bleiben gültig.


Dies ist eine **experimentelle Vorschau**, keine Stable-Freigabe. Verwende deine
separate Ubuntu-Test-VM unter Proxmox. Deine bisherigen Server/anderen VMs bleiben
unangetastet. Die Snapshot-Verwaltung und diese Installation führst **du selbst**
aus. Codex hat nichts auf deiner VM installiert oder zurückgesetzt.

## 1. VM vorbereiten

- Ubuntu Server **24.04 LTS**, normaler Benutzer mit `sudo`-Rechten und laufendem
  systemd. Ubuntu 26.04 LTS bleibt ebenfalls in der Betriebssystemfreigabe.
- Empfohlen: 2 CPU-Kerne, 4 GiB RAM, mindestens **8 GiB freier Platz** für Quellen
  und Build. Unter 4 GiB wird mit einem Prozess gebaut; mindestens 2 GiB RAM plus
  Swap werden verlangt. Mehr Platz für Updates/Sicherungen einplanen.
- Internetzugriff auf Ubuntu-Paketquellen und GitHub; erreichbare IP im eigenen LAN.
- Auf dieser sauberen VM darf noch kein anderer Dienst Port **64739 TCP/UDP** oder
  den Dienstnamen `mumble-funk` verwenden. Nicht über eine bestehende Installation
  einer anderen VM installieren.
- Vorher in Proxmox selbst einen sauberen Snapshot erstellen. Ein Snapshot ersetzt
  keine externe Sicherung wichtiger Daten. Vor späterem Zurücksetzen prüfen, ob
  Testdaten erhalten werden sollen.

Alle folgenden Befehle im Terminal **dieser Test-VM** als normaler Benutzer
aufrufen. `setup.sh` bekommt kein vorangestelltes `sudo`; es fragt bei Bedarf danach.

## 2. Experimentellen Zweig herunterladen und installieren

```bash
sudo apt update
sudo apt install --yes git ca-certificates
git clone --branch feature/squelch-audio --single-branch \
  https://github.com/Shawna7han/mumble-funk.git ~/mumble-funk
cd ~/mumble-funk
git rev-parse HEAD
bash scripts/funk/setup.sh --experimental-squelch
```

Die ausgegebene vollständige Commit-ID für deinen Testbericht notieren. Beim ersten
Build werden Abhängigkeiten installiert und Submodule geladen; das kann mehrere
Minuten dauern. Der Installer kompiliert den Server, führt sämtliche CTest-Programme
aus und richtet danach den normalen Dienst **mumble-funk** mit automatischem Start
ein. Er prüft Version, Herkunft, unveränderte Quellen und den veröffentlichten
Feature-Stand. Ohne `--experimental-squelch` wird dieser Zweig bewusst abgelehnt.
Keine Stable-Umbenennung, kein zusätzlicher Preview-Dienst, kein Port 64742.

Bei einem Fehler anhalten und die Fehlermeldung prüfen; nicht Schutzprüfungen
entfernen oder einen anderen Branch vortäuschen. Nach einem Speicherfehler zuerst
Platz schaffen; nicht einfach vorhandene Datenbanken löschen.

```bash
sudo mumble-funk-info
sudo systemctl status mumble-funk --no-pager
sudo mumble-funk-audio status
```

`mumble-funk-info` zeigt lokal Verbindungsdaten und das
zufällig erzeugte **Serverpasswort**. Dieses nicht öffentlich weitergeben.
`mumble-funk-audio status` zeigt ausschließlich Audioeinstellungen. Simplex ist
bei einer frischen Einrichtung über setup aktiviert; Audio und Squelch bleiben
zunächst **aus**. Die angezeigte Quellrevision gehört zum installierten Binary.

## 3. Verbindung ohne Effekte prüfen

Unveränderte Windows-Mumble- und Android-Mumla-Clients mit der IP der Test-VM und
Port **64739** verbinden. Das angezeigte Serverpasswort verwenden und das neue
Testserver-Zertifikat nach eigener Prüfung bestätigen. Mindestens zwei Geräte
verwenden. PTT als Eingabemethode einstellen; Mikrofon/Stummschaltung prüfen.

Wenn auf der VM bereits UFW aktiv ist (`sudo ufw status`), dafür erlauben:

```bash
sudo ufw allow 64739/tcp
sudo ufw allow 64739/udp
```

UFW hierfür nicht ungeprüft einschalten. Proxmox-/Router-Firewalls bei Bedarf selbst
prüfen. Im LAN ist normalerweise keine Router-Portweiterleitung nötig. Docker
bleibt bei Host **64740**, intern **64739**, jeweils TCP und UDP. Effekte benötigen
keinen zusätzlichen Port.

## 4. Squelch erstmals aktivieren

```bash
sudo mumble-funk-audio enable-squelch
sudo mumble-funk-audio status
sudo tail -n 60 /var/log/mumble-funk/server.log
```

Der Befehl aktiviert die Audio-Hauptfreigabe und Squelch; der Dienst wird kurz neu
gestartet, Clients verbinden sich gegebenenfalls erneut. Im Log sollte
`Experimental Squelch enabled` erscheinen. Beide mitgelieferten WAVs sind bereits
installiert. Open und Close haben zunächst Lautstärke 50. Es gibt keine Garantie
für perfekte zeitliche Reihenfolge: kurze Überlappung mit Sprache ist zulässig.
Die originale Sprachweiterleitung bleibt vorrangig.

Praktischer Ablauf:

1. Zwei Geräte im selben Kanal: A spricht einen Satz mit klarer erster und letzter
   Silbe; B prüft Open, vollständige Sprache und Close. Danach Rollen tauschen.
2. Ein drittes Gerät in einem anderen Kanal darf keine Signale dieses Kanals hören.
3. A hält PTT, B versucht zu senden: Busy bleibt privat bei B; B darf nach A nicht
   automatisch übernehmen. B lässt los und drückt erneut. Busy gehört zur bereits
   vorhandenen Audiofunktion und wird mit der Audio-Hauptfreigabe ebenfalls hörbar.
4. Kurze PTT-Wechsel, längere Sprechpausen, Kanalwechsel und erneute Verbindung
   prüfen. Fehlende optionale Signale unter Last protokollieren; abgeschnittene
   Sprache oder falsche Empfänger sind Fehler.
5. Nur Close und anschließend nur Open testen:

```bash
sudo mumble-funk-audio disable-open
sudo mumble-funk-audio enable-open
sudo mumble-funk-audio disable-close
sudo mumble-funk-audio enable-close
```

6. Mit deaktiviertem Squelch vergleichen; Busy bleibt dabei erhalten:

```bash
sudo mumble-funk-audio disable-squelch
sudo mumble-funk-audio enable-squelch
```

Wenn der Client eine TCP-only-Einstellung anbietet, zusätzlich damit testen;
ansonsten normale UDP-Verbindung verwenden. Nicht aus einer erfolgreichen
TCP-Verbindung allein schließen, dass UDP funktioniert. Pro Test Geräte-/Client-
Version, Kanal, Transport, Pegel und beobachtete erste/letzte Silben notieren.
Bis diese Hörtests bestanden sind, bleibt PR #4 Draft und die Version experimentell.

## 4a. Mumla 3.7.3: optionalen Close bei Timeout testen

Beim ersten Hörtest des Betreibers funktionierten Open/Close bei Windows-Mumble.
Bei Android-Mumla 3.7.3 fehlte Close; die Diagnose meldete dort
`released / timeout_suspected`, beim PC `released / marker_confirmed`.
Dafür gibt es jetzt einen **eigenen, standardmäßig ausgeschalteten** Fallback.
Die neue Version muss zunächst auf deiner Test-VM aktualisiert/installiert sein
(siehe Schritt 6); der alte Server kennt den Schalter noch nicht.

Squelch und Close müssen bereits eingeschaltet sein (Schritt 4). Dann:

```bash
sudo mumble-funk-audio enable-timeout-close
sudo mumble-funk-audio status
```

Das setzt `squelchclosetimeoutfallback=true` und startet nur `mumble-funk` kurz neu.
Der Befehl verändert weder Simplex noch die Audio-/Squelch-Hauptschalter oder Busy.

1. Windows sendet normal und lässt PTT los: Close soll weiterhin nach dem bestätigten
   Marker kommen, ohne zusätzliche Timeout-Wartezeit und ohne zweiten Close.
2. Mumla sendet einen kurzen Satz und lässt PTT los: Falls wieder kein Endmarker
   ankommt, soll Close nach dem bisherigen Simplex-Timeout hörbar sein. Bei 500 ms
   Timeout entsteht ungefähr eine halbe Sekunde plus Timer-/Audio-Verzögerung.
3. Auch einen einzelnen kurzen PTT-Druck testen. Nach dem Close mehrere Sekunden
   warten: Ohne neue angenommene Übertragung darf kein weiterer Close kommen.
4. Falls bereits Simplex-Diagnose aktiv ist, die Gründe vergleichen:
   Windows weiterhin `marker_confirmed`, Mumla gegebenenfalls `timeout_suspected`.
   Der Fallback erzeugt keinen künstlichen Sprach-Endmarker und ändert diese
   Diagnosegründe nicht. Ohne Diagnose keine Audiodateien/Sprachaufzeichnungen nötig.
5. Rollen schnell wechseln, Kanal wechseln und einen Client trennen/neu verbinden:
   keine alten Close-Töne an neue/fremde Teilnehmer. Auch den bisherigen Busy-Test
   durchführen. Zum Vergleich wieder ausschalten:

```bash
sudo mumble-funk-audio disable-timeout-close
```

**Grenze:** Ein Timeout bedeutet nur, dass der Server längere Zeit keine passenden
Sprachpakete erhalten hat. Er beweist **nicht**, dass die PTT-Taste losgelassen wurde.
Bei einer Sprechpause mit unterbrochenen Paketen, Paketverlust oder Netzabbruch kann
Close auch bei noch gedrückter Taste entstehen. Auf der reinen Test-VM zusätzlich
PTT halten und lange schweigen: Beobachtung, Client-Version und Zeitabstand notieren.
Die normale Simplex-Retry-Regel gilt weiterhin; der Fallback gibt kein Senderecht.
Bereits auf dem Client gepufferte Töne können nicht zurückgerufen werden. Bei
Busy/Überlast kann der optionale Close entfallen. Die geänderte Funktion bleibt
bis zu diesem erneuten Hörtest experimentell.

## 5. Lautstärke und eigene WAV-Dateien

```bash
sudo mumble-funk-audio open-volume 30
sudo mumble-funk-audio close-volume 40
sudo mumble-funk-audio volume 50
```

Der letzte Befehl regelt Busy. Werte 0 bis 100; 0 ist stumm. Änderungen starten den
normalen Dienst neu. Die Befehle aktivieren keine ausgeschalteten Signale.

Eigene Dateien per SFTP/SCP zunächst in dein Benutzerverzeichnis kopieren, z. B.
`~/my-open.wav` und `~/my-close.wav`. Rechte an den Aufnahmen müssen geklärt sein.
Format: **PCM-WAV, mono, 16 Bit signed little-endian, 8/16/24/48 kHz**, 1 bis
1000 ms, höchstens 128 KiB. Stereo/Float-WAV vorher selbst passend exportieren.
Keine Links oder Spezialdateien verwenden.

```bash
sudo mumble-funk-audio set-open "$HOME/my-open.wav"
sudo mumble-funk-audio set-close "$HOME/my-close.wav"
sudo mumble-funk-audio status
```

Die Verwaltung prüft mit dem echten Serverparser und kopiert gültige Dateien nach
`/etc/mumble-funk/sounds/`, mit eigenem Namen anhand ihrer Prüfsumme. Das Dienstkonto
kann sie lesen; dein Home muss nicht freigegeben werden. Eine fehlerhafte Datei
ändert die ausgewählte Konfiguration nicht. Keine Neukompilierung nötig.

Auf die mitgelieferten Standards zurückstellen:

```bash
sudo mumble-funk-audio reset-open
sudo mumble-funk-audio reset-close
```

Busy bleibt austauschbar (`set-busy-tone DATEI`, `reset-busy-tone`); für Busy gelten
jetzt 20–1250 ms; siehe [neuer Standard-Busy-Tone](busy-tone.md) für den aktuellen
Feature-Branch und seine ausdrückliche Installationsoption. `reset-busy-tone`
wählt weiterhin den programmatisch erzeugten Doppelton; `use-standard-busy-tone`
die neue mitgelieferte Aufnahme.
Squelch hat keinen synthetischen Fallback. Eigene Dateien werden beim Zurücksetzen
nicht gelöscht und bei Updates nicht überschrieben.

## 6. Fehler, Updates und Rückkehr zum Snapshot

```bash
sudo systemctl status mumble-funk --no-pager
sudo journalctl -u mumble-funk -n 80 --no-pager
sudo tail -n 80 /var/log/mumble-funk/server.log
```

`Squelch disabled` weist z. B. auf eine ungültige/unlesbare aktivierte WAV hin.
Sprache und Busy können weiterlaufen. Für alle Effekte aus:

```bash
sudo mumble-funk-audio disable
```

Bei jeder Audioänderung wird die vorherige private Konfiguration als
`/etc/mumble-funk/server.ini.before-audio-change` gesichert. Nicht öffentlich
hochladen: Sie enthält Zugangsdaten. Der Installer sichert bei Aktualisierungen
Zustand und Konfiguration unter `/var/backups/mumble-funk/`; vorherige Binärdateien
bleiben unter `/opt/mumble-funk/releases/`. Für diese experimentellen Versuche ist
der von dir verwaltete VM-Snapshot der einfache vollständige Rückweg.

Nur wenn du diese Testinstallation später bewusst aktualisieren möchtest:

```bash
cd ~/mumble-funk
git pull --ff-only
bash scripts/funk/setup.sh --experimental-squelch
```

Vorher Testdaten sichern oder selbst einen Snapshot erstellen. Bestehende INI,
Datenbank, Passwort, Zertifikat, ausgewählte WAVs und Pegel bleiben erhalten.
Es findet keine automatische Git-Aktualisierung oder Portmigration statt.
Nachher Quellrevision, Status und Hörtests erneut prüfen. Andere VMs bleiben unberührt.

Die vollständigen Optionen und technischen Grenzen stehen in
[squelch-preview.md](squelch-preview.md). Automatisierte Tests ersetzen keine
Hörtests auf deinen Geräten und keine Prüfung eines echten VM-Neustarts.
