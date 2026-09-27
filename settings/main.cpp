// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QVariantMap>

#include "SettingsBackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Hydrogen Settings"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("hydrogen.org"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    SettingsBackend settingsBackend;
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.setInitialProperties({
        {QStringLiteral("settingsBackend"), QVariant::fromValue(&settingsBackend)},
    });
    engine.loadFromModule(QStringLiteral("Hydrogen.Settings"), QStringLiteral("Main"));

    if (qEnvironmentVariableIsSet("HYDROGEN_SMOKE_TEST")) {
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
