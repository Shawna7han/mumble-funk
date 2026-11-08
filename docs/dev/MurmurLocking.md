# Locking in Murmur

## Murmur's threads

Murmur makes use of multiple operating system threads.

Most logic in Murmur happens on the main thread.

Each virtual server in Murmur is represented by the
`Server` class. Most of the `Server` class runs on the
main thread, such as all control channel traffic (TLS).
Murmur's RPC systems usually run on a separate thread --
but all RPC methods are run on the main thread via
`ExecEvent` (see `Server.cpp`/`Server.h`).

The `Server` class is a subclass of `QThread`. The
thread that `Server` runs, is the *voice thread*.
This thread handles incoming UDP packets (ping and
voice), and rebroadcasts them as necessary.
The methods that run on the voice thread are:

- `void Server::run()`
- `void Server::processMsg(ServerUser *u, const char *data, int len)`
- `void Server::sendMessage(ServerUser *u, const char *data, int len, QByteArray &cache, bool force)`
- `bool Server::checkDecrypt(ServerUser *u, const char *encrypt, char *plain, unsigned int len)`

but these methods may call other methods
(such as `ChanACL::hasPermission()`) in order to
do their job.

In general, when speaking about the *voice thread*,
it refers to any code running in the `Server` methods
listed above.

The voice thread methods access various data in the
`Server` class to do their job. Besides being accessed
by the voice thread, this data is also read and written
to by the main thread.

To properly synchronize the access to the data shared
between the two threads, the `Server` class contains
a non-recursive reader-writer lock, `Server->qrwlVoiceThread`. 

This lock provides synchronization between the
main thread and the voice thread. These are the
only two threads that access a `Server`'s data.

The easiest way to understand the locking strategy
and synchronization between the main thread and the
`Server`'s voice thread is by using the concept of
ownership.

## Understanding data ownership in Murmur

A thread owning an object means that it is the only
thread that is allowed to write to that object. To
make changes to it.

Most data in the `Server` class is owned by the main
thread. That means that the main thread is the only
thread that writes/updates those structures.

When processing incoming voice data (and re-
broadcasting that voice data), the `Server`'s voice
thread needs to access various parts of `Server`'s data.

To ensure correct synchronization between the two
threads it is important that the rules for accessing
data in `Server` are clearly documented.

Most of these ownership rules relate to the use of
`Server->qrwlVoiceThread` mentioned earlier. It is
the main reader/writer lock between the voice thread
and the main thread. It is important to know *how*
exactly Murmur uses this lock, because it is not
immediately obvious.

Since the data protected by `Server->qrwlVoiceThread`
is only ever accessed by two threads, Murmur is clever in
the way that it uses the lock.

It uses the ownership rule defined above ("a thread owning
an object means that it is the only thread allowed to write
to that object") to avoid taking locks in some situations.
This is to avoid littering the code with locks, but also to
avoid deadlocks and/or misuse of the non-recursive reader/write
locker.

When reading data in the main thread that Murmur knows is
only ever written to by the main thread (that is, the data
is *owned* by the main thread), Murmur will avoid taking a
read lock on `Server->qrwlVoiceThread`.

However, not all data in `Server` is owned by the main thread.
When data is owned by the voice thread, the same kind of rule
applies. The voice thread will not take a read lock when reading
data that it knows only *it* will write to.

This behavior is not immediately obvious, but it is important
to know for understanding how the `Server->qrwlVoiceThread`
lock is used throughout the code base.

Another important detail to keep in mind is that while the
main thread can avoid locking `Server->qrwlVoiceThread`
during most of its operation, the opposite it true for
the voice thread. Most of the time, when processing incoming
packets, and especially in the
`void Server::processMsg(ServerUser *u, const char *data, int len)`
method, the voice thread holds a read lock `Server->qrwlVoiceThread`.
This is to ensure that the main thread is properly excluded when the
voice thread is reading data, and the voice thread is excluded when
the main thread is writing data.

## Ownership of shared data between multiple threads

This section documents the owners of data in Murmur that is
accessed by multiple threads at once. It documents how one
should synchronize access to that data to avoid introducing
data races in Murmur.

### Data owned by the voice thread

These are never accessed by the main thread, except in `ServerUser`'s constructor.
There is no synchronization on these.

- `ServerUser->sUdpSocket`
- `ServerUser->saiUdpAddress`

### Data owned by the main thread

The rules for accessing these objects are:

- To read from the main thread: No lock is required.
  A read lock is implied because the main thread is the owner.
- To read from the voice thread: The voice thread must hold read lock on qrwlVoiceThread.
- To write from the main thread: The main thread must hold write lock on qrwlVoiceThread.
- To write from the voice thread: Illegal. Disallowed.

The objects are:

- `Server->qhUsers`
- `Server->qhChannels`
- `ServerUser->uiSession`
- `ServerUser->sState`
- `ServerUser->bMute`
- `ServerUser->bSuppress`
- `ServerUser->bSelfMute`
- `ServerUser->bDeaf`
- `ServerUser->bSelfDeaf`
- `ServerUser->ssContext`
- `ServerUser->cChannel`
- `ServerUser->qmTargets`
- `ServerUser->qmWhisperRedirect`
- `Channel->qlUsers`
- `Channel->qhLinks`
- `Channel->qhPermLinks`
- `Channel->qlChannels`
- `Channel->qhGroups`
- `Channel->qlACL`
- `WhisperTarget->qlChannels`
- `WhisperTarget->bLinks`
- `WhisperTarget->bChildren`
- `WhisperTarget->qlSessions`

### Data with shared ownership (main thread and voice thread)

The rules for accessing these objects are:

- To read from the main thread: The main thread must hold a read lock on qrwlVoiceThread.
- To read from the voice thread: The voice thread must hold a read lock on qrwlVoiceThread.
- To write from the main thread: The main thread must hold a write lock on qrwlVoiceThread.
- To write from the voice thread: The voice thread must hold a write lock on qrwlVoiceThread.

The objects with shared ownership are:

- `Server->qhHostUsers`
- `Server->qhPeerUsers`
-  `ServerUser->qmTargetCache`

### Data with no ownership (synchronized via atomic types)

These objects are explicitly synchronized by using atomic types
and need no extra synchronization when read or written to.
The objects are:

- `ServerUser->aiUdpFlag`

### Data with no ownership (synchronized via mutexes)

These objects use their own locking schemes. This is can be
an internal mutex that is automatically taken when the object's
methods are called. It can also be an external mutex that must
be held before accessing the object itself.

- `ServerUser->csCrypt` (Locked via `User->qmCrypt` mutex.)
- `ServerUser->bwr` (Internal locking inside `BandwidthRecord`. All methods can be called without extra synchronization.)
- `Server->acCache` (Locked via `Server->qmCache` mutex.)

### Optional voice diagnostics

`Server->m_voiceDiagnostics` is created before the voice thread starts and is
destroyed after `stopThread()`. Configuration changes require a restart; the
pointer is never replaced while packets are processed. UDP and TCP copy only
value metadata into its bounded queue while the existing user/channel lifetime
locks are held. The queue uses its own mutex with `try_lock` for producers;
failed submissions are counted. The writer never acquires `qrwlVoiceThread` and
never holds the queue mutex during file I/O. Only the writer owns stream state.

`ServerUser->diagnosticConnection` is assigned before publication.
`diagnosticUserName` is written before the authenticated `sState` is published and
then remains immutable. `diagnosticChannelName` is updated on the main thread
under a write lock on `qrwlVoiceThread`; readers hold its read lock. These name
snapshots avoid reading normally editable names concurrently from the voice
thread. Queue records contain no user/channel pointers or borrowed audio spans.

### Optional SimplexController (Funk v0.2)

`m_simplex` and its configuration are immutable for the lifetime of a running
virtual server. The controller/logger are created before the UDP thread starts;
shutdown stops that thread and the expiry timer before destroying them. No
controller or logger exists with `simplex=false`. The original receiver routing
and rate-limit path remain active in that case.

UDP and tunneled TCP enter `processMsg` holding `qrwlVoiceThread` for reading.
`SimplexController::begin` locks its own mutex and returns a move-only
`Transmission` guard. **Keep that guard until every outgoing packet has been
sent or queued.** Releasing the mutex just after the ownership decision would
allow a terminator's successor to overtake the preceding forwarding operation.
The mutex serializes the short routing/send operations across channels, although
ownership is independent per channel. There is no sleep, file I/O or audio codec
work under this mutex. Original per-user bandwidth accounting has its own mutex.

The enabled path only calls `AudioReceiverBuffer::addReceiver` for current
channel members. In particular, it never enters the whisper path that upgrades
the voice read lock to a write lock. Such an upgrade while holding the controller
mutex could deadlock against control-plane invalidation. The normal existing
encoding and send loops are reused with unchanged opaque audio payloads.

Control paths (disconnect, channel change, mute/suppress and ACL suppression)
acquire the voice **write** lock before invalidating the connection's lease.
`clearACLCache` additionally holds this write lock only in enabled mode, before
`qmCache`; both are released before `clearWhisperTargetCache` acquires its own
write lock. Callers must not already hold the non-recursive voice lock. The
main-thread expiry timer acquires only the controller mutex and never accesses
live user/channel objects. The order is voice lock -> optional cache lock ->
controller mutex -> nonblocking log queue attempt. Nothing in the controller or
logger acquires the voice/cache lock in the reverse order.

The controller stores value records keyed by the immutable connection identifier
(shared with PTT diagnostics), not session-reusable user pointers. Names are the
immutable login snapshots. Disconnect removes the record; memory is proportional
to authenticated connections that have sent packets and occupied channels, not
the number of transmissions. Expiry checks the current owner/heartbeat with a
monotonic clock rather than scheduling callbacks against an old lease. The timer
runs every 50 ms; acquisition also checks expiry of the destination channel.

Rejected attempts are latched independently of channel ownership. Advancing
packets continue the rejection even after the channel becomes free. Only a valid
normal-path marker or the configured per-user quiet interval re-arms an attempt.
Out-of-order and duplicate payload frames cannot refresh or release a lease.
A legacy empty terminator at the latest frame number is allowed for an active
attempt. Sequence resets are accepted after the quiet interval. These rules are
conservative inference, not authentication of a physical PTT event. A long
network gap or arbitrary delayed frames after a reset remain ambiguous.

`SimplexLogger` owns a separate background thread. Producers use `try_lock` and a
4096-record queue; rejected submissions are counted. The writer swaps batches
under the queue mutex, then performs JSON serialization, append/flush and rotation
outside it. It never accesses the server or controller. File failure disables
logging only. Counts are summarized per attempt, not logged on every rejected
packet. Diagnostic records can be lost under load; they must not be used as the
source of truth for enforcing ownership.

## UDP transmission scratch buffer

TCP-tunneled audio on the main thread and incoming UDP audio on the voice thread
can both reach the raw UDP `Server::sendMessage` overload. Its reusable ciphertext
buffer is thread-local: a shared vector could be overwritten after the user's
crypto lock is released and before `sendmsg` consumes it. The per-user crypto
lock still protects encryption state; the thread-local buffer separately protects
the lifetime of the outgoing bytes. The concurrent-ingress regression checks
1,024 authenticated packets and their unchanged payloads at one UDP receiver.

### Isolated AnalogVoiceService (B3; not active in Server)

`tools/analog-voice` optionally builds one worker with four preconstructed codec
slots. **Server does not own or call this service in B3.** The existing
`Transmission` guard and direct forwarding above remain unchanged.

The tested future handoff order is voice lock -> Simplex `Transmission` mutex ->
short service mutex. `Transmission::started` identifies an actually accepted new transmission independently
of diagnostic callbacks and source frame resets. While the guard still owns its lock, copy recipient identities
and payload into `open`/`submit` (or reserve `finish` for a technical boundary).
Only successful ordered admission may replace direct forwarding in B4. A rejected
admission is a delivery failure; it must never send originals around an existing
channel barrier. No service method obtains a voice or Simplex lock. Worker codec,
DSP, reset, abort and wake callback calls execute outside the service mutex. The
worker never calls `SimplexController::begin` or decides eligibility.

The dispatcher acknowledges one coalesced wake, takes bounded batches under the
service mutex, releases that mutex, then resolves recipients and sends. B4 must
hold the appropriate voice/lifetime protection across `take` and local network
submission, and must recheck the current permission set without expanding the
returned recipients. Never hold the service mutex over server/network calls or
call the dispatcher synchronously from `Wake`. At most one notification may be
pending in addition to the currently running callback. Callbacks must not throw;
the injected clock must be monotonic, short, nonthrowing and must not reenter.

Control-plane changes update connection routing epochs under voice protection;
service invalidation never waits for a codec. Recipient Deaf/SelfDeaf changes use
`setReceive`, not an additional Simplex send restriction. It irrevocably prunes
that identity from every pending recipient snapshot without cancelling its own
sending generation. `update` is for channel/send-routing changes and invalidates
that connection's own queued generations; neither method changes Simplex ownership.
Release callbacks invoked under Simplex may only reserve a service control state;
they must not acquire voice locks. A confirmed marker closes via its admitted
payload ticket, not a premature callback invalidation. Genuine invalidation
cancels pending results; a normal Simplex release does not.

Shutdown owner: stop admission and UDP/control producers, invalidate queued Qt
callbacks via their QObject/lifetime context, call `shutdown`/join outside voice,
Simplex and service locks, then destroy service/server data. Shutdown is idempotent
but concurrent shutdown callers/destruction are not supported. Keep the Wake
context alive until join returns. See [B3](../funk/analog-voice-b3.md) and the
[B4 handoff](../funk/analog-voice-b4-handoff.md) for remaining integration work.

### Experimental Server integration (B4)

With a successfully configured `analogvoice=true` service, `processMsg` replaces
local submission with `open`/`submit` while its original Transmission guard still
owns the Simplex mutex. `started` alone opens a generation. The order is voice
read/write -> Simplex -> short service mutex. `m_analogTransmissions` is accessed
only under Simplex; ServerUser identities are published under voice write. Pure
receive changes use `setReceive`; send/channel changes use `update`.

The release sink only reserves `finish` on timeout or `cancel` on invalidation;
confirmed markers remain valid until their admitted payload ends. It takes no
voice lock and accesses no users. No worker takes a server lock or touches Qt
network objects. No executing codec call owns a server/Simplex lock.

The queued main-thread dispatcher acquires voice **write** with try-lock before
acknowledging or popping anything. A failed try-lock leaves the coalesced service
notification pending and schedules one context-bound retry. After `take` releases
the service mutex, identities and permissions are checked and the shared
`sendVoicePacket` path packages the packet. The write lock also excludes UDP
updates of its destination scratch state. TCP is submitted immediately in this
main-thread path, with a bounded TLS backlog check; no new session-only queued
transmit may outlive that identity check. The default direct path retains its
original signal behavior. Worker join occurs after stopping UDP and Simplex
expiry, with no voice/Simplex/cache lock held; QObject context survives the join.

Squelch/Noise are suppressed only for a successfully enabled analog service until
B5 defines their output-order lifecycle. The disabled path retains its original
guard-through-send-and-effect contract. See [B4](../funk/analog-voice-b4.md).

### Ordered analog effect lifecycle (B5)

The main-thread dispatcher holds voice write protection across local speech
handoff and its subsequent original-ticket observation. `AnalogVoiceLifecycle`
uses one fixed state per service lane/generation; source tickets are observed at
most once in source order. A send result records local submission only. TLS
backlog rejection or a failed UDP syscall cannot create a successful recipient.
Recipient intersections can only shrink. Terminal batches finish the lifecycle
after the complete speech drain, using the original monotonic end time.

Simplex timeout callbacks reserve `finish` with the timeout frame/time; they
never take voice protection or access users. Real invalidations may invalidate
Squelch metadata under its existing short lock. No Squelch operation acquires a
service, voice or Simplex lock in reverse. The worker never invokes effects.
AudioFeedback/Noise continue on their existing timer/producer architecture.
The original direct-path locking and Unix queued SIGTERM shutdown remain intact.
