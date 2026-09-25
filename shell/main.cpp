// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include "BenchmarkRunner.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Hydrogen Shell Preview"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("hydrogen.org"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Hydrogen shell prototype"));
    parser.addHelpOption();
    QCommandLineOption visualTestOption(
        QStringLiteral("visual-test"),
        QStringLiteral("Capture a deterministic test image and exit."), QStringLiteral("path"));
    parser.addOption(visualTestOption);
    QCommandLineOption benchmarkOutputOption(
        QStringLiteral("benchmark-output"),
        QStringLiteral("Measure the built-in frame scenario and write JSON."),
        QStringLiteral("path"));
    QCommandLineOption benchmarkRefreshOption(QStringLiteral("benchmark-refresh"),
                                              QStringLiteral("Target refresh rate (60 or 120)."),
                                              QStringLiteral("hz"), QStringLiteral("60"));
    QCommandLineOption benchmarkFramesOption(QStringLiteral("benchmark-frames"),
                                             QStringLiteral("Number of measured frame intervals."),
                                             QStringLiteral("count"), QStringLiteral("600"));
    QCommandLineOption benchmarkWarmupOption(QStringLiteral("benchmark-warmup"),
                                             QStringLiteral("Number of warm-up frame intervals."),
                                             QStringLiteral("count"), QStringLiteral("120"));
    QCommandLineOption hardwareLabelOption(
        QStringLiteral("hardware-label"),
        QStringLiteral("Stable identifier from docs/HARDWARE_MATRIX.md."), QStringLiteral("label"));
    parser.addOptions({
        benchmarkOutputOption,
        benchmarkRefreshOption,
        benchmarkFramesOption,
        benchmarkWarmupOption,
        hardwareLabelOption,
    });
    parser.process(app);

    const QString visualTestPath = parser.value(visualTestOption);
    const QString benchmarkOutputPath = parser.value(benchmarkOutputOption);
    const bool benchmarkMode = !benchmarkOutputPath.isEmpty();
    bool refreshRateValid = false;
    bool frameCountValid = false;
    bool warmupCountValid = false;
    const int targetRefreshHz = parser.value(benchmarkRefreshOption).toInt(&refreshRateValid);
    const int measuredFrames = parser.value(benchmarkFramesOption).toInt(&frameCountValid);
    const int warmupFrames = parser.value(benchmarkWarmupOption).toInt(&warmupCountValid);

    if (!visualTestPath.isEmpty() && benchmarkMode) {
        qCritical("--visual-test and --benchmark-output cannot be combined");
        return EXIT_FAILURE;
    }
    if (benchmarkMode &&
        (!refreshRateValid || (targetRefreshHz != 60 && targetRefreshHz != 120) ||
         !frameCountValid || measuredFrames < 10 || !warmupCountValid || warmupFrames < 0)) {
        qCritical("invalid benchmark arguments: refresh must be 60 or 120, frames "
                  "at least 10, and warm-up non-negative");
        return EXIT_FAILURE;
    }

    if (!visualTestPath.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    if (!visualTestPath.isEmpty() || benchmarkMode) {
        if (!QFontDatabase::families().contains(QStringLiteral("DejaVu Sans"))) {
            qCritical("DejaVu Sans is required for deterministic test and benchmark runs");
            return EXIT_FAILURE;
        }
        QGuiApplication::setFont(QFont(QStringLiteral("DejaVu Sans"), 10));
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    if (!visualTestPath.isEmpty() || benchmarkMode) {
        engine.setInitialProperties({
            {QStringLiteral("clockText"), QStringLiteral("09:41")},
            {QStringLiteral("reduceMotion"), !benchmarkMode},
            {QStringLiteral("reduceTransparency"), false},
            {QStringLiteral("benchmarkMode"), benchmarkMode},
        });
    }

    if (benchmarkMode) {
        Hydrogen::Performance::startFrameBenchmark(
            app, engine,
            {
                .outputPath = benchmarkOutputPath,
                .hardwareLabel = parser.value(hardwareLabelOption),
                .warmupFrames = warmupFrames,
                .measuredFrames = measuredFrames,
                .targetRefreshHz = targetRefreshHz,
            });
    }
    engine.loadFromModule(QStringLiteral("Hydrogen.Shell"), QStringLiteral("Main"));

    if (!visualTestPath.isEmpty()) {
        QTimer::singleShot(500, &app, [&app, &engine, visualTestPath] {
            if (engine.rootObjects().isEmpty()) {
                app.exit(EXIT_FAILURE);
                return;
            }

            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            const QImage image = window ? window->grabWindow() : QImage();
            const QFileInfo output(visualTestPath);
            if (image.isNull() || !output.dir().exists() || !image.save(visualTestPath)) {
                app.exit(EXIT_FAILURE);
                return;
            }
            app.exit(EXIT_SUCCESS);
        });
    }

    if (visualTestPath.isEmpty() && !benchmarkMode &&
        qEnvironmentVariableIsSet("HYDROGEN_SMOKE_TEST")) {
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
