// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "FrameMetrics.h"

class FrameMetricsTest : public QObject
{
    Q_OBJECT

  private slots:
    void calculatesNearestRankPercentiles();
    void countsMissedPresentationSlots();
    void handlesEmptyInput();
};

void FrameMetricsTest::calculatesNearestRankPercentiles()
{
    QList<double> values;
    for (int value = 1; value <= 100; ++value) {
        values.append(value);
    }

    const auto result = Hydrogen::Performance::summarizeFrameTimes(values, 200.0);
    QCOMPARE(result.meanMs, 50.5);
    QCOMPARE(result.p50Ms, 50.0);
    QCOMPARE(result.p95Ms, 95.0);
    QCOMPARE(result.p99Ms, 99.0);
    QCOMPARE(result.maximumMs, 100.0);
}

void FrameMetricsTest::countsMissedPresentationSlots()
{
    const QList<double> values{8.0, 16.0, 17.0, 25.1, 34.0};
    const auto result = Hydrogen::Performance::summarizeFrameTimes(values, 16.67);

    QCOMPARE(result.framesOverBudget, 3);
    QCOMPARE(result.droppedFrames, 2);
}

void FrameMetricsTest::handlesEmptyInput()
{
    const auto result = Hydrogen::Performance::summarizeFrameTimes({}, 16.67);
    QCOMPARE(result.p95Ms, 0.0);
    QCOMPARE(result.droppedFrames, 0);
}

QTEST_APPLESS_MAIN(FrameMetricsTest)

#include "tst_frame_metrics.moc"
