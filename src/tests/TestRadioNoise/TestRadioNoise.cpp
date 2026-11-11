// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "RadioNoise.h"
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <QtCore/QCryptographicHash>
#include <iostream>
#include <opus.h>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class F> void rejects(F f) { bool failed = false; try { f(); } catch (const std::exception &) { failed = true; } CHECK(failed); }
static RadioNoise::Packet next(RadioNoise &noise) {
    RadioNoise::Packet packet;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!noise.take(packet)) {
        CHECK(noise.error().isEmpty()); CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    CHECK(noise.buffered() <= RadioNoise::QueueLimit); return packet;
}
static QByteArray wav(int samples) {
    QByteArray b(44 + samples * 2, 0);
    auto u16 = [&](int at, quint16 v) { qToLittleEndian(v, b.data() + at); };
    auto u32 = [&](int at, quint32 v) { qToLittleEndian(v, b.data() + at); };
    b.replace(0, 4, "RIFF"); b.replace(8, 8, "WAVEfmt "); b.replace(36, 4, "data");
    u32(4, quint32(b.size() - 8)); u32(16, 16); u16(20, 1); u16(22, 1); u32(24, 48000);
    u32(28, 96000); u16(32, 2); u16(34, 16); u32(40, quint32(samples * 2));
    // Programmatically generated periodic test tone; no third-party recordings.
    for (int i = 0; i < samples; ++i) u16(44 + i * 2, static_cast<quint16>(static_cast<qint16>(3000 * std::sin(2 * 3.141592653589793 * i / 97))));
    return b;
}
static QString save(const QString &dir, const QByteArray &bytes) {
    const QString path = dir + "/generated.wav"; QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly)); CHECK(file.write(bytes) == bytes.size()); return path;
}
static void loading(const QString &dir) {
    const auto original = wav(1699201); // ~35.4 s, deliberately not divisible by 960.
    auto path = save(dir, original); const auto pcm = RadioNoise::load(path);
    CHECK(pcm->size() == 1699201);
    for (std::size_t i = 0; i < pcm->size(); ++i)
        CHECK((*pcm)[i] == qFromLittleEndian<qint16>(original.constData() + 44 + i * 2) / 32768.f);
    QFile unchanged(path); CHECK(unchanged.open(QIODevice::ReadOnly)); CHECK(unchanged.readAll() == original);
    for (int field : {0, 4, 8, 16, 20, 22, 24, 28, 32, 34, 40}) {
        auto bad = original; bad[field] = char(bad.at(field) ^ 1);
        rejects([&] { RadioNoise::load(save(dir, bad)); });
    }
    rejects([&] { RadioNoise::load(save(dir, wav(959))); });
    rejects([&] { RadioNoise::load(save(dir, wav(48000 * 60 + 1))); });
    rejects([&] { RadioNoise::load(save(dir, QByteArray(8 * 1024 * 1024 + 1, 0))); });
    rejects([&] { RadioNoise::load(dir + "/missing.wav"); });
    std::cout << "WAV validation/exact samples/original unchanged: OK\n";
}
static void continuity(RadioNoise::PCM pcm, int count, std::size_t start) {
    RadioNoise noise(pcm, 100, start);
    int error = 0;
    std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)> reference(
        opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error), opus_encoder_destroy);
    std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder(
        opus_decoder_create(48000, 1, &error), opus_decoder_destroy);
    CHECK(reference && decoder && !error);
    CHECK(opus_encoder_ctl(reference.get(), OPUS_SET_BITRATE(24000)) == OPUS_OK);
    std::array<float, 960> input, output; std::array<unsigned char, 512> encoded;
    float peak = 0; for (float sample : *pcm) peak = std::max(peak, std::abs(sample));
    const float gain = (peak > .2f ? .2f / peak : 1.f) * 100.f / 100.f;
    std::size_t cursor = start;
    for (int n = 0; n < count; ++n) {
        const auto packet = next(noise);
        CHECK(packet.epoch == 1 && packet.frame == std::uint64_t(n) * 2 && packet.firstSample == cursor);
        // Independent sample-index reference catches padding, skipped samples,
        // speed changes and encoder resets at every loop boundary.
        for (float &sample : input) { sample = (*pcm)[cursor] * gain; if (++cursor == pcm->size()) cursor = 0; }
        int size = opus_encode_float(reference.get(), input.data(), 960, encoded.data(), encoded.size());
        CHECK(size > 0 && packet.opus == QByteArray(reinterpret_cast<const char *>(encoded.data()), size));
        CHECK(opus_packet_get_nb_samples(reinterpret_cast<const unsigned char *>(packet.opus.data()), packet.opus.size(), 48000) == 960);
        CHECK(opus_decode_float(decoder.get(), encoded.data(), size, output.data(), 960, 0) == 960);
        for (float sample : output) CHECK(std::isfinite(sample) && std::abs(sample) < 1);
    }
}
static void lifecycle(RadioNoise::PCM pcm) {
    std::mt19937 rng(2345); bool different = false; const auto first = RadioNoise::chooseStart(pcm->size(), rng);
    for (int i = 0; i < 100; ++i) {
        auto start = RadioNoise::chooseStart(pcm->size(), rng); CHECK(start < pcm->size() && start % 960 == 0); different |= start != first;
    }
    CHECK(different); rejects([&] { RadioNoise::chooseStart(0, rng); });
    RadioNoise a(pcm, 20, first), b(pcm, 20, first);
    RadioNoise::Packet last;
    for (int i = 0; i < 8; ++i) { last = next(a); CHECK(last.opus == next(b).opus); }
    a.pause(); CHECK(!a.current(last.epoch)); CHECK(a.buffered() == 0); CHECK(!a.take(last));
    // The other recipient continues unaffected while the first hears Busy.
    const auto continued = next(b); CHECK(continued.frame == 16);
    a.resume(); const auto resumed = next(a);
    CHECK(resumed.epoch > last.epoch && resumed.frame > last.frame);
    CHECK(resumed.firstSample == (last.firstSample + 960) % pcm->size());
    // Re-entry requires a fresh decoder segment; never splice into a running
    // shared decoder. Compare with a clean encoder at the same PCM position.
    RadioNoise fresh(pcm, 20, resumed.firstSample); CHECK(resumed.opus == next(fresh).opus);
    int error = 0; auto decoder = opus_decoder_create(48000, 1, &error); CHECK(decoder && !error);
    std::array<float, 960> output;
    CHECK(opus_decode_float(decoder, reinterpret_cast<const unsigned char *>(resumed.opus.data()), resumed.opus.size(), output.data(), 960, 0) == 960);
    opus_decoder_destroy(decoder);
    for (int i = 0; i < 100; ++i) { a.pause(); a.resume(); }
    const auto final = next(a); CHECK(final.epoch > resumed.epoch);
    RadioNoise zero(pcm, 0, first); zero.resume(); CHECK(!zero.take(last) && zero.buffered() == 0);
    rejects([&] { RadioNoise invalid(pcm, -1, first); });
    rejects([&] { RadioNoise invalid(pcm, 101, first); });
    rejects([&] { RadioNoise invalid(pcm, 20, pcm->size()); });
    std::cout << "Random start/shared PCM/independent interruption/fresh re-entry/zero: OK\n";
}
// Compare every packet across all three phases against ONE independent encoder.
// This catches resets and queued-packet loss, not merely decodability.
static void unified(RadioNoise::PCM noisePCM, bool shortBurst, bool withOpen, bool withClose) {
    auto open = std::make_shared<const std::vector<float>>(897, .07f);
    auto close = std::make_shared<const std::vector<float>>(2880, -.05f);
    RadioNoise producer(noisePCM, 100, 0, true, withOpen ? open : RadioNoise::PCM{}, withClose ? close : RadioNoise::PCM{});
    CHECK(producer.tryRestart(0, withOpen));
    int error=0, lookahead=0;
    std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)> encoder(opus_encoder_create(48000,1,OPUS_APPLICATION_AUDIO,&error),opus_encoder_destroy);
    std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder(opus_decoder_create(48000,1,&error),opus_decoder_destroy);
    CHECK(encoder && decoder && !error);
    CHECK(opus_encoder_ctl(encoder.get(),OPUS_SET_BITRATE(24000))==OPUS_OK);
    CHECK(opus_encoder_ctl(encoder.get(),OPUS_GET_LOOKAHEAD(&lookahead))==OPUS_OK);
    std::size_t noiseCursor=0, openCursor=0, closeCursor=0; bool closing=false, finishing=false;
    float peak=0;for(float v:*noisePCM)peak=std::max(peak,std::abs(v));const float gain=peak>.2f?.2f/peak:1.f;
    int count=0, closes=0;
    while(true) {
        if(!finishing && count==(shortBurst?1:100)) {
            while(!producer.tryFinish(withClose)) std::this_thread::yield();
            finishing=true;
        }
        auto packet=next(producer); CHECK(packet.frame==100+std::uint64_t(count)*2);
        const bool closePhase=packet.segment==RadioNoise::Segment::Close || packet.segment==RadioNoise::Segment::End;
        if(closePhase) {closing=true;++closes;}
        std::array<float,960> pcm{},decoded{};std::array<unsigned char,512> bytes{};
        for(std::size_t i=0;i<pcm.size();++i) {
            if(closing) {
                if(withClose && closeCursor<close->size())pcm[i]=(*close)[closeCursor];
                ++closeCursor;
            } else if(withOpen && openCursor<open->size())pcm[i]=(*open)[openCursor++];
            else {pcm[i]=(*noisePCM)[noiseCursor]*gain;noiseCursor=(noiseCursor+1)%noisePCM->size();}
        }
        int size=opus_encode_float(encoder.get(),pcm.data(),960,bytes.data(),bytes.size());
        CHECK(size>0 && packet.opus==QByteArray(reinterpret_cast<const char *>(bytes.data()),size));
        CHECK(opus_decode_float(decoder.get(),bytes.data(),size,decoded.data(),960,0)==960);
        for(float v:decoded)CHECK(std::isfinite(v) && std::abs(v)<1);
        ++count;CHECK(count<120);
        if(packet.last)break;
    }
    CHECK(finishing && closes==(withClose?4:1));
    CHECK(!withOpen || openCursor==open->size());
    std::cout<<"Unified encoder: short="<<shortBurst<<" open="<<withOpen<<" close="<<withClose<<" packets="<<count<<": OK\n";
}
int main(int argc, char **argv) {
    try {
        QTemporaryDir dir; CHECK(dir.isValid()); loading(dir.path());
        const auto pcm = RadioNoise::load(save(dir.path(), wav(1699201)));
        const auto begin = std::chrono::steady_clock::now(); const auto cpu = std::clock();
        continuity(pcm, 5400, 17280); // More than three complete ~35.4-s loops.
        continuity(RadioNoise::load(save(dir.path(), wav(1007))), 100, 960);
        std::cout << "5500 continuously decoded 20-ms frames: CPU=" << double(std::clock() - cpu) / CLOCKS_PER_SEC
                  << "s wall=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() << "s\n";
        lifecycle(pcm);
        for(bool early:{false,true})for(bool open:{false,true})for(bool close:{false,true})unified(pcm,early,open,close);
        if (argc == 2) {
            QFile file(QString::fromLocal8Bit(argv[1])); CHECK(file.open(QIODevice::ReadOnly));
            const auto original = file.readAll();
            CHECK(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex() == "dc0a1c97115eb797f261af52631288b868a648d0d9310fff0a431e8163536312");
            auto real = RadioNoise::load(file.fileName()); CHECK(real->size() == 1698926);
            continuity(real, 5400, 17280); file.seek(0); CHECK(file.readAll() == original);
            std::cout << "Private real WAV: exact hash/format, 3+ loops/reference/decode, unchanged: OK\n";
        }
        std::vector<std::unique_ptr<RadioNoise>> bounded;
        for (int i = 0; i < RadioNoise::MaxInstances; ++i) bounded.emplace_back(new RadioNoise(pcm, 20, 0));
        rejects([&] { RadioNoise overflow(pcm, 20, 0); });
        std::cout << "16-producer cap and bounded queues: OK; shared PCM=" << pcm->size() * sizeof(float) << " bytes\n";
    } catch (const std::exception &error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
