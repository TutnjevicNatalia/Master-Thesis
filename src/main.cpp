#include <iostream>
#include <iomanip>
#include <exception>

#include "WavReader.h"
#include "ReverberationEstimator.h"

// VoiceQC prototype entry point.
//
// Usage: VoiceQC <path-to-wav-file> [lojLevel]
//
// Loads a single WAV recording (expected to be a handclap or other
// impulsive excitation) and prints both RT60 estimates: the manual
// Swell style -20 dB crossing method, and the Schroeder backward
// integration method. If lojLevel (1, 2 or 3) is given, also prints the
// LOJ compliance classification for that single recording.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path-to-wav-file> [lojLevel]\n";
        return 1;
    }

    std::string filePath = argv[1];
    int lojLevel = 0;
    if (argc >= 3) {
        lojLevel = std::atoi(argv[2]);
    }

    try {
        voiceqc::WavData wav = voiceqc::WavReader::Load(filePath);

        std::cout << "Loaded: " << filePath << "\n";
        std::cout << "  Sample rate: " << wav.sampleRate << " Hz\n";
        std::cout << "  Channels (source): " << wav.channels << "\n";
        std::cout << "  Bits per sample: " << wav.bitsPerSample << "\n";
        std::cout << "  Frames: " << wav.samples.size() << "\n\n";

        double manualRT60 = voiceqc::EstimateRT60ManualMethod(wav.samples, wav.sampleRate);
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Manual method RT60 estimate:    " << manualRT60 << " s\n";

        try {
            double schroederRT60 = voiceqc::EstimateRT60Schroeder(wav.samples, wav.sampleRate);
            std::cout << "Schroeder integration RT60:     " << schroederRT60 << " s\n";
        } catch (const std::exception& ex) {
            std::cout << "Schroeder integration RT60:     unavailable (" << ex.what() << ")\n";
        }

        if (lojLevel >= 1 && lojLevel <= 3) {
            std::vector<std::vector<float>> singleRecordingSet = {wav.samples};
            voiceqc::RoomAcousticsResult result =
                voiceqc::EvaluateRoom(singleRecordingSet, wav.sampleRate, lojLevel);

            std::cout << "\nLOJ Kravniva " << lojLevel << " classification: "
                      << voiceqc::ToString(result.status) << "\n";
            std::cout << "  " << result.explanation << "\n";
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
