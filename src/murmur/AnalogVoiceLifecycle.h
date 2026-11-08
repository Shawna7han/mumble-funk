// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#pragma once
#include "AnalogVoiceService.h"
#include "SquelchPlan.h"

// Main-thread adapter only. No audio, queue, worker or effect renderer. State is
// bounded by the service's existing lanes and replaced on generation changes.
class AnalogVoiceLifecycle {
    using Service = funkvoice::AnalogVoiceService;
    struct State {
        std::uint64_t generation = 0, observed = 0;
        bool started = false, failed = false;
    };
    std::array<State, Service::MaxTransmissions> m_states{};
    std::uint64_t m_observations = 0;
    State &state(const Service::Batch &batch) {
        auto &value = m_states.at(batch.handle.lane);
        if (value.generation != batch.handle.generation)
            value = {batch.handle.generation, 0, false, false};
        return value;
    }
    static SquelchPlan::Identity identity(const Service::Identity &id) {
        return {id.connection, id.session, id.channel};
    }
    static std::int64_t timeUs(Service::Time time) {
        return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
    }
    bool failed(SquelchPlan &plan, const Service::Batch &batch, State &value) {
        if (batch.failure != Service::Failure::None) {
            if (value.started && !value.failed) plan.invalidate(batch.completed.sender.connection);
            value.failed = true;
        }
        return value.failed;
    }
public:
    std::uint64_t observations() const { return m_observations; }
    // Call after each actual network handoff, including an empty delivery set.
    std::vector<SquelchPlan::Cue> delivered(SquelchPlan &plan, const Service::Batch &batch,
                                           std::size_t packet, const Service::Recipients &delivered) {
        auto &value = state(batch);
        if (failed(plan, batch, value)) return {};
        const auto sender = identity(batch.completed.sender);
        std::vector<SquelchPlan::Identity> recipients;
        for (std::size_t i = 0; i < delivered.count; ++i)
            recipients.push_back(identity(delivered.identities[i]));
        if (value.started) plan.retainRecipients(sender, recipients);
        std::vector<SquelchPlan::Cue> cues;
        if (recipients.empty()) return cues;
        for (std::size_t i = 0; i < batch.sourceCounts[packet]; ++i) {
            const auto &source = batch.sources[packet][i];
            // Technical spans have no originals. The service emits source spans
            // in media order; a monotone ticket watermark deduplicates lookahead.
            if (!source.samples || source.technical || source.generation != value.generation
                || source.channelTicket <= value.observed) continue;
            value.observed = source.channelTicket;
            ++m_observations;
            value.started = true;
            // Even a payload+marker may still have later encoded output. End is
            // handled separately after the terminal batch, never per output span.
            const auto next = plan.observe(sender, true, source.sourceFrame, true, false, true, recipients);
            cues.insert(cues.end(), next.begin(), next.end());
        }
        return cues;
    }
    std::vector<SquelchPlan::Cue> complete(SquelchPlan &plan, const Service::Batch &batch) {
        auto &value = state(batch);
        if (failed(plan, batch, value) || !batch.terminal || !value.started) return {};
        const auto &end = batch.completed;
        std::vector<SquelchPlan::Cue> cues;
        if (!end.technical && !end.samples && end.channelTicket > value.observed) {
            // Empty original marker has no Source span, but still belongs to
            // this ordered terminal batch. It cannot create an Open.
            value.observed = end.channelTicket;
            ++m_observations;
            cues = plan.observe(identity(end.sender), true, end.sourceFrame, false, true, false, {}, timeUs(end.accepted));
        } else {
            // Same completion operation for timeout, but caller retains the
            // distinction and only requests its explicitly enabled fallback.
            cues = plan.end(identity(end.sender), end.sourceFrame, timeUs(end.accepted));
        }
        value.started = false; // Repeat completion never creates another Close.
        return cues;
    }
};
