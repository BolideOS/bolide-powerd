#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QJsonObject>
#include "healthstore.h"
#include "sensorcontroller.h"
#include "profilemodel.h"

// ─── Health settings (user intent, persisted independently of profiles) ─────

struct HealthMetricConfig {
    bool enabled = false;
    // Minimum acceptable sensor mode — profiles can go higher but not lower
    // when health tracking is enabled for this metric.
    // HR: Low=30min, Medium=5min, High=1min
    SensorMode minimumMode = SensorMode::Low;
};

struct HealthSettings {
    bool enabled = false;                       // master switch
    HealthMetricConfig heartRate;               // background HR monitoring
    HealthMetricConfig steps;                   // step counting (accel-based)
    HealthMetricConfig hrv;                     // HRV analysis
    HealthMetricConfig spo2;                    // SpO2 periodic
    int retentionDays = 30;                     // how long to keep data

    QJsonObject toJson() const;
    static HealthSettings fromJson(const QJsonObject &obj);

    // Given a profile's SensorConfig, return the effective config with
    // health minimums enforced. Does NOT modify the profile — returns a copy.
    SensorConfig effectiveSensorConfig(const SensorConfig &profileConfig) const;
};

// ─── HealthMonitor — burst sampling engine ─────────────────────────────────

class HealthMonitor : public QObject
{
    Q_OBJECT

public:
    explicit HealthMonitor(HealthStore *store, SensorController *sensors,
                           const QString &configDir, QObject *parent = nullptr);

    void start();
    void stop();

    // Settings
    HealthSettings settings() const { return m_settings; }
    void setSettings(const HealthSettings &settings);

    // Called when active profile changes — recalculate effective sensor config
    void onProfileChanged(const SensorConfig &profileSensorConfig);

    // Workout integration — during workout, health monitor defers to
    // the workout profile's sensor config (higher priority)
    void setWorkoutActive(bool active);

signals:
    // Emitted when health settings change effective sensor requirements.
    // main.cpp connects this to re-apply sensor config.
    void effectiveSensorConfigChanged(const SensorConfig &effective);

    // Emitted when new health data is available (for UI refresh)
    void healthDataUpdated(const QString &metric);

public slots:
    // Called when display state changes (from MCE signal)
    void onDisplayStateChanged(const QString &state);

private slots:
    void onSampleTimer();
    void onFlushTimer();

private:
    void loadSettings();
    void saveSettings();

    void scheduleSampling();
    void stopSampling();

    // Collect a burst of sensor readings and buffer them
    void collectHeartRateSample();
    void collectStepSample();

    void flushBuffer();
    void ensureFlushTimer();

    HealthStore *m_store;
    SensorController *m_sensors;
    QString m_configDir;

    HealthSettings m_settings;
    SensorConfig m_profileSensorConfig;  // what the profile wants

    // Sampling state
    QTimer *m_sampleTimer;      // fires at the sampling interval
    QTimer *m_flushTimer;       // fires to batch-write buffered samples to DB
    QVector<HealthStore::Sample> m_sampleBuffer;

    bool m_screenOn;
    bool m_workoutActive;
    bool m_running;

    // Step tracking
    qint64 m_lastStepCount;
    qint64 m_lastStepTs;
};

#endif // HEALTHMONITOR_H
