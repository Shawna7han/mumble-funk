> **Current Dev:** Squelch and timeout Close have been integrated since PR #4;
> PR #9 added the common Open/Noise/Close stream and continuous noise. The phase
> text below describes the earlier feature snapshot, including its old defaults.
> For current installation, defaults and administration see
> [Dev installation](install-dev.md), [audio defaults](audio-defaults.md),
> [operator commands](ubuntu-commands.md) and [radio noise](radio-noise.md).

# Squelch Phase 2C — experimental preview

**Neuer Nachfolgestand:** [Standard-Audioeinstellungen](audio-defaults.md) auf
`feature/audio-defaults` aktiviert Funk-Audio bei Neuinstallationen mit +8 dB.
Die branchspezifischen Defaults und Installationsoptionen unten dokumentieren den
vorherigen Entwicklungsstand; bestehende Installationen werden nicht migriert.

Für den experimentellen **Audio Boost (0–12 dB)** aus `feature/audio-volume`
siehe [Audio-Boost-Anleitung mit Update und Hörtest](audio-volume.md).
Die bisherigen branchspezifischen Anleitungen unten bleiben gültig.


This experimental preview is developed on `feature/squelch-audio`, based on dev
`f82f5aeda5111711b8e0c351e3f8b2400fc4a90a`. It is not a published release.
No existing server installation is updated by this work.

## Behaviour

With Simplex and audio feedback enabled, each eligible listener can receive Open
at the start of an accepted Opus transmission and Close after a confirmed end.
The sender receives neither of their own transmission's cues. Other channels,
linked-channel listeners and whispers do not receive these cues.

Voice packets are forwarded first, with their payloads, sender session, sequence
and end markers intact. Signals use the listener's **existing own session**, the
same private feedback mechanism already used for Busy. No bots or fabricated
sessions, client changes, additional audio ports or speech transcoding are used.
Signals travel over the existing TCP connection even when speech uses encrypted
UDP. Speech and the private effect have independent client decoders/jitter buffers.
Some overlap or a late Open is expected; this is not sample-synchronized mixing.

Busy keeps its waveform, file, gain, full packet sequence and one-second cooldown.
One scheduler owns each recipient's feedback sequence. Busy takes priority over
Squelch. An active optional cue ends at its next packet boundary; Busy follows a
short retirement gap (normally within about 80 ms). This avoids injecting Busy
into an unterminated cue. Queued Squelch for a recipient currently hearing Busy
is omitted. The existing Busy-only path retains its behaviour when Squelch is off.

Close requires an accepted transmission start. By default only a confirmed end
marker ends the signal lifecycle; the optional timeout policy below is an explicit
experimental exception. With Open enabled it also requires Open dispatch; in Close-only mode that start is
acknowledged silently, without sending an Open packet. Timeout with the fallback off, disconnect,
channel changes, deaf/mute/permission changes invalidate optional work. A new
transmission cancels the previous generation's unsent cues. Audio already buffered
on a client cannot be recalled. Late/congested cues are omitted, never speech.
No more than 128 pending requests per signal queue, 64 playback recipients and
128 lifecycle channel records are retained. Ended records are bounded but retained
until replacement/invalidation; this preview is not a large-scale capacity release.

## Assets and the 20-ms question

Original files in `assets/audio/` remain byte-identical, licensed under CC0 by the
project owner. No Internet samples or new third-party dependencies were added.

The existing Opus audio encoder has 312 samples (6.5 ms at 48 kHz) of lookahead.
The 897-sample Open therefore needs 1209 output samples to preserve its full tail:
two 20-ms packets, **40 ms**, remain necessary with this encoder configuration.
Merely sending the first packet would omit the final 249 original samples; their
energy is small but nonzero. We do not silently truncate them or change playback
speed. This 40-ms signal does **not** impose a 40-ms delay on forwarded speech.

Signal encoding now pads only the lookahead reported by Opus, instead of a whole
extra 20-ms input frame. The supplied Close uses **four packets / 80 ms**, reduced
from five packets / 100 ms. Busy encoding is unchanged. Runtime WAVs are encoded
once at startup, not loaded or encoded in the voice forwarding path.

## Configuration for the dedicated snapshot test VM

The owner manages Proxmox snapshots and authorizes any installation separately.
Codex must not reset or install the VM automatically. Other VMs stay untouched.
The target is the normal native layout: service `mumble-funk`, `/opt/mumble-funk`,
`/etc/mumble-funk/server.ini`, `/var/lib/mumble-funk`, `/var/log/mumble-funk`,
port **64739 TCP/UDP**. No new preview service/directory or port 64742 is needed.
Docker remains **host 64740 TCP/UDP → container 64739 TCP/UDP**; preserve custom
host ports, `.env`, project names, volumes, passwords and certificates.

Phase 2C adds explicit experimental installation and WAV management to the normal
installation. See the [German snapshot-VM installation and hearing-test guide](squelch-ubuntu.md).
`setup.sh`, `build.sh` and a direct `install.sh` accept this exact feature branch
only with **`--experimental-squelch`**. Tracked files must be clean, the official
origin and cached feature revision must match, and the actual published feature
head must equal HEAD. Other unapproved branches remain rejected. This is an opt-in,
not a Stable designation. Existing Stable/version checks are retained.

New native installs supply the two original WAVs and their CC0 notice under
`/usr/local/share/mumble-funk/audio/` (readable standards, mode 0644). The normal
service account can read custom imports in `/etc/mumble-funk/sounds/` (directory
root:mumble-funk 0750, files root:mumble-funk 0640). Configuration stays at
`/etc/mumble-funk/server.ini`; existing configuration is never rewritten by setup.
The release directory contains BUILD-INFO with revision, branch and experimental
status; `sudo mumble-funk-info` displays these along with connection information.
It displays the server password unless `--hide-password` is used; run it only
locally in your terminal and never paste that password into reports. Audio status never displays passwords.

New-install defaults (native and Docker):

```ini
audiofeedback=false
busytone=true
busytonefile=
busytonevolume=50
squelch=false
squelchopen=true
squelchclose=true
squelchclosetimeoutfallback=false
squelchopenfile=/usr/local/share/mumble-funk/audio/squelch-open.wav
squelchclosefile=/usr/local/share/mumble-funk/audio/squelch-close.wav
squelchopenvolume=50
squelchclosevolume=50
```

Server-level defaults for file paths remain empty if no installer/configuration
supplies them. Both switches default true, but the master Squelch switch defaults
false. Squelch still requires Simplex and master audio feedback. Root INI keys
belong before any section header, without duplicates. Missing/invalid **enabled**
files disable Squelch with a startup log; disabled signals need no valid file.
There is no synthetic Squelch fallback. Busy's existing fallback is unchanged.

## Optional timeout Close (Mumla 3.7.3)

The operator's first real hearing test reported working Open/Close from
Windows-Mumble, but only Open from Android Mumla 3.7.3. Diagnostics showed
`released / marker_confirmed` for Windows and `released / timeout_suspected` for
that Mumla transmission. This is a report about that device/configuration; it
is not proof that every Mumla version always omits a terminator.

New setting **`squelchclosetimeoutfallback=false`**, deliberately off by default.
With Simplex, master audio, Squelch and Close already enabled, opt in using:

```sh
sudo mumble-funk-audio enable-timeout-close
sudo mumble-funk-audio status
# Return to confirmed-marker-only Close:
sudo mumble-funk-audio disable-timeout-close
```

Docker uses `bash scripts/docker/audio.sh enable-timeout-close` and
`disable-timeout-close` in the existing Compose project. Modifications restart only
that installation as documented below. Existing INI files are not migrated;
a missing setting is false. This switch does not automatically enable any effect.

The actual Simplex release callback now preserves the already accepted, valid Opus
stream on a timeout **only** for this opt-in. It snapshots Close for that stream's
existing recipients and transmission generation; it does not create a new start,
recipient list, session or speech end marker. The last observed frame must match
the controller's released stream. Missing/unvalidated observations omit the effect.
Generation invalidation, recipient eligibility, Close-once reservation and queue
expiry still apply on dispatch and during playback. If Open was enabled but never
dispatched, no Close is played. Close-only mode acknowledges the valid start silently.
A new accepted transmission supersedes unsent old Close; disconnect, channel
out-and-back, mute/deaf/permission changes and connection/session replacement still
cancel pending work. Already transmitted client-buffered sound cannot be recalled.

Confirmed markers keep their existing immediate post-forwarding path, without
waiting for timeout. Already ended streams and repeated expiry do not produce a
second Close. With this setting off the previous timeout invalidation remains.
The callback does not acquire the voice lock or call back into Simplex: only the
existing short planner metadata lock and bounded try-lock enqueue are used. No
encoding, file IO or network delivery occurs in that callback. Normal audio dispatch
revalidates identities under the existing voice lock; Busy retains its priority.

**A timeout proves packet silence, not release of a physical PTT button.** Close
will be delayed by the existing `simplextimeout` (normally 500 ms), the timer and
optional audio scheduling. A long gap while PTT is still held, packet loss or
network interruption can trigger it too. The Simplex diagnostic reason stays
`timeout_suspected`; nothing is relabeled `marker_confirmed`. The existing retry
rules may still block further speech until the client rearms. No Simplex timer,
retry rule, speech payload, sequence, marker or forwarding delay is changed.
Optional Close may be omitted under congestion or Busy; this is not a guaranteed
PTT-release notification. The changed fallback still needs a new device hearing test.

## Normal native audio management

Every modifying command validates input, saves `server.ini.before-audio-change`,
updates only the relevant audio keys and restarts **only mumble-funk**. Status is
read-only. Service identity and the installer lock are checked. A failed restart
attempt restores the old configuration and attempts to start it again; check the
service/log if it still cannot start. The backup contains private configuration,
retains its permissions and must not be shared publicly.

| Command after `sudo mumble-funk-audio` | Result |
| --- | --- |
| `status` | Audio settings only; no password |
| `enable-squelch` / `disable-squelch` | Squelch on/off; enabling also enables master audio feedback and requires Simplex already enabled |
| `enable-open` / `disable-open` | Open independently on/off |
| `enable-close` / `disable-close` | Close independently on/off |
| `enable-timeout-close` / `disable-timeout-close` | Experimental timeout Close opt-in; leaves master/Squelch/Busy switches unchanged |
| `open-volume 30` / `close-volume 40` | Independent gain, integer 0–100; 0 is silent |
| `set-open /absolute/file.wav` / `set-close /absolute/file.wav` | Validate and import a custom cue |
| `reset-open` / `reset-close` | Select the shipped standard again |
| `set-busy-tone /absolute/file.wav` / `reset-busy-tone` | Existing custom Busy / synthetic Busy fallback |
| `volume 50` | Busy volume |
| `boost 6` / `boost 0` | Shared effect boost, integer 0–12 dB; 0 preserves previous gain |
| `enable` / `disable` | Master feedback switch, including Busy and Squelch |

Enabling an individual cue does not turn on master Squelch. Disabling Squelch
leaves Busy and master feedback untouched. `enable-squelch` can make an already
configured Busy tone audible too; use `busytone=false` in the configuration if
intentionally testing without Busy. The commands never alter Simplex rules.

Import accepts regular files only, no symlinks/FIFOs/directories, maximum 128 KiB.
The actual C++ WAV parser and Opus encoder validate before selecting the file:
PCM WAV, mono, signed 16-bit little-endian, 8/16/24/48 kHz, **1–1000 ms** for Squelch.
Busy now accepts **20–1250 ms**; see the [new standard Busy tone](busy-tone.md).
Squelch limits are unchanged. Peak attenuation caps
signals at 0.2 PCM before the selected volume and optional `feedbackboostdb` (default 0). Imports have content-addressed names;
updates refresh only packaged standards/helpers and retain custom files, selection,
gain and other configuration. Reset does not delete imports. An invalid import
leaves the selected configuration unchanged. No recompilation or hot reload is
needed: WAV/configuration changes take effect at the controlled service restart.

## Docker assets and management

The same standards, validator and shared admin code are included in the existing
runtime image. UID/GID 10001 can read the standards. New configuration keeps
Squelch and master feedback off. Existing configuration is preserved byte-for-byte
at initialization, including files created before these keys existed.

From the **existing Compose checkout/project** on the Docker host:

```sh
bash scripts/docker/audio.sh status
bash scripts/docker/audio.sh enable-squelch
bash scripts/docker/audio.sh disable-open
bash scripts/docker/audio.sh close-volume 40
bash scripts/docker/audio.sh set-close /absolute/local/close.wav
bash scripts/docker/audio.sh reset-close
bash scripts/docker/audio.sh disable-squelch
```

The same command names as the native table are supported. The host wrapper stops
only its Compose service for modifications, runs offline administration, then
restarts it **only if it was running before** (also on errors). Expect a brief
connection interruption. The custom WAV is copied through a bounded temporary
read-only bind mount into the existing persistent state volume at
`/var/lib/mumble-funk/audio/`. Source files are not modified. Config backups and
imports are covered by the existing Docker backup/restore. No new volumes,
project/service names, ports, automatic enablement or `.env` updates are introduced.
Status works without stopping the server; direct modifying `funk-container audio`
commands refuse while the server holds its existing instance lock.

Use the existing [Docker backup/update procedure](docker.md#9-aktualisieren).
Preserve `.env`, Compose project name and volumes. Never use `down -v` to update.
The feature must be built from `feature/squelch-audio` to obtain these commands;
the old Stable image is not silently changed by this work.

## Practical check with unchanged clients

Use at least two clients in the same channel and optionally one in another.
Transmit a sentence with clear first and last syllables. Check Open/Close at the
listener, no own cue at the sender, no cue in the other channel. While one client
holds the channel, press PTT on another: Busy must remain private and the blocked
client must not automatically take over. Repeat short presses, channel changes,
and reconnects. Compare `squelch=false` with `squelch=true`, and test normal UDP
and the client's TCP-only option if available.

Automated tests cannot establish what a physical device actually sounds like.
The operator reported the initial Windows/Mumla result above. The timeout fallback
has not yet been hearing-tested on those devices; no long-term load test or arbitrary
loss/jitter qualification has been performed for this preview. Do not call it production-ready.

## Later continuous noise / mixing

`SquelchPlan` owns accepted transmission/recipient generations; `AudioFeedback`
currently renders finite clips. Signal kind, WAV selection and gain are separate
from Simplex ownership. A later optional renderer can consume the same lifecycle
and recipient validation to mix or maintain a continuous effect stream, sharing
feedback sequence ownership. It must still invalidate on transmission end/moves
and coexist with private Busy. No background noise, loop playback or mixing engine
is implemented now; disabling effects always leaves direct voice forwarding.
