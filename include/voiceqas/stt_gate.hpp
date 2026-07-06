#pragma once

#include "voiceqas/metrics.hpp"

namespace voiceqas {

class SttGate {
public:
    explicit SttGate(const AnalyzerConfig& config);

    double composite_score(const WindowMetrics& metrics) const;
    bool evaluate(WindowMetrics& metrics);
    void reset();

private:
    AnalyzerConfig config_;
    int consecutive_ok_ = 0;
    int consecutive_bad_ = 0;
    bool current_state_ = false;

    bool passes_thresholds(const WindowMetrics& metrics) const;
};

}  // namespace voiceqas
