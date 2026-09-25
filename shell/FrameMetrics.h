// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

namespace Hydrogen::Performance
{

struct FrameSummary
{
    double meanMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double maximumMs = 0.0;
    qsizetype framesOverBudget = 0;
    qsizetype droppedFrames = 0;
};

FrameSummary summarizeFrameTimes(const QList<double> &frameTimesMs, double frameBudgetMs);

} // namespace Hydrogen::Performance
