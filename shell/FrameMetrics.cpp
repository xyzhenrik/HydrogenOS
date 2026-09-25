// SPDX-License-Identifier: GPL-3.0-or-later

#include "FrameMetrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{

double percentile(const QList<double> &sortedValues, double percentileValue)
{
    if (sortedValues.isEmpty()) {
        return 0.0;
    }

    const auto rank = static_cast<qsizetype>(
        std::ceil(percentileValue * static_cast<double>(sortedValues.size())));
    return sortedValues.at(std::clamp<qsizetype>(rank - 1, 0, sortedValues.size() - 1));
}

} // namespace

namespace Hydrogen::Performance
{

FrameSummary summarizeFrameTimes(const QList<double> &frameTimesMs, double frameBudgetMs)
{
    FrameSummary result;
    if (frameTimesMs.isEmpty() || frameBudgetMs <= 0.0) {
        return result;
    }

    QList<double> sortedValues = frameTimesMs;
    std::sort(sortedValues.begin(), sortedValues.end());

    result.meanMs = std::accumulate(sortedValues.cbegin(), sortedValues.cend(), 0.0) /
                    static_cast<double>(sortedValues.size());
    result.p50Ms = percentile(sortedValues, 0.50);
    result.p95Ms = percentile(sortedValues, 0.95);
    result.p99Ms = percentile(sortedValues, 0.99);
    result.maximumMs = sortedValues.constLast();

    for (const double frameTimeMs : sortedValues) {
        if (frameTimeMs > frameBudgetMs) {
            ++result.framesOverBudget;
        }

        const auto presentationSlots =
            static_cast<qsizetype>(std::floor((frameTimeMs / frameBudgetMs) + 0.5));
        result.droppedFrames += std::max<qsizetype>(0, presentationSlots - 1);
    }

    return result;
}

} // namespace Hydrogen::Performance
