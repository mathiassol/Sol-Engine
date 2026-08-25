#include <engine/audio/xaudio2/audio_xaudio2.hpp>

#include <engine/core/log.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <xaudio2.h>

namespace engine::audio::xaudio2 {
namespace {

constexpr u32 kMaxSounds = 32;
constexpr u32 kMaxVoices = 16;

struct StoredSound {
    std::vector<u8> pcm;
    WAVEFORMATEX format{};
    u32 generation = 0;
    bool live = false;
};

struct VoiceSlot;

class VoiceCallback final : public IXAudio2VoiceCallback {
public:
    VoiceSlot* slot = nullptr;

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override;
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

struct VoiceSlot {
    IXAudio2SourceVoice* voice = nullptr;
    VoiceCallback callback{};
    std::atomic<bool> done{false};
};

void VoiceCallback::OnBufferEnd(void*) {
    if (slot) {
        slot->done.store(true, std::memory_order_release);
    }
}

class XAudio2Audio final : public IAudio {
public:
    XAudio2Audio() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com_owned_ = com == S_OK;
        if (FAILED(com) && com != RPC_E_CHANGED_MODE && com != S_FALSE) {
            log(LogLevel::Error, LogChannel::Audio, "XAudio2: COM init failed");
            return;
        }

        IXAudio2* xaudio = nullptr;
        if (FAILED(XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !xaudio) {
            log(LogLevel::Error, LogChannel::Audio, "XAudio2Create failed");
            return;
        }
        xaudio_ = xaudio;

        IXAudio2MasteringVoice* master = nullptr;
        if (FAILED(xaudio_->CreateMasteringVoice(&master)) || !master) {
            log(LogLevel::Error, LogChannel::Audio, "XAudio2 mastering voice failed");
            xaudio_->Release();
            xaudio_ = nullptr;
            return;
        }
        master_ = master;
        for (VoiceSlot& slot : voices_) {
            slot.callback.slot = &slot;
        }
        ready_ = true;
        log(LogLevel::Info, LogChannel::Audio, "XAudio2 audio ready");
    }

    ~XAudio2Audio() override {
        for (VoiceSlot& slot : voices_) {
            destroy_voice(slot);
        }
        if (master_) {
            master_->DestroyVoice();
            master_ = nullptr;
        }
        if (xaudio_) {
            xaudio_->Release();
            xaudio_ = nullptr;
        }
        if (com_owned_) {
            CoUninitialize();
        }
    }

    SoundHandle create_sound(const SoundDesc& desc) override {
        if (!ready_ || desc.pcm.empty() || desc.sample_rate == 0
            || (desc.channels != 1 && desc.channels != 2)
            || desc.bits_per_sample != kPcmBits
            || (desc.pcm.size() % (desc.channels * (kPcmBits / 8))) != 0) {
            return {};
        }

        u32 index = kMaxSounds;
        for (u32 i = 0; i < kMaxSounds; ++i) {
            if (!sounds_[i].live) {
                index = i;
                break;
            }
        }
        if (index == kMaxSounds) {
            return {};
        }

        StoredSound& sound = sounds_[index];
        sound.pcm.assign(desc.pcm.begin(), desc.pcm.end());
        sound.format = {};
        sound.format.wFormatTag = WAVE_FORMAT_PCM;
        sound.format.nChannels = desc.channels;
        sound.format.nSamplesPerSec = desc.sample_rate;
        sound.format.wBitsPerSample = kPcmBits;
        sound.format.nBlockAlign = static_cast<WORD>(desc.channels * (kPcmBits / 8));
        sound.format.nAvgBytesPerSec = desc.sample_rate * sound.format.nBlockAlign;
        sound.generation = sound.generation == 0 ? 1 : sound.generation + 1;
        sound.live = true;

        SoundHandle handle{};
        handle.id = index + 1;
        handle.generation = sound.generation;
        return handle;
    }

    bool play(SoundHandle handle, f32 volume) override {
        if (!ready_ || !handle.valid() || handle.id > kMaxSounds) {
            return false;
        }
        StoredSound& sound = sounds_[handle.id - 1];
        if (!sound.live || sound.generation != handle.generation || sound.pcm.empty()) {
            return false;
        }

        VoiceSlot* slot = acquire_voice();
        if (!slot) {
            return false;
        }

        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(xaudio_->CreateSourceVoice(&voice, &sound.format, 0, 2.f, &slot->callback))
            || !voice) {
            return false;
        }
        slot->voice = voice;
        slot->done.store(false, std::memory_order_release);

        const f32 vol = std::clamp(volume, 0.f, 1.f);
        voice->SetVolume(vol);

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(sound.pcm.size());
        buffer.pAudioData = sound.pcm.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0))) {
            destroy_voice(*slot);
            return false;
        }
        return true;
    }

    void tick() override {
        for (VoiceSlot& slot : voices_) {
            if (slot.voice && slot.done.load(std::memory_order_acquire)) {
                destroy_voice(slot);
            }
        }
    }

    u32 playing_count() const override {
        u32 count = 0;
        for (const VoiceSlot& slot : voices_) {
            if (slot.voice && !slot.done.load(std::memory_order_acquire)) {
                count += 1;
            }
        }
        return count;
    }

    std::string_view name() const override { return "xaudio2"; }

    bool ready() const { return ready_; }

private:
    VoiceSlot* acquire_voice() {
        for (VoiceSlot& slot : voices_) {
            if (slot.voice && slot.done.load(std::memory_order_acquire)) {
                destroy_voice(slot);
            }
            if (!slot.voice) {
                return &slot;
            }
        }
        return nullptr;
    }

    static void destroy_voice(VoiceSlot& slot) {
        if (slot.voice) {
            slot.voice->Stop(0);
            slot.voice->DestroyVoice();
            slot.voice = nullptr;
        }
        slot.done.store(false, std::memory_order_relaxed);
    }

    IXAudio2* xaudio_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    std::array<StoredSound, kMaxSounds> sounds_{};
    std::array<VoiceSlot, kMaxVoices> voices_{};
    bool ready_ = false;
    bool com_owned_ = false;
};

} // namespace

std::unique_ptr<IAudio> create_audio() {
    auto audio = std::make_unique<XAudio2Audio>();
    if (!audio->ready()) {
        return nullptr;
    }
    return audio;
}

} // namespace engine::audio::xaudio2
