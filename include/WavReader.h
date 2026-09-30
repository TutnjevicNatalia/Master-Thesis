#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace voiceqc {

// One RIFF chunk header encountered while scanning the file: its 4
// character id (e.g. "fmt ", "data", or Soundswell's own "SWEL") and the
// size in bytes of its payload, as declared in the file. Every top level
// chunk is recorded here, including ones the parser does not otherwise
// understand, so callers can see the full chunk layout of a file.
struct ChunkInfo {
    std::string id;
    uint32_t sizeBytes = 0;
};

// Result of loading a WAV file. Multi channel files are downmixed to mono
// by averaging channels, since RT60 and level estimation in this project
// only needs a single channel signal.
struct WavData {
    int sampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::vector<float> samples; // mono, normalized to the range [ -1, 1 ]
    std::vector<ChunkInfo> chunks; // every top level chunk found, in file order
};

// Minimal PCM WAV parser supporting 16, 24 and 32 bit integer PCM, and
// 32 bit IEEE float PCM. Walks every top level RIFF chunk generically,
// so files with extra, non standard chunks (such as Soundswell's own
// "SWEL" chunk) are handled correctly: recognized chunks ("fmt " and
// "data") are decoded, anything else is skipped using its declared size
// and recorded in WavData::chunks so it is still visible to the caller.
class WavReader {
public:
    // Loads filePath and returns the parsed, mono downmixed signal.
    // Throws std::runtime_error on any parsing failure.
    static WavData Load(const std::string& filePath);
};

} // namespace voiceqc
