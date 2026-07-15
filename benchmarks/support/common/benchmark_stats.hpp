#ifndef FOREST_SORTING_SUPPORT_BENCHMARK_STATS_HPP
#define FOREST_SORTING_SUPPORT_BENCHMARK_STATS_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::test_support {

enum class OutputFormat : uint8_t {
    Table,
    Csv,
    Tsv,
    Json,
};

inline OutputFormat parseFormat(std::string_view value) {
    if (value == "table") {
        return OutputFormat::Table;
    }
    if (value == "csv") {
        return OutputFormat::Csv;
    }
    if (value == "tsv") {
        return OutputFormat::Tsv;
    }
    if (value == "json") {
        return OutputFormat::Json;
    }
    throw std::runtime_error("unknown format: " + std::string(value));
}

struct ConfidenceInterval {
    double low = 0.0;
    double high = 0.0;
};

struct SampleStats {
    double min = 0.0;
    double median = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
    double max = 0.0;
    ConfidenceInterval ci95;
};

enum class ComparisonEligibility : uint8_t {
    Ok,
    InvalidCandidate,
    InvalidBaseline,
};

inline ComparisonEligibility comparisonEligibility(bool candidateValid,
                                                   bool baselineValid) {
    if (!candidateValid) {
        return ComparisonEligibility::InvalidCandidate;
    }
    if (!baselineValid) {
        return ComparisonEligibility::InvalidBaseline;
    }
    return ComparisonEligibility::Ok;
}

inline constexpr std::size_t kBootstrapResamples = 1000;
inline constexpr uint32_t kBootstrapSeed = 0x51a751c5U;

inline std::string_view
classifyBenchmarkWinner(double deltaMedianPct,
                        const ConfidenceInterval &deltaPctCi95) noexcept {
    if (deltaMedianPct < 0.0 && deltaPctCi95.high < 0.0) {
        return "candidate";
    }
    if (deltaMedianPct > 0.0 && deltaPctCi95.low > 0.0) {
        return "baseline";
    }
    return "tie";
}

inline std::vector<std::size_t> makeSequentialIndexOrder(std::size_t count) {
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    return order;
}

inline void shuffleBenchmarkOrder(std::vector<std::size_t> &order,
                                  uint32_t orderSeed, int passIndex) {
    std::mt19937 engine(orderSeed + static_cast<uint32_t>(passIndex));
    std::shuffle(order.begin(), order.end(), engine);
}

inline double medianOfSortedSamples(const std::vector<double> &sortedSamples) {
    if (sortedSamples.empty()) {
        throw std::runtime_error("cannot compute median of empty samples");
    }

    const std::size_t middle = sortedSamples.size() / 2U;
    if ((sortedSamples.size() % 2U) == 1U) {
        return sortedSamples[middle];
    }
    return (sortedSamples[middle - 1U] + sortedSamples[middle]) / 2.0;
}

inline double medianOfSamples(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return medianOfSortedSamples(samples);
}

inline double meanOfSamples(const std::vector<double> &samples) {
    if (samples.empty()) {
        throw std::runtime_error("cannot compute mean of empty samples");
    }
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return total / static_cast<double>(samples.size());
}

inline double sampleStddev(const std::vector<double> &samples, double mean) {
    if (samples.size() < 2U) {
        return 0.0;
    }

    double squaredDifferenceTotal = 0.0;
    for (double sample : samples) {
        const double difference = sample - mean;
        squaredDifferenceTotal += difference * difference;
    }
    return std::sqrt(squaredDifferenceTotal /
                     static_cast<double>(samples.size() - 1U));
}

inline ConfidenceInterval
bootstrapMeanCi95(const std::vector<double> &samples,
                  uint32_t seed = kBootstrapSeed,
                  std::size_t resamples = kBootstrapResamples) {
    if (samples.empty()) {
        throw std::runtime_error("cannot bootstrap empty samples");
    }
    if (samples.size() == 1U || resamples == 0U) {
        return {samples.front(), samples.front()};
    }

    std::mt19937 engine(seed);
    std::uniform_int_distribution<std::size_t> distribution(0U, samples.size() -
                                                                    1U);
    std::vector<double> resampledMeans;
    resampledMeans.reserve(resamples);

    for (std::size_t resampleIdx = 0; resampleIdx < resamples; ++resampleIdx) {
        double total = 0.0;
        for (std::size_t sampleIdx = 0; sampleIdx < samples.size();
             ++sampleIdx) {
            total += samples[distribution(engine)];
        }
        resampledMeans.push_back(total / static_cast<double>(samples.size()));
    }

    std::sort(resampledMeans.begin(), resampledMeans.end());
    const std::size_t lowIdx = (resampledMeans.size() * 25U) / 1000U;
    std::size_t highIdx = (resampledMeans.size() * 975U) / 1000U;
    if (highIdx >= resampledMeans.size()) {
        highIdx = resampledMeans.size() - 1U;
    }
    return {resampledMeans[lowIdx], resampledMeans[highIdx]};
}

inline SampleStats computeSampleStats(const std::vector<double> &samples) {
    if (samples.empty()) {
        throw std::runtime_error("cannot summarize empty samples");
    }

    std::vector<double> sortedSamples = samples;
    std::sort(sortedSamples.begin(), sortedSamples.end());

    SampleStats stats;
    stats.min = sortedSamples.front();
    stats.median = medianOfSortedSamples(sortedSamples);
    stats.mean = meanOfSamples(samples);
    stats.stddev = sampleStddev(samples, stats.mean);
    stats.max = sortedSamples.back();
    stats.ci95 = bootstrapMeanCi95(samples);
    return stats;
}

inline std::vector<double>
pairedAbsoluteDeltas(const std::vector<double> &samples,
                     const std::vector<double> &baselineSamples) {
    const std::size_t pairCount =
        std::min(samples.size(), baselineSamples.size());
    if (pairCount == 0U) {
        throw std::runtime_error("cannot compare empty benchmark samples");
    }

    std::vector<double> deltas;
    deltas.reserve(pairCount);
    for (std::size_t sampleIdx = 0; sampleIdx < pairCount; ++sampleIdx) {
        deltas.push_back(samples[sampleIdx] - baselineSamples[sampleIdx]);
    }
    return deltas;
}

inline std::vector<double>
pairedRelativeDeltas(const std::vector<double> &samples,
                     const std::vector<double> &baselineSamples) {
    const std::size_t pairCount =
        std::min(samples.size(), baselineSamples.size());
    if (pairCount == 0U) {
        throw std::runtime_error("cannot compare empty benchmark samples");
    }

    std::vector<double> deltas;
    deltas.reserve(pairCount);
    for (std::size_t sampleIdx = 0; sampleIdx < pairCount; ++sampleIdx) {
        const double baseline = baselineSamples[sampleIdx];
        if (baseline == 0.0) {
            deltas.push_back(0.0);
        } else {
            deltas.push_back(((samples[sampleIdx] - baseline) / baseline) *
                             100.0);
        }
    }
    return deltas;
}

inline ConfidenceInterval
bootstrapPairedRelativeDeltaCi95(const std::vector<double> &samples,
                                 const std::vector<double> &baselineSamples,
                                 uint32_t seed = kBootstrapSeed,
                                 std::size_t resamples = kBootstrapResamples) {
    const std::size_t pairCount =
        std::min(samples.size(), baselineSamples.size());
    if (pairCount == 0U) {
        throw std::runtime_error("cannot compare empty benchmark samples");
    }

    if (pairCount == 1U || resamples == 0U) {
        const auto deltas = pairedRelativeDeltas(samples, baselineSamples);
        return {deltas.front(), deltas.front()};
    }

    std::mt19937 engine(seed);
    std::uniform_int_distribution<std::size_t> distribution(0U, pairCount - 1U);
    std::vector<double> resampledDeltas;
    resampledDeltas.reserve(resamples);

    for (std::size_t resampleIdx = 0; resampleIdx < resamples; ++resampleIdx) {
        double total = 0.0;
        for (std::size_t sampleIdx = 0; sampleIdx < pairCount; ++sampleIdx) {
            const std::size_t selectedIdx = distribution(engine);
            const double baseline = baselineSamples[selectedIdx];
            if (baseline != 0.0) {
                total += ((samples[selectedIdx] - baseline) / baseline) * 100.0;
            }
        }
        resampledDeltas.push_back(total / static_cast<double>(pairCount));
    }

    std::sort(resampledDeltas.begin(), resampledDeltas.end());
    const std::size_t lowIdx = (resampledDeltas.size() * 25U) / 1000U;
    std::size_t highIdx = (resampledDeltas.size() * 975U) / 1000U;
    if (highIdx >= resampledDeltas.size()) {
        highIdx = resampledDeltas.size() - 1U;
    }
    return {resampledDeltas[lowIdx], resampledDeltas[highIdx]};
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_BENCHMARK_STATS_HPP
