#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace acid3::params
{
// Parameter IDs (SPEC §7.1). Knob mappings to physical values live in dsp/Voice.cpp
// (cutoffKnobToHz, decayKnobToMs) and Parameters.cpp (display strings).
inline constexpr const char* wave = "wave";
inline constexpr const char* tune = "tune";
inline constexpr const char* cutoff = "cutoff";
inline constexpr const char* reso = "reso";
inline constexpr const char* envmod = "envmod";
inline constexpr const char* decay = "decay";
inline constexpr const char* accent = "accent";
inline constexpr const char* volume = "volume";
inline constexpr const char* drift = "drift";
inline constexpr const char* seqOn = "seqOn";
inline constexpr const char* swing = "swing";
inline constexpr const char* accThr = "accThr";
inline constexpr const char* pattern = "pattern";
inline constexpr const char* transpose = "transpose";
inline constexpr const char* bpm = "bpm";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace acid3::params
