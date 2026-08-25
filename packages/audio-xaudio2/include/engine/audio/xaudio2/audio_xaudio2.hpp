#pragma once

#include <engine/audio/audio.hpp>

#include <memory>

namespace engine::audio::xaudio2 {

std::unique_ptr<IAudio> create_audio();

} // namespace engine::audio::xaudio2
