#include "ReverberationEstimator.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <sstream>

namespace voiceqc {

namespace {

// Simple RMS envelope computed over sliding, non overlapping windows of
// windowSize samples. Returns one RMS value per window.
std::vector<double> ComputeRmsEnvelope(const std::vector<float>& samples, size_t windowSize) {
    std::vector<double> envelope;
    if (windowSize == 0) {
        throw std::runtime_error("ComputeRmsEnvelope: windowSize must be > 0");
    }
    for (size_t start = 0; start < samples.size(); start += windowSize) {
        size_t end = std::min(start + windowSize, samples.size());
        double sumSquares = 0.0;
        for (size_t i = start; i < end; ++i) {
            double v = static_cast<double>(samples[i]);
            sumSquares += v * v;
        }
        size_t count = end - start;
        envelope.push_back(count > 0 ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0);
    }
    return envelope;
}

// Least squares linear fit y = a*x + b restricted to the points whose y
// value (in dB) falls within [lowerDb, upperDb]. Returns {a, b}.
std::pair<double, double> LinearFitInDbRange(const std::vector<double>& timesSeconds,
                                              const std::vector<double>& levelsDb,
                                              double upperDb,
                                              double lowerDb) {
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
    size_t n = 0;

    for (size_t i = 0; i < levelsDb.size(); ++i) {
        double y = levelsDb[i];
        if (y <= upperDb && y >= lowerDb) {
            double x = timesSeconds[i];
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumXX += x * x;
            ++n;
        }
    }

    if (n < 2) {
        throw std::runtime_error("LinearFitInDbRange: not enough points in the requested dB range "
                                  "to fit a line; widen fitUpperDb/fitLowerDb or check the recording");
    }

    double nd = static_cast<double>(n);
    double denom = (nd * sumXX - sumX * sumX);
    if (std::fabs(denom) < 1e-12) {
        throw std::runtime_error("LinearFitInDbRange: degenerate fit (all points share the same time)");
    }

    double a = (nd * sumXY - sumX * sumY) / denom; // slope, dB per second
    double b = (sumY - a * sumX) / nd;              // intercept, dB
    return {a, b};
}

} // namespace

double EstimateRT60ManualMethod(const std::vector<float>& samples, int sampleRate, double rmsWindowSeconds) {
    if (samples.empty() || sampleRate <= 0) {
        throw std::runtime_error("EstimateRT60ManualMethod: invalid input");
    }
    if (rmsWindowSeconds <= 0.0) {
        throw std::runtime_error("EstimateRT60ManualMethod: rmsWindowSeconds must be > 0");
    }

    // Work on the RMS envelope rather than instantaneous samples, so that a
    // decaying tone's own zero crossings are not mistaken for the envelope
    // reaching the -20 dB threshold.
    size_t windowSize = std::max<size_t>(
        1, static_cast<size_t>(std::lround(rmsWindowSeconds * sampleRate)));
    std::vector<double> envelope = ComputeRmsEnvelope(samples, windowSize);

    size_t peakWindow = 0;
    double peakValue = envelope.empty() ? 0.0 : envelope[0];
    for (size_t i = 1; i < envelope.size(); ++i) {
        if (envelope[i] > peakValue) {
            peakValue = envelope[i];
            peakWindow = i;
        }
    }
    if (peakValue <= 0.0) {
        throw std::runtime_error("EstimateRT60ManualMethod: signal peak is zero");
    }

    // Threshold at 10 percent of peak envelope amplitude, i.e. approximately -20 dB.
    double threshold = peakValue * 0.1;

    size_t crossingWindow = envelope.size() - 1;
    bool found = false;
    for (size_t i = peakWindow; i < envelope.size(); ++i) {
        if (envelope[i] <= threshold) {
            crossingWindow = i;
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::runtime_error("EstimateRT60ManualMethod: signal never decays to -20 dB of peak; "
                                  "recording may be too short or too noisy");
    }

    double deltaSeconds = static_cast<double>(crossingWindow - peakWindow) * rmsWindowSeconds;
    return deltaSeconds * 3.0;
}

double EstimateRT60Schroeder(const std::vector<float>& samples,
                              int sampleRate,
                              double fitUpperDb,
                              double fitLowerDb) {
    if (samples.empty() || sampleRate <= 0) {
        throw std::runtime_error("EstimateRT60Schroeder: invalid input");
    }

    size_t n = samples.size();

    // Backward integration of the squared impulse response:
    // energy[i] = sum_{k=i}^{n-1} samples[k]^2
    std::vector<double> energy(n, 0.0);
    double runningSum = 0.0;
    for (size_t i = n; i-- > 0;) {
        double v = static_cast<double>(samples[i]);
        runningSum += v * v;
        energy[i] = runningSum;
    }

    double totalEnergy = energy.empty() ? 0.0 : energy[0];
    if (totalEnergy <= 0.0) {
        throw std::runtime_error("EstimateRT60Schroeder: signal has zero energy");
    }

    std::vector<double> timesSeconds(n);
    std::vector<double> levelsDb(n);
    for (size_t i = 0; i < n; ++i) {
        timesSeconds[i] = static_cast<double>(i) / static_cast<double>(sampleRate);
        double normalized = energy[i] / totalEnergy;
        // Guard against log(0) at the very tail of the decay.
        double safe = std::max(normalized, 1e-12);
        levelsDb[i] = 10.0 * std::log10(safe);
    }

    auto fit = LinearFitInDbRange(timesSeconds, levelsDb, fitUpperDb, fitLowerDb);
    double slopeDbPerSecond = fit.first;

    if (slopeDbPerSecond >= 0.0) {
        throw std::runtime_error("EstimateRT60Schroeder: fitted decay slope is not negative; "
                                  "check the recording or the fit range");
    }

    // Time for the fitted line to drop by 60 dB.
    return -60.0 / slopeDbPerSecond;
}

std::string ToString(ComplianceStatus status) {
    switch (status) {
        case ComplianceStatus::Godkand:  return "Godkand";
        case ComplianceStatus::Osaker:   return "Osaker";
        case ComplianceStatus::Underkand: return "Underkand";
    }
    return "Unknown";
}

const std::map<int, double> kLojRt60MaxSeconds = {
    {1, 0.50},
    {2, 0.25},
    {3, 0.10},
};

RoomAcousticsResult EvaluateRoom(const std::vector<std::vector<float>>& clapRecordings,
                                  int sampleRate,
                                  int targetLevel,
                                  double uncertaintyMarginSeconds) {
    if (clapRecordings.empty()) {
        throw std::runtime_error("EvaluateRoom: no clap recordings provided");
    }

    auto thresholdIt = kLojRt60MaxSeconds.find(targetLevel);
    if (thresholdIt == kLojRt60MaxSeconds.end()) {
        throw std::runtime_error("EvaluateRoom: unknown LOJ Kravniva level requested");
    }
    double thresholdSeconds = thresholdIt->second;

    std::vector<double> estimates;
    estimates.reserve(clapRecordings.size());
    for (const auto& recording : clapRecordings) {
        estimates.push_back(EstimateRT60ManualMethod(recording, sampleRate));
    }

    double mean = std::accumulate(estimates.begin(), estimates.end(), 0.0) /
                  static_cast<double>(estimates.size());

    double variance = 0.0;
    for (double v : estimates) {
        double d = v - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(estimates.size());
    double stdDev = std::sqrt(variance);

    ComplianceStatus status;
    std::ostringstream explanation;

    if (mean <= thresholdSeconds - uncertaintyMarginSeconds) {
        status = ComplianceStatus::Godkand;
        explanation << "Mean RT60 of " << mean << " s is below the LOJ Kravniva " << targetLevel
                    << " threshold of " << thresholdSeconds << " s with margin to spare.";
    } else if (mean >= thresholdSeconds + uncertaintyMarginSeconds) {
        status = ComplianceStatus::Underkand;
        explanation << "Mean RT60 of " << mean << " s exceeds the LOJ Kravniva " << targetLevel
                    << " threshold of " << thresholdSeconds << " s.";
    } else {
        status = ComplianceStatus::Osaker;
        explanation << "Mean RT60 of " << mean << " s is within " << uncertaintyMarginSeconds
                    << " s of the LOJ Kravniva " << targetLevel << " threshold of "
                    << thresholdSeconds << " s; classification is uncertain given measurement spread.";
    }

    RoomAcousticsResult result;
    result.meanRT60Seconds = mean;
    result.stdDevRT60Seconds = stdDev;
    result.numberOfClapsUsed = static_cast<int>(estimates.size());
    result.status = status;
    result.explanation = explanation.str();
    return result;
}

} // namespace voiceqc
