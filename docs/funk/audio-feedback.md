> **Historischer Stand `feature/audio-feedback` (alte Preview auf 64740).**
> Diese Anleitung bzw. diese Ergebnisse gelten nicht für `feature/audio-engine`.
> Für die neue, getrennte Phase-1-Preview auf 64741 siehe [Audio-Engine](audio-engine.md)
> und [Prüfbericht](AUDIO-ENGINE-STATUS.md). Alte Installationen nicht damit aktualisieren.

# Audio Feedback v0.3 — experimental

This document describes **`feature/audio-feedback`**, based on Mumble Server
1.5.939 and Simplex Engine v0.2. It is an experimental preview, not a stable release.
`funk-v0.1` remains the v0.2 reference. Nothing is installed on a running VM by
publishing this branch.

**Deutsch:** [Installation, Bedienung und Gerätetest](installation-audio-preview.md) ·
[Prüfbericht](AUDIO-FEEDBACK-STATUS.md)

## What exists

| Component | Implemented behavior |
| --- | --- |
| Simplex decision | Direct, per-attempt rejection indication, separate from diagnostic events |
| Signal assets | Synthetic default or a validated PCM WAV file; shared loader/resampler/Opus encoder |
| Busy feedback | Private double tone for the rejected connection, with configurable volume |
| Delivery | Standard Opus audio in a TCP `UDPTunnel`, addressed only to that connection |
| Repeat guard | One indication per inferred attempt, plus a one-second playback cooldown |
| Administration | Validated WAV import, reset, enable/disable and volume commands; no recompilation |
| Diagnostics | Optional and independent; feedback works with both JSONL diagnostic systems disabled |

Roger Beep, squelch and other radio effects **are not implemented**. `Signal`, the
asset loader/encoder, request queue, repeat guard and recipient delivery are
separate extension points. Adding another signal should not require rebuilding
Simplex access rules around a logging format.

## The signal and its provenance

The fallback is synthesized locally in `AudioSignal::syntheticBusy()`. It consists
of two 600 Hz sine bursts of 100 ms each, separated by 40 ms silence, followed by a
20 ms silent codec tail: **260 ms total**. Each burst has a 5 ms linear attack and
release to reduce clicks. The formula is `0.2 × envelope × sin(2π × 600 × n/48000)`.

See the [audio provenance and license inventory](AUDIO-RESOURCE-LICENSES.md), including
the reviewed Opus dependency and shipped notices.

There are no downloaded recordings, audio libraries of uncertain provenance or
recorded microphone contents. The generator is project source under the existing
BSD license. The tone does not purport to reproduce a specific country's telephone
busy cadence.

## Configuration

The ordinary server defaults to feedback **off**. The separate preview installer
creates its own configuration with feedback on; existing files are preserved.
These settings require a restart, and may also be overridden by a virtual server's
existing database configuration, just like other Mumble settings.

```ini
simplex=true
# Keep your existing Simplex timeouts unchanged.
audiofeedback=true
busytone=true
busytonevolume=50
# Empty means the synthesized fallback:
busytonefile=
```

| Key | Server default | Meaning |
| --- | --- | --- |
| `audiofeedback` | `false` | Master feedback switch; currently requires Simplex |
| `busytone` | `true` | Enable busy feedback when the master switch is on |
| `busytonevolume` | `50` | Relative signal level, 0–100%; 0 disables tone work, >100 clamps to 100 |
| `busytonefile` | empty | WAV path; loaded once at startup, never in the incoming voice path |

Mute/deafen, output-device routing and output volume in the receiving client still
apply. No sound is forced through a deafened client. Audiofeedback does not change
server access permissions or the client's microphone mode.

## Custom WAV assets

The first supported format is deliberately narrow:

- RIFF/WAVE, little-endian **PCM format 1**, 16-byte `fmt` chunk.
- **Signed 16-bit, mono**, sample rate **8000, 16000, 24000 or 48000 Hz**.
- Audio length **20–1000 ms**, including any silence in the file.
- File size **at most 128 KiB**, including headers/metadata.
- Exactly one `fmt` chunk and one `data` chunk; bounded ancillary chunks are allowed.
  RIFF/chunk sizes, block alignment, byte rate and complete samples are checked.
- Stereo, float, compressed WAV, WAV extensible and 44.1 kHz are rejected.

Files at lower supported sample rates are linearly resampled to 48 kHz. This simple
resampler is intended for short indication sounds, not high-fidelity music. One
20 ms silent codec tail is added and the signal is padded to 20 ms Opus frames.
Custom output can therefore be up to 1020 ms long. The encoded signal is prepared
once during startup; there is no file access or Opus encoding per rejection.

Before encoding, signals exceeding a 0.2 PCM peak (about −14 dBFS) are attenuated;
then `busytonevolume` is applied. Quiet files are not automatically amplified.
Opus can introduce small reconstructed-peak differences. This is a digital level
limit, not a guarantee of perceived headphone loudness.

An invalid, missing or unreadable configured file produces a startup warning and
uses the synthesized fallback. Failure to initialize the encoder disables feedback
with a startup message; ordinary Simplex and speech continue. The management command
rejects invalid files **before changing the active configuration** instead of
silently installing them.

The preview service account must be able to read manually configured files. The
management command avoids permission problems by copying validated content to
`/etc/mumble-funk-audio/sounds/busy-<sha256>.wav`, owned by root and readable by the
preview group. Source symlinks and special files are rejected by that command.
Files managed there survive service upgrades and uninstall-by-default.

```bash
sudo mumble-funk-audio set-busy-tone /path/to/own.wav
sudo mumble-funk-audio reset-busy-tone
sudo mumble-funk-audio volume 35
sudo mumble-funk-audio disable
sudo mumble-funk-audio enable
```

Each command restarts **only** the preview service, briefly disconnecting its
clients. It keeps the preceding configuration as
`/etc/mumble-funk-audio/server.ini.before-audio-change`. A failed restart restores
the prior configuration and attempts to restart it. Old content-addressed WAV files
are retained for recovery. Import/reset needs neither C++ changes nor a new build.
The master enable command does not override `simplex=false`, `busytone=false` or a
zero volume in a manually customized configuration.

The validator is also available without starting a server or opening its database:

```bash
/opt/mumble-funk-audio/current/mumble-server --validate-feedback-wav /path/to/own.wav
```

Use only audio you own or are allowed to use. For a longer/different-format own
file, an optional local conversion is:

```bash
sudo apt install ffmpeg
ffmpeg -i own-input.wav -ac 1 -ar 48000 -c:a pcm_s16le -t 1 busy.wav
```

## Decisions, attempts and unchanged access rules

`SimplexController::Transmission` returns both the existing `allowed` decision and
a new `feedback` indication. The indication is calculated from actual rejection
paths, not the diagnostic sink. This includes initial `busy`, `retry_required`
after lease expiry, `sequence_not_advanced` and stale blocked packets that do not
emit a new diagnostic event. Repeated rejected packets of the same inferred attempt
are latched. An empty end marker cannot start a tone.

A trusted end marker or the existing quiet interval re-arms the attempt indication.
Stale markers, sequence rollback, a channel change or a permission invalidation do
not invent a new physical button press. The existing one-second playback cooldown
can suppress feedback on very rapid separate attempts. Feedback omitted by the
cooldown or overload does not imply that the transmission was accepted.

Example: Alice owns the channel. Bob begins transmitting, his audio is discarded,
and Bob alone receives the busy tone. Alice keeps transmitting. Even if Alice stops,
Bob cannot automatically take over while continuing the rejected attempt. Bob must
satisfy the **same v0.2 marker/quiet-interval retry rules**. Feedback never claims or
releases a channel and does not shorten either timeout.

Muted/suppressed users, unsupported whisper/loopback targets and bandwidth-ineligible
packets do not trigger feedback. A repeated sequence from an otherwise eligible
sender can be rejected and indicated even when that sender still owns a lease.
Therefore the tone means “this detected attempt/packet was rejected”; it is not
proof that another physical radio's PTT button is down.

## Protocol and playback evidence

The server emits real Opus packets: mono 48 kHz, 20 ms per packet, nominal 24 kbit/s.
It uses the existing server-side audio encoder for each recipient's protocol
version, with sequence numbers in 10 ms units and an end marker on the last packet.
Payloads are framed as ordinary TCP `UDPTunnel` messages, including for clients that
send their microphone packets over encrypted UDP. No new Mumble message type or
client extension is used.

The source session is **the recipient's own existing session**, an approach supported
by existing server-loopback playback. No fictitious user or bot client is introduced.
Only that recipient's TCP connection is written to. These packets bypass channel
broadcast/routing, listener lists, whisper caches and the Simplex acquisition path.
They are not Alice's microphone stream. Consequently a self-talking indicator or a
client recording can attribute the tone to oneself; the unchanged client has no
special “server busy” source identity or busy indicator.

Evidence examined:

- Desktop Mumble 1.5.939: `ServerHandler::handleVoicePacket()` accepts a known session
  and submits it to `AudioOutput::addFrameToBuffer()`. There is no own-session/PTT
  rejection in this path. `AudioInput` already supports server loopback. Local mute
  and output volume still affect playback.
- [Mumla source](https://github.com/quite/mumla/tree/882880a92cf42531f44bc114588e5cb7802256f2),
  inspected commit `882880a92cf42531f44bc114588e5cb7802256f2`.
  Its pinned [Humla audio library](https://gitlab.com/quite/humla/-/tree/7966f3828d6ed87ef29c517abfade6ad7998cdc5),
  commit `7966f3828d6ed87ef29c517abfade6ad7998cdc5`, has
  `AudioHandler.messageVoiceData()` submitting input to `AudioOutput.queueVoiceData()`.
  Playback accepts known, locally unmuted users, without filtering out the local
  session or requiring microphone input to be stopped. Input and output paths are
  separate. This inspection does not establish the exact version on your phone.
- Integration tests connect real protocol clients to a real server, send rejected
  TCP/encrypted-UDP traffic, receive and Opus-decode the private packets, including
  while rejected input continues. Legacy and protobuf audio framing are covered.

These establish server delivery and **source-level plausibility of playback while
PTT is held**. They do not establish actual audible playback on Windows/Android
hardware. Android audio routing, Bluetooth/headset behavior, client settings and
version differences require the [device test](installation-audio-preview.md#gerätetest).
No real-device Busy Tone hearing test has been performed in this cloud environment.

## Concurrency and bounded work

The Simplex mutex still protects the decision and ordinary forwarding. On rejection
it is unlocked **before** submitting a small identity snapshot. No audio encoding,
network transmission or logging file access occurs in the feedback decision path.

The producers use a try-lock and at most 128 queued requests. The Qt main thread
services them every 20 ms; at most 64 recipient/cooldown entries exist per server.
Requests older than 300 ms are discarded; stalled playback is abandoned instead of
bursting old frames. Cooldown state expires after 60 seconds. Saturation can omit
an indication, never grant a sender access or accumulate unlimited work.

There is no feedback worker thread per user or signal. A pre-encoded clip is shared.
A slow client's pending plain/encrypted TCP output is checked against 64 KiB before
each feedback packet. This bounds additional feedback, not all ordinary Mumble
network traffic. TCP congestion can delay or prevent timely playback.

Every delivery checks connection identity **and** session **and** channel again on
the main thread. Channel changes and disconnects cancel pending/active feedback
under the existing voice lock. A reused session number cannot inherit a previous
connection's tone. Shutdown stops the UDP producer first, then the timer/engine.
Packets already sent to a client cannot be recalled and may briefly finish playing
after a channel change or disconnect.

Two narrow UDP correctness fixes are included on the feature branch. After
ciphertext is probed for a public ping, the decoder restores the authenticated
user's actual protocol version. Otherwise nonce byte `0x01` can be mistaken for a
protobuf header and silently drop a valid legacy audio packet. A regression test
forces this nonce byte: it fails before the fix and passes after it.

The feature also makes the existing outgoing UDP encryption scratch buffer
`thread_local`: standard TCP and UDP forwarding can otherwise share that static
storage concurrently. This is a storage-race fix, not a protocol, audio-content or
Simplex-policy change. The v0.2 branch remains untouched. On this feature branch only, the ordinary
installer also rejects direct production deployment from a non-`funk-v0.1` branch,
so an experimental build cannot accidentally be installed through that entry point.

## Tests and limits

See [the verification report](AUDIO-FEEDBACK-STATUS.md) for executed tests and their
limits. The new tests cover waveform timing/frequency/volume, WAV validation and
resampling, real targeted packet delivery, disabled combinations, repeated/stale
rejections, cancellation, channel separation, cooldown/overload and retained
ownership. Existing Simplex tests continue to cover permission/channel lifecycle,
UDP/TCP competition and timeout/retry rules.

Still open: audible Android/Windows tests, real network disturbance testing,
high-population load tests and a long-running v0.3 stability test. There is no
native busy display or exact physical-button detection. Roger Beep, squelch, general
radio profiles and high-fidelity audio effects remain future ideas.
