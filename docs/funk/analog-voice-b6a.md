# B6A: technische Stabilisierung und experimenteller Installer

Ausgangspunkt `7aeb835c9b4290af0cc060ab933ff5d01981efb2` auf
`feature/analog-voice-live`, bestehender Draft-PR #15. Zwischenstand: vollständige
Abnahme läuft noch. Keine Produktivfreigabe, kein Merge, keine Installation auf
Proxmox/VPS und kein veröffentlichtes Docker-Image. B6B bleibt unbegonnen.

## Nachgewiesene Ursachen und Korrekturen

### ACL-Cache beim Serverabbau

Der alte Destruktor rief `clearACLCache()` auf. Diese Laufzeitfunktion löscht
Cachewerte, ruft danach aber `flushClientPermissionCache` und Speak-Prüfungen auf.
Damit entstehen für noch authentifizierte Benutzer neue `ChanCache`-Objekte.
Die spätere Zerstörung des QHash mit rohen Zeigerwerten gibt diese Objekte nicht
frei. Genau diese Allokationskette steht im bisherigen 224-Byte-LSan-Bericht.

Nach Stop von UDP, Timern und Worker löscht der Destruktor nun ausschließlich die
besessenen Cachewerte unter `qmCache`, ohne neue Berechtigungsberechnungen oder
Clientbenachrichtigungen anzustoßen. Die Laufzeitfunktion mit ihrer Invalidation,
Sperrreihenfolge und Rechteaktualisierung bleibt unverändert. Der neue Sockettest
`shutdownWithPermissionCache` füllt echte Berechtigungsantworten für angemeldete
Clients und verlangt geordneten Exit 0, auch unter LeakSanitizer.

### CryptographicHash

`CryptographicHash` allokierte `CryptographicHashPrivate`, besaß jedoch keinen
Destruktor. Dadurch wurde weder der private Kontext noch sein EVP_MD_CTX freigegeben.
Ein unique_ptr mit außerhalb des Headers definiertem Destruktor besitzt ihn nun
eindeutig. Kopien mit gemeinsamem rohem Eigentümer sind damit ausgeschlossen.
Die Hashberechnung und Fehlerzustände bleiben identisch. Der zusätzliche Test
zerstört wiederholt sowohl unfertige als auch finalisierte SHA1-/SHA256-Kontexte;
bestehende Hashvektoren und wiederholtes result/addData bleiben geprüft.

### OCB2

XOR dereferenzierte `subblock*` auf externen Plaintext-/Ciphertext-Bytepuffern,
obwohl diese keine 32-/64-Bit-Ausrichtung garantieren. XOR liest und schreibt nun
je Subblock über memcpy in ausgerichtete lokale Werte; auch In-place-Aufrufe
bleiben gültig. S2/S3 arbeiten weiterhin auf ausgerichteten internen keyblocks.
Typisierte Casts der externen Puffer entfallen. Zusätzlich berechnet EVP Final
seinen Ausgabecursor in Bytes: der bisherige Ausdruck `(dst)+outlen` konnte bei
einem subblock-Pointer bereits einen ungültigen Pointer jenseits des Blocks bilden,
auch wenn Final bei abgeschaltetem Padding keine Bytes schreibt. Der Algorithmus,
Nonceverarbeitung, Tags und Testvektoren werden nicht geändert.
Der neue Test verschlüsselt und entschlüsselt mit acht Pufferversätzen und
mehreren Block-/Teilblocklängen und prüft weiterhin Replay-Ablehnung.

### TCP-Tunnelheader

Die bisherigen quint16-/quint32-Stores über Casts wurden durch die byteorientierte
qToBigEndian-Überladung ersetzt. `writeUDPTunnelHeader` schreibt genau denselben
Typ 1 und die vier Byte Payloadlänge in denselben sechs Byte Header. Der Test
prüft alle acht Pufferversätze gegen `000112345678` sowie die Grenze zum Payload.
Netzwerkformat, Clients und Routing bleiben unverändert.

## Sporadische Empfangsfehler

Die produktiven 40-ms-Alters-/Queuegrenzen und die Kanalbarrieren bleiben unverändert.
Der 120-ms-Profiltest hatte bereits Serverausgabe bei Empfangsfehlern. Der
Überlasttest gibt jetzt auch vorzeitige Empfangsfehler mit Testphase, TCP-Transport,
20-ms-Eingang und vollständigem Serverlog einschließlich vorhandener Dienstzähler
und Shutdownstatus aus. Es wurden keine Prüfungen entfernt oder Timeouts erhöht.

Wiederholungen und Schlussfolgerungen folgen nach Abschluss. Ein nicht
reproduzierter Fehler gilt nicht als behoben. Als mögliche Einflussgröße bleibt
das produktive Jobalter unter Compiler-/Tracinglast zu untersuchen; dies ist noch
keine nachgewiesene Diagnose der B5-Zwischenläufe.

## Installer

`--experimental-analog-voice` wird durch setup.sh, build.sh und install.sh gereicht.
Nur der exakte Branch feature/analog-voice-live wird dafür akzeptiert. Mumble-
Abstammung, offizieller Origin, saubere Quellen, keine unversionierten Ergänzungen,
lokaler Remote-Ref und live publizierter Commit werden weiterhin geprüft. Andere
Opt-ins erlauben den Analogbranch nicht; der neue Schalter erlaubt keine anderen
Branches. BUILD-INFO kennzeichnet Branch/Revision und experimental=true.

INI, Datenbank, WAVs, Port, Zugangsdaten, Sicherungen und Release-/Previous-Struktur
verwenden das bestehende Installationskonzept. Analogvoice wird nicht aktiviert.
Die Stagingtests prüfen einen Dev-Vorstand, ein neues Binary, eigene WAVs, SQLite,
einen individuellen Port, unveränderte INI und das Previous-Ziel. Die Setup-Mocks
prüfen zusätzlich die unveränderte Installation nach fehlgeschlagenem Build/Test.
Echte externe Installation wurde nicht durchgeführt.

[Anleitung für den Betreiber](install-analog-voice-test.md) und
[B6B-Übergabe](analog-voice-b6b-handoff.md).

## Tatsächliche Abnahme

Noch laufend; endgültige Ergebnisse werden vor dem Abschluss nachgetragen.
Die bisherigen B5-Ergebnisse bleiben als historische Befunde dokumentiert.
Externe Qt-/OpenSSL-/libopus-Bibliotheken sind lokal nicht selbst instrumentiert.

## Unveränderte Grenzen

Keine Audioarchitektur, DSP-Presets, Audioassets, Worker-/Encoderanzahl,
Queuegrenzen, Netzwerkports oder 300-ms-Close-Frist wurden verändert. Der
Unix-SIGTERM-Notifier bleibt erhalten. Serverübergabe beweist keine tatsächliche
Client-Wiedergabe. Reale Hör-/Last-/2-GB-VPS-Abnahme, TLS-Sättigung,
Sessionreuse und die moderne Decodergrenze bei leerem Raw-Opus bleiben B6B-Themen.

### Weitere durch striktes UBSan nachgewiesene Altfehler

`halt_on_error=1` machte zwei Null-Längen-Kopien sichtbar: OCB2 erhielt bei leerem
Klartext nullptr und übergab ihn an memcpy mit Länge 0; PacketDataStream::append
machte dasselbe bei leerem Audio. Die Kopien werden für Länge 0 jetzt ausgelassen.
Auch das Auffüllen eines Streams mit Kapazität 0 vermeidet memset(nullptr,0,0);
sein vorhandener Overshoot-/Fehlerzustand bleibt erhalten. Ein Nullkapazitäts-
Regressionstest prüft leeres Append sowie korrekt abgewiesenes nichtleeres Append.

Der nächste strikte Lauf wies unaligned uint64-Zugriffe in Legacy-Pings nach
(MumbleProtocol.cpp, Timestamp an Offset 4). Die Kodierung und beide
Dekodierpfade verwenden nun memcpy für den opaken Timestamp und byteorientierte
qTo/FromBigEndian-Aufrufe für die übrigen Felder. Die bisherige Timestamp-
Bytereihenfolge wird ausdrücklich nicht konvertiert. Die bestehenden Pingtests
prüfen jetzt zusätzlich Puffer mit um ein Byte versetztem Beginn. Diese kleinen
Erweiterungen außerhalb der anfänglich vier Stellen sind durch konkrete UBSan-
Stacks begründet; keine Änderung des Paketformats oder der Audioarchitektur.

Ein weiterer strikter UBSan-Stack zeigte den Überlauf im Metadatenindex für
`INVALID_DB_ADJUSTMENT` (INT_MAX): Der alte Code subtrahierte einen negativen
Bereichsanfang bereits vor seiner Sentinel-Prüfung. Der Index wird nun in int64
berechnet. Gültige vorcodierte dB-Werte und der bisherige Fallback bleiben
identisch; es entsteht keine neue Lautstärkefunktion.

Der erste lokale Dockerlauf bestand die Teststufe, scheiterte aber beim
Runtime-COPY der Lizenzdatei an `no space left on device` unter vfs. Die drei
Lizenz-COPYs werden deshalb zu einer Kopierschicht zusammengefasst: dieselben
Dateien werden im Build nach /out/doc zusammengetragen und unter denselben
/usr/share/doc/mumble-funk-Pfaden ausgeliefert. Runtime-UID, Konfiguration und
Aktivierung bleiben unverändert. Dies ist eine gezielte Reaktion auf den
nachgewiesenen temporären Speicherbedarf, keine neue Docker-Betriebsarchitektur.
