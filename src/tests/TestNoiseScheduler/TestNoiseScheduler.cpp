// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "AudioFeedback.h"
#include <QtCore/QFile>
#include <QtCore/QReadWriteLock>
#include <future>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <array>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <opus.h>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
using Plan = SquelchPlan;
static QString asset(const char *name) { return QStringLiteral(AUDIO_ASSET_DIR) + '/' + name; }
struct Heard { AudioFeedback::Recipient recipient; QByteArray bytes; std::uint64_t frame; bool last; std::int64_t time; RadioNoise::Segment segment; };
struct AudioFeedbackTestAccess {
    static std::vector<std::unique_lock<std::mutex>> holdProducers(AudioFeedback &feedback) {
        std::vector<std::unique_lock<std::mutex>> held;
        for (auto &slot : feedback.m_noiseSlots) if (slot.producer) held.emplace_back(slot.producer->m_mutex);
        return held; // Block only test producers/try-locks; do not reset codec epochs.
    }
    static std::size_t streams(const AudioFeedback &feedback) { return feedback.m_noise.size(); }
    static std::size_t terminals(const AudioFeedback &feedback) { return feedback.m_terminals.size(); }
    static QByteArray reserve(const AudioFeedback &feedback, std::uint64_t connection) {
        const auto &state=feedback.m_noise.at(connection);
        CHECK(state.hasReserve && state.reserve.segment==RadioNoise::Segment::Noise);
        return state.reserve.opus;
    }
    static RadioNoise::Segment segment(AudioFeedback &feedback, std::uint64_t connection) {
        auto state=feedback.m_noise.find(connection);
        return state==feedback.m_noise.end()?RadioNoise::Segment::End:state->second.reserve.segment;
    }
};
struct Rig {
    std::int64_t now = 0;
    Plan plan{true, [this] { return now; }};
    AudioFeedback feedback{50};
    QTemporaryDir dir;
    Plan::Identity source{1, 11, 0};
    std::vector<Plan::Identity> receivers{{2, 12, 0}, {3, 13, 0}};
    std::vector<Heard> heard;
    bool open, openEnabled, closeEnabled;
    int bufferedBytes = 0;
    Rig(bool squelch = true, int count = 2, bool withOpen = true, bool withClose = true) : open(squelch), openEnabled(withOpen), closeEnabled(withClose) {
        QByteArray wav(44 + 1007 * 2, 0);
        auto w16 = [&](int at, quint16 x) { qToLittleEndian(x, wav.data() + at); };
        auto w32 = [&](int at, quint32 x) { qToLittleEndian(x, wav.data() + at); };
        wav.replace(0, 4, "RIFF"); wav.replace(8, 8, "WAVEfmt "); wav.replace(36, 4, "data");
        w32(4, quint32(wav.size() - 8)); w32(16, 16); w16(20, 1); w16(22, 1);
        w32(24, 48000); w32(28, 96000); w16(32, 2); w16(34, 16); w32(40, 2014);
        for (int i = 0; i < 1007; ++i) w16(44 + 2 * i, quint16(i * 19 % 5000));
        QFile f(dir.path() + "/noise.wav"); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(wav) == wav.size()); f.close();
        if (open) feedback.configureSquelch(asset("squelch-open.wav"), asset("squelch-close.wav"), 50, 50, withOpen, withClose);
        CHECK(feedback.configureNoise(f.fileName(), 20));
        receivers.clear(); for (int i = 0; i < count; ++i) receivers.push_back({std::uint64_t(i + 2), unsigned(i + 12), 0});
    }
    void queue(const std::vector<Plan::Cue> &cues) {
        for (const auto &cue : cues) {
            if (cue.kind == Plan::Kind::Open) CHECK(feedback.requestNoise(cue));
            if (open && ((cue.kind == Plan::Kind::Open && openEnabled) || (cue.kind == Plan::Kind::Close && closeEnabled)))
                CHECK(feedback.requestSquelch(cue));
            else if (cue.kind == Plan::Kind::Open) CHECK(plan.consume(cue, cue.sender, cue.recipient, true));
        }
    }
    void start(bool last = false) { queue(plan.observe(source, true, 0, true, last, true, receivers)); }
    void end() { queue(plan.observe(source, true, 2, true, true, true, receivers)); }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const auto before = heard.size();
            feedback.tick(now, [&](const AudioFeedback::Recipient &r, const QByteArray &p, std::uint64_t frame, bool last) {
                if(std::none_of(receivers.begin(),receivers.end(),[&](const Plan::Identity &id){return id.connection==r.connection && id.session==r.session && id.channel==r.channel;}))return false;
                for (auto it = heard.rbegin(); it != heard.rend(); ++it) if (it->recipient.connection == r.connection) {
                    if (frame <= it->frame)
                        std::cerr << "Effect sequence reset at " << now << " us: " << it->frame << " -> " << frame << '\n';
                    CHECK(frame > it->frame); break;
                }
                heard.push_back({r, p, frame, last, now, AudioFeedbackTestAccess::segment(feedback,r.connection)}); return true;
            }, [&](const Plan::Cue &cue, bool starting) {
                return starting ? plan.consume(cue, cue.sender, cue.recipient, true) : plan.current(cue);
            }, [&](const AudioFeedback::Recipient &, bool continuous) {
                return bufferedBytes < (continuous?1024:65536);
            });
            int noisePackets = 0;
            for (std::size_t j = before; j < heard.size(); ++j) {
                bool clip = false;
                for (auto signal : {AudioFeedback::Signal::Busy, AudioFeedback::Signal::SquelchOpen, AudioFeedback::Signal::SquelchClose}) {
                    const auto &packets = feedback.clip(signal);
                    clip |= std::find(packets.begin(), packets.end(), heard[j].bytes) != packets.end();
                }
                if (!clip) ++noisePackets;
            }
            CHECK(noisePackets <= RadioNoise::MaxInstances);
            now += 20000; std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::vector<Heard> forUser(std::uint64_t c) {
        std::vector<Heard> out; for (const auto &h : heard) if (h.recipient.connection == c) out.push_back(h); return out;
    }
    void decode() {
        std::map<std::uint64_t, OpusDecoder *> decoders;
        for (const auto &h : heard) {
            auto &decoder = decoders[h.recipient.connection]; int error = 0;
            if (!decoder) decoder = opus_decoder_create(48000, 1, &error);
            CHECK(decoder && !error); std::array<float, 960> pcm;
            CHECK(opus_decode_float(decoder, reinterpret_cast<const unsigned char *>(h.bytes.data()), h.bytes.size(), pcm.data(), 960, 0) == 960);
            if (h.last) { opus_decoder_destroy(decoder); decoder = nullptr; }
        }
        for (auto &entry : decoders) if (entry.second) opus_decoder_destroy(entry.second);
    }
};
static int shortCloseProbe() {
    int failed = 0;
    for (bool timeoutEnd : {false, true}) for (int mode = 0; mode < 6; ++mode) {
        // All transmissions are two seconds long. Only the final transition is disturbed.
        const char *names[] = {"confirmed", "timeout", "tcp-1024", "producer-80ms", "producer-140ms", "dispatch-120ms"};
        try {
            Rig r(true, 1); r.start(); r.tick(100);
            if (timeoutEnd || mode == 1) r.queue(r.plan.timeout(r.source, 0)); else r.end();
            if (mode == 2) { r.bufferedBytes = 1024; r.tick(); r.bufferedBytes = 0; }
            if (mode == 3 || mode == 4) {
                auto held=AudioFeedbackTestAccess::holdProducers(r.feedback);
                r.tick(mode == 3 ? 4 : 7);
            }
            if (mode == 5) r.now += 120000;
            r.tick(20);
            const auto packets = r.forUser(2);
            const auto close = r.feedback.clip(AudioFeedback::Signal::SquelchClose);
            auto first = std::find_if(packets.begin(), packets.end(), [&](const Heard &h) { return h.segment == RadioNoise::Segment::Close; });
            CHECK(first != packets.end() && first != packets.begin());
            CHECK(std::size_t(packets.end() - first) == close.size());
            for (std::size_t i = 0; i < close.size(); ++i) {
                CHECK((first + std::ptrdiff_t(i))->segment == RadioNoise::Segment::Close);
                CHECK((first + std::ptrdiff_t(i))->last == (i + 1 == close.size()));
            }
            std::cout << names[mode] << " timeout=" << (timeoutEnd || mode == 1) << ": close_packets=" << close.size()
                      << " noise_last=" << (first - 1)->last << " gap_us=" << first->time - (first - 1)->time
                      << " close_seq=" << first->frame << " prior_seq=" << (first - 1)->frame << '\n';
            r.decode();
            CHECK(!(first - 1)->last);
            CHECK(first->time - (first - 1)->time == 20000);
        } catch (const std::exception &e) { ++failed; std::cerr << names[mode] << ": " << e.what() << '\n'; }
    }
    return failed ? 1 : 0;
}
static void delayedLifecycleEndRegression() {
    Rig r(false,1);r.start();r.tick(10);
    const auto before=r.forUser(2);CHECK(!before.empty() && !before.back().last);
    const auto reserve=AudioFeedbackTestAccess::reserve(r.feedback,2);
    r.plan.invalidate(1);r.feedback.cancel(1);
    CHECK(AudioFeedbackTestAccess::streams(r.feedback)==0);
    CHECK(AudioFeedbackTestAccess::terminals(r.feedback)==1);
    const auto cancelledAt=r.now;
    std::vector<Heard> delivered;
    const auto deliver=[&](const AudioFeedback::Recipient &recipient,const QByteArray &bytes,std::uint64_t frame,bool last) {
        delivered.push_back({recipient,bytes,frame,last,r.now,RadioNoise::Segment::End});return true;
    };
    // Advance only the model clock: no callback during the old 80-ms drain,
    // then deliver inside the old 160-ms "must be empty" observation window.
    r.now+=160000;
    CHECK(delivered.empty());
    r.feedback.tick(r.now,deliver);
    CHECK(delivered.size()==1);
    const auto &end=delivered.front();
    CHECK(end.recipient.connection==2 && end.recipient.session==12 && end.recipient.channel==0);
    CHECK(end.last && end.frame==before.back().frame+2 && end.bytes==reserve);
    CHECK(end.time==cancelledAt+160000);
    CHECK(AudioFeedbackTestAccess::terminals(r.feedback)==0);
    for(const auto delay:{20000,160000,1000000}) {
        r.now+=delay;r.feedback.tick(r.now,deliver);CHECK(delivered.size()==1);
    }
    std::cout<<"Delayed lifecycle end: exact reserved terminal at +160ms, correct identity/sequence, no later audio: OK\n";
}
static void lifecycleRegressions() {
    delayedLifecycleEndRegression();
    for(bool open:{false,true})for(bool close:{false,true})for(bool timeout:{false,true}) {
        Rig r(true,1,open,close);r.start();r.tick(5);
        if(timeout)r.queue(r.plan.timeout(r.source,0));else r.end();
        r.tick(20);const auto h=r.forUser(2);CHECK(!h.empty() && h.back().last);
        CHECK(std::count_if(h.begin(),h.end(),[](const Heard &p){return p.last;})==1);
        CHECK(std::count_if(h.begin(),h.end(),[](const Heard &p){return p.segment==RadioNoise::Segment::Open;})==(open?1:0));
        CHECK(std::count_if(h.begin(),h.end(),[](const Heard &p){return p.segment==RadioNoise::Segment::Close;})==(close?4:0));
        for(std::size_t i=1;i<h.size();++i)CHECK(h[i].frame==h[i-1].frame+2);
        r.decode();
    }
    for(bool ended:{false,true}) {
        Rig r(true,1);r.start();r.tick(10);if(ended)r.end();
        {auto held=AudioFeedbackTestAccess::holdProducers(r.feedback);r.tick(22);}
        r.tick(8);const auto h=r.forUser(2);CHECK(h.back().last);
        CHECK(std::none_of(h.begin(),h.end(),[](const Heard &p){return p.segment==RadioNoise::Segment::Close;}));
        const auto size=h.size();r.tick(8);CHECK(r.forUser(2).size()==size);r.decode();
    }
    {
        Rig r(true,1);r.start();r.tick(8);r.now+=120000;r.tick(5);
        auto h=r.forUser(2);CHECK(h.back().last);CHECK(r.forUser(2).size()==h.size());r.decode();
    }
    {
        Rig r(true,1);r.start();r.tick(8);r.end();r.start();r.tick(12);r.end();r.tick(16);
        const auto h=r.forUser(2);CHECK(h.back().last);
        CHECK(std::count_if(h.begin(),h.end(),[](const Heard &p){return p.last;})==2);
        CHECK(std::count_if(h.begin(),h.end(),[](const Heard &p){return p.segment==RadioNoise::Segment::Close;})==4);
        r.decode(); // Old Close was invalidated, new generation has exactly one complete Close.
    }
    for(int change:{0,1,2}) {
        Rig r(true,1);r.start();r.tick(8);r.end();
        if(change==0) {r.plan.invalidate(1);r.feedback.cancel(1);} // Sender disconnect: end, never Close.
        if(change==1) {r.plan.invalidate(2);r.feedback.cancel(2);r.receivers[0].channel=1;}
        if(change==2) {r.plan.invalidate(2);r.feedback.cancel(2);r.receivers[0].connection=9;} // Reused session.
        r.tick(10);const auto h=r.forUser(2);
        CHECK(std::none_of(h.begin(),h.end(),[](const Heard &p){return p.segment==RadioNoise::Segment::Close;}));
        CHECK(r.forUser(9).empty());
    }
    {
        Rig r(true,1);r.start();r.tick(10);r.end();
        for(int i=0;i<12;++i) {
            r.tick();auto h=r.forUser(2);
            if(!h.empty() && h.back().segment==RadioNoise::Segment::Close)break;
        }
        {auto held=AudioFeedbackTestAccess::holdProducers(r.feedback);r.tick(15);}
        r.tick(8);CHECK(AudioFeedbackTestAccess::streams(r.feedback)==0);
        CHECK(r.forUser(2).back().last);r.decode();
    }
    {
        Rig r(true,1);r.start();r.tick(8);r.end();r.bufferedBytes=65536;r.tick(20);
        CHECK(AudioFeedbackTestAccess::streams(r.feedback)==0);
        auto size=r.heard.size();r.bufferedBytes=0;r.tick(10);CHECK(r.heard.size()==size);
        // A permanently blocked transport cannot receive an end packet. Abort
        // boundedly, without relaxing either TCP budget or playing stale Close.
    }
    std::cout<<"Unified lifecycle: disabled phases, confirmed/timeout, >300ms starvation, delayed tick, rapid PTT, disconnect/channel/reuse: OK\n";
}
// A precise Qt timer has fixed deadlines, but callback entry time still jitters.
// Alternating 21/19-ms callback spacing must not halve a 20-ms audio stream.
static void timerJitterRegression() {
    Rig r(true,1);r.start();r.tick(10);const auto start=r.now;const auto before=r.heard.size();
    for(int i=0;i<500;++i) {r.now=start+std::int64_t(i)*20000+(i%2==0?1000:0);r.tick();}
    const auto count=r.heard.size()-before;
    std::int64_t longest=0;
    for(std::size_t i=before+1;i<r.heard.size();++i)longest=std::max(longest,r.heard[i].time-r.heard[i-1].time);
    std::cout<<"Timer jitter: callbacks=500 packets="<<count<<" maximum_gap_us="<<longest<<std::endl;
    CHECK(count>=499 && count<=501);CHECK(longest<=21000);
    r.end();r.tick(20);const auto h=r.forUser(2);CHECK(h.back().last);r.decode();
}
// A real second-thread reader holds the same lock type used by Server. Test-only
// synchronization may wait; production keeps tryLockForWrite()/producer try-locks.
struct HeldVoiceLock {
    std::promise<void> ready, release;
    std::thread thread;
    explicit HeldVoiceLock(QReadWriteLock &lock) : thread([&] {
        QReadLocker held(&lock);ready.set_value();release.get_future().wait();
    }) {ready.get_future().wait();}
    ~HeldVoiceLock(){release.set_value();thread.join();}
};
static void adaptiveTimingRegressions() {
    for(int mode=0;mode<7;++mode) {
        const std::int64_t duration=mode==6?62000000:2000000;
        Rig r(true,1);r.start();QReadWriteLock voice;
        std::unique_ptr<HeldVoiceLock> heldVoice;
        std::vector<std::unique_lock<std::mutex>> heldProducers;
        std::int64_t next=0,nextSkip=200000;int retries=0,skips=0;
        while(next<duration) {
            r.now=next;
            if((mode==1 || mode==6) && r.now>=nextSkip) {
                // Qt coalesces missed expirations; there is no callback to tick().
                r.now+=20000;nextSkip+=200000;++skips;
            }
            if(mode==2 && r.now>=300000 && r.now<320000 && !heldVoice)heldVoice.reset(new HeldVoiceLock(voice));
            if(mode==2 && r.now>=320000)heldVoice.reset();
            if(mode==3 && r.now>=300000 && r.now<360000 && heldProducers.empty())
                heldProducers=AudioFeedbackTestAccess::holdProducers(r.feedback);
            if(mode==3 && r.now>=360000)heldProducers.clear();
            r.bufferedBytes=mode==4 && r.now>=300000 && r.now<360000?1024:0;
            if(mode==5 && r.now>=300000 && !skips) {r.now+=120000;++skips;} // Controlled abort, not a normal gap.
            if(!voice.tryLockForWrite()) {
                ++retries;next=r.now+AudioFeedback::RetryIntervalMs*1000;continue;
            }
            const auto at=r.now;r.tick();voice.unlock();
            const int delay=r.feedback.nextTickDelayMs(at);CHECK(delay>=1 && delay<=20);
            next=at+delay*1000;
        }
        heldVoice.reset();heldProducers.clear();
        const auto live=r.forUser(2);
        if(mode==5) {
            CHECK(live.back().last);CHECK(AudioFeedbackTestAccess::streams(r.feedback)==0);
        } else {
            CHECK(live.size()>=std::size_t(duration/20000-2));
            CHECK(std::none_of(live.begin(),live.end(),[](const Heard &h){return h.last;}));
            CHECK(AudioFeedbackTestAccess::streams(r.feedback)==1);
        }
        r.now=duration;r.end();const auto end=r.now;r.tick(20);const auto all=r.forUser(2);
        CHECK(all.back().last);
        const auto first=std::find_if(all.begin(),all.end(),[](const Heard &h){return h.segment==RadioNoise::Segment::Close;});
        if(mode!=5) {
            CHECK(first!=all.end() && first->time-end<=Plan::MaxCloseCueAgeUs);
            CHECK(std::size_t(all.end()-first)==r.feedback.clip(AudioFeedback::Signal::SquelchClose).size());
            CHECK(std::count_if(all.begin(),all.end(),[](const Heard &h){return h.last;})==1);
        }
        for(std::size_t i=1;i<all.size();++i)CHECK(all[i].frame==all[i-1].frame+2);
        if(mode==2)CHECK(retries>=4);
        r.decode();
        std::cout<<"Adaptive timing: mode="<<mode<<" duration_us="<<duration<<" live_packets="<<live.size()
                 <<" missed_ticks="<<skips<<" voice_lock_retries="<<retries<<": OK\n";
    }
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--lifecycle-end-probe") {
        try {delayedLifecycleEndRegression();return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 2 && std::string(argv[1]) == "--timing-probe") {
        try {timerJitterRegression();adaptiveTimingRegressions();return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 2 && std::string(argv[1]) == "--timer-probe") {
        try {timerJitterRegression();return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 2 && std::string(argv[1]) == "--short-close-probe") return shortCloseProbe();
    try {
        CHECK(shortCloseProbe()==0);
        lifecycleRegressions();
        for (bool timeoutEnd : {false, true}) {
            // Both end reasons after >60 s retain ONE encoder/sequence stream,
            // no intermediate end marker and one complete, timely Close.
            Rig r(true, 1); r.start(); r.tick(3100);
            std::cout << "Long end at " << r.now << " us, timeout=" << timeoutEnd << "\n";
            if (timeoutEnd) r.queue(r.plan.timeout(r.source, 0)); else r.end();
            r.tick(20);
            const auto packets = r.forUser(2);
            const auto close = r.feedback.clip(AudioFeedback::Signal::SquelchClose);
            auto first = std::find_if(packets.begin(), packets.end(), [&](const Heard &h) { return h.segment == RadioNoise::Segment::Close; });
            CHECK(first != packets.end() && first != packets.begin());
            CHECK(!(first - 1)->last);
            CHECK(first->time - (first - 1)->time == 20000);
            CHECK(first->time <= 62000000 + Plan::MaxCloseCueAgeUs);
            CHECK(first->frame == (first - 1)->frame + 2);
            CHECK(std::size_t(packets.end() - first) == close.size());
            for (std::size_t i = 0; i < close.size(); ++i) {
                CHECK((first + std::ptrdiff_t(i))->segment == RadioNoise::Segment::Close);
                CHECK((first + std::ptrdiff_t(i))->last == (i + 1 == close.size()));
            }
            r.decode();
        }
        {
            Rig r; CHECK(r.plan.observe(r.source, false, 0, true, false, true, r.receivers).empty());
            r.tick(5); CHECK(r.heard.empty()); r.start(); r.tick(20);
            const auto a = r.forUser(2), b = r.forUser(3); CHECK(a.size() > 5 && b.size() > 5);
            CHECK(a[2].bytes == b[2].bytes); // Shared ORIGINAL random start, separate encoders.
            const auto open = r.feedback.clip(AudioFeedback::Signal::SquelchOpen);
            CHECK(a[0].segment == RadioNoise::Segment::Open && !a[0].last);
            CHECK(a[1].segment == RadioNoise::Segment::Noise && !a[1].last);
            r.end(); r.tick(15); const auto ended = r.forUser(2);
            const auto close = r.feedback.clip(AudioFeedback::Signal::SquelchClose);
            CHECK(ended.size() >= close.size());
            for (std::size_t i = 0; i < close.size(); ++i) CHECK(ended[ended.size() - close.size() + i].segment == RadioNoise::Segment::Close);
            CHECK(ended.back().last); const auto size = r.heard.size(); r.tick(10); CHECK(r.heard.size() == size); r.decode();
        }
        {
            Rig r(false); r.start(); r.tick(10); auto before = r.forUser(2); CHECK(before.size() > 2);
            CHECK(r.feedback.request(AudioFeedback::Signal::Busy, {2, 12, 0}, r.now)); r.tick(24);
            auto after = r.forUser(2); const auto busy = r.feedback.clip(AudioFeedback::Signal::Busy);
            auto first = std::find_if(after.begin(), after.end(), [&](const Heard &h) { return h.bytes == busy[0]; });
            CHECK(first != after.end() && first != after.begin() && (first - 1)->last);
            for (std::size_t i = 0; i < busy.size(); ++i) CHECK((first + std::ptrdiff_t(i))->bytes == busy[i]);
            CHECK(std::size_t(after.end() - first) > busy.size()); // Noise resumed, no new start event.
            CHECK(r.forUser(3).size() > before.size()); r.plan.invalidate(1); r.tick(3);
            const auto stopped = r.heard.size(); r.tick(5); CHECK(r.heard.size() == stopped); r.decode();
        }
        {
            Rig r; r.start(); r.tick(10); r.queue(r.plan.timeout(r.source, 0));
            CHECK(r.plan.timeout(r.source, 0).empty()); r.tick(15);
            auto a = r.forUser(2); CHECK(a.back().last);
            const auto close = r.feedback.clip(AudioFeedback::Signal::SquelchClose);
            CHECK(std::count_if(a.begin(), a.end(), [&](const Heard &h) { return h.segment == RadioNoise::Segment::Close; }) == int(close.size()));
        }
        {
            Rig r; r.start(true); r.tick(20); const auto a = r.forUser(2);
            CHECK(a.size() >= 5 && a.back().last);
            CHECK(a.front().segment == RadioNoise::Segment::Open);
            CHECK(std::count_if(a.begin(),a.end(),[](const Heard &h){return h.last;})==1); // One complete stream even for one speech packet.
        }
        {
            Rig r(false); r.start(); r.tick(7); r.plan.invalidate(2); r.feedback.cancel(2); r.tick(3);
            const auto count = r.forUser(2).size(); r.tick(5); CHECK(r.forUser(2).size() == count);
            r.plan.invalidate(1); r.tick(5); r.source.connection = 8; r.start(); r.tick(8);
            CHECK(r.forUser(3).size() > 8); // New generation, same channel/session is not old work.
        }
        {
            Rig r(false); r.start(); r.tick(10);
            CHECK(r.feedback.request(AudioFeedback::Signal::Busy, {2, 12, 0}, r.now)); r.tick(5);
            r.plan.invalidate(1); r.tick(25);
            auto packets = r.forUser(2); auto busy = r.feedback.clip(AudioFeedback::Signal::Busy);
            CHECK(packets.back().bytes == busy.back() && packets.back().last);
            auto size = packets.size(); r.tick(8); CHECK(r.forUser(2).size() == size);
        }
        {
            Rig r(true, 17); r.start(); r.tick(20);
            int noisy = 0; for (const auto &who : r.receivers) if (r.forUser(who.connection).size() > 2) ++noisy;
            CHECK(noisy > 0); // Optional underruns may rotate slots; per-tick cap is asserted above.
            r.end(); r.tick(18);
            int omitted=0;
            for (const auto &who : r.receivers) { auto h = r.forUser(who.connection);
                if(h.empty()) ++omitted; else CHECK(h.size() >= 5 && h.back().last);
            }
            CHECK(omitted >= 1); // Fixed 16-worker cap: omit optional effects, never spawn a 17th encoder.
        }
        std::cout << "Noise scheduler: accepted/denied, shared start, Open/Close, Busy/resume, timeout, short bursts, lifecycle, 16-slot cap: OK\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
