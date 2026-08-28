// SPDX-FileCopyrightText: Copyright 2026 Strato Audio Contributors
// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <array>
#include <limits>

#include <audio_core/common/bit_cast.h>
#include <audio_core/renderer/adsp/command_list_processor.h>
#include <audio_core/renderer/command/effect/biquad_filter_and_mix.h>

namespace AudioCore::AudioRenderer {
namespace {

using FilterParameter = SplitterDestinationData::BiquadFilterParameter2;
using FilterState = VoiceState::BiquadFilterState;

struct RuntimeState {
    f64 x1{};
    f64 x2{};
    f64 y1{};
    f64 y2{};
};

RuntimeState LoadState(const FilterState& state) {
    return {
        Common::BitCast<f64>(state.s0),
        Common::BitCast<f64>(state.s1),
        Common::BitCast<f64>(state.s2),
        Common::BitCast<f64>(state.s3),
    };
}

void StoreState(FilterState& state, const RuntimeState& runtime) {
    state.s0 = Common::BitCast<s64>(runtime.x1);
    state.s1 = Common::BitCast<s64>(runtime.x2);
    state.s2 = Common::BitCast<s64>(runtime.y1);
    state.s3 = Common::BitCast<s64>(runtime.y2);
}

f64 ProcessSample(const FilterParameter& filter, RuntimeState& state, const f64 input) {
    const f64 output{
        input * static_cast<f64>(filter.numerator[0]) +
        state.x1 * static_cast<f64>(filter.numerator[1]) +
        state.x2 * static_cast<f64>(filter.numerator[2]) +
        state.y1 * static_cast<f64>(filter.denominator[0]) +
        state.y2 * static_cast<f64>(filter.denominator[1])};

    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = output;
    return output;
}

s32 ClampSample(const f64 sample) {
    constexpr f64 minimum{static_cast<f64>(std::numeric_limits<s32>::min())};
    constexpr f64 maximum{static_cast<f64>(std::numeric_limits<s32>::max())};
    return static_cast<s32>(std::clamp(sample, minimum, maximum));
}

void PrepareState(FilterState& state, FilterState& previous, const bool needs_init,
                  const bool is_first_mix_buffer) {
    if (is_first_mix_buffer) {
        if (needs_init)
            state = {};
        previous = state;
    } else {
        state = previous;
    }
}

bool VerifyIndexes(const ADSP::CommandListProcessor& processor, const s16 input,
                   const s16 output) {
    if (!processor.sample_count || input < 0 || output < 0)
        return false;

    const auto buffer_count{
        static_cast<s32>(processor.mix_buffers.size() / processor.sample_count)};
    return input < buffer_count && output < buffer_count;
}

} // namespace

void BiquadFilterAndMixCommand::Dump(
    [[maybe_unused]] const ADSP::CommandListProcessor& processor, std::string& string) {
    string += fmt::format(
        "BiquadFilterAndMixCommand\n\tinput {:02X} output {:02X} init {} ramp {} first {}\n",
        input, output, needs_init, has_volume_ramp, is_first_mix_buffer);
}

void BiquadFilterAndMixCommand::Process(const ADSP::CommandListProcessor& processor) {
    auto* current{reinterpret_cast<FilterState*>(state)};
    auto* previous{reinterpret_cast<FilterState*>(previous_state)};
    PrepareState(*current, *previous, needs_init, is_first_mix_buffer);

    auto runtime{LoadState(*current)};
    auto input_buffer{
        processor.mix_buffers.subspan(input * processor.sample_count, processor.sample_count)};
    auto output_buffer{
        processor.mix_buffers.subspan(output * processor.sample_count, processor.sample_count)};

    const f64 step{
        has_volume_ramp
            ? (static_cast<f64>(volume1) - static_cast<f64>(volume0)) /
                  static_cast<f64>(processor.sample_count)
            : 0.0};
    f64 volume{has_volume_ramp ? static_cast<f64>(volume0) : static_cast<f64>(volume1)};

    for (u32 i{}; i < processor.sample_count; i++) {
        const f64 filtered{ProcessSample(biquad, runtime, static_cast<f64>(input_buffer[i]))};
        output_buffer[i] =
            ClampSample(static_cast<f64>(output_buffer[i]) + filtered * volume);
        volume += step;
    }

    StoreState(*current, runtime);
}

bool BiquadFilterAndMixCommand::Verify(const ADSP::CommandListProcessor& processor) {
    return VerifyIndexes(processor, input, output);
}

void MultiTapBiquadFilterAndMixCommand::Dump(
    [[maybe_unused]] const ADSP::CommandListProcessor& processor, std::string& string) {
    string += fmt::format(
        "MultiTapBiquadFilterAndMixCommand\n\tinput {:02X} output {:02X} ramp {} first {}\n",
        input, output, has_volume_ramp, is_first_mix_buffer);
}

void MultiTapBiquadFilterAndMixCommand::Process(const ADSP::CommandListProcessor& processor) {
    std::array<FilterState*, MaxBiquadFilters> current{};
    std::array<FilterState*, MaxBiquadFilters> previous{};
    std::array<RuntimeState, MaxBiquadFilters> runtime{};

    for (u32 filter{}; filter < MaxBiquadFilters; filter++) {
        current[filter] = reinterpret_cast<FilterState*>(states[filter]);
        previous[filter] = reinterpret_cast<FilterState*>(previous_states[filter]);
        PrepareState(*current[filter], *previous[filter], needs_init[filter],
                     is_first_mix_buffer);
        runtime[filter] = LoadState(*current[filter]);
    }

    auto input_buffer{
        processor.mix_buffers.subspan(input * processor.sample_count, processor.sample_count)};
    auto output_buffer{
        processor.mix_buffers.subspan(output * processor.sample_count, processor.sample_count)};

    const f64 step{
        has_volume_ramp
            ? (static_cast<f64>(volume1) - static_cast<f64>(volume0)) /
                  static_cast<f64>(processor.sample_count)
            : 0.0};
    f64 volume{has_volume_ramp ? static_cast<f64>(volume0) : static_cast<f64>(volume1)};

    for (u32 i{}; i < processor.sample_count; i++) {
        f64 sample{static_cast<f64>(input_buffer[i])};
        for (u32 filter{}; filter < MaxBiquadFilters; filter++)
            sample = ProcessSample(biquads[filter], runtime[filter], sample);

        output_buffer[i] =
            ClampSample(static_cast<f64>(output_buffer[i]) + sample * volume);
        volume += step;
    }

    for (u32 filter{}; filter < MaxBiquadFilters; filter++)
        StoreState(*current[filter], runtime[filter]);
}

bool MultiTapBiquadFilterAndMixCommand::Verify(const ADSP::CommandListProcessor& processor) {
    return VerifyIndexes(processor, input, output);
}

} // namespace AudioCore::AudioRenderer
