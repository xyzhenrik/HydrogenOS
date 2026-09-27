// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QVariant>

#include <functional>
#include <memory>

class QDBusInterface;

class SettingsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(uint schemaVersion READ schemaVersion NOTIFY schemaVersionChanged)
    Q_PROPERTY(QString materialQuality READ materialQuality WRITE setMaterialQuality NOTIFY
                   materialQualityChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(bool reduceTransparency READ reduceTransparency WRITE setReduceTransparency NOTIFY
                   reduceTransparencyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

  public:
    explicit SettingsBackend(QObject *parent = nullptr);
    SettingsBackend(const QDBusConnection &connection, QString serviceName,
                    QObject *parent = nullptr);
    ~SettingsBackend() override;

    bool available() const;
    bool busy() const;
    uint schemaVersion() const;
    QString materialQuality() const;
    bool reduceMotion() const;
    bool reduceTransparency() const;
    QString errorMessage() const;

  public slots:
    void setMaterialQuality(const QString &quality);
    void setReduceMotion(bool enabled);
    void setReduceTransparency(bool enabled);
    void reload();

  signals:
    void availableChanged();
    void busyChanged();
    void schemaVersionChanged();
    void materialQualityChanged();
    void reduceMotionChanged();
    void reduceTransparencyChanged();
    void errorMessageChanged();

  private:
    void setAvailable(bool available);
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void finishLoad(quint64 generation, const QString &errorMessage = {});
    void sendWrite(const QString &method, const QVariant &argument,
                   const std::function<void()> &rollback);
    void handleOwnerChange(const QString &oldOwner, const QString &newOwner);

    QDBusConnection m_connection;
    QString m_serviceName;
    QDBusServiceWatcher m_serviceWatcher;
    std::unique_ptr<QDBusInterface> m_interface;
    bool m_available = false;
    bool m_busy = false;
    uint m_schemaVersion = 0;
    QString m_materialQuality = QStringLiteral("full");
    bool m_reduceMotion = false;
    bool m_reduceTransparency = false;
    QString m_errorMessage;
    quint64 m_generation = 0;
    int m_pendingLoads = 0;
    bool m_loadFailed = false;
};
