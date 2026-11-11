# Squelch Phase 2C — experimental development handoff

Repository: https://github.com/Shawna7han/mumble-funk
Original dev base: `f82f5aeda5111711b8e0c351e3f8b2400fc4a90a`.
Feature branch: `feature/squelch-audio`; PR target: `dev`.
Mumble server basis: 1.5.939. This is NOT a released or hearing-tested version.

## Current authorization and test strategy

The owner has explicitly authorized publication of this feature branch and a
**Draft** PR against dev. Earlier Phase 2A local-only restrictions in historical
commits have been superseded. No direct dev/stable changes, force pushes, merges,
releases, deployments, existing server updates or Docker-instance changes.
Use the GitHub No-Reply author/committer identity in AGENTS.md.

The owner now uses a dedicated Ubuntu Proxmox snapshot VM. Snapshot management and
installation require the owner's separate approval; Codex must not reset/install
it. Use the normal service `mumble-funk`, installation `/opt/mumble-funk`, config
`/etc/mumble-funk/server.ini`, state `/var/lib/mumble-funk`, log `/var/log/mumble-funk`.
Native: **64739 TCP/UDP**. Docker: **64740 TCP/UDP host → 64739 TCP/UDP container**.
Preserve operator overrides, .env, project names, volumes and all existing data.
The previous plan for a parallel Ubuntu preview on port 64742 is CANCELLED.
Do not create another preview service, installation root or parallel-instance setup.
Other VMs remain untouched. GitHub is now the requested development backup;
repeating the extensive local ZIP/patch export is no longer required.

## What is implemented

- Optional `squelch=false` by default, gated by `simplex=true` and `audiofeedback=true`.
- Actual Open/Close delivery through the existing private feedback transport.
- Separate WAV files and volumes, loaded/encoded at startup; no file IO or encoding
  in voice processing. Invalid files disable Squelch while Busy/speech continue.
- Lifecycle and recipient generations in SquelchPlan, shared with server dispatch.
- One AudioFeedback scheduler owns each recipient's effect sequence, with Busy
  priority and an independent Busy cooldown. No competing private output counters.
- Original speech payloads, session identities, sequence numbers and terminators
  are forwarded intact. No decoding, mixing, buffering or rewriting of speech.
- Original CC0 Open/Close assets preserved byte-for-byte; no third-party resources
  or new dependencies added. No client/protocol/bot changes or new audio ports.

## Architecture and important code paths

`Server::processMsg` first performs the existing Simplex and normal receiver/send
path. Only after forwarding does it validate the Opus header, take at most 64
actual receiver snapshots from AudioReceiverBuffer and call SquelchPlan::observe.
Self, deaf/self-deaf users, foreign channels, listeners and whispers follow the
existing Simplex receiver policy. The planner cannot grant a blocked transmission.

SquelchPlan receives accepted-packet/lifecycle information independently of tone
synthesis. Identities include connection generation, real session, channel and a
monotonic transmission generation. Session numbers alone are insufficient.
Open occurs on the first accepted valid nonempty Opus packet; standalone empty
markers do not start it. Close is produced on a confirmed end, even on a single
payload+terminator packet. It is consumed only after Open was consumed. Phase 2C
adds Close-only mode: disabled Open is silently acknowledged in the post-forward
path, preserving the accepted-start and recipient-generation prerequisites without
queueing audio. Enabled Open still must actually start before Close can start. Duplicate
or stale input remains subject to the existing Simplex decision.

Timeout with the new fallback off, disconnect, channel changes and relevant
mute/deaf/permission changes invalidate optional work. Channel out-and-back must invalidate even if the final
channel ID is unchanged. New transmissions replace the channel generation, making
unsent old Close invalid. Sender disconnect or recipient session reuse cannot
redirect pending signals. Timeout is suspected; only the explicit experimental fallback described below
may plan its private Close cue. It never synthesizes a speech end marker.

The Simplex event callback invalidates on non-confirmed releases, except for the
explicit timeout opt-in described below. Confirmed
release is reported *before* final forwarding by Simplex; Close must be scheduled
from the post-forwarding path, not directly from that callback. SimplexController
itself and its lock/ownership/retry rules are unchanged.

`AudioFeedback` sends pre-encoded signals through TCP UDPTunnel under the
recipient's **own existing session** (the existing Busy/server-loopback mechanism).
Voice retains the actual speaker session and can use TCP or encrypted UDP.
No fabricated identities or extra users are announced. Separate client decoders
avoid injecting independently encoded cues into the speech decoder.

A main-thread 20-ms timer revalidates source and recipient generation, channel,
authentication, Opus support, mute/deaf state and writable socket. With Squelch on,
it attempts the voice write lock without waiting, making validation plus delivery
atomic against concurrent UDP lifecycle changes. A busy voice path causes the
optional timer turn to yield. The planner and request enqueue also use try-lock
in the post-forwarding producer path. No user pointer survives delayed dispatch.
Lock order is voice → Simplex → planner on producers; dispatch uses voice → planner
without calling Simplex. Do not invert this order in future changes.

## Busy arbitration, sequences and timing

Sequences use Mumble's 10-ms units: each 20-ms packet advances by two. All private
signals to a connection use the same output sequence state, with a +100 sequence
gap between independent clips (this is NOT a client playback clock). Preserve that
state across channel cancellation when Squelch is enabled; a move is not a new
session. Idle records expire after 60 seconds. A new connection gets new state.

Busy has a separate 128-entry producer queue, so optional cues cannot fill it.
An active optional signal is ended with its next nonempty packet/terminator when
Busy arrives. Busy then follows a 60-ms packet-duration/retirement gap, normally
within about 80 ms, with all original Busy packets. No Busy packets are shortened
or replaced. Squelch queued behind active/waiting Busy is omitted. Busy waveform,
custom WAV, gain and one-second cooldown remain intact. The old Busy-only path is
preserved when Squelch is disabled. This is best-effort audio, not a timing guarantee
under a stalled event loop or client jitter. Effects may be omitted under overload.

Bounds: 128 optional queued cues, 64 playback recipients, 128 lifecycle channels,
64 recipients per transmission. Ended planner records remain until invalidated or
replaced; capacity exhaustion omits new effects, not speech. A production expansion
may need smarter bounded eviction. No burst catch-up after timer stalls.

Open must start within 100 ms; Close may start within 300 ms. These are server queue
expiry bounds, not measured device latency. Once started, each packet is checked
against the current lifecycle. Already delivered client-buffered audio cannot be
recalled. A short grace gap is a pragmatic measure, not proof of perfect jitterbuffer
retirement on every client. Rapid handoffs can omit optional cues or overlap them
with speech. Real-device audibility remains the acceptance gate.

## Opus lookahead and original assets

Open: 48 kHz/16-bit/mono, 897 samples / 18.6875 ms.
SHA-256 `81573e3a11db089de7cf2bbb2918e0747a2edde12923c399ac5042513d038770`.
Close: 48 kHz/16-bit/mono, 3122 samples / 65.0416667 ms.
SHA-256 `8d9091e0e4ee36433eb8aa707a7fa4a19ccec8ce5dee85ef1d91fa478afd0e31`.
Both are supplied by the maintainer under CC0; see assets/audio/LICENSE.md.

`AudioSignal::encodeWavSignal` uses the existing encoder configuration and queries
OPUS_GET_LOOKAHEAD instead of blindly adding 960 samples. The current encoder reports
312 samples / 6.5 ms. Open needs 897+312=1209 samples, still **two packets / 40 ms**.
A single packet would omit 249 nonzero source samples; the tail energy is small
(about 0.365% of source energy) but not zero. No tail truncation, speed change or
alternative encoder mode was forced. Open encoding is identical to the previous
two packets. Close now uses **four packets / 80 ms**, instead of five / 100 ms;
the retained decoded prefix is identical. Busy uses its unchanged original API.
No speech delay is added to wait for the 40-ms Open.

Phase 2A found small PCM differences when prepending independently encoded Opus
to the same speech decoder, and independent clocks across Speex buffers. Those
measurements do not prove audible speech damage. The chosen separate private
feedback stream avoids that speech-state splice; it deliberately accepts overlap.
No further mixing/architecture experiment is required before real-client trials.

## Optional Mumla timeout Close follow-up

Starting commit for this follow-up: `c4200c38b2ccd1c042fdadfad940d0fdde5dfa6e`.
The operator reported a first real device test: Windows-Mumble Open/Close works
with `released/marker_confirmed`; Android Mumla 3.7.3 Open works but Close is absent,
with `released/timeout_suspected`. This is an operator report, not a Codex hearing
test or evidence about every Mumla version/configuration. Device acceptance of
the changed fallback is still outstanding.

Historical policy for this follow-up: **`squelchclosetimeoutfallback=false`** in C++ Meta, new-install native and
Docker defaults and the common admin status whitelist. A missing key is false.
`mumble-funk-audio enable-timeout-close` / `disable-timeout-close` changes only
that policy and performs the existing controlled restart. Docker uses the same
commands through `scripts/docker/audio.sh`. No automatic enabling of Squelch,
master audio, Simplex or Close. Existing INI/custom sounds/ports/volumes retained.

The critical change is the existing `SimplexController::Event` sink in Server.cpp:

- Confirmed `released/marker_confirmed` remains untouched in the callback; the
  existing post-forwarding path still plans Close immediately.
- Only `released/timeout_suspected` with explicit fallback, enabled Close and an
  initialized Squelch/feedback system invokes `SquelchPlan::timeout` synchronously.
- Every other non-confirmed release, and timeout with opt-in off, still invalidates
  as before. Explicit disconnect/channel/mute/deaf lifecycle invalidation remains.
- timeout() reads only the existing bounded stream/recipient metadata; no start,
  new audience, user pointer, speech marker or controller decision is manufactured.
  Identity and last accepted frame must match a valid previously observed stream.
  A mismatched final frame retires the stream without a signal. Already ended
  streams produce nothing. The existing stream generation is captured in the cue.
- The controller's lock serializes the synchronous timeout snapshot against a new
  accepted start. Do not defer only a bare session/frame event for later lookup:
  a reset sequence and reused session could otherwise hit a newer transmission.
  The planner takes the same short metadata lock formerly used by invalidate;
  it releases it before the bounded try-lock feedback enqueue. No voice lock,
  controller reentry, IO, encoding or network dispatch occurs in this callback.
- Existing dispatch validation under the voice lock checks connection/session/
  channel/transmission and eligibility again on start and each packet. Close is
  consumed once, and requires prior Open dispatch (or explicit Close-only silent
  acknowledgment). New observed streams supersede unsent old generations.
  Queue expiry and Busy priority remain; optional Close can be omitted.

SimplexController.cpp/.h, its timers/retry logic, protocol decoders, voice payloads,
forwarding and the shared feedback scheduler are unchanged. Timeout does not prove
PTT release: packet loss, long silence or network interruption can trigger it
while a physical button is still held. Close follows the existing timeout plus
optional timer/queue delay; diagnostics remain timeout_suspected. Already sent
client-buffered audio cannot be recalled. Read the new Ubuntu test case before
operator-controlled installation; Codex has not changed any existing server.

Regression additions: four deterministic planner/controller/arbitration tests and
52 real-server integration data rows covering both legacy/protobuf and TCP/encrypted
UDP, confirmed/no/late markers, one-packet starts, repeated expiry, on/off switches,
Close-only, invalid/no/denied starts, channel out/back, disconnect/deaf, actual sender
and recipient numeric-session reuse and stale-Close suppression at a new grant.
The unchanged protobuf decoder rejects empty opus_data even with a terminator:
modern valid confirmed tests use a payload+marker; the empty-modern case expects
an unconfirmed timeout. Do not relax or change the protocol for this work.
Busy cutoff checks require the full original Busy clip and decodable monotonic
private output. Speech assertions preserve first/last payload/frame/end bits.
The extra admin test checks opt-in/off and update retention without enabling effects.

Local build succeeded. The first enlarged run passed 23/23 CTest entries locally
(198.37 seconds) and in the PR's native and Docker jobs. The additional push job
hit the existing 180-second watchdog in the enlarged Squelch executable on a slow
runner, without an assertion failure before termination. This is a real incomplete
run, not a pass.

Correction: CMake registers the same executable in three bounded groups: the six
original methods, timeoutEnd, and timeoutBoundaries/sessionReuse/handoff. All ten
QTest methods were compared with `-functions` and are registered exactly once;
each group retains its **180-second** watchdog and every original assertion.
No packet/cue timing condition was loosened. The final full local grouped run
passed **25/25 CTest entries** (188.78 seconds), still using 23 test executables.
The three Squelch groups took 38.72, 44.49 and 32.73 seconds respectively. This
includes the 52 new integration data rows and four new deterministic foundation
methods. **90 Python checks passed**: source17, setup17, native install5, assets8,
legacy preview5, audio security13, common audio admin10, Docker helpers/wrapper15.
The six timing-sensitive handoff and actual session-reuse rows also passed in
three consecutive targeted runs. Python compilation, Bash syntax and whitespace
checks passed. No tests removed or skipped. Local evidence: `/workspace/work/squelch-backup/timeout-close-*.log`.
The first targeted run found two incorrect test expectations for an empty modern
marker. Inspection of unchanged MumbleProtocol.cpp confirmed intentional rejection
of empty opus_data. The corrected complete suite passes, requiring the appropriate
release reason for each format instead of changing/weakening the decoder.
After publication, check all current-head Actions including the real Docker suite;
old successful runs do not substitute for the changed commit. The prior cloud
storage limitation still prevents another full local Docker build.

## Phase 2C installation and management

Read [squelch-preview.md](squelch-preview.md) and the concrete German
[snapshot-VM guide](squelch-ubuntu.md). No new audio architecture was introduced.
The following changes are additional to the Phase 2B implementation:

- `scripts/funk/source.sh`, `setup.sh`, `build.sh`, `install.sh`: only the exact
  `feature/squelch-audio` branch may opt in with `--experimental-squelch`. Require
  clean tracked files, the official origin, cached origin head and live published
  branch head equality. Stable/funk-v0.1 and annotated version-tag guards remain.
  No fake Stable designation, no acceptance of arbitrary development branches.
- Normal `mumble-funk` service/paths and native port 64739 retained. Both 24.04 and
  26.04 OS checks retained. BUILD-INFO records source revision/branch/experimental
  status; normal `mumble-funk-info` displays it. Never publish its password output.
- Installer validates the original signals with the real server binary before
  deployment writes. Public CC0 standards go to `/usr/local/share/mumble-funk/audio/`
  (0644); imports to `/etc/mumble-funk/sounds/` (root:mumble-funk 0750/0640).
  Existing config and imports survive updates. Packaged standards are not user files.
- New normal `mumble-funk-audio` uses the existing safe INI/atomic-file/WAV-copy
  helpers. Status whitelists audio keys; no secrets. Separate Open/Close switches,
  gains, custom imports, reset, master enable/disable and existing Busy operations.
  Config backup before modifications; only its own verified systemd service is
  restarted. Invalid assets/values rejected before selecting a configuration.
- Minimal server settings `squelchopen=true`, `squelchclose=true` independently gate
  signals. Disabled files are not loaded. A separate configured-state flag preserves
  private sequence state in Close-only mode across cancellation. No Simplex changes.
- `--validate-squelch-wav` uses existing AudioSignal::encodeWavSignal (1–1000 ms).
  Busy retained its existing validator and 20–1000 ms bounds at Phase 2C;
  Phase A below extends only Busy to 1250 ms. Both allow PCM mono
  signed 16-bit LE 8/16/24/48 kHz, maximum 128 KiB, attenuation to 0.2 before gain.
- Docker copies standards/license and the same admin Python into the existing image.
  Fresh config has master feedback/Squelch false; file paths point to the standards.
  Existing INI remains untouched. Custom files use existing state-volume `audio/`.
- `scripts/docker/audio.sh` administers only the current Compose project, with a
  temporary bounded read-only WAV import. It stops its service for changes, and
  resumes only if previously running, including failure cleanup. Runtime mutation
  uses the existing exclusive instance lock; status is read-only during operation.
  No .env rewrite, ports/volume/project names changed, or automatic activation.
- Workflow changes add the normal-admin suite and shellcheck for the new wrapper.
  Docker integration now checks original asset hashes/readability as UID10001,
  switches/gain/import/reset, rootless file permissions, force-recreate, private
  Busy, TCP/encrypted UDP routing and inclusion/restoration of imported WAVs.
  Its old Busy fixture now updates the existing key instead of duplicating new
  default keys; no routing assertion or failure check was removed.

New-install audio defaults are the same for native and Docker: audiofeedback=false,
Squelch=false, Busy=true (inaudible while master off), both individual cues=true,
all gains=50, standard Squelch paths and empty Busy path (synthetic fallback).
The C++ file defaults remain empty when no configuration supplies them. Disabled
Squelch retains normal voice and the original Busy-only path. Disabled individual
cue files need not exist. WAV/volume changes restart the server, never recompile.
No hot reload, new dependency/audio source, parallel preview service or port64742.

Changed-file groups: Dockerfile; build/pr-checks workflows; native source/setup/build/
install/setup_support/template; shared legacy copy helper (backward-compatible
optional arguments); new native admin; Docker runtime/new wrapper/tests; Meta,
Server, AudioFeedback, main validator; SquelchIntegration; native source/setup/new
admin tests; these Squelch docs plus Docker/native installation cross-links.

## Phase 2C local verification

The existing cloud environment was reused. Server and all test targets rebuilt
successfully using GCC14, Qt5.15.15, Opus1.5.2, Release with ICE. After an aborted
container attempt and storage recovery, the complete suite was rerun:

- **23/23 CTest programs passed**, 115.21 seconds. Includes all existing Simplex,
  Busy, Squelch transport/audience/session-reuse and disabled-path regressions.
- Eight additional integration rows cover every Open/Close enable combination
  over TCP and encrypted UDP. Disabled files deliberately do not exist; first/last
  speech packet/sequence/marker remains exact and expected private cue packets match.
- **89 Python checks passed**: source guards17, setup17, native installer5,
  AudioSignal assets8, legacy preview5, audio security13, new normal audio admin9,
  Docker helpers/wrapper15. No tests removed. Staged installs use temporary roots;
  setup orchestration uses explicit command fixtures, not a booted Ubuntu VM.
- New admin tests verify original assets/permissions, configuration/password privacy,
  independent switches/gains, unsafe/corrupt/oversized inputs, Busy bounds/fallback,
  update/config/custom-file preservation, no binary change and container storage.
- Wrapper tests verify read-only status, conditional resume, failure cleanup and
  unchanged .env. Compose rendering confirms 64740TCP/UDP→64739TCP/UDP; historical
  custom-port/default rendering assertions remain in the real integration runner.
- Python compilation, Bash syntax and git diff whitespace checks passed.

**Local Docker/Ubuntu-container limitation:** the cloud disk was already nearly
full (32 GiB, Docker VFS). Attempting to start the old 2-GiB Ubuntu24 build-cache
image failed with no space before a container was registered. It is not a passed
Ubuntu test. No containers were running. Only regenerable local C++ objects and
one unused Ubuntu26 test-cache image were removed to regain working space; sources,
Git history, binaries, volumes and existing servers were not removed. No second
full local Docker build was attempted under this constraint. The current GitHub
build workflow runs on Ubuntu24 and must independently pass the complete native
build/CTest/admin suite, production Dockerfile build/CTest and real isolated Docker
integration at the pushed commit. See PR #4 checks for the exact SHA and results;
do not infer CI success from the local results above.

Local evidence: `/workspace/work/squelch-backup/phase2c-*.log`. These are temporary
cloud paths, not external backups. The feature branch and Draft PR are authorized
for publication; there is no separate required ZIP refresh in this phase.

## Historical Phase 2B tests and results

Build: GCC 14, Qt 5.15.15, Opus 1.5.2, Release, BUILD_NUMBER=939,
client=OFF, server=ON, tests=ON, ICE=ON, plugins/overlay/LTO=OFF.
Pinned existing dependencies reused; no new third-party package or source code.

- Full CTest run after the two fixes: **23/23 programs passed**, 111.08 seconds.
  Includes existing Simplex/Busy integrations and disabled-feature passthrough.
- TestSquelchIntegration: all eight legacy/protobuf × TCP/encrypted-UDP ingress ×
  TCP/encrypted-UDP voice egress combinations. Signals use authenticated TCP.
  Checks every first/last speech payload, source, sequence and marker, exact
  decoded speech samples, signal decoding, expected audience and private Busy.
- Confirmed empty end and one-packet transmission (Open + Close), suspected timeout,
  sender/recipient channel out-and-back, disconnect, receiver deaf-and-return,
  actual disconnected session number reuse, explicit/master disable and bad WAVs.
- New fast-handoff cases subsequently added for TCP and encrypted UDP: alternating
  accepted speakers, all original voice packets preserved, private sequence order
  and decodability. See the final validation addendum for their repeat results.
- Foundation tests cover generation invalidation, rapid replacement, expiry,
  bounded/concurrent state, malformed assets, actual lookahead, separate volume,
  Busy arbitration/packet preservation and decoded signal prefix equality.
- Additional Python suites: native installer **5**, source guard **14**, setup **16**,
  audio security **13**, Docker helper tests **11**, WAV/admin **8**, existing audio
  preview regressions **5**: **72 checks passed**. They use temporary roots/servers.
  Python 3.13 printed nonfatal SQLite ResourceWarnings in existing Docker helper
  tests; no failure. These are not Docker runtime or VM installation tests.

Failures were kept visible and fixed, not skipped:
1. Close on a single-packet transmission expired after 100 ms, before Open's playback
   and retirement gap had elapsed. Close now has its own bounded 300-ms start limit;
   Open retains 100 ms. The test still requires all two Open and four Close packets.
2. The reuse test used graceful TLS disconnect while waiting only on another client;
   its own socket had not completed closing. Use socket.abort() to model an actual
   abrupt disconnect, then require UserRemove and actual numeric session reuse.
   No server disconnect or authentication logic was changed.

Local logs are under /workspace/work/squelch-backup/phase2b-*.log. Typical commands:

```sh
. /workspace/.cloud-deps/funk-v0.1/activate.sh
cmake --build /workspace/build/squelch --parallel 2
ctest --test-dir /workspace/build/squelch --output-on-failure
ctest --test-dir /workspace/build/squelch -R '^TestSquelchIntegration$' --repeat until-fail:3 --output-on-failure
python3 tests/funk-install/test_audio_assets.py /workspace/build/squelch/mumble-server
```

At the historical Phase 2B test point, Windows-Mumble/Android-Mumla hearing tests
and the Proxmox snapshot installation had NOT been performed. The operator has
since reported the initial device test above. Hearing tests of the new timeout
fallback, arbitrary network loss/jitter, long-term load and sanitizers remain open.
Synthetic voiced harmonics with a syllabic envelope were used as decodable input;
these are not recordings or proof of human perception. No claim of production readiness.

## Next tasks for GPT-6.1 Sol Hoch

1. Read the current handoff and PR #4 checks at its exact head. Keep Draft/unmerged.
2. Follow up any failing CI at the feature branch; never weaken tests or merge
   unproven code. Native Ubuntu24 and full production Docker build/integration are
   checked by CI; the local disk limitation above must remain visible.
3. Let the owner install the documented feature opt-in on their clean Ubuntu VM,
   after managing their own snapshot. The operator has since installed a test instance and reported initial hearing
   results; complete systemd/autoboot, file permissions and setup acceptance on
   that VM are still owner-controlled checks. Codex must not execute remote installation or snapshot operations.
4. Owner hearing tests with unchanged Windows Mumble and Android Mumla: first/last
   syllables, Open/Close-only modes and gains, own WAVs/reset, Busy priority, rapid
   PTT, channels and reconnect. Compare effects off/on and TCP/UDP. No claim that
   automated decodability proves device playback. Record exact installed SHA.
5. Arbitrary loss/jitter, long-term load and sanitizers remain open. Optional cues
   may overlap speech or be omitted. No production claim until practical approval.
6. Keep accepted-transmission lifecycle/recipient separation for possible later
   continuous noise; signal kinds/files/gains already differ. A future renderer may
   extend/replace finite-clip dispatch without rebuilding Simplex/recipient rules.
   No noise, mixer, new session identity, client protocol or additional port now.
7. No dev/stable changes, release, deployment or merge without explicit approval.

## Historical Phase 2B final local validation addendum

The extended TestSquelchIntegration, including fast TCP/encrypted-UDP handoffs,
passed **three complete consecutive runs**, 96.82 seconds in total. No test was
removed, skipped or relaxed to hide a server failure. The full 23-program run
preceded these two additional cases; production code did not change afterward.
All 72 additional Python checks also completed successfully. CI results must be
checked separately on GitHub; local success is not a claim of completed Actions.


## First published CI: Docker build-context correction

At published commit 96db44a980efba232eee5ec38537054df2356997, GitHub's native
server/installation tests and CodeQL passed. The Docker build compiled successfully
but failed its SquelchFoundation/Integration tests because .dockerignore excluded
all WAV files, including the two required public CC0 fixtures. Missing assets made
the test server disable optional Squelch. This was a build-context packaging error,
not evidence of a speech transport failure. The Docker runtime integration step
could not run after that build failed.

The feature branch now explicitly allows only assets/audio/squelch-open.wav and
assets/audio/squelch-close.wav into the build context; arbitrary private WAVs remain
excluded. No test assertions, runtime audio, ports or data paths were changed.
The existing two failing test programs are the regression checks in Docker's build
stage. Check the subsequent GitHub run at the branch's new head for final results.

The correction was checked with real local Docker/BuildKit scratch builds using
the original and corrected ignore files. Before: both fixtures absent. After:
both fixtures byte-identical to the originals. Dummy private WAV, .env and key
files remained excluded in both contexts. This verifies context selection without
repeating the complete server compilation; the GitHub build reruns full tests.

## Audio-volume follow-up after PR #4 merged

PR #4 is closed/merged, not an open preview PR. Its feature branch is retained.
`feature/audio-volume` starts directly from current origin/dev
`e8894e5b6f42aaeee4e71262e58fadc471f4bea4`, which includes the full
`128ec3ce1d2c45b6ddf00df33f1e54d83ff946b0` head. Do not reopen/change PR #4.
The owner authorizes only this new feature branch and a separate Draft PR to dev;
no merge, direct dev/stable updates, tag changes, deployment or VM access.

Read [audio-volume.md](audio-volume.md) for the operator procedure and measured
Opus output. `feedbackboostdb=0` (missing also zero), strict integer 0..12,
multiplies the existing peak attenuation/individual volume by 10^(dB/20).
One startup-only gain stored in AudioFeedback applies to synthetic/custom Busy
and both enabled Squelch WAVs. Synthetic waveform, files, lengths, lookahead,
queues, sequence ownership, recipient/lifecycle gates and speech are unchanged.
At zero the frozen independent legacy encoder reference produces identical bytes.

A real adversarial square-wave regression showed that bounding only encoder-input
PCM to 0.796215 does NOT prevent Opus reconstruction overshoot above 1.0.
Nonzero boost therefore has bounded startup-only decode verification: if peak
exceeds 0.98, lower whole-clip gain toward 0.95, reset encoder and retry (max eight,
then explicit exception). No clipping, per-sample limiter, voice decoding or
real-time audio work. Pathological WAVs can receive less effective boost; do not
remove that guard to obtain the requested dB gain. Zero bypasses all new decoding
and retains exact legacy arithmetic and behavior. Decoder loss concealment/client
amplification are outside this file-level verification; do not promise universal
post-device peak bounds. Shipped sounds do not trigger the extra attenuation.

Common native/container admin adds `boost N`, safe status includes feedbackboostdb.
Updates preserve INI/imports; new native/Docker configs default zero. Docker host
wrapper rejects invalid boost before stopping its existing project. No changes to
Compose, .env, ports, volumes, services or public assets. The native source guard
accepts exactly feature/audio-volume with --experimental-audio-volume and the
same clean-source/official-origin/cached+live published-head checks; the old Squelch
branch/flag and stable/legacy/release-tag checks remain. BUILD-INFO marks it
experimental. Setup/build/install forward the opt-in without Stable relabeling.

Test additions: TestAudioVolume decodes all 0/3/6/9/12 levels for all original
signals, custom Busy, quiet/full-scale sine, square, impulse, alternating PCM and
seeded noise; checks silence, no clipping/nonfinite, lengths, individual volumes,
invalid numerical inputs and byte-identical legacy zero. Existing real Squelch
transport assertions run additionally at boost12 for both protocol formats and
TCP/encrypted UDP in/out; original voice bytes/frames/end flags and private Busy
bytes remain checked. Timeout end tests retain original cases and add boosted
no-marker cases. Python checks direct invalid INI rejection, all admin boost
values, old missing-key default, retained imports/config, setup flag/metadata and
same-project Docker orchestration. CI includes the new invalid-INI test and
Docker wrapper gain/recreate/restore retention checks.

Local validation for this follow-up: server build succeeded, all 26 CTest entries
passed (24 executables, 210.65 s). After adding four boost12 timeout rows and the
custom-Busy zero reference, the two changed test programs passed again (51.70 s).
95 Python tests passed: source18, setup18, install5, assets8, legacy-preview5,
security13, common-admin11, Docker-unit16, direct-INI1 (12 invalid inputs).
Compose rendering passed for absent host override, 64740, and custom 65123, TCP
and UDP ->64739. WAV SHA-256 hashes unchanged. Bash syntax, Python compilation
and git diff whitespace checks passed. ShellCheck is not installed locally;
the existing CI installs it. Due to the existing 32-GB/VFS cloud disk constraint,
full Docker image build and actual container integration run in GitHub CI instead
of duplicating that large build here. Verify current-head CI before declaring it
passed. No local or remote user installation has been modified; real-device boost
hearing tests and final approval remain outstanding.

First current-head CI: native and Docker builds, both 26-test suites, installer
checks and CodeQL succeeded. Actual Docker integration reached private audio and
boost/recreate checks but then failed the full settings comparison after the new
backup fixture intentionally selected boost9: info --json returns all INI keys,
while the old expected snapshot still contained boost0. Correct the expected
snapshot for precisely feedbackboostdb=9; retain equality of all other keys,
password/certificate/database checks and restored audio status=9. A unit regression
now compares complete info JSON after intentional settings changes. This is a test
fixture correction, not a server behavior change or suppressed container failure.
Rerun the full current-head GitHub pipelines after pushing the follow-up commit.

## Phase A: packaged Busy recording (feature/busy-tone-asset)

Base: dev `6b5322ef0fb404fc726b97eff44b7d213129fd85`, including merged Squelch
and Audio Volume work. See [busy-tone.md](busy-tone.md) for operator instructions,
public file SHA-256 and PCM SHA-256. The maintainer confirmed ownership and CC0
publication. Only the optional `bext` metadata chunk was removed after checking
it contained no legal notice; all other chunks and all PCM samples are unchanged.
The original remains private outside Git and must never be staged or published.

Only AudioSignal Busy limits change: WAV 20–1250 ms, 128 KiB unchanged;
60000 source samples at 48 kHz plus existing 960-sample tail, generic Busy encoder
limit 60960 samples / at most 64 packets. ShortSignal retains 1–1000 ms and its
existing encoding/tail policy. Synthetic waveform, volume and 0–12 dB boost,
feedback scheduler, Server.cpp, Simplex and SquelchPlan are unchanged.
The recording has 57468 source samples, encodes to 61 packets and fits completely
including codec lookahead. Tests pin the actual nonempty decoded tail.

New native/Docker templates select the packaged Busy path. Existing INIs are
preserved byte-for-byte; missing or empty busytonefile still means synthetic.
Native install copies public assets 0644; a pre-existing different file/symlink
at the new Busy standard path causes refusal before deployment writes.
`use-standard-busy-tone` validates and selects the shipped file.
`reset-busy-tone` deliberately retains its old meaning: synthetic tone.
`set-busy-tone` still makes a validated content-addressed private copy. None of
these selections auto-enable audio, reset gain, alter Simplex or remove imports.
Native setup/build/install require exact feature branch plus the explicit
`--experimental-busy-tone` opt-in and all existing source checks.

The existing scheduler intentionally abandons optional feedback after event-loop
stalls of over 100 ms; the longer Busy does not change that policy. An active Busy
does not restart or queue when its one-second cooldown expires. Optional Squelch
still loses to Busy. Voice never waits. Private feedback still uses TCP UDPTunnel,
even when the rejected speech arrived via authenticated encrypted UDP.

New TestBusyToneAsset covers raw file/PCM hashes, all rate/length boundaries,
decoded completeness/levels, fallback, fake-clock cooldown and priority.
Twelve additional AudioFeedbackIntegration rows cover legacy/protobuf × TCP/UDP
× boost0/boost12/volume0 with exact 61-packet expectations, identity/channel
isolation, unchanged voice/terminators and rejection/rearm rules. TestAudioVolume
compares the new asset's boost0 encoding to the frozen previous encoder.
Native admin/source/setup and Docker initialization/wrapper cases extend their
existing suites; no previous tests are removed.

FunkDockerProbe now accepts an explicit expected count (default 13, bounds 1–64),
checks the exact sequence/last marker and really decodes every Busy packet.
Its assertions remain strict. Docker integration explicitly tests packaged61,
synthetic13 and private-WAV61 with boost12, then compares original configuration
and tests private Busy and Close files through backup and restore. The build
context admits only the three public CC0 WAVs; private WAV exclusions stay intact.

Local evidence: all 100 Python cases passed; the cleaned public asset passed
its strengthened hash/PCM test and the complete AudioFeedbackIntegration suite.
Four isolated native protocol-probe runs passed: packaged boost0/12, synthetic,
and private copy boost12, each with TCP and encrypted UDP Simplex checks. A first
full-suite run alongside installer tests lost parts of two optional Busy clips;
the strict serial AudioFeedbackIntegration rerun passed without changing scheduler
or weakening assertions. Full final-suite and current-head GitHub CI results
are recorded in the feature PR; do not infer success from older commits.
The cloud host lacks disk space for another full local Docker build; use the
actual current-head GitHub build/Compose integration, not a mocked success.
Windows/Mumla hearing tests and deployment remain the operator's next steps.

## Fresh-install audio defaults (feature/audio-defaults)

Base dev: `35f83e9315945c50cbc3c0dc60c0b6f89ca83bb1`, including merged Busy
asset PR #6. Only new native/Docker configuration generation enables
simplex/audiofeedback/busytone/squelch/open/close, volume50 each, boost8 and
timeout-close true (PR #7 follow-up). Core Meta defaults, AudioSignal, AudioFeedback, SquelchPlan
and Server.cpp remain unchanged; so do admin DEFAULTS. Existing INIs, including
absent keys, retain the previous disabled/boost0 semantics and are never migrated.
Native template now has active simplex=true even for manual fresh installation;
create_config handles either a commented or active unique new-template key.
The existing early return for an existing INI remains first.

Exact feature branch installation requires `--experimental-audio-defaults` and
all existing source checks. [Operator guide](audio-defaults.md) distinguishes
new defaults from the older phase guides. Assets, licensing, ports, .env, project
names, volumes, custom WAVs and reset-to-synthetic behavior are unchanged.

test_audio_defaults.py compares native setup/manual and real Docker runtime INI
generation. It launches isolated native servers with the generated configurations
and tests actual effects and volume0 for each config source; no live deployment.
FunkDockerProbe --audio-defaults uses valid Opus test speech, the existing 30-ms
fresh bandwidth-window allowance, and owner keepalives under the unmodified
500-ms lease. It compares exact shipped Open/Close/Busy packets at volume50/boost8,
decodes effects, verifies sessions/sequence/terminators and unchanged first/last
speech. --audio-defaults-muted checks Busy omission and silent Squelch packets
(the same <1e-7 float decoder threshold as TestAudioVolume). Server code is not
changed for these tests. Explicit QString materialization avoids QStringBuilder
temporary lifetimes in this new test helper.

Docker integration first tests fresh automatic audio against the actual container,
then explicitly selects the pre-existing off/boost0 regression fixture. All earlier
assertions and private-WAV backup/restore checks remain. Unit restore tests add
missing-key, disabled and custom/volume0 INIs with exact byte comparisons.
Full local container builds are constrained by cloud disk space; current-head
Docker/CodeQL results belong to GitHub CI. The owner requested no waiting or
repeated CI polling after publication; the new Draft PR must remain unmerged.

Local final validation: incremental server/probe build succeeded, 27/27 CTest
entries passed (266.31 s), and all 106 Python cases passed. The latter include
three new default tests (four live config/volume subcases) and 18 Docker helper
cases with real temporary SQLite/tar backup/restore. Real Compose configuration
resolution verified default64740, pinned64739/64740 and custom ports with unchanged
.env, including update/rollback comparison. Bash syntax, Python compilation and
git diff --check passed. Full local Docker image/integration and CodeQL are not
claimed successful for this feature; they remain for GitHub CI. No VM hearing
test, deployment or live service modification was performed.

### PR #7: enable Mumla timeout Close for fresh configurations

Only the native template and Docker initialize() fresh-INI text now select
squelchclosetimeoutfallback=true. Core Meta and admin DEFAULTS remain false;
existing missing/false/true keys are never migrated. Existing native and Docker
configuration/restore cases explicitly retain false and absent keys. The fresh
protocol probe also waits past the lease after a confirmed end (no second Close),
then sends a valid new stream without a marker (one timeout Close, no fabricated
speech marker), and checks repeated expiry cannot duplicate the cue. The existing
Simplex/BUSY/timeout lifecycle regressions remain in place. No server code changed.
A timeout still cannot prove release of the physical PTT key.

Local follow-up validation: build succeeded; 34 Python cases and all 10 relevant
CTest entries passed (244.82 s), including timeout lifecycle, Simplex and Busy.
GitHub CI is intentionally not polled or awaited after publication.
