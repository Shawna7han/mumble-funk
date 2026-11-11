# Mumble-Funk

**Server-side simplex radio communication for Mumble**

Mumble-Funk is an independent, open-source fork of **Mumble Server 1.5.939**.
It recreates radio-style communication with one transmitter per channel and
optional server-generated audio effects. **Mumble clients stay unchanged.**
Windows-Mumble and Android-Mumla are used in the project's practical tests.

**This README describes `dev`: experimental development, not a Stable release.**
[Deutsch](README.de.md) · [Dev installation](docs/funk/install-dev.md) ·
[Docker](docs/funk/docker.md) · [Branches and evidence](docs/funk/development.md)

## Features

| Feature in `dev` | Behaviour |
| --- | --- |
| Simplex Engine v0.2 | One accepted transmitter per channel; UDP and TCP share the same channel ownership. |
| Competing transmissions | Rejected senders do not automatically take over when the channel becomes free. A new attempt is required. |
| Busy Tone | Private feedback to a rejected sender; higher priority than the other effects. |
| Squelch Open / Close | Optional cues for eligible listeners at the start and end of an accepted transmission. |
| Timeout Close | Optional Close when packet inactivity releases a transmission without a confirmed end marker. A timeout does not prove physical PTT release. |
| Continuous radio noise | An independent effect alongside unchanged speech. Open → Noise → Close share one private effect stream with continuous sequence numbers. |
| Audio controls | Separate volumes, Open/Close switches and a 0–12 dB boost for Busy/Open/Close. Noise has its own volume and is not boosted. |
| Packaged and custom audio | Four CC0 WAV recordings; replaceable WAVs without recompilation. Synthetic Busy remains a fallback. |
| Administration and diagnostics | Audio CLI, read-only JSON status, optional voice-packet and Simplex diagnostics; no recording of speech content. |
| Installation and recovery | Native Ubuntu systemd service and Docker Compose; configuration/data backups and restore/rollback procedures. |

For example, Anna starts transmitting and listeners hear Open and noise alongside
her speech. Ben's competing transmission is rejected and only Ben hears Busy.
Anna's confirmed end, or an inactivity timeout, releases the channel. Ben must end
his attempt and try again; he is not automatically granted the channel.

## Current development status

| Source | Scope |
| --- | --- |
| [`stable`](https://github.com/Shawna7han/mumble-funk/tree/stable) | Previous Ubuntu/Docker Simplex installation candidate, **without the new audio engine**. This is still GitHub's default branch. |
| [`dev`](https://github.com/Shawna7han/mumble-funk/tree/dev) | Integrated Audio Engine, Squelch, Boost, packaged sounds, audio defaults, radio noise and experimental Dev installer. |

PR #9 was merged into `dev` at
[`a6b63175b7e94e503c410706dba87e23d861f46e`](https://github.com/Shawna7han/mumble-funk/commit/a6b63175b7e94e503c410706dba87e23d861f46e).
Use the full Git revision in test reports: “Mumble 1.5.939” alone does not identify
a Mumble-Funk version. Ubuntu and Docker use the same source when built from the
same commit. See the [complete branch overview](docs/funk/development.md).

| Evidence | What it establishes |
| --- | --- |
| Implemented | The features above are present in the integrated Dev source. |
| Automated verification recorded before PR #9 merged | 33/33 server CTests natively and in the Docker build, 131 Python checks, and real temporary Compose tests including TCP/encrypted UDP, Simplex, effect decoding, backup/restore and image update/rollback. |
| Operator-reported device tests | Successful native Ubuntu installation and Windows-Mumble/Android-Mumla listening tests. The documented `6f976a2` test confirmed continuous noise, repaired dropouts and Close when Windows sends and Mumla receives. |
| Still open | Broader device/network coverage, a separate Docker listening test, target-host boot/recovery checks and a completed long-term/load test. |

These are recorded results, **not a live CI badge or production-readiness claim**.
This documentation update requires no server rebuild. Details and limits are in
[development and verification](docs/funk/development.md#tests-und-praxiserfahrung).

## Installation

For the **current experimental Dev version**, use a separate Ubuntu Server
**24.04 or 26.04 LTS** test VM. Start from a fresh directory as a normal sudo user:

```bash
git clone --branch dev --single-branch --recurse-submodules https://github.com/Shawna7han/mumble-funk.git
cd mumble-funk
bash scripts/funk/setup.sh --experimental-dev
```

Follow the [Dev guide](docs/funk/install-dev.md) for prerequisites, source checks,
updates and connection details. It installs the normal `mumble-funk` service;
there is no extra preview service. On an existing installation it updates that
service, so use the test VM deliberately.

For **Docker from Dev**, follow the [Docker guide](docs/funk/docker.md). It builds
locally from the Dev checkout; there is no additional audio port or automatic
registry deployment. For **Stable without Dev audio**, use the
[Stable setup guide](https://github.com/Shawna7han/mumble-funk/blob/stable/docs/funk/setup.md).
Do not apply Dev audio instructions to a historical Stable checkout.

| Current Dev installation | Default ports, TCP and UDP |
| --- | --- |
| Native Ubuntu | **64739** |
| Docker | **Host 64740 → container 64739** |

Custom host ports remain supported. Updates preserve existing `.env`, project
names and volumes. Historical Stable Compose defaults differ; its existing port
must be retained when migrating. See [update protection](docs/funk/docker.md#9-aktualisieren).

**Prepared prebuilt-image route:** [GHCR Dev image guide (German)](docs/funk/docker-image.md).
This feature prepares an explicitly approved publication; no image has been
published by this development task. The existing local build remains available.

## Audio defaults

**Only newly created Ubuntu-setup and Docker INIs** enable all of these effects:

| Setting | New-install value |
| --- | --- |
| `simplex`, `audiofeedback`, `busytone` | `true` |
| `squelch`, `squelchopen`, `squelchclose` | `true` |
| `squelchclosetimeoutfallback`, `radionoise` | `true` |
| `busytonevolume`, `squelchopenvolume`, `squelchclosevolume` | `50` |
| `feedbackboostdb` | `8` — Busy/Open/Close only |
| `radionoisevolume` | `20` |

Packaged WAVs are selected from `/usr/local/share/mumble-funk/audio/`:
`busy-tone.wav`, `squelch-open.wav`, `squelch-close.wav` and `radio-noise.wav`.
Open, Noise and Close follow one another within the effect stream; speech is
forwarded separately without decoding, mixing or re-encoding it.

**Updates do not migrate existing INIs**, including missing keys, disabled effects,
volume zero or custom WAV selections. Missing old noise keys retain `false` /
empty file / volume `20`; missing timeout-fallback remains `false`.
[Defaults and compatibility](docs/funk/audio-defaults.md) · [Audio licenses](assets/audio/LICENSE.md)

## Administration

On native Ubuntu:

```bash
sudo mumble-funk-audio status
sudo mumble-funk-info --hide-password
```

Audio status is read-only JSON, includes the configured noise values and does not
restart the server or confirm that a WAV actually loaded. `mumble-funk-info`
**without** `--hide-password` displays the access password; keep that output private.
Changing audio settings can restart the service. Docker uses
`bash scripts/docker/audio.sh status` and the existing Compose management scripts.
See [operator commands](docs/funk/ubuntu-commands.md) and [Docker administration](docs/funk/docker.md).

## Documentation

Operational guides are currently in German. Historical phase reports remain available.

| Topic | Guide |
| --- | --- |
| Development, all branches, integration evidence | [Development](docs/funk/development.md) |
| Current Dev installation and updates | [Dev on Ubuntu](docs/funk/install-dev.md) |
| Stable Ubuntu installation details | [Ubuntu guide](docs/funk/installation-ubuntu.md) |
| Docker, persistent data, backup and restore | [Docker](docs/funk/docker.md) |
| Simplex, PTT and diagnostics | [Simplex](docs/funk/simplex.md) · [Diagnostics](docs/funk/diagnose.md) |
| Busy and synthetic fallback | [Busy Tone](docs/funk/busy-tone.md) |
| Squelch switches, WAVs and timeout behaviour | [Squelch](docs/funk/squelch-preview.md) |
| Shared effect stream and continuous noise | [Radio noise](docs/funk/radio-noise.md) |
| Status, WAV selection and volumes | [Audio administration](docs/funk/ubuntu-commands.md) |
| Audio rights and provenance | [CC0 assets](assets/audio/LICENSE.md) |

## Known limitations

- Physical PTT buttons are not visible to the server. Packet silence can also mean
  network interruption or a speaking pause; timeout-based Close remains an inference.
- Unchanged clients have no native server-busy indicator. Busy feedback requires
  an Opus-capable, non-deafened client and audible local playback settings.
- Rapid PTT changes, packet loss and client jitter buffers still need broader
  testing. Speech and effects are separate streams, not sample-synchronized audio.
- Simplex routes accepted speech within the current channel; whisper, linked-channel,
  extra channel-listener and server-loopback routes are not alternative radio paths.
- Practical evidence is limited. Neither Dev nor the older Stable candidate is
  presented as a generally production-ready radio system.

## Roadmap

Implemented: Simplex, Busy, Squelch Open/Close, timeout fallback, audio controls,
continuous noise, common effect stream, Ubuntu and Docker administration.
Current priorities are wider real-device/network tests, reliable fast PTT changes,
Docker listening tests and longer stability runs. Roger Beep, digital radio
profiles and optional future mixing are **ideas without a delivery commitment**.
Squelch and continuous noise are already implemented, not future promises.

## Upstream and license

Mumble-Funk is independent of [Mumble](https://github.com/mumble-voip/mumble).
It is not an official Mumble release and does not claim endorsement or support
from the original developers. Upstream website: [mumble.info](https://www.mumble.info/).

> Copyright (C) 2005-2022 The Mumble Developers
>
> All rights reserved.

Source terms: [LICENSE](LICENSE) (BSD 3-Clause). Original copyright notices and
component licenses are retained; see [3rdPartyLicenses](3rdPartyLicenses/) and
submodule notices. The packaged recordings have separate [CC0 1.0 Universal terms](assets/audio/LICENSE.md).
