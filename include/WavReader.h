#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace voiceqc {

// Result of loading a WAV file. Multi channel files are downmixed to mono
// by averaging channels, since RT60 and level estimation in this project
// only needs a single channel signal.
struct WavData {
    int sampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::vector<float> samples; // mono, normalized to the range [ -1, 1 ]
};

// Minimal PCM WAV parser supporting 16, 24 and 32 bit integer PCM, and
// 32 bit IEEE float PCM. Only the canonical RIFF/WAVE layout is handled
// (fmt and data chunks), which covers files produced by Swell/Soundswell
// and by common recording and editing tools.
class WavReader {
public:
    // Loads filePath and returns the parsed, mono downmixed signal.
    // Throws std::runtime_error on any parsing failure.
    static WavData Load(const std::string& filePath);
};

} // namespace voiceqc
