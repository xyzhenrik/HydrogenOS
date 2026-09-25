// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QTimer>

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
        QStringLiteral("Capture a deterministic test image and exit."),
        QStringLiteral("path"));
    parser.addOption(visualTestOption);
    parser.process(app);

    const QString visualTestPath = parser.value(visualTestOption);
    if (!visualTestPath.isEmpty()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        if (!QFontDatabase::families().contains(QStringLiteral("DejaVu Sans"))) {
            qCritical("DejaVu Sans is required for deterministic visual tests");
            return EXIT_FAILURE;
        }
        QGuiApplication::setFont(QFont(QStringLiteral("DejaVu Sans"), 10));
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    if (!visualTestPath.isEmpty()) {
        engine.setInitialProperties({
            {QStringLiteral("clockText"), QStringLiteral("09:41")},
            {QStringLiteral("reduceMotion"), true},
            {QStringLiteral("reduceTransparency"), false},
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

    if (visualTestPath.isEmpty() && qEnvironmentVariableIsSet("HYDROGEN_SMOKE_TEST")) {
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
