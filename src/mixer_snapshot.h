#pragma once

#include "loop_scheduler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

namespace resonance
{
inline constexpr std::size_t maxMixerTracks = 8;

struct MixerTrackSnapshot
{
    SequenceSnapshot sequence {};
    float gainLinear = 1.0f;
    float pan = 0.0f;
    int midiInputChannel = 0;
    int midiOutputChannel = 1;
    bool enabled = false;
    bool muted = false;
    bool solo = false;
};

// A MixerSnapshot embeds a fixed-capacity sequence per lane, so it is hundreds of
// kilobytes. It must live on the heap or as a member of a heap-allocated owner;
// constructing one as a stack local overflows a default 1 MB thread stack.
struct MixerSnapshot
{
    std::array<MixerTrackSnapshot, maxMixerTracks> tracks {};
    std::size_t trackCount = 0;
};

struct StereoTrackGain
{
    float left = 0.0f;
    float right = 0.0f;
};

inline bool hasActiveSolo (const MixerSnapshot& snapshot) noexcept
{
    const auto count = std::min (snapshot.trackCount, maxMixerTracks);
    for (std::size_t index = 0; index < count; ++index)
        if (snapshot.tracks[index].enabled && snapshot.tracks[index].solo)
            return true;

    return false;
}

inline StereoTrackGain resolveStereoTrackGain (const MixerSnapshot& snapshot,
                                               std::size_t trackIndex) noexcept
{
    const auto count = std::min (snapshot.trackCount, maxMixerTracks);
    if (trackIndex >= count)
        return {};

    const auto& track = snapshot.tracks[trackIndex];
    if (! track.enabled || track.muted || (hasActiveSolo (snapshot) && ! track.solo))
        return {};

    const auto gain = std::max (0.0f, track.gainLinear);
    const auto pan = std::clamp (track.pan, -1.0f, 1.0f);
    return { gain * std::min (1.0f, 1.0f - pan),
             gain * std::min (1.0f, 1.0f + pan) };
}

static_assert (std::is_trivially_copyable_v<MixerTrackSnapshot>);
static_assert (std::is_trivially_copyable_v<MixerSnapshot>);
} // namespace resonance
