// SPDX-FileCopyrightText: Copyright 2026 Strato Audio Contributors
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <array>
#include <string>

#include <audio_core/renderer/command/icommand.h>
#include <audio_core/renderer/splitter/splitter_destinations_data.h>
#include <audio_core/renderer/voice/voice_state.h>

namespace AudioCore::AudioRenderer {
namespace ADSP {
class CommandListProcessor;
}

/**
 * REV12+ splitter command that applies one destination biquad and mixes the
 * filtered signal into an output mix buffer.
 *
 * A destination may feed several output buffers. `previous_state` stores the
 * filter state at the beginning of the frame so each output sees the same
 * filtered source while the persistent state advances only once.
 */
struct BiquadFilterAndMixCommand : ICommand {
    void Dump(const ADSP::CommandListProcessor& processor, std::string& string) override;
    void Process(const ADSP::CommandListProcessor& processor) override;
    bool Verify(const ADSP::CommandListProcessor& processor) override;

    s16 input{};
    s16 output{};
    SplitterDestinationData::BiquadFilterParameter2 biquad{};
    CpuAddr state{};
    CpuAddr previous_state{};
    f32 volume0{};
    f32 volume1{};
    bool needs_init{};
    bool has_volume_ramp{};
    bool is_first_mix_buffer{};
};

/**
 * REV12+ two-filter splitter command. Both biquads are applied serially before
 * the result is mixed into the destination.
 */
struct MultiTapBiquadFilterAndMixCommand : ICommand {
    void Dump(const ADSP::CommandListProcessor& processor, std::string& string) override;
    void Process(const ADSP::CommandListProcessor& processor) override;
    bool Verify(const ADSP::CommandListProcessor& processor) override;

    s16 input{};
    s16 output{};
    std::array<SplitterDestinationData::BiquadFilterParameter2, MaxBiquadFilters> biquads{};
    std::array<CpuAddr, MaxBiquadFilters> states{};
    std::array<CpuAddr, MaxBiquadFilters> previous_states{};
    f32 volume0{};
    f32 volume1{};
    std::array<bool, MaxBiquadFilters> needs_init{};
    bool has_volume_ramp{};
    bool is_first_mix_buffer{};
};

} // namespace AudioCore::AudioRenderer
