// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
#include "RadioNoise.h"
#include "AudioSignal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <opus.h>
#include <stdexcept>

std::atomic<unsigned> RadioNoise::s_instances{0};
RadioNoise::PCM RadioNoise::load(const QString &path) {
    return std::make_shared<const std::vector<float>>(AudioSignal::loadWav(path, AudioSignal::LengthPolicy::RadioNoise));
}
std::size_t RadioNoise::chooseStart(std::size_t samples, std::mt19937 &random) {
    if (!samples) throw std::invalid_argument("Empty noise recording");
    return std::uniform_int_distribution<std::size_t>(0, (samples - 1) / FrameSamples)(random) * FrameSamples;
}
RadioNoise::RadioNoise(PCM pcm, int volume, std::size_t start, bool paused, PCM open, PCM close)
    : m_pcm(std::move(pcm)), m_open(std::move(open)), m_close(std::move(close)), m_cursor(start), m_taken(start) {
    if (!m_pcm || m_pcm->size() < FrameSamples || m_pcm->size() > 48000 * 60 || start >= m_pcm->size()
        || volume < 0 || volume > 100) throw std::invalid_argument("Invalid noise recording/start/volume");
    for (const auto &signal : {m_open, m_close}) {
        if (!signal) continue;
        if (signal->empty() || signal->size() > 48000) throw std::invalid_argument("Invalid stream signal length");
        for (float sample : *signal) if (!std::isfinite(sample) || std::abs(sample) > 1)
            throw std::invalid_argument("Invalid stream signal level");
    }
    float peak = 0;
    for (float sample : *m_pcm) {
        if (!std::isfinite(sample) || std::abs(sample) > 1) throw std::invalid_argument("Invalid noise level");
        peak = std::max(peak, std::abs(sample));
    }
    // Independent level, no global feedback boost or upward normalization.
    m_gain = (peak > .2f ? .2f / peak : 1.f) * static_cast<float>(volume) / 100.f;
    if (!volume) return; // Truly silent: no encoder, thread or packets.
    if (s_instances.fetch_add(1) >= MaxInstances) {
        s_instances.fetch_sub(1); throw std::runtime_error("Noise producer capacity reached");
    }
    m_active = !paused;
    try { m_worker = std::thread(&RadioNoise::run, this); }
    catch (...) { s_instances.fetch_sub(1); throw; }
}
RadioNoise::~RadioNoise() {
    { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; }
    m_ready.notify_one();
    if (m_worker.joinable()) { m_worker.join(); s_instances.fetch_sub(1); }
}
bool RadioNoise::take(Packet &packet) {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || !m_active || m_packets.empty()) return false;
    packet = std::move(m_packets.front()); m_packets.pop_front();
    m_taken = packet.nextSample;
    m_ready.notify_one(); return true;
}
bool RadioNoise::tryPause() {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock) return false;
    if (m_active) { m_active = false; ++m_epoch; m_packets.clear(); m_cursor = m_taken; }
    return true;
}
bool RadioNoise::tryRestart(std::size_t start, bool withOpen) {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || !m_worker.joinable() || !m_error.isEmpty() || start >= m_pcm->size()) return false;
    ++m_epoch; m_packets.clear(); m_cursor = m_taken = start; m_frame += 100;
    m_withOpen = withOpen; m_finish = m_withClose = m_producedEnd = false;
    m_active = true; m_ready.notify_one(); return true;
}
bool RadioNoise::tryFinish(bool withClose) {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || !m_active) return false;
    if (!m_finish) { m_finish = true; m_withClose = withClose; }
    m_ready.notify_one(); return true;
}
void RadioNoise::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) return;
    m_active = false; ++m_epoch; m_packets.clear(); m_cursor = m_taken;
    m_ready.notify_one();
}
void RadioNoise::resume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_active || !m_worker.joinable() || !m_error.isEmpty()) return;
    ++m_epoch; m_frame += 100; m_active = true; m_finish = m_withClose = m_producedEnd = false; m_withOpen = false; m_ready.notify_one();
}
bool RadioNoise::current(std::uint64_t epoch) const {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    return lock && m_active && epoch == m_epoch;
}
std::size_t RadioNoise::buffered() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_packets.size();
}
QString RadioNoise::error() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_error;
}
void RadioNoise::run() noexcept {
    try {
        int status = 0;
        std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)> encoder(
            opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &status), opus_encoder_destroy);
        if (!encoder || status || opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(24000)) != OPUS_OK)
            throw std::runtime_error("Cannot create noise encoder");
        std::uint64_t encoderEpoch = 0;
        Segment segment = Segment::Noise;
        std::size_t signalCursor = 0;
        int lookahead = 0;
        if (opus_encoder_ctl(encoder.get(), OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK || lookahead < 0 || lookahead > 960)
            throw std::runtime_error("Unexpected stream encoder lookahead");
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true) {
            m_ready.wait(lock, [&] { return m_stop || (m_active && !m_producedEnd && m_packets.size() < QueueLimit); });
            if (m_stop) break;
            const auto epoch = m_epoch;
            const auto cursor = m_cursor;
            const bool withOpen = m_withOpen, finish = m_finish, withClose = m_withClose;
            lock.unlock(); // Encoding never holds the control/consumer mutex.
            if (encoderEpoch != epoch) {
                if (opus_encoder_ctl(encoder.get(), OPUS_RESET_STATE) != OPUS_OK)
                    throw std::runtime_error("Cannot reset noise encoder");
                encoderEpoch = epoch;
                segment = withOpen && m_open && !m_open->empty() ? Segment::Open : Segment::Noise;
                signalCursor = 0;
            }
            if (finish && segment != Segment::Close && segment != Segment::End
                && !(segment == Segment::Open && signalCursor == 0)) {
                segment = withClose && m_close && !m_close->empty() ? Segment::Close : Segment::End;
                signalCursor = 0;
            }
            const auto packetSegment = segment;
            std::size_t nextCursor = cursor;
            std::array<float, FrameSamples> frame{};
            bool last = false;
            for (std::size_t i = 0; i < frame.size(); ++i) {
                if (segment == Segment::Open) {
                    frame[i] = (*m_open)[signalCursor++];
                    if (signalCursor == m_open->size()) { segment = Segment::Noise; signalCursor = 0; }
                } else if (segment == Segment::Noise) {
                    frame[i] = (*m_pcm)[nextCursor] * m_gain;
                    nextCursor = (nextCursor + 1) % m_pcm->size();
                } else {
                    const std::size_t count = segment == Segment::Close ? m_close->size() : 0;
                    if (signalCursor < count) frame[i] = (*m_close)[signalCursor];
                    if (++signalCursor >= count + std::size_t(lookahead)) { last = true; break; }
                }
            }
            std::array<unsigned char, 512> bytes;
            const int size = opus_encode_float(encoder.get(), frame.data(), FrameSamples, bytes.data(), bytes.size());
            if (size < 0) throw std::runtime_error("Cannot encode noise frame");
            lock.lock();
            if (!m_active || epoch != m_epoch || m_stop) continue; // Old work cannot cross Busy/stop.
            m_packets.push_back({QByteArray(reinterpret_cast<const char *>(bytes.data()), size), epoch, m_frame, cursor, packetSegment, last, nextCursor});
            m_cursor = nextCursor; m_frame += 2; m_producedEnd = last;
        }
    } catch (const std::exception &error) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = QString::fromUtf8(error.what()); m_active = false; m_packets.clear();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = QStringLiteral("Unexpected noise worker failure"); m_active = false; m_packets.clear();
    }
}
