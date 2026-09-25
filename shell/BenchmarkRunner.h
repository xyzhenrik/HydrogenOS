// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QGuiApplication;
class QQmlApplicationEngine;

namespace Hydrogen::Performance
{

struct BenchmarkConfig
{
    QString outputPath;
    QString hardwareLabel;
    int warmupFrames = 0;
    int measuredFrames = 0;
    int targetRefreshHz = 60;
};

void startFrameBenchmark(QGuiApplication &app, QQmlApplicationEngine &engine,
                         const BenchmarkConfig &config);

} // namespace Hydrogen::Performance
