> **Historischer Stand `feature/audio-feedback` (alte Preview auf 64740).**
> Diese Anleitung bzw. diese Ergebnisse gelten nicht für `feature/audio-engine`.
> Für die neue, getrennte Phase-1-Preview auf 64741 siehe [Audio-Engine](audio-engine.md)
> und [Prüfbericht](AUDIO-ENGINE-STATUS.md). Alte Installationen nicht damit aktualisieren.

# Mumble-Funk v0.3 separat installieren und testen

Diese Anleitung gilt ausschließlich für den experimentellen Zweig
**`feature/audio-feedback`**. Die normale [Schnellinstallation](setup.md) bleibt bei
**v0.2 / `funk-v0.1`**. Es gibt keine automatische Umstellung oder Zusammenführung.

Du kannst v0.3 auf deiner zweiten Ubuntu-VM zusätzlich installieren. Auf deiner VM
wurde im Rahmen dieser Entwicklung nichts installiert oder verändert.

## Voraussetzungen

- Ubuntu Server **24.04 LTS oder 26.04 LTS**, auch die jeweiligen Punktversionen.
- Normaler Benutzer mit `sudo`-Rechten; Setup nicht als root starten.
- Internetzugang; GitHub und die Ubuntu-Paketquellen müssen erreichbar sein.
- Mindestens 2 GiB RAM einschließlich Swap, empfohlen 4 GiB RAM; mindestens 8 GiB
  freier Speicher im Quellverzeichnis. Mit wenig RAM dauert der Build länger.
- Laufendes systemd und ein freier Port **64740 für TCP und UDP**. Ist dieser belegt,
  bricht das Setup ab, ohne den fremden Server zu stoppen.
- Unverändertes Mumla auf Android und Mumble auf Windows für die späteren Hörtests.

Das Setup installiert fehlende Build-Pakete automatisch. Neu für v0.3 ist
`libopus-dev`; Qt5, OpenSSL, Protobuf, Boost, Ice, Avahi, SQLite und die bisherigen
Werkzeuge bleiben die Grundlage. Die Betriebssystemprüfung bleibt erhalten.

## 1. Eigenes Quellverzeichnis anlegen

Melde dich per SSH an der Test-VM an. Führe die Befehle einzeln aus. Verwende ein
**neues Verzeichnis**, damit deine v0.2-Quellen ebenfalls erhalten bleiben:

```bash
sudo apt update
sudo apt install -y git
cd ~
git clone --branch feature/audio-feedback --recurse-submodules \
  https://github.com/Shawna7han/mumble-funk.git mumble-funk-audio-preview
cd ~/mumble-funk-audio-preview
bash scripts/funk/setup-audio-preview.sh
```

Falls das Zielverzeichnis schon existiert, **nicht löschen oder darüber kopieren**;
verwende den Update-Abschnitt. Das Setup fragt bei Bedarf nach deinem Ubuntu-Passwort
für `sudo`. Die Kompilierung kann je nach VM einige Zeit dauern.

Das Setup kompiliert in `build/audio-preview`, führt alle Servertests aus und
installiert erst danach. Bei einem Fehler hält es an. Es verändert weder
`mumble-server.service` noch `mumble-funk.service`, deren Ports oder Dateien.

## 2. Verbindung herstellen

Nach erfolgreichem Setup:

```bash
sudo mumble-funk-audio-info
sudo systemctl status mumble-funk-audio --no-pager
```

Notiere die angezeigte **IP-Adresse der VM**, den **Port 64740** und das neue
**Zugangspasswort**. Das Passwort unterscheidet sich von v0.2 und verleiht keine
Administratorrechte. Zeige es nicht in öffentlichen Screenshots.

Lege in Mumla und Windows-Mumble jeweils **einen neuen Servereintrag** an:

| Feld | Eingabe |
| --- | --- |
| Name | Mumble-Funk v0.3 Test |
| Adresse | Die erreichbare IP-Adresse deiner Test-VM |
| Port | `64740` |
| Benutzername | Zum Beispiel `Android` bzw. `Windows` |
| Passwort | Das von `sudo mumble-funk-audio-info` angezeigte Passwort |

Beide Benutzer müssen im selben Kanal sein. Es sind keine Änderungen an den Clients
nötig. Verwende gewöhnliche Benutzernamen, **nicht `SuperUser`**: Mumble erlaubt
SuperUser grundsätzlich kein Sprechen.

Die Instanz erzeugt ein eigenes Serverzertifikat in ihrer eigenen Datenbank. Prüfe
beim ersten Verbinden die Zertifikatsanzeige entsprechend deinem bisherigen
Mumble-Verfahren. Ein v0.2-Zertifikat wird nicht übernommen.

Wenn deine Firewall aktiv ist, erlaube den neuen Port zusätzlich. Bei UFW:

```bash
sudo ufw status
sudo ufw allow 64740/tcp
sudo ufw allow 64740/udp
```

Führe die Freigaben nur aus, wenn UFW tatsächlich deine Firewall ist. Eine
Proxmox-Firewall oder ein Router kann zusätzlich eine Freigabe benötigen. Das Setup
ändert keine Firewall- oder Routerregeln. **64738 und 64739 bleiben erhalten.**

## 3. Busy Tone bedienen

Bei einer **neuen** Preview-Installation sind Simplex und Busy Tone bereits an.
Die Simplex-Kanal- und Wiederholungs-Timeouts betragen jeweils 500 ms. Vorhandene
Preview-Konfigurationen werden bei Updates unverändert erhalten.

```bash
# Feedback einschalten:
sudo mumble-funk-audio enable

# Feedback ausschalten; Simplex bleibt aktiv:
sudo mumble-funk-audio disable

# Lautstärke auf 35 Prozent setzen:
sudo mumble-funk-audio volume 35
```

Diese Verwaltungsbefehle starten **nur `mumble-funk-audio`** neu. Dabei werden die
Verbindungen zu v0.3 kurz getrennt; die Clients können danach neu verbinden. Die
Befehle benötigen keine Neukompilierung. `enable` ändert nicht deine unabhängig
angepassten Einstellungen `simplex`, `busytone` oder eine Lautstärke von 0.

Für eine manuelle Konfiguration:

```bash
sudo nano /etc/mumble-funk-audio/server.ini
```

Die Schlüssel jeweils nur einmal eintragen bzw. die vorhandene Zeile ändern:

```ini
simplex=true
audiofeedback=true
busytone=true
busytonevolume=50
busytonefile=
```

Speichern in nano: `Strg+O`, Eingabetaste; Beenden: `Strg+X`. Danach:

```bash
sudo systemctl restart mumble-funk-audio
```

Das Serverprogramm selbst hat weiterhin `audiofeedback=false` als Standard. Nur die
separate neue Preview-Konfiguration aktiviert die experimentelle Funktion.

## 4. Eigene WAV-Datei verwenden

Der Standard ist ein im Programm erzeugter Doppelton: zweimal 100 ms bei 600 Hz,
40 ms Pause und 20 ms Ausklang für den Codec. Es wurden keine fremden Tonaufnahmen
heruntergeladen. [Technische Angaben und Herkunft](audio-feedback.md#the-signal-and-its-provenance).

Für deinen **mit REAPER und Dexed erzeugten Busy Tone**: Stelle beim Export in
REAPER **WAV / PCM 16 Bit, Mono (1 Kanal), 48000 Hz** ein und markiere nur das kurze
Signal mit höchstens einer Sekunde Länge. Ein Stereo- oder 24-Bit-Export aus der DAW
muss zuerst passend exportiert oder konvertiert werden.

Deine eigene Datei muss folgendes Format haben:

| Eigenschaft | Zulässig |
| --- | --- |
| Format | RIFF/WAV, PCM, vorzeichenbehaftete 16 Bit, little-endian |
| Kanäle | Mono, also genau ein Kanal |
| Abtastrate | 8000, 16000, 24000 oder 48000 Hz |
| Länge | 20 bis 1000 Millisekunden |
| Dateigröße | Höchstens 128 KiB einschließlich Dateikopf |

Stereo, MP3, Float-WAV und 44100 Hz werden nicht angenommen. Niedrigere unterstützte
Abtastraten werden automatisch in 48000 Hz umgerechnet. Zu hohe digitale Pegel
werden abgesenkt; die eingestellte Lautstärke wird zusätzlich angewandt.

Übertrage deine eigene, rechtmäßig verwendbare Datei vom PC zur VM. Ein Beispiel
in PowerShell auf Windows (Benutzer und Adresse ersetzen):

```powershell
scp "C:\Users\DEINNAME\Music\busy.wav" DEINUBUNTUBENUTZER@IP-DER-VM:~/busy.wav
```

Damit liegt die Arbeitskopie unter `/home/DEINUBUNTUBENUTZER/busy.wav`, also kurz
`~/busy.wav`. Den endgültigen Speicherort musst du nicht von Hand einrichten.

Danach wieder in der SSH-Sitzung auf Ubuntu:

```bash
sudo mumble-funk-audio set-busy-tone "$HOME/busy.wav"
```

Der Befehl prüft die Datei, kopiert sie in das geschützte Preview-Signalverzeichnis,
trägt sie in die Preview-Konfiguration ein und startet nur den Preview-Dienst neu.
Die ursprüngliche Datei bleibt erhalten. Deine v0.2-Dateien werden nicht angefasst.

Zurück zum synthetischen Standardton:

```bash
sudo mumble-funk-audio reset-busy-tone
```

Bei einer fehlerhaften Datei lehnt der Verwaltungsbefehl den Austausch ab; die aktive
Konfiguration bleibt erhalten. Wird bei manuellem Eintragen später eine Datei
unlesbar oder ungültig, startet der Server mit dem Standardton und schreibt eine
Warnung ins Log. Ein Signalwechsel erfordert einen Neustart, **keinen neuen Build**.

## Gerätetest

Für Busy Tone wurden hier noch keine echten Android- oder Windows-Geräte abgehört.
Der Serverversand und die dekodierbaren Audiodaten sind automatisiert prüfbar; das
Hören während gedrückter PTT muss dieser Test bestätigen.

1. Verbinde Windows und Android mit **64740**, im selben Kanal. Wähle auf beiden
   Push-to-Talk und schalte die normale Audioausgabe ein. Nicht auf „taub“ stellen.
   Teste zunächst kurz die gewöhnliche Sprache in beide Richtungen.
2. **Windows sendet zuerst:** PTT drücken und mindestens fünf Sekunden sprechen.
   Währenddessen drückt Android PTT und spricht ebenfalls. Android soll einen
   kurzen Doppelton hören. Windows soll sein Senderecht behalten. Ein drittes
   Gerät im Kanal darf nur Windows hören, weder Androids Sprache noch dessen Ton.
3. Android hält PTT weiter gedrückt. Es soll **kein ständiger Dauerton** entstehen.
   Windows beendet seine Sendung. Android darf nicht automatisch hörbar werden.
   Android lässt PTT los, wartet mindestens eine Sekunde und versucht erneut.
4. **Rollen tauschen:** Android beginnt; Windows versucht währenddessen zu senden.
   Jetzt soll nur Windows den Ton hören. Androids Sendung läuft weiter.
5. **Schnelle Wechsel:** Versucht mehrere schnelle Wechsel und notiert, wann ein
   Versuch abgewiesen und ein Ton hörbar wurde. Der Server erkennt Sprachpakete,
   keine physischen Tastendrücke. Fehlende Ende-Markierungen, kurze Paketpausen
   und der zusätzliche Ton-Abstand von einer Sekunde beeinflussen das Ergebnis.
   Nicht jeder schnelle Tastendruck muss deshalb einen eigenen Ton auslösen.
6. **Eigene Datei:** Spiele dieselbe Situation nach einem WAV-Austausch erneut
   durch. Danach auf den Standardton zurücksetzen und vergleichen.
7. **Abschalten:** `sudo mumble-funk-audio disable` ausführen und neu verbinden.
   Simplex soll genauso arbeiten, aber ohne Ton. Anschließend wieder einschalten.
8. **Kanalwechsel/Trennung:** Während eines abgewiesenen Versuchs den Kanal
   wechseln bzw. die Verbindung trennen. Nach erneutem Verbinden darf kein neu
   gestarteter alter Ton an den neuen Benutzer gelangen. Bereits empfangene
   Audiostücke können beim Client noch kurz ausklingen.

Für jeden Durchlauf notieren: Datum, Client-Version, Android-/Windows-Version,
Kopfhörer/Bluetooth/Lautsprecher, wer zuerst sendete, ob der Ton **während gehaltener
PTT** oder erst danach hörbar war, Dauer und Auffälligkeiten. Ein drittes Gerät
macht die Prüfung der gezielten Zustellung erheblich leichter.

Wenn kein Ton hörbar ist, zuerst normale Audioausgabe, Deafen/„taub“, Lautstärke,
PTT-Modus und den wirklich verwendeten Port prüfen. Auch Android-Audiorouting oder
Bluetooth kann das Ergebnis verändern. [Architektur und Grenzen](audio-feedback.md).

## Logs und optionale Diagnose

```bash
sudo tail -n 80 /var/log/mumble-funk-audio/server.log
sudo journalctl -u mumble-funk-audio -n 40 --no-pager
```

Die eigentlichen Serverausgaben stehen wegen der Dienstkonfiguration vor allem in
`server.log`. Erfolgreiche Aktivierung meldet `Experimental audio feedback enabled`;
die Quelle wird als `synthetic double tone` oder `custom PCM WAV` angegeben.
Feedback hängt nicht von JSONL-Protokollen ab; diese sind in der neuen Preview
zunächst abgeschaltet.

Wenn wir die PTT-Diagnose für einen Gerätetest brauchen, in **dieser** Konfiguration
`voicediagnostics=true` sowie gegebenenfalls `simplexdiagnostics=true` setzen und den
Preview-Dienst neu starten. Die Dateien liegen unter
`/var/log/mumble-funk-audio/voice.1.jsonl` bzw. `simplex.1.jsonl`.
Sie beschreiben eingehende Pakete/Entscheidungen, nicht den tatsächlichen
Lautsprecherschall. Es werden serverseitig keine Sprachinhalte aufgezeichnet.

## Updates mit drei Befehlen

```bash
cd ~/mumble-funk-audio-preview
git pull --ff-only origin feature/audio-feedback
bash scripts/funk/setup-audio-preview.sh
```

Bei einem Git-Konflikt oder eigenen lokalen Änderungen anhalten und die Meldung
prüfen lassen; keine Dateien zurücksetzen oder erzwingen. Setup erhält vorhandene
Konfiguration, WAV-Dateien, Passwort, Datenbank und Zertifikat. Vor dem Dienstwechsel
wird unter `/var/backups/mumble-funk-audio/` gesichert. Nur der Preview-Dienst wird
für das Update gestoppt. Die letzte Binärversion bleibt als `previous` erhalten.

## Beenden und entfernen

Vorläufig nur stoppen und den automatischen Start ausschalten:

```bash
sudo systemctl disable --now mumble-funk-audio
```

Den Preview-Dienst und seine Verwaltungsbefehle entfernen, Daten aber behalten:

```bash
cd ~/mumble-funk-audio-preview
sudo bash scripts/funk/audio-preview/uninstall.sh
```

Das Entfernen bewahrt absichtlich Einstellungen, eigene Töne, Datenbank, Zertifikat,
Logs, Backups und Binärversionen. Damit kannst du die Preview später erneut mit
`setup-audio-preview.sh` installieren. **v0.2 wird dabei nicht angefasst.**

Nur wenn du die Preview-Daten endgültig löschen möchtest, nach dem Entfernen:

```bash
sudo rm -r -- /etc/mumble-funk-audio /var/lib/mumble-funk-audio \
  /var/log/mumble-funk-audio /opt/mumble-funk-audio /var/backups/mumble-funk-audio
```

Dieser letzte Befehl löscht ausschließlich die ausdrücklich genannten Preview-Daten
unwiderruflich; eine nicht vorhandene Backup-Datei/ein nicht vorhandenes Verzeichnis
kann eine harmlose Fehlermeldung verursachen. Für den normalen Test ist er nicht
nötig. Die Verzeichnisse ohne `-audio` gehören zu v0.2 und dürfen stehen bleiben.

## Trennung der Instanzen

| Bereich | Standard-Mumble | Mumble-Funk v0.2 | Audio-Preview v0.3 |
| --- | --- | --- | --- |
| Port | 64738 | 64739 | **64740** |
| Dienst | Bestehender Dienst | `mumble-funk` | `mumble-funk-audio` |
| Konfiguration | Bestehend | `/etc/mumble-funk/` | `/etc/mumble-funk-audio/` |
| Datenbank/Zertifikat | Bestehend | `/var/lib/mumble-funk/` | `/var/lib/mumble-funk-audio/` |
| Logs | Bestehend | `/var/log/mumble-funk/` | `/var/log/mumble-funk-audio/` |
| Programmstände | Bestehend | `/opt/mumble-funk/` | `/opt/mumble-funk-audio/` |
| Systembenutzer | Bestehend | `mumble-funk` | `mumble-funk-audio` |

Die Preview-Dienstdatei verwendet dieselben bewährten systemd-Schutzoptionen wie
v0.2, aber ausschließlich eigene Verzeichnisse. Der tatsächliche Systemstart auf
deiner Proxmox-VM bleibt ein Praxistest; Containerprüfungen ersetzen ihn nicht.
