// SPDX-License-Identifier: GPL-3.0-or-later

#include "BenchmarkRunner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include "FrameMetrics.h"

namespace
{

QString graphicsApiName(QSGRendererInterface::GraphicsApi graphicsApi)
{
    switch (graphicsApi) {
    case QSGRendererInterface::Software:
        return QStringLiteral("software");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("opengl");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("vulkan");
    case QSGRendererInterface::Metal:
        return QStringLiteral("metal");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("direct3d11");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("direct3d12");
    case QSGRendererInterface::Null:
        return QStringLiteral("null");
    default:
        return QStringLiteral("unknown");
    }
}

bool writeBenchmarkResult(const Hydrogen::Performance::BenchmarkConfig &config,
                          QQuickWindow *window, const QList<double> &frameTimesMs)
{
    const double frameBudgetMs = 1000.0 / static_cast<double>(config.targetRefreshHz);
    const auto summary = Hydrogen::Performance::summarizeFrameTimes(frameTimesMs, frameBudgetMs);
    const QString platformName = QGuiApplication::platformName();
    const QString graphicsApi = graphicsApiName(window->rendererInterface()->graphicsApi());
    const double detectedRefreshHz = window->screen() ? window->screen()->refreshRate() : 0.0;
#ifdef NDEBUG
    constexpr bool optimizedBuild = true;
#else
    constexpr bool optimizedBuild = false;
#endif

    QJsonArray qualificationReasons;
    if (!optimizedBuild) {
        qualificationReasons.append(QStringLiteral("an optimized release build is required"));
    }
    if (platformName != QStringLiteral("wayland")) {
        qualificationReasons.append(QStringLiteral("a Wayland session is required"));
    }
    if (graphicsApi == QStringLiteral("software") || graphicsApi == QStringLiteral("null") ||
        graphicsApi == QStringLiteral("unknown")) {
        qualificationReasons.append(QStringLiteral("hardware-accelerated rendering is required"));
    }
    if (config.hardwareLabel.isEmpty()) {
        qualificationReasons.append(QStringLiteral("--hardware-label is required"));
    }
    if (detectedRefreshHz <= 0.0 || std::abs(detectedRefreshHz - config.targetRefreshHz) > 1.0) {
        qualificationReasons.append(
            QStringLiteral("detected refresh rate does not match the target"));
    }

    const QJsonObject document{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("kind"), QStringLiteral("frame_time")},
        {QStringLiteral("scenario"), QStringLiteral("prototype_card_and_dock_transition")},
        {QStringLiteral("configuration"),
         QJsonObject{
             {QStringLiteral("material_level"), QStringLiteral("full")},
             {QStringLiteral("reduce_motion"), false},
             {QStringLiteral("reduce_transparency"), false},
         }},
        {QStringLiteral("target"),
         QJsonObject{
             {QStringLiteral("refresh_hz"), config.targetRefreshHz},
             {QStringLiteral("frame_budget_ms"), frameBudgetMs},
         }},
        {QStringLiteral("samples"),
         QJsonObject{
             {QStringLiteral("warmup_frames"), config.warmupFrames},
             {QStringLiteral("measured_frames"), frameTimesMs.size()},
         }},
        {QStringLiteral("metrics"),
         QJsonObject{
             {QStringLiteral("mean_ms"), summary.meanMs},
             {QStringLiteral("p50_ms"), summary.p50Ms},
             {QStringLiteral("p95_ms"), summary.p95Ms},
             {QStringLiteral("p99_ms"), summary.p99Ms},
             {QStringLiteral("maximum_ms"), summary.maximumMs},
             {QStringLiteral("frames_over_budget"), summary.framesOverBudget},
             {QStringLiteral("dropped_frames"), summary.droppedFrames},
         }},
        {QStringLiteral("assessment"),
         QJsonObject{
             {QStringLiteral("p95_budget_passed"), summary.p95Ms <= frameBudgetMs},
         }},
        {QStringLiteral("environment"),
         QJsonObject{
             {QStringLiteral("hardware_label"), config.hardwareLabel},
             {QStringLiteral("optimized_build"), optimizedBuild},
             {QStringLiteral("platform"), platformName},
             {QStringLiteral("session_type"), qEnvironmentVariable("XDG_SESSION_TYPE")},
             {QStringLiteral("desktop"), qEnvironmentVariable("XDG_CURRENT_DESKTOP")},
             {QStringLiteral("graphics_api"), graphicsApi},
             {QStringLiteral("detected_refresh_hz"), detectedRefreshHz},
             {QStringLiteral("device_pixel_ratio"), window->devicePixelRatio()},
             {QStringLiteral("logical_width"), window->width()},
             {QStringLiteral("logical_height"), window->height()},
             {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
             {QStringLiteral("os"), QSysInfo::prettyProductName()},
             {QStringLiteral("kernel"), QSysInfo::kernelVersion()},
             {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
         }},
        {QStringLiteral("qualification"),
         QJsonObject{
             {QStringLiteral("eligible"), qualificationReasons.isEmpty()},
             {QStringLiteral("reasons"), qualificationReasons},
         }},
    };

    const QFileInfo output(config.outputPath);
    if (!QDir().mkpath(output.dir().absolutePath())) {
        return false;
    }

    QSaveFile file(config.outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    return file.commit();
}

struct BenchmarkState
{
    QElapsedTimer timer;
    QList<double> frameTimesMs;
    int warmupFramesRemaining = 0;
    int measuredFrames = 0;
    std::atomic_bool complete = false;
};

} // namespace

namespace Hydrogen::Performance
{

void startFrameBenchmark(QGuiApplication &app, QQmlApplicationEngine &engine,
                         const BenchmarkConfig &config)
{
    QTimer::singleShot(0, &app, [&app, &engine, config] {
        if (engine.rootObjects().isEmpty()) {
            qCritical("benchmark window was not created");
            app.exit(EXIT_FAILURE);
            return;
        }

        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window) {
            qCritical("benchmark root object is not a QQuickWindow");
            app.exit(EXIT_FAILURE);
            return;
        }

        auto state = std::make_shared<BenchmarkState>();
        state->warmupFramesRemaining = config.warmupFrames;
        state->measuredFrames = config.measuredFrames;
        state->frameTimesMs.reserve(config.measuredFrames);

        QObject::connect(
            window, &QQuickWindow::frameSwapped, window,
            [&app, window, state, config] {
                if (state->complete.load()) {
                    return;
                }
                if (!state->timer.isValid()) {
                    state->timer.start();
                    return;
                }

                const double elapsedMs =
                    static_cast<double>(state->timer.nsecsElapsed()) / 1'000'000.0;
                state->timer.restart();
                if (state->warmupFramesRemaining > 0) {
                    --state->warmupFramesRemaining;
                    return;
                }

                state->frameTimesMs.append(elapsedMs);
                if (state->frameTimesMs.size() < state->measuredFrames ||
                    state->complete.exchange(true)) {
                    return;
                }

                QMetaObject::invokeMethod(
                    &app,
                    [&app, window, state, config] {
                        const bool written =
                            writeBenchmarkResult(config, window, state->frameTimesMs);
                        if (!written) {
                            qCritical("could not write benchmark result");
                        }
                        app.exit(written ? EXIT_SUCCESS : EXIT_FAILURE);
                    },
                    Qt::QueuedConnection);
            },
            Qt::DirectConnection);

        const int expectedDurationMs =
            ((config.warmupFrames + config.measuredFrames) * 1000) / config.targetRefreshHz;
        QTimer::singleShot(std::max(30'000, expectedDurationMs * 5), window, [&app, state] {
            if (!state->complete.exchange(true)) {
                qCritical("benchmark timed out before collecting all frame intervals");
                app.exit(EXIT_FAILURE);
            }
        });
    });
}

} // namespace Hydrogen::Performance
