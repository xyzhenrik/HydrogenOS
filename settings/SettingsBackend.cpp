// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsBackend.h"

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariant>

#include <utility>

namespace
{

constexpr auto busName = "org.hydrogen.Settings";
constexpr auto objectPath = "/org/hydrogen/Settings1";
constexpr auto interfaceName = "org.hydrogen.Settings1";

} // namespace

SettingsBackend::SettingsBackend(QObject *parent)
    : SettingsBackend(QDBusConnection::sessionBus(), QString::fromLatin1(busName), parent)
{
}

SettingsBackend::SettingsBackend(const QDBusConnection &connection, QString serviceName,
                                 QObject *parent)
    : QObject(parent), m_connection(connection), m_serviceName(std::move(serviceName)),
      m_serviceWatcher(m_serviceName, m_connection, QDBusServiceWatcher::WatchForOwnerChange, this)
{
    connect(&m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &oldOwner, const QString &newOwner) {
                handleOwnerChange(oldOwner, newOwner);
            });
    reload();
}

SettingsBackend::~SettingsBackend() = default;

bool SettingsBackend::available() const { return m_available; }

bool SettingsBackend::busy() const { return m_busy; }

uint SettingsBackend::schemaVersion() const { return m_schemaVersion; }

QString SettingsBackend::materialQuality() const { return m_materialQuality; }

bool SettingsBackend::reduceMotion() const { return m_reduceMotion; }

bool SettingsBackend::reduceTransparency() const { return m_reduceTransparency; }

QString SettingsBackend::errorMessage() const { return m_errorMessage; }

void SettingsBackend::setMaterialQuality(const QString &quality)
{
    if (!m_available || m_busy || quality == m_materialQuality ||
        (quality != QStringLiteral("full") && quality != QStringLiteral("efficient") &&
         quality != QStringLiteral("opaque"))) {
        return;
    }

    const QString previous = m_materialQuality;
    m_materialQuality = quality;
    emit materialQualityChanged();
    sendWrite(QStringLiteral("SetMaterialQuality"), quality, [this, previous] {
        if (m_materialQuality != previous) {
            m_materialQuality = previous;
            emit materialQualityChanged();
        }
    });
}

void SettingsBackend::setReduceMotion(bool enabled)
{
    if (!m_available || m_busy || enabled == m_reduceMotion) {
        return;
    }

    const bool previous = m_reduceMotion;
    m_reduceMotion = enabled;
    emit reduceMotionChanged();
    sendWrite(QStringLiteral("SetReduceMotion"), enabled, [this, previous] {
        if (m_reduceMotion != previous) {
            m_reduceMotion = previous;
            emit reduceMotionChanged();
        }
    });
}

void SettingsBackend::setReduceTransparency(bool enabled)
{
    if (!m_available || m_busy || enabled == m_reduceTransparency) {
        return;
    }

    const bool previous = m_reduceTransparency;
    m_reduceTransparency = enabled;
    emit reduceTransparencyChanged();
    sendWrite(QStringLiteral("SetReduceTransparency"), enabled, [this, previous] {
        if (m_reduceTransparency != previous) {
            m_reduceTransparency = previous;
            emit reduceTransparencyChanged();
        }
    });
}

void SettingsBackend::reload()
{
    ++m_generation;
    const quint64 generation = m_generation;
    m_pendingLoads = 4;
    m_loadFailed = false;
    setAvailable(false);
    setBusy(false);
    setErrorMessage({});

    if (!m_connection.isConnected()) {
        m_pendingLoads = 0;
        m_loadFailed = true;
        setErrorMessage(tr("The session bus is unavailable."));
        return;
    }

    m_interface =
        std::make_unique<QDBusInterface>(m_serviceName, QString::fromLatin1(objectPath),
                                         QString::fromLatin1(interfaceName), m_connection);

    auto *schemaWatcher =
        new QDBusPendingCallWatcher(m_interface->asyncCall(QStringLiteral("SchemaVersion")), this);
    connect(schemaWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<uint> reply = *watcher;
                watcher->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    finishLoad(generation, reply.error().message());
                    return;
                }
                if (m_schemaVersion != reply.value()) {
                    m_schemaVersion = reply.value();
                    emit schemaVersionChanged();
                }
                finishLoad(generation);
            });

    auto *qualityWatcher = new QDBusPendingCallWatcher(
        m_interface->asyncCall(QStringLiteral("MaterialQuality")), this);
    connect(qualityWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    finishLoad(generation, reply.error().message());
                    return;
                }
                if (m_materialQuality != reply.value()) {
                    m_materialQuality = reply.value();
                    emit materialQualityChanged();
                }
                finishLoad(generation);
            });

    auto *motionWatcher =
        new QDBusPendingCallWatcher(m_interface->asyncCall(QStringLiteral("ReduceMotion")), this);
    connect(motionWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<bool> reply = *watcher;
                watcher->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    finishLoad(generation, reply.error().message());
                    return;
                }
                if (m_reduceMotion != reply.value()) {
                    m_reduceMotion = reply.value();
                    emit reduceMotionChanged();
                }
                finishLoad(generation);
            });

    auto *transparencyWatcher = new QDBusPendingCallWatcher(
        m_interface->asyncCall(QStringLiteral("ReduceTransparency")), this);
    connect(transparencyWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<bool> reply = *watcher;
                watcher->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    finishLoad(generation, reply.error().message());
                    return;
                }
                if (m_reduceTransparency != reply.value()) {
                    m_reduceTransparency = reply.value();
                    emit reduceTransparencyChanged();
                }
                finishLoad(generation);
            });
}

void SettingsBackend::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    emit availableChanged();
}

void SettingsBackend::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void SettingsBackend::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void SettingsBackend::finishLoad(quint64 generation, const QString &errorMessage)
{
    if (generation != m_generation || m_pendingLoads <= 0) {
        return;
    }
    if (!errorMessage.isEmpty()) {
        m_loadFailed = true;
        setErrorMessage(errorMessage);
    }
    --m_pendingLoads;
    if (m_pendingLoads == 0) {
        setAvailable(!m_loadFailed);
        if (!m_loadFailed) {
            setErrorMessage({});
        }
    }
}

void SettingsBackend::sendWrite(const QString &method, const QVariant &argument,
                                const std::function<void()> &rollback)
{
    if (!m_interface) {
        rollback();
        return;
    }

    setBusy(true);
    setErrorMessage({});
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_interface->asyncCall(method, argument), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation, rollback](QDBusPendingCallWatcher *finishedWatcher) {
                const QDBusPendingReply<> reply = *finishedWatcher;
                finishedWatcher->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                setBusy(false);
                if (reply.isError()) {
                    rollback();
                    setErrorMessage(reply.error().message());
                    reload();
                }
            });
}

void SettingsBackend::handleOwnerChange(const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty()) {
        ++m_generation;
        m_pendingLoads = 0;
        m_interface.reset();
        setAvailable(false);
        setBusy(false);
        setErrorMessage(tr("The settings service is unavailable."));
        return;
    }
    reload();
}
