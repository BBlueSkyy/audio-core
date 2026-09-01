// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: MPL-2.0

#include <audio_core/renderer/effect/compressor.h>

namespace AudioCore::AudioRenderer {

void CompressorInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                            const InParameterVersion1& in_params, const PoolMapper& pool_mapper) {}

void CompressorInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                            const InParameterVersion2& in_params, const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion1*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion1));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void CompressorInfo::UpdateForCommandGeneration() {
    if (enabled) {
        usage_state = UsageState::Enabled;
    } else {
        usage_state = UsageState::Disabled;
    }

    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};
    params->state = ParameterState::Updated;
    params->statistics_reset_required = false;
}

void CompressorInfo::InitializeResultState(EffectResultState& result_state) {
    auto* statistics{reinterpret_cast<StatisticsInternal*>(result_state.state.data())};
    statistics->maximum_mean = 0.0f;
    statistics->minimum_gain = 1.0f;
    statistics->last_samples.fill(0.0f);
}

void CompressorInfo::UpdateResultState(EffectResultState& cpu_state,
                                       EffectResultState& dsp_state) {
    auto* cpu_statistics{reinterpret_cast<StatisticsInternal*>(cpu_state.state.data())};
    const auto* dsp_statistics{
        reinterpret_cast<const StatisticsInternal*>(dsp_state.state.data())};
    *cpu_statistics = *dsp_statistics;
}

CpuAddr CompressorInfo::GetWorkbuffer(s32 index) {
    return GetSingleBuffer(index);
}

} // namespace AudioCore::AudioRenderer
