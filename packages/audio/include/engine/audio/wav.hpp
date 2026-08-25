#pragma once

#include <engine/audio/audio.hpp>
#include <engine/math/constants.hpp>

#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace engine::audio {

struct WavPcm {
    std::span<const u8> samples{};
    u32 sample_rate = 0;
    u16 channels = 0;
    u16 bits_per_sample = 0;
};

inline u16 read_u16_le(std::span<const u8> bytes, usize offset) {
    return static_cast<u16>(bytes[offset] | (static_cast<u16>(bytes[offset + 1]) << 8));
}

inline u32 read_u32_le(std::span<const u8> bytes, usize offset) {
    return static_cast<u32>(bytes[offset])
        | (static_cast<u32>(bytes[offset + 1]) << 8)
        | (static_cast<u32>(bytes[offset + 2]) << 16)
        | (static_cast<u32>(bytes[offset + 3]) << 24);
}

inline void append_u16_le(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
}

inline void append_u32_le(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 24));
}

inline bool parse_wav(std::span<const u8> file, WavPcm& out) {
    out = {};
    if (file.size() < 44) {
        return false;
    }
    if (std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    usize cursor = 12;
    bool have_fmt = false;
    u16 format = 0;
    u16 channels = 0;
    u32 sample_rate = 0;
    u16 bits = 0;
    std::span<const u8> samples{};

    while (cursor + 8 <= file.size()) {
        const char* tag = reinterpret_cast<const char*>(file.data() + cursor);
        const u32 chunk_size = read_u32_le(file, cursor + 4);
        cursor += 8;
        if (cursor + chunk_size > file.size()) {
            return false;
        }
        const std::span<const u8> chunk = file.subspan(cursor, chunk_size);
        if (std::memcmp(tag, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return false;
            }
            format = read_u16_le(chunk, 0);
            channels = read_u16_le(chunk, 2);
            sample_rate = read_u32_le(chunk, 4);
            bits = read_u16_le(chunk, 14);
            have_fmt = true;
        } else if (std::memcmp(tag, "data", 4) == 0) {
            samples = chunk;
        }
        cursor += chunk_size + (chunk_size & 1u);
    }

    if (!have_fmt || format != 1 || (channels != 1 && channels != 2) || bits != kPcmBits
        || sample_rate == 0 || samples.empty() || (samples.size() % (channels * (bits / 8))) != 0) {
        return false;
    }

    out.samples = samples;
    out.sample_rate = sample_rate;
    out.channels = channels;
    out.bits_per_sample = bits;
    return true;
}

inline std::vector<u8> write_pcm16_wav(std::span<const i16> pcm, u32 sample_rate, u16 channels) {
    std::vector<u8> out;
    if (pcm.empty() || sample_rate == 0 || (channels != 1 && channels != 2)) {
        return out;
    }
    const u32 data_bytes = static_cast<u32>(pcm.size() * sizeof(i16));
    const u32 byte_rate = sample_rate * channels * (kPcmBits / 8);
    const u16 block_align = static_cast<u16>(channels * (kPcmBits / 8));
    out.reserve(44 + data_bytes);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    append_u32_le(out, 36 + data_bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append_u32_le(out, 16);
    append_u16_le(out, 1);
    append_u16_le(out, channels);
    append_u32_le(out, sample_rate);
    append_u32_le(out, byte_rate);
    append_u16_le(out, block_align);
    append_u16_le(out, kPcmBits);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    append_u32_le(out, data_bytes);
    const u8* bytes = reinterpret_cast<const u8*>(pcm.data());
    out.insert(out.end(), bytes, bytes + data_bytes);
    return out;
}

inline std::vector<i16> make_tone_pcm16(u32 sample_rate, u32 duration_ms, f32 hz, f32 amplitude) {
    const u32 count = (sample_rate * duration_ms) / 1000;
    std::vector<i16> pcm(count);
    const f32 amp = amplitude < 0.f ? 0.f : (amplitude > 1.f ? 1.f : amplitude);
    for (u32 i = 0; i < count; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(sample_rate);
        const f32 s = std::sin(2.f * math::kPi * hz * t) * amp;
        pcm[i] = static_cast<i16>(s * 32767.f);
    }
    return pcm;
}

inline SoundDesc sound_desc_from_wav(const WavPcm& wav) {
    SoundDesc desc{};
    desc.pcm = wav.samples;
    desc.sample_rate = wav.sample_rate;
    desc.channels = wav.channels;
    desc.bits_per_sample = wav.bits_per_sample;
    return desc;
}

} // namespace engine::audio
