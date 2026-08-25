#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string_view>

namespace engine::audio {

// 2D one-shot playback. 3D, buses, and streaming wait on Audio #3–#5.
// Swap backends by linking a different audio-* package.

inline constexpr u16 kPcmBits = 16;
inline constexpr f32 kDefaultVolume = 0.4f;

struct SoundHandle {
    u32 id = 0;
    u32 generation = 0;

    bool valid() const { return id != 0 && generation != 0; }
    bool operator==(SoundHandle other) const {
        return id == other.id && generation == other.generation;
    }
};

struct SoundDesc {
    std::span<const u8> pcm{};
    u32 sample_rate = 44100;
    u16 channels = 1;
    u16 bits_per_sample = kPcmBits;
};

class IAudio {
public:
    virtual ~IAudio() = default;

    virtual SoundHandle create_sound(const SoundDesc& desc) = 0;
    virtual bool play(SoundHandle handle, f32 volume = kDefaultVolume) = 0;
    virtual void tick() = 0;
    virtual u32 playing_count() const = 0;
    virtual std::string_view name() const = 0;
};

} // namespace engine::audio
