#pragma once

#include <vector>
#include <string>
#include <map>

namespace voiceqc {

// Port of the manual Swell handclap procedure: locate the impulse peak,
// then find where the RMS envelope has dropped to about 10 percent of the
// peak amplitude (roughly -20 dB), and scale that time delta by 3 to
// extrapolate an RT60 estimate. The envelope is computed over short RMS
// windows (rmsWindowSeconds) rather than from instantaneous samples, so a
// decaying tone's own zero crossings are not mistaken for the decay.
double EstimateRT60ManualMethod(const std::vector<float>& samples,
                                 int sampleRate,
                                 double rmsWindowSeconds = 0.005);

// Schroeder backward integration method. Integrates the squared impulse
// response from the end of the signal backwards, fits a line to the
// resulting decay curve (in dB) between fitUpperDb and fitLowerDb, and
// extrapolates that line to a 60 dB drop.
double EstimateRT60Schroeder(const std::vector<float>& samples,
                              int sampleRate,
                              double fitUpperDb = -5.0,
                              double fitLowerDb = -25.0);

// LOJ compliance classification.
enum class ComplianceStatus {
    Godkand,  // approved / within spec
    Osaker,   // uncertain / near threshold
    Underkand // rejected / outside spec
};

std::string ToString(ComplianceStatus status);

struct RoomAcousticsResult {
    double meanRT60Seconds = 0.0;
    double stdDevRT60Seconds = 0.0;
    int numberOfClapsUsed = 0;
    ComplianceStatus status = ComplianceStatus::Osaker;
    std::string explanation;
};

// RT60 upper limit per LOJ Kravniva (requirement level), for the stricter
// above 500 Hz thresholds: level 1 -> 0.50 s, level 2 -> 0.25 s,
// level 3 -> 0.10 s.
extern const std::map<int, double> kLojRt60MaxSeconds;

// Averages RT60 estimates from a set of handclap recordings, computes the
// spread across them, and classifies the room against the LOJ threshold
// for targetLevel with an uncertainty margin.
RoomAcousticsResult EvaluateRoom(const std::vector<std::vector<float>>& clapRecordings,
                                  int sampleRate,
                                  int targetLevel,
                                  double uncertaintyMarginSeconds = 0.03);

} // namespace voiceqc
