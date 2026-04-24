#ifndef RADIOCONTROLLER_H
#define RADIOCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QDBusInterface>
#include <QTime>
#include "profilemodel.h"

class RadioController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(RadioController)

public:
    explicit RadioController(QObject *parent = nullptr);
    ~RadioController() override;

    bool applyConfig(const RadioConfig &config);
    RadioConfig currentConfig() const;

    // Individual radio control
    bool setBleState(bool enabled);
    bool setWifiState(bool enabled);
    bool setLteState(const QString &state);
    bool setNfcState(bool enabled);

    // Sync control
    void triggerSync();
    bool isBleAvailable() const;
    bool isWifiAvailable() const;
    
    // Sleep mode (set by automation engine)
    void setSleepMode(bool sleeping);

signals:
    void radioError(const QString &radio, const QString &error);
    void syncTriggered();

private slots:
    void onSyncTimer();

private:
    void setupSyncSchedule(const RadioEntry &bleConfig, const RadioEntry &wifiConfig);
    void stopSyncSchedule();
    QTime calculateNextWindowStart(const QString &startTime);
    int calculateIntervalMs(int hours);

    QDBusInterface *m_bluezAdapter;
    QDBusInterface *m_bluezProperties;
    QDBusInterface *m_connmanManager;
    QDBusInterface *m_connmanTechnology;      // WiFi
    QDBusInterface *m_connmanBtTechnology;    // Bluetooth
    QDBusInterface *m_neardManager;
    
    QTimer *m_syncTimer;
    RadioConfig m_currentConfig;
    bool m_sleepMode;
    
    // State tracking
    bool m_bluezAvailable;
    bool m_connmanAvailable;
    bool m_neardAvailable;
};

#endif // RADIOCONTROLLER_H
