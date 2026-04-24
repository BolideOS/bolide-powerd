#ifndef SENSORFWBACKEND_H
#define SENSORFWBACKEND_H

#include "sensorcontroller.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QMap>
#include <QTimer>
#include <QSharedPointer>

class SensorFwBackend : public SensorController {
    Q_OBJECT
    Q_DISABLE_COPY(SensorFwBackend)

public:
    explicit SensorFwBackend(QObject *parent = nullptr);
    ~SensorFwBackend() override;

    bool applyConfig(const SensorConfig &config) override;
    bool setSensorMode(const QString &sensorName, const QString &mode) override;
    SensorConfig currentConfig() const override;
    bool isAvailable(const QString &sensorName) const override;
    QStringList availableSensors() const override;
    int lastHeartRate() const override;
    qint64 lastStepCount() const override;

private slots:
    void onGpsTimerTick();

private:
    struct SensorSession {
        QSharedPointer<QDBusInterface> interface;
        QString sensorId;
        bool running;
        int currentInterval;
    };

    // Initialize SensorFW connection
    bool initializeSensorFw();
    
    // Sensor-specific control methods
    bool setAccelerometerMode(SensorMode mode);
    bool setGyroscopeMode(SensorMode mode);
    bool setHeartRateMode(SensorMode mode);
    bool setHrvMode(HrvMode mode);
    bool setSpo2Mode(Spo2Mode mode);
    bool setBarometerMode(BaroMode mode);
    bool setCompassMode(CompassMode mode);
    bool setAmbientLightMode(AmbientLightMode mode);
    bool setGpsMode(GpsMode mode);

    // SensorFW session management
    bool startSensor(const QString &sensorId, int intervalMs);
    bool stopSensor(const QString &sensorId);
    bool setSensorInterval(const QString &sensorId, int intervalMs);
    QSharedPointer<QDBusInterface> getSensorSession(const QString &sensorId);
    
    // GPS control (uses geoclue2)
    bool startGpsContinuous();
    bool startGpsPeriodic();
    bool stopGps();

    // Interval mapping helpers
    int getAccelGyroInterval(SensorMode mode) const;
    int getHeartRateInterval(SensorMode mode) const;
    int getBarometerInterval(BaroMode mode) const;
    int getAmbientLightInterval(AmbientLightMode mode) const;

    // Check if we're in sleep hours for HRV
    bool isNightTime() const;

    QSharedPointer<QDBusInterface> m_sensorManager;
    QMap<QString, SensorSession> m_activeSensors;
    SensorConfig m_currentConfig;
    
    // GPS state
    QSharedPointer<QDBusInterface> m_geoclueManager;
    QSharedPointer<QDBusInterface> m_geoclueClient;
    QTimer *m_gpsPeriodicTimer;
    bool m_gpsActiveInCycle;
    
    // Sensor name to SensorFW ID mapping
    QMap<QString, QString> m_sensorIdMap;
    
    // Available sensors cache
    QStringList m_availableSensors;
    bool m_sensorfwAvailable;
};

#endif // SENSORFWBACKEND_H
