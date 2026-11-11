# Tests der Ubuntu-Installation

## Experimentelle Rausch-Testinstallation (B2c-1)

```bash
python3 tests/funk-install/test_source.py
python3 tests/funk-install/test_setup.py
python3 tests/funk-install/test_radio_noise_install.py build/funk/mumble-server
```

Die Quelltests verwenden echte wegwerfbare Git-Repositories mit lokalen Bare-
Remotes statt GitHub-Schreibzugriffen. Sie prüfen den eigenen Opt-in-Schalter,
exakten lokalen/gecacheten/veröffentlichten Commit, Herkunft, Mumble-Basis und
unveröffentlichte Dateien. Orchestrierungstests prüfen die Weitergabe des Schalters
über Setup, Build und Install sowie Bestandsschutz; apt/systemd/Build sind dabei
Mocks. Der neue Staging-Test verwendet das echte vorhandene Serverbinary: frische
Installation mit bytegenauer CC0-Standardaufnahme und automatisch aktiviertem
Rauschen bei 20 %, danach Erhalt bestehender INIs (aktiv/deaktiviert/fehlende
Schlüssel), SQLite und eigener WAVs. Konflikte am neuen Paketpfad einschließlich
symbolischer Links werden vor dem Überschreiben abgewiesen. Es wird kein echter
Dienst verändert.
Der normale Server-CI-Workflow führt ihn ebenfalls aus. Das ersetzt weder eine
vollständige Ubuntu-VM-Installation noch Hörtests; Anleitung: [radio-noise.md](../../docs/funk/radio-noise.md).

Die Installationstests laufen getrennt von den 18 CTest-Programmen des Servers:

```bash
python3 tests/funk-install/test_install.py build/funk/mumble-server
python3 tests/funk-install/test_setup.py
bash -n scripts/funk/*.sh
shellcheck scripts/funk/*.sh
```

`shellcheck` ist ein optionales Entwicklungswerkzeug (`sudo apt install shellcheck`).
Die Tests benötigen keine root-Rechte und installieren keinen dauerhaften Dienst.
Sie prüfen fünf Fälle:

1. Wiederholung erhält eigene Einstellungen und Datenbank sowie Dateien einer
   nachgebildeten Standardinstallation. Programmdatei und Dateirechte stimmen.
2. Ein Versionswechsel behält das vorige Programm für die Rückkehr vor; eine
   Wiederholung derselben Installation verändert diesen Verweis nicht.
3. Eine falsche Programmversion sowie `/` als Testziel werden abgewiesen.
4. Zwei echte Serverprozesse starten parallel auf TCP/UDP 64738 und 64739 mit
   getrennten SQLite-Datenbanken und Logdateien. Nur beim zweiten ist die Diagnose
   aktiviert. Zusätzlich ist nur auf 64739 Simplex mit eigener Diagnose aktiv;
   die JSONL-Datei und Startmeldungen werden geprüft. Beide Prozesse werden
   anschließend beendet und Testdaten entfernt.
5. Die Betriebssystemprüfung erlaubt ausschließlich Ubuntu 24.04 und 26.04;
   ältere, neuere, andere und fehlende Kennungen werden abgewiesen (zehn Kombinationen).

Wenn einer dieser Ports schon belegt ist, wird der vierte Fall übersprungen.
Kein bestehender Prozess wird beendet. Netzwerkzugriff auf localhost muss in der
Testumgebung gestattet sein.

`install.sh --stage VERZEICHNIS SERVERBINARY` ist der intern verwendete Testmodus.
Er erzeugt ausschließlich eine Verzeichnisstruktur unter dem angegebenen Ziel,
legt keine Systembenutzer an und führt kein systemctl aus. Die erzeugten
Konfigurationspfade sind die späteren Produktivpfade, keine direkt ausführbare
Konfiguration für dieses Testverzeichnis.

Dieser Testmodus allein prüft weder systemd noch den Start beim Hochfahren.
Für den Produktivbetrieb gilt die [Ubuntu-Anleitung](../../docs/funk/installation-ubuntu.md).


## Automatisches Setup

`test_setup.py` prüft 16 zusätzliche Fälle. Es verwendet echte temporäre
Konfigurationsdateien und SQLite-Datenbanken; Paketmanager, Build-Werkzeuge und
systemd-Aufrufe werden für die Orchestrierungs- und Fehlerfalltests ausdrücklich
nachgebildet. Zu den Fällen zählen Paket-/Netzwerk-/Build-/Testfehler, Ressourcen,
Portkonflikte, fremde Dienste, Passworterhaltung, Datenbank-Overrides, frische
Startmeldungen und beide Listener. Der Socket-Test benötigt localhost-Zugriff.
Die Orchestrierungstests müssen als normaler Benutzer ausgeführt werden.
Ein eigener Test prüft die Verzeichnisrechte auch bei `umask 077`.

`ubuntu_container_check.sh` ist ausschließlich für frische wegwerfbare
Ubuntu-Docker-Container bestimmt. Es benötigt `--init --cap-add SYS_PTRACE`
in einem eigenen PID-Namensraum, damit root die Socket-Besitzer des
Dienstbenutzers erkennen kann. Ohne diese Berechtigung greift die sichere
Abweisung unbekannter Portbesitzer. Es installiert fehlende Build-Abhängigkeiten
über das echte Setup und baut/testet den echten Server als normaler Benutzer.
`mock_systemctl.py` startet den echten Server unter dem Dienstbenutzer, ersetzt
aber den systemd-Dienstmanager. Es ist **kein Nachweis von systemd-Supervision oder
VM-Startverhalten**. Der Prüflauf muss außerdem eine nur lesbare Quellkopie unter
`/input` und die öffentliche Proxy-CA unter `/run/proxy-ca.pem` erhalten, wenn die
Cloud-Umgebung diese benötigt. Niemals auf einer produktiven VM verwenden.

Die tatsächlich ausgeführten Ubuntu-Prüfungen werden in
[SETUP-STATUS.md](../../docs/funk/SETUP-STATUS.md) festgehalten.

## Codec-Version bei externen Docker-Proben

Die Protokollprobe vergleicht unter anderem kodierte Busy-Paketbytes. Dafür müssen
Probe und Testimage dieselbe Opus-Version verwenden. Der native Cloud-Build kann
eine andere Bibliothek laden als das Ubuntu-24.04-Image (beispielsweise 1.5.2
gegenüber 1.4); deren Bitströme müssen nicht bytegleich sein. In der GitHub-CI
werden beide auf Ubuntu 24.04 gebaut. Bei lokalen Docker-Prüfungen außerhalb
dieser Umgebung die Probe mit der zum Image passenden Bibliothek ausführen.
Die Paketvergleiche bleiben aktiv; weder Serververhalten noch Grenzwerte ändern.
