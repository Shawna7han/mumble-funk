> **Einordnung:** Simplex ist auch im aktuellen Dev mit den integrierten
> Audiofunktionen enthalten. Die historischen Branch-/Updatebefehle unten sind
> kein Dev-Installationsweg. Für Dev [install-dev.md](install-dev.md) mit
> `--experimental-dev` verwenden; bestehende INIs behalten ihre Werte.

# Simplex v0.2: aktualisieren, einschalten und testen

Unser Funkmodus erlaubt **einen Sender je Mumble-Kanal**. Der erste berechtigte
Sender, dessen Sprachpaket der Server akzeptiert, belegt den Kanal. Andere Sender
werden verworfen. Verschiedene Kanäle können gleichzeitig benutzt werden.
Mumla und Windows-Mumble brauchen weder Änderungen noch Zusatzsoftware.

**Der Kern und der manuelle Installationsweg sind standardmäßig aus.** Ein Update
allein schaltet den Modus nicht ein. Die neue [automatische Installation](setup.md)
aktiviert Simplex nur bei einer neuen Konfiguration mit 500/500 ms. Die manuelle
Aktivierung unten verwendet weiterhin die konservativeren 500/1000 ms.
Es gibt keine neuen Klangeffekte und keine Speicherung von Sprachinhalten.
Alle folgenden Befehle betreffen ausschließlich unseren Dienst `mumble-funk`
auf **64739 TCP/UDP**, nicht `mumble-server` auf 64738.

## 1. Vorhandene Installation aktualisieren

Melde dich per SSH an deiner Ubuntu-VM an. Verwende deinen normalen Benutzer mit
sudo-Rechten. Die vorhandenen Abhängigkeiten reichen weiterhin aus; es kommen
keine Pakete hinzu. Ubuntu 24.04 und 26.04 bleiben unterstützt.

```bash
cd "$HOME/src/mumble-funk"
git status --short --branch
```

Falls du das Projekt anderswo gespeichert hast, verwende diesen Ordner. Unter der
Zweigzeile sollen keine eigenen Änderungen stehen. Bei eigenen Änderungen oder
Fehlern anhalten, diese aufbewahren und nichts zurücksetzen oder löschen.
Führe die folgenden Befehle nacheinander aus; nach einem Fehler nicht weitergehen:

```bash
git switch funk-v0.1
git pull --ff-only origin funk-v0.1
git submodule update --init --recursive
bash scripts/funk/build.sh
```

Das Skript kompiliert den Server und führt **alle 18 Servertestprogramme** aus.
Erwartet: `100% tests passed, 0 tests failed out of 18`.
Es nutzt vorhandene Build-Ergebnisse. Bei wenig RAM stattdessen:
`FUNK_BUILD_JOBS=1 bash scripts/funk/build.sh`.
Der bisherige Dienst läuft währenddessen weiter. Tests laufen in eigenen
Verzeichnissen und auf freien lokalen Ports, mit simulierten Testclients.

Nur nach erfolgreichem Build und Test installieren:

```bash
sudo bash scripts/funk/install.sh
sudo systemctl is-active mumble-funk
sudo systemctl is-enabled mumble-funk
```

Erwartet werden `active` und `enabled`. Das Installationsskript erhält deine
Konfiguration, Kanäle, Benutzer, Datenbank und das Zertifikat. Es sichert vor dem
Austausch Datenbankordner und Konfiguration unter `/var/backups/mumble-funk/`.
Nur der Funkdienst wird kurz neu gestartet. Der Standardserver bleibt in Betrieb.
Für eine Erstinstallation gilt die [Ubuntu-Anleitung](installation-ubuntu.md).

Tests später ohne erneuten Build wiederholen:

```bash
ctest --test-dir build/funk --output-on-failure
```

## 2. Simplex aktivieren

Eine vorhandene INI-Datei wird beim Update absichtlich nicht überschrieben.
Ergänze die neuen Einstellungen deshalb einmalig:

```bash
sudo cp -an /etc/mumble-funk/server.ini /etc/mumble-funk/server.ini.before-simplex
sudo nano /etc/mumble-funk/server.ini
```

Der Sicherungsbefehl überschreibt keine bereits vorhandene Sicherung. Füge die
folgenden Zeilen hinzu. Falls eine Einstellung bereits **ohne `#`** existiert,
ändere diese Zeile, statt sie ein zweites Mal anzulegen. Zeilen mit `#` sind nur
Kommentare und schalten nichts ein.

```ini
simplex=true
simplextimeout=500
simplexretrytimeout=1000
simplexdiagnostics=true
simplexlogfile=/var/log/mumble-funk/simplex
simplexlogmaxsize=10485760
simplexloggrants=true
simplexlogdenials=true
simplexlogreleases=true
```

In nano: `Strg+O`, Eingabetaste, `Strg+X`. Anschließend:

```bash
sudo systemctl restart mumble-funk
sudo systemctl --no-pager --full status mumble-funk
sudo tail -n 60 /var/log/mumble-funk/server.log
sudo tail -n 10 /var/log/mumble-funk/simplex.1.jsonl
```

Im Serverlog muss beim **aktuellen Start** die Meldung
`Simplex enabled: current channel only; timeout=500 ms, retry=1000 ms` erscheinen
(mit zusätzlichem Hinweis auf den nötigen Neustart). Die Diagnosedatei enthält
`simplex_log_started`. Ein altes `granted` im Log allein beweist nicht, dass der
Modus im aktuellen Prozess aktiv ist. Danach mit beiden Geräten neu verbinden.

Die bestehende PTT-Diagnose kann unabhängig davon eingeschaltet bleiben:

```ini
voicediagnostics=true
```

Sie protokolliert weiterhin **eingehende** Sprachpakete, auch wenn Simplex sie
anschließend verwirft. Ihre Datei `voice.1.jsonl` ist daher kein Nachweis, dass ein
Empfänger diese Pakete erhalten hat. Beide Diagnosefunktionen zeichnen nur
Metadaten auf, darunter Benutzernamen; keine Sprache.

## 3. Was die Einstellungen bedeuten

| Einstellung | Standard ohne eigenen Eintrag | Bedeutung |
| --- | --- | --- |
| `simplex` | `false` | Funkmodus ein/aus |
| `simplextimeout` | `500` | Millisekunden ohne gültiges neues Paket des Besitzers bis zur Kanalfreigabe |
| `simplexretrytimeout` | `1000` | Millisekunden Paketruhe dieses Benutzers, ab denen ein neuer Versuch ohne Ende-Markierung angenommen wird |
| `simplexdiagnostics` | `false` | Eigene Diagnose der Simplex-Entscheidungen |
| `simplexlogfile` | `simplex` | Dateipräfix; für unseren Dienst unbedingt den oben gezeigten absoluten Pfad verwenden |
| `simplexlogmaxsize` | `10485760` | Größe je Logdatei in Bytes, mindestens 4096 |
| `simplexloggrants` | `true` | Vergabe protokollieren |
| `simplexlogdenials` | `true` | Ablehnung und deren zusammengefasstes Ende protokollieren |
| `simplexlogreleases` | `true` | Freigabe protokollieren |

Alle Änderungen erfordern einen **Neustart des Funkdienstes**. Zeiten werden auf
50 bis 60.000 ms begrenzt; die Paketruhe für einen neuen Versuch ist immer
mindestens so lang wie der Belegungstimeout. Beginne mit 500/1000 ms. Die
PTT-Diagnose hat ihren eigenen, hiervon unabhängigen Timeout.

An das Dateipräfix wird `.1.jsonl` angehängt (1 = virtuelle Servernummer).
Andere virtuelle Server erhalten ihre eigene Nummer. Relative Pfade beziehen
sich auf den Ordner der INI-Datei; unser Dienst darf dort keine Logs schreiben.
Die Datei rotiert ab etwa 10 MiB, mit einer Sicherung `simplex.1.jsonl.1`.
Die Zeilen enthalten Kanalnummer, Benutzer, Verbindung, Transportweg und monotone
Zeit in Mikrosekunden. Diese Zeit ist für Abstände innerhalb eines Laufs bestimmt;
`run` trennt Serverläufe, der Startdatensatz enthält zusätzlich die UTC-Uhrzeit.

## 4. Verhalten und bewusste Grenzen

- Während der berechtigte Sender weiter Pakete liefert, bleibt der Kanal belegt,
  auch bei Stille oder stummgeschaltetem Mikrofon. Der Server wertet keinen Klang aus.
- Eine gültige Ende-Markierung gibt das Senderecht frei. Fehlt sie, erfolgt die
  Freigabe nach 500 ms ohne gültige neue Sprachpakete, zuzüglich möglicher
  Verzögerung der Ereignisschleife. Ein neuer Konkurrent prüft den
  abgelaufenen Timeout ebenfalls beim Eintreffen seines Pakets.
- **Ein abgewiesener Sender bleibt bei fortlaufenden Paketen stumm**, auch nachdem
  der bisherige Besitzer losgelassen hat. Er muss seine Aussendung beenden und
  einen neuen Versuch beginnen. Es gibt keine Warteschlange.
- Ohne Ende-Markierung gilt erst mindestens 1000 ms Paketruhe als neuer Versuch.
  Praktisch: **PTT loslassen, etwa zwei Sekunden warten, neu drücken.** Das ist
  insbesondere für die bisher getestete Mumla-Version wichtig. Sehr schnelles
  erneutes Drücken kann verworfen werden oder als Fortsetzung gelten. Mit gültigen
  Markern und steigenden Sequenznummern ist ein unmittelbarer neuer Versuch möglich.
- Setzt ein Client seine Sequenznummer ohne ausreichende Paketruhe zurück, werden
  seine Pakete vorsichtshalber verworfen, bis ein verlässlicher neuer Versuch
  erkennbar ist. Alte oder doppelte Pakete verlängern kein Senderecht.
- **Der Server kennt die physische PTT-Taste nicht.** Eine lange Netzunterbrechung
  und tatsächliches Loslassen mit erneutem Drücken sind nicht sicher unterscheidbar.
  Nach mindestens 1000 ms Paketruhe kann deshalb auch ein noch gedrückter Sender
  erneut zugelassen werden. Beliebig verspätete Pakete nach einer solchen Pause
  sind ebenfalls nicht vollständig von einer neuen Aussendung unterscheidbar.
  Ein verlorener Marker wird nicht erfunden; das Ende bleibt eine Timeout-Vermutung.
- Beim Kanalwechsel wird der alte Kanal frei. Im neuen Kanal ist ein neuer Versuch
  erforderlich. Ebenso bei verlorenem Senderecht durch Stummschaltung oder ACL:
  Die spätere Rückgabe der Berechtigung macht eine laufende Aussendung nicht hörbar.
- Beim erkannten Verbindungsabbruch wird sofort freigegeben. Bei einem unbemerkten
  Netzausfall greift zuerst der Sprachpaket-Timeout. Ein Neustart löscht alle Sperren.
- Im Funkmodus wird ausschließlich an Mitglieder des **aktuellen Kanals** gesendet.
  Whisper, Kanalverknüpfungen, zusätzliche Kanal-Abonnements und Server-Loopback
  übertragen keine Sprache über alternative Wege. Die zugehörigen Clientanzeigen
  und gespeicherten Einstellungen können trotzdem bestehen bleiben. Bei
  `simplex=false` gelten wieder die normalen Mumble-Sprachwege.
- Unveränderte Clients zeigen ein abgewiesenes Senden nicht zuverlässig als
  „besetzt“ an. Eine leuchtende Sendeanzeige beweist keine Weiterleitung.
  Bereits unterwegs befindliche Pakete und Wiedergabepuffer der Empfänger können
  beim Senderwechsel noch kurz hörbar sein; der Server steuert die Weiterleitung.

## 5. Diagnose lesen

```bash
sudo tail -F /var/log/mumble-funk/simplex.1.jsonl
```

`Strg+C` beendet nur die Anzeige. `-F` folgt auch einer Dateirotation.

| Ereignis bzw. Grund | Bedeutung |
| --- | --- |
| `granted` | Senderecht vergeben |
| `denied`, `busy` | Versuch wegen belegtem Kanal abgewiesen |
| `denied`, `retry_required` | Fortsetzung einer gesperrten Aussendung |
| `denied_end` | Ende eines abgewiesenen Versuchs, mit zusammengefasster Paketanzahl |
| `released`, `marker_confirmed` | Ende-Markierung empfangen und akzeptiert |
| `released`, `timeout_suspected` | Ohne gültige neue Pakete freigegeben; Ende nur vermutet |
| `released`, `channel_changed` / `disconnected` | Kanalwechsel / Verbindung beendet |
| `released`, `permission_lost` | Sendeberechtigung verloren |
| `released`, `not_eligible` | Paket nicht mehr zulässig, z. B. Bandbreitenlimit |
| `unsupported_target` | Verbotener alternativer Sprachweg |
| `sequence_not_advanced` | Sequenznummer passt nicht zu einem neuen Versuch |
| `quiet_rearm` | Ausreichende Paketruhe beendet die Sperre des alten Versuchs |
| `server_stop` | Freigabe beim Herunterfahren |
| `diagnostic_loss` | Logeinträge gingen verloren; `dropped_total` nennt die Anzahl |

Es wird nicht für jedes verworfene Paket eine Zeile geschrieben. Die erste
Ablehnung und ihr Ende werden zusammengefasst. Die Diagnose schreibt aus einem
eigenen Hintergrundthread mit begrenzter Warteschlange. Bei Überlast können
Diagnosezeilen fehlen; das ändert die Senderechte nicht. Bei einem Dateifehler
erscheint eine Warnung im Serverlog, während Simplex weiter arbeitet.

## 6. Praxistest mit Android und Windows

Beide Geräte verbinden sich mit **64739**, benutzen PTT und zunächst denselben
Kanal. Für vergleichbare Ergebnisse Sprachaktivierung und dauerhaftes Senden
abschalten. Ohrhörer vermeiden Rückkopplungen. Für jeden Teiltest zuerst beide
PTT-Tasten loslassen und zwei Sekunden warten. Parallel das Simplex-Log anzeigen.

1. **Android allein:** Windows hört zu. Auf Android PTT drücken, fünf Sekunden
   sprechen, fünf Sekunden schweigen, wieder sprechen und loslassen. Windows hört
   beide Sprachabschnitte; im Log bleibt dasselbe Senderecht während der Pause
   bestehen. Nach Loslassen folgt Marker oder Timeout. Bei eurer bisher getesteten
   Mumla-Version ist `timeout_suspected` zu erwarten.
2. **Windows allein:** Rollen tauschen. Erwartung wie oben. Bei eurer bisher
   getesteten Windows-Version ist `marker_confirmed` zu erwarten.
3. **Besetzter Kanal und keine automatische Übernahme:** Android beginnt zu senden.
   Nach zwei Sekunden drückt Windows ebenfalls PTT und hält sie gedrückt. Android
   lässt nach weiteren drei Sekunden los. Windows hält weiterhin fünf Sekunden
   gedrückt und spricht. Android darf Windows dabei **nicht plötzlich hören**.
   Im Log: Android `granted`, Windows `denied`, Android `released`, aber kein neues
   `granted` für Windows während dessen fortlaufender Aussendung. Windows lässt los,
   wartet zwei Sekunden und drückt erneut: Jetzt hört Android Windows.
   Danach denselben Ablauf mit Windows als erstem Sender wiederholen.
4. **Sehr kurze PTT-Pausen:** Nach einem abgewiesenen Android-Versuch PTT nur kurz
   loslassen und sofort wieder drücken. Ohne Marker kann Android stumm bleiben.
   Dann loslassen, zwei Sekunden warten und erneut drücken: Das muss bei freiem
   Kanal funktionieren. Windows mit bestätigtem Marker ebenfalls prüfen.
5. **Fast gleichzeitig:** Auf „drei“ beide PTT-Tasten drücken. Im Log darf nur eine
   Vergabe pro Kanal erscheinen. Beide loslassen, zwei Sekunden warten, mehrmals
   wiederholen. Welcher Sender gewinnt, ist nicht vorgegeben.
6. **Verbindungsabbruch:** Android beginnt; während PTT gedrückt ist WLAN/Mobilfunk
   abschalten. Nach spätestens Timeout plus Netz-/Anzeigeverzögerung muss ein
   frischer Windows-Versuch den Kanal belegen können. Windows vorher loslassen und
   für den neuen Versuch erneut drücken. Android danach wieder verbinden.
7. **Kanalwechsel:** Zwei Testkanäle im Windows-Client anlegen (dafür sind
   Administratorrechte nötig). Während Windows sendet, in den zweiten Kanal wechseln.
   Android muss den alten Kanal mit einem frischen Versuch belegen können. Windows
   im neuen Kanal loslassen, zwei Sekunden warten und erneut drücken.
8. **Neustart:** Während Android sendet, auf Ubuntu
   `sudo systemctl restart mumble-funk` ausführen. Beide Clients wieder verbinden.
   Nach Loslassen und neuem Versuch muss der Kanal frei nutzbar sein.

**Für einen eindeutigen Hörnachweis beider konkurrierender Sender ist eine dritte
Verbindung empfohlen.** Zwei gleichzeitig sendende Geräte sind keine unabhängigen
Beobachter. Eine weitere unveränderte Mumble-Instanz auf einem zweiten Computer
oder ein zweites Smartphone kann mit Namen `Beobachter` in denselben Kanal gehen,
Mikrofon stummschalten und mit Kopfhörern zuhören. Auf Windows kann auch
`mumble.exe -m` eine weitere Instanz starten; dafür im Installationsordner eine
Eingabeaufforderung öffnen. Die zweite Instanz nur zum Hören benutzen.

Wiederhole Test 3 und 5 mit diesem Beobachter: Er darf nur den gewählten Sender
hören und nach dessen Ende nicht die weiter gedrückte, abgewiesene Aussendung.
Für zwei unabhängige Kanäle sind vier Verbindungen (je Sender und Empfänger) am
aussagekräftigsten. Ohne diese Geräte liefern die automatisierten Tests den
Nachweis der Paketweiterleitung; die Simplex-Logs zeigen Entscheidungen, aber
keine garantierte Ankunft beim Endgerät.

Notiere bei jedem Versuch Clientversion, Senderreihenfolge, gedrückte Tasten,
ungefähre Wartezeiten und Beobachtung. Sichere dazu beide JSONL-Dateien nach dem
Test. Für Vergleiche mit Standardverhalten Simplex wie unten abschalten.

## 7. Abschalten oder vorherige Version wiederherstellen

Zum Abschalten in `sudo nano /etc/mumble-funk/server.ini` setzen:

```ini
simplex=false
simplexdiagnostics=false
```

Danach `sudo systemctl restart mumble-funk`. Konfiguration und Datenbank bleiben
erhalten. Die PTT-Diagnose kann weiterhin eingeschaltet sein.

Falls das neue Programm Probleme macht, zuerst prüfen, ob die vorherige Version
vorhanden ist:

```bash
sudo test -x /opt/mumble-funk/previous/mumble-server && echo "Vorherige Version vorhanden"
```

Nur wenn die Bestätigung erscheint, zurückschalten:

```bash
sudo bash -c 'set -e; systemctl stop mumble-funk; ln -sfn "$(readlink /opt/mumble-funk/previous)" /opt/mumble-funk/current.rollback; mv -Tf /opt/mumble-funk/current.rollback /opt/mumble-funk/current; systemctl start mumble-funk'
sudo systemctl --no-pager --full status mumble-funk
```

Dies wechselt nur das Programm. Datenbank und Servereinstellungen bleiben erhalten.
Simplex v0.2 ändert kein Datenbankformat. Ältere Versionen kennen die neuen
Simplex-Einstellungen nicht; für eine eindeutige Konfiguration diese auf `false`
setzen bzw. auskommentieren. Keinen Datenbankordner löschen oder neu anlegen.

## Testnachweis und technische Hintergründe

Der genaue ausgeführte Prüfstand steht in [SIMPLEX-STATUS.md](SIMPLEX-STATUS.md).
Die Synchronisierung ist in [MurmurLocking.md](../dev/MurmurLocking.md) beschrieben.
Die automatisierten Clients prüfen Paketweiterleitung, nicht den Klang eines
Android-/Windows-Geräts. Der obige Praxistest bleibt deshalb nach dem Update nötig.
