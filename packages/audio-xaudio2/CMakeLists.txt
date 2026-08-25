cmake_minimum_required(VERSION 3.24)

engine_add_package(audio-xaudio2
    SOURCES src/audio_xaudio2.cpp
    PUBLIC_DEPS engine::audio
    PRIVATE_DEPS xaudio2 ole32
)
