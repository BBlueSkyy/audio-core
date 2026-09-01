// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: MPL-2.0

#include <audio_core/common/audio_renderer_parameter.h>
#include <audio_core/common/workbuffer_allocator.h>
#include <audio_core/renderer/behavior/behavior_info.h>
#include <audio_core/renderer/splitter/splitter_context.h>
#include <audio_core/common/alignment.h>

namespace AudioCore::AudioRenderer {

SplitterDestinationData* SplitterContext::GetDesintationData(const s32 splitter_id,
                                                             const s32 destination_id) {
    return splitter_infos[splitter_id].GetData(destination_id);
}

SplitterInfo& SplitterContext::GetInfo(const s32 splitter_id) {
    return splitter_infos[splitter_id];
}

u32 SplitterContext::GetDataCount() const {
    return destinations_count;
}

u32 SplitterContext::GetInfoCount() const {
    return info_count;
}

SplitterDestinationData& SplitterContext::GetData(const u32 index) {
    return splitter_destinations[index];
}

void SplitterContext::Setup(std::span<SplitterInfo> splitter_infos_, const u32 splitter_info_count_,
                            SplitterDestinationData* splitter_destinations_,
                            const u32 destination_count_, const bool splitter_bug_fixed_,
                            const BehaviorInfo& behavior) {
    splitter_infos = splitter_infos_;
    info_count = splitter_info_count_;
    splitter_destinations = splitter_destinations_;
    destinations_count = destination_count_;
    splitter_bug_fixed = splitter_bug_fixed_;
    splitter_prev_volume_reset_supported = behavior.IsSplitterPrevVolumeResetSupported();
    splitter_biquad_param_supported = behavior.IsBiquadFilterParameterForSplitterEnabled();
    splitter_float_coeff_supported = behavior.IsSplitterDestinationV2bSupported();
}

bool SplitterContext::UsingSplitter() const {
    return splitter_infos.size() > 0 && info_count > 0 && splitter_destinations != nullptr &&
           destinations_count > 0;
}

void SplitterContext::ClearAllNewConnectionFlag() {
    for (s32 i = 0; i < info_count; i++) {
        splitter_infos[i].SetNewConnectionFlag();
    }
}

bool SplitterContext::Initialize(
    const BehaviorInfo& behavior, const AudioRendererParameterInternal& params,
    WorkbufferAllocator& allocator,
    std::span<VoiceState::BiquadFilterState> splitter_biquad_states_) {
    if (behavior.IsSplitterSupported() && params.splitter_infos > 0 &&
        params.splitter_destinations > 0) {
        splitter_infos = allocator.Allocate<SplitterInfo>(params.splitter_infos, 0x10);

        for (u32 i = 0; i < params.splitter_infos; i++) {
            std::construct_at<SplitterInfo>(&splitter_infos[i], static_cast<s32>(i));
        }

        if (splitter_infos.size() == 0) {
            splitter_infos = {};
            return false;
        }

        splitter_destinations =
            allocator.Allocate<SplitterDestinationData>(params.splitter_destinations, 0x10).data();

        for (s32 i = 0; i < params.splitter_destinations; i++) {
            std::construct_at<SplitterDestinationData>(&splitter_destinations[i], i);
        }

        if (params.splitter_destinations <= 0) {
            splitter_infos = {};
            splitter_destinations = nullptr;
            return false;
        }

        if (behavior.IsBiquadFilterParameterForSplitterEnabled()) {
            const auto expected_states{
                static_cast<size_t>(params.splitter_destinations) * BiquadStatesPerDestination};
            if (splitter_biquad_states_.size() < expected_states) {
                return false;
            }
            splitter_biquad_states = splitter_biquad_states_.first(expected_states);
        } else {
            splitter_biquad_states = {};
        }

        Setup(splitter_infos, params.splitter_infos, splitter_destinations,
              params.splitter_destinations, behavior.IsSplitterBugFixed(), behavior);
    }
    return true;
}

bool SplitterContext::Update(const u8* input, u32& consumed_size) {
    auto in_params{reinterpret_cast<const InParameterHeader*>(input)};

    if (destinations_count == 0 || info_count == 0) {
        consumed_size = 0;
        return true;
    }

    if (in_params->magic != GetSplitterInParamHeaderMagic()) {
        consumed_size = 0;
        return false;
    }

    for (auto& splitter_info : splitter_infos) {
        splitter_info.ClearNewConnectionFlag();
    }

    u32 offset{sizeof(InParameterHeader)};
    offset = UpdateInfo(input, offset, in_params->info_count);
    offset = UpdateData(input, offset, in_params->destination_count);

    consumed_size = Common::AlignUp(offset, 0x10);
    return true;
}

u32 SplitterContext::UpdateInfo(const u8* input, u32 offset, const u32 splitter_count) {
    for (u32 i = 0; i < splitter_count; i++) {
        auto info_header{reinterpret_cast<const SplitterInfo::InParameter*>(input + offset)};

        if (info_header->magic != GetSplitterInfoMagic())
            break;

        if (info_header->id < 0 || info_header->id >= info_count) {
            break;
        }

        auto& info{splitter_infos[info_header->id]};
        RecomposeDestination(info, info_header);

        offset += info.Update(info_header);
    }

    return offset;
}

u32 SplitterContext::UpdateData(const u8* input, u32 offset, const u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (!splitter_biquad_param_supported) {
            const auto* data_header{
                reinterpret_cast<const SplitterDestinationData::InParameter*>(input + offset)};
            const u32 stride{static_cast<u32>(sizeof(*data_header))};

            if (data_header->magic != GetSplitterSendDataMagic())
                break;

            if (data_header->id >= 0 && data_header->id < destinations_count) {
                splitter_destinations[data_header->id].Update(
                    *data_header, splitter_prev_volume_reset_supported);
            }
            offset += stride;
            continue;
        }

        if (!splitter_float_coeff_supported) {
            const auto* data_header{
                reinterpret_cast<const SplitterDestinationData::InParameterVersion2a*>(
                    input + offset)};
            const u32 stride{static_cast<u32>(sizeof(*data_header))};

            if (data_header->magic != GetSplitterSendDataMagic())
                break;

            if (data_header->id >= 0 && data_header->id < destinations_count) {
                SplitterDestinationData::InParameter common{};
                common.magic = data_header->magic;
                common.id = data_header->id;
                common.mix_volumes = data_header->mix_volumes;
                common.mix_id = data_header->mix_id;
                common.in_use = data_header->in_use;
                common.reset_prev_volume = data_header->reset_prev_volume;

                auto& destination{splitter_destinations[data_header->id]};
                destination.Update(common, splitter_prev_volume_reset_supported);

                constexpr f32 q14_scale{1.0f / 16384.0f};
                auto filters{destination.GetBiquadFilters()};
                for (u32 filter = 0; filter < MaxBiquadFilters; filter++) {
                    const auto& src{data_header->biquad_filters[filter]};
                    auto& dst{filters[filter]};
                    if (dst.enabled != src.enabled)
                        destination.SetBiquadInitialized(filter, false);
                    dst.enabled = src.enabled;
                    for (u32 n = 0; n < 3; n++) {
                        dst.numerator[n] = static_cast<f32>(src.b[n]) * q14_scale;
                    }
                    for (u32 n = 0; n < 2; n++) {
                        dst.denominator[n] = static_cast<f32>(src.a[n]) * q14_scale;
                    }
                }
            }
            offset += stride;
            continue;
        }

        const auto* data_header{
            reinterpret_cast<const SplitterDestinationData::InParameterVersion2b*>(input + offset)};
        const u32 stride{static_cast<u32>(sizeof(*data_header))};

        if (data_header->magic != GetSplitterSendDataMagic())
            break;

        if (data_header->id >= 0 && data_header->id < destinations_count) {
            SplitterDestinationData::InParameter common{};
            common.magic = data_header->magic;
            common.id = data_header->id;
            common.mix_volumes = data_header->mix_volumes;
            common.mix_id = data_header->mix_id;
            common.in_use = data_header->in_use;
            common.reset_prev_volume = data_header->reset_prev_volume;

            auto& destination{splitter_destinations[data_header->id]};
            destination.Update(common, splitter_prev_volume_reset_supported);
            auto filters{destination.GetBiquadFilters()};
            for (u32 filter = 0; filter < MaxBiquadFilters; filter++) {
                if (filters[filter].enabled != data_header->biquad_filters[filter].enabled)
                    destination.SetBiquadInitialized(filter, false);
                filters[filter] = data_header->biquad_filters[filter];
            }
        }
        offset += stride;
    }

    return offset;
}

void SplitterContext::UpdateInternalState() {
    for (s32 i = 0; i < info_count; i++) {
        splitter_infos[i].UpdateInternalState();
    }
}

void SplitterContext::RecomposeDestination(SplitterInfo& out_info,
                                           const SplitterInfo::InParameter* info_header) {
    auto destination{out_info.GetData(0)};
    while (destination != nullptr) {
        auto dest{destination->GetNext()};
        destination->SetNext(nullptr);
        destination = dest;
    }
    out_info.SetDestinations(nullptr);

    auto dest_count{info_header->destination_count};
    if (!splitter_bug_fixed) {
        dest_count = std::min(dest_count, GetDestCountPerInfoForCompat());
    }

    if (dest_count == 0) {
        return;
    }

    std::span<const u32> destination_ids{reinterpret_cast<const u32*>(&info_header[1]), dest_count};

    auto head{&splitter_destinations[destination_ids[0]]};
    auto current_destination{head};
    for (u32 i = 1; i < dest_count; i++) {
        auto next_destination{&splitter_destinations[destination_ids[i]]};
        current_destination->SetNext(next_destination);
        current_destination = next_destination;
    }

    out_info.SetDestinations(head);
    out_info.SetDestinationCount(dest_count);
}

u32 SplitterContext::GetDestCountPerInfoForCompat() const {
    if (info_count <= 0) {
        return 0;
    }
    return static_cast<u32>(destinations_count / info_count);
}

std::span<VoiceState::BiquadFilterState>
SplitterContext::GetBiquadFilterStates(const s32 destination_id) {
    if (destination_id < 0 || splitter_biquad_states.empty()) {
        return {};
    }

    const auto offset{static_cast<size_t>(destination_id) * BiquadStatesPerDestination};
    if (offset + BiquadStatesPerDestination > splitter_biquad_states.size()) {
        return {};
    }

    return splitter_biquad_states.subspan(offset, BiquadStatesPerDestination);
}

u64 SplitterContext::CalcWorkBufferSize(const BehaviorInfo& behavior,
                                        const AudioRendererParameterInternal& params) {
    u64 size{0};
    if (!behavior.IsSplitterSupported()) {
        return size;
    }

    size += params.splitter_destinations * sizeof(SplitterDestinationData) +
            params.splitter_infos * sizeof(SplitterInfo);

    if (behavior.IsSplitterBugFixed()) {
        size += Common::AlignUp(params.splitter_destinations * sizeof(u32), 0x10);
    }

    if (behavior.IsBiquadFilterParameterForSplitterEnabled() &&
        params.splitter_destinations > 0) {
        size = Common::AlignUp(size, 0x10);
        size += static_cast<u64>(params.splitter_destinations) *
                BiquadStatesPerDestination * sizeof(VoiceState::BiquadFilterState);
    }

    return size;
}

} // namespace AudioCore::AudioRenderer
