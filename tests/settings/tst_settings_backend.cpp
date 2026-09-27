// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QtTest>

#include "SettingsBackend.h"

namespace
{

constexpr auto testServiceName = "org.hydrogen.SettingsTest";
constexpr auto objectPath = "/org/hydrogen/Settings1";

class FakeSettingsService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.hydrogen.Settings1")

  public:
    QString materialQuality = QStringLiteral("efficient");
    bool reduceMotion = true;
    bool reduceTransparency = false;
    bool failNextWrite = false;

  public slots:
    uint SchemaVersion() const { return 1; }

    QString MaterialQuality() const { return materialQuality; }

    void SetMaterialQuality(const QString &quality) { materialQuality = quality; }

    bool ReduceMotion() const { return reduceMotion; }

    void SetReduceMotion(bool enabled)
    {
        if (failNextWrite) {
            failNextWrite = false;
            sendErrorReply(QDBusError::Failed, QStringLiteral("injected write failure"));
            return;
        }
        reduceMotion = enabled;
    }

    bool ReduceTransparency() const { return reduceTransparency; }

    void SetReduceTransparency(bool enabled) { reduceTransparency = enabled; }
};

} // namespace

class SettingsBackendTest : public QObject
{
    Q_OBJECT

  private slots:
    void connectsWritesAndRecoversAfterRestart();
};

void SettingsBackendTest::connectsWritesAndRecoversAfterRestart()
{
    QDBusConnection connection = QDBusConnection::sessionBus();
    QVERIFY(connection.isConnected());

    SettingsBackend backend(connection, QString::fromLatin1(testServiceName));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.errorMessage().isEmpty(), 2'000);
    QVERIFY(!backend.available());

    FakeSettingsService service;
    QVERIFY(connection.registerObject(QString::fromLatin1(objectPath), &service,
                                      QDBusConnection::ExportAllSlots));
    QVERIFY(connection.registerService(QString::fromLatin1(testServiceName)));

    QTRY_VERIFY_WITH_TIMEOUT(backend.available(), 2'000);
    QCOMPARE(backend.schemaVersion(), 1U);
    QCOMPARE(backend.materialQuality(), QStringLiteral("efficient"));
    QVERIFY(backend.reduceMotion());
    QVERIFY(!backend.reduceTransparency());

    service.failNextWrite = true;
    backend.setReduceMotion(false);
    QVERIFY(!backend.reduceMotion());
    QVERIFY(backend.busy());
    QTRY_VERIFY_WITH_TIMEOUT(backend.reduceMotion(), 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.available(), 2'000);
    QVERIFY(service.reduceMotion);

    backend.setReduceMotion(false);
    QVERIFY(!backend.reduceMotion());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 2'000);
    QVERIFY(!service.reduceMotion);

    backend.setReduceTransparency(true);
    QVERIFY(backend.reduceTransparency());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 2'000);
    QVERIFY(service.reduceTransparency);

    backend.setMaterialQuality(QStringLiteral("opaque"));
    QCOMPARE(backend.materialQuality(), QStringLiteral("opaque"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 2'000);
    QCOMPARE(service.materialQuality, QStringLiteral("opaque"));

    backend.setMaterialQuality(QStringLiteral("invalid"));
    QCOMPARE(backend.materialQuality(), QStringLiteral("opaque"));
    QCOMPARE(service.materialQuality, QStringLiteral("opaque"));

    QVERIFY(connection.unregisterService(QString::fromLatin1(testServiceName)));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.available(), 2'000);
    QVERIFY(!backend.errorMessage().isEmpty());

    service.materialQuality = QStringLiteral("full");
    QVERIFY(connection.registerService(QString::fromLatin1(testServiceName)));
    QTRY_VERIFY_WITH_TIMEOUT(backend.available(), 2'000);
    QCOMPARE(backend.materialQuality(), QStringLiteral("full"));

    QVERIFY(connection.unregisterService(QString::fromLatin1(testServiceName)));
    connection.unregisterObject(QString::fromLatin1(objectPath));
}

QTEST_GUILESS_MAIN(SettingsBackendTest)

#include "tst_settings_backend.moc"
