#include "WavReader.h"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace voiceqc {

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

int32_t SignExtend24(uint32_t v) {
    // v holds a 24 bit value in the low bits; sign extend to 32 bit.
    if (v & 0x00800000u) {
        v |= 0xFF000000u;
    }
    return static_cast<int32_t>(v);
}

} // namespace

WavData WavReader::Load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("WavReader: could not open file: " + filePath);
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    if (buffer.size() < 12) {
        throw std::runtime_error("WavReader: file too small to be a WAV file: " + filePath);
    }

    if (std::memcmp(buffer.data(), "RIFF", 4) != 0 ||
        std::memcmp(buffer.data() + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("WavReader: not a RIFF/WAVE file: " + filePath);
    }

    size_t pos = 12;
    bool haveFmt = false;
    bool haveData = false;

    uint16_t audioFormat = 0; // 1 = PCM integer, 3 = IEEE float
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;

    const uint8_t* dataPtr = nullptr;
    uint32_t dataSize = 0;

    while (pos + 8 <= buffer.size()) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, buffer.data() + pos, 4);
        uint32_t chunkSize = ReadU32LE(buffer.data() + pos + 4);
        size_t chunkDataStart = pos + 8;

        if (chunkDataStart + chunkSize > buffer.size()) {
            // Truncated or malformed chunk size; stop parsing further chunks.
            break;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                throw std::runtime_error("WavReader: fmt chunk too small in: " + filePath);
            }
            const uint8_t* fmt = buffer.data() + chunkDataStart;
            audioFormat = ReadU16LE(fmt + 0);
            numChannels = ReadU16LE(fmt + 2);
            sampleRate = ReadU32LE(fmt + 4);
            bitsPerSample = ReadU16LE(fmt + 14);
            haveFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataPtr = buffer.data() + chunkDataStart;
            dataSize = chunkSize;
            haveData = true;
        }

        // Chunks are padded to even sizes.
        size_t advance = chunkSize + (chunkSize % 2);
        pos = chunkDataStart + advance;

        if (haveFmt && haveData) {
            break;
        }
    }

    if (!haveFmt) {
        throw std::runtime_error("WavReader: missing fmt chunk in: " + filePath);
    }
    if (!haveData) {
        throw std::runtime_error("WavReader: missing data chunk in: " + filePath);
    }
    if (numChannels == 0) {
        throw std::runtime_error("WavReader: zero channels reported in: " + filePath);
    }

    bool isFloat = (audioFormat == 3);
    bool isPcmInt = (audioFormat == 1);
    if (!isFloat && !isPcmInt) {
        throw std::runtime_error("WavReader: unsupported audio format code in: " + filePath +
                                  " (only PCM integer and IEEE float are supported)");
    }

    int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0 || (bitsPerSample % 8) != 0) {
        throw std::runtime_error("WavReader: unsupported bits per sample in: " + filePath);
    }

    int frameSize = bytesPerSample * numChannels;
    if (frameSize == 0) {
        throw std::runtime_error("WavReader: invalid frame size in: " + filePath);
    }

    uint32_t numFrames = dataSize / static_cast<uint32_t>(frameSize);

    WavData result;
    result.sampleRate = static_cast<int>(sampleRate);
    result.bitsPerSample = bitsPerSample;
    result.channels = numChannels;
    result.samples.reserve(numFrames);

    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        double frameSum = 0.0;
        const uint8_t* framePtr = dataPtr + static_cast<size_t>(frame) * frameSize;

        for (int ch = 0; ch < numChannels; ++ch) {
            const uint8_t* samplePtr = framePtr + static_cast<size_t>(ch) * bytesPerSample;
            double normalized = 0.0;

            if (isFloat && bitsPerSample == 32) {
                float value;
                std::memcpy(&value, samplePtr, sizeof(float));
                normalized = static_cast<double>(value);
            } else if (isPcmInt && bitsPerSample == 16) {
                int16_t value = static_cast<int16_t>(ReadU16LE(samplePtr));
                normalized = static_cast<double>(value) / 32768.0;
            } else if (isPcmInt && bitsPerSample == 24) {
                uint32_t raw = static_cast<uint32_t>(samplePtr[0]) |
                               (static_cast<uint32_t>(samplePtr[1]) << 8) |
                               (static_cast<uint32_t>(samplePtr[2]) << 16);
                int32_t value = SignExtend24(raw);
                normalized = static_cast<double>(value) / 8388608.0;
            } else if (isPcmInt && bitsPerSample == 32) {
                int32_t value = static_cast<int32_t>(ReadU32LE(samplePtr));
                normalized = static_cast<double>(value) / 2147483648.0;
            } else if (isPcmInt && bitsPerSample == 8) {
                // 8 bit PCM is unsigned, centered at 128.
                int value = static_cast<int>(samplePtr[0]) - 128;
                normalized = static_cast<double>(value) / 128.0;
            } else {
                throw std::runtime_error("WavReader: unsupported bit depth/format combination in: " +
                                          filePath);
            }

            frameSum += normalized;
        }

        result.samples.push_back(static_cast<float>(frameSum / numChannels));
    }

    return result;
}

} // namespace voiceqc
