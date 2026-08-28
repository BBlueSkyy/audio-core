// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: MPL-2.0

#include <audio_core/renderer/splitter/splitter_destinations_data.h>

namespace AudioCore::AudioRenderer {

SplitterDestinationData::SplitterDestinationData(const s32 id_) : id{id_} {}

void SplitterDestinationData::ClearMixVolume() {
    mix_volumes.fill(0.0f);
    prev_mix_volumes.fill(0.0f);
}

s32 SplitterDestinationData::GetId() const {
    return id;
}

bool SplitterDestinationData::IsConfigured() const {
    return in_use && destination_id != UnusedMixId;
}

s32 SplitterDestinationData::GetMixId() const {
    return destination_id;
}

f32 SplitterDestinationData::GetMixVolume(const u32 index) const {
    if (index >= mix_volumes.size()) {
        LOG_ERROR(Service_Audio, "SplitterDestinationData::GetMixVolume Invalid index {}", index);
        return 0.0f;
    }
    return mix_volumes[index];
}

std::span<f32> SplitterDestinationData::GetMixVolume() {
    return mix_volumes;
}

f32 SplitterDestinationData::GetMixVolumePrev(const u32 index) const {
    if (index >= prev_mix_volumes.size()) {
        LOG_ERROR(Service_Audio, "SplitterDestinationData::GetMixVolumePrev Invalid index {}",
                  index);
        return 0.0f;
    }
    return prev_mix_volumes[index];
}

std::span<f32> SplitterDestinationData::GetMixVolumePrev() {
    return prev_mix_volumes;
}

void SplitterDestinationData::Update(const InParameter& params,
                                           const bool explicit_prev_volume_reset) {
    if (params.id != id || params.magic != GetSplitterSendDataMagic()) {
        return;
    }

    destination_id = params.mix_id;
    mix_volumes = params.mix_volumes;

    const bool reset_previous{
        explicit_prev_volume_reset ? params.reset_prev_volume : (!in_use && params.in_use)};
    if (reset_previous) {
        prev_mix_volumes = mix_volumes;
        need_update = false;
    }

    in_use = params.in_use;
    if (!in_use)
        biquad_initialized.fill(false);
}

void SplitterDestinationData::MarkAsNeedToUpdateInternalState() {
    need_update = true;
}

void SplitterDestinationData::UpdateInternalState() {
    if (in_use && need_update) {
        prev_mix_volumes = mix_volumes;
    }
    need_update = false;
}

SplitterDestinationData* SplitterDestinationData::GetNext() const {
    return next;
}

void SplitterDestinationData::SetNext(SplitterDestinationData* next_) {
    next = next_;
}

std::span<SplitterDestinationData::BiquadFilterParameter2>
SplitterDestinationData::GetBiquadFilters() {
    return biquad_filters;
}

std::span<const SplitterDestinationData::BiquadFilterParameter2>
SplitterDestinationData::GetBiquadFilters() const {
    return biquad_filters;
}

bool SplitterDestinationData::IsBiquadInitialized(const u32 index) const {
    return index < biquad_initialized.size() && biquad_initialized[index];
}

void SplitterDestinationData::SetBiquadInitialized(const u32 index,
                                                   const bool initialized) {
    if (index < biquad_initialized.size())
        biquad_initialized[index] = initialized;
}

} // namespace AudioCore::AudioRenderer
