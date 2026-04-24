#ifndef DBUSINTERFACE_H
#define DBUSINTERFACE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <QDBusAbstractAdaptor>
#include "profilemanager.h"
#include "batterymonitor.h"
#include "healthmonitor.h"
#include "healthstore.h"
#include "common.h"

class DBusInterface : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", POWERD_INTERFACE)

public:
    explicit DBusInterface(ProfileManager *pm, BatteryMonitor *bm,
                           HealthMonitor *hm = nullptr, HealthStore *hs = nullptr);

public Q_SLOTS:
    // Profile management
    QString GetProfiles();
    QString GetActiveProfile();
    bool SetActiveProfile(const QString &id);
    QString GetProfile(const QString &id);
    bool UpdateProfile(const QString &profileJson);
    QString AddProfile(const QString &profileJson);
    bool DeleteProfile(const QString &id);

    // Workout
    bool StartWorkout(const QString &workoutType);
    bool StopWorkout();
    QString GetWorkoutProfiles();
    bool SetWorkoutProfile(const QString &workoutType, const QString &profileId);

    // Battery telemetry
    QString GetBatteryHistory(int hours);
    QString GetBatteryPrediction();
    QString GetBatteryHealth();
    QString GetCurrentState();

    // Charge limit (battery protection)
    bool GetChargeLimitEnabled();
    int GetChargeLimitPercent();
    bool SetChargeLimitEnabled(bool enabled);
    bool SetChargeLimitPercent(int percent);

    // ── Health monitoring ─────────────────────────────────────────────
    QString GetHealthSettings();
    bool SetHealthSettings(const QString &settingsJson);
    QString GetHealthData(const QString &metric, qint64 fromTs, qint64 toTs);
    QString GetHealthDataAggregated(const QString &metric, qint64 fromTs,
                                    qint64 toTs, int bucketMinutes);
    QString GetHealthLatest(const QString &metric, int count);

    // ── Sensor access coordination ─────────────────────────────────────
    // Called by fitness/health apps to ensure a sensor stays active at the
    // requested minimum sampling interval.  Powerd will not down-clock
    // or disable the sensor while at least one access grant is active.
    bool RequestSensorAccess(const QString &sensorName, int minIntervalMs);
    bool ReleaseSensorAccess(const QString &sensorName);
    QString GetAvailableSensors();
    QString GetActiveSensorGrants();

Q_SIGNALS:
    void ActiveProfileChanged(const QString &id, const QString &name);
    void ProfilesChanged();
    void WorkoutStarted(const QString &workoutType, const QString &profileId);
    void WorkoutStopped();
    void BatteryLevelChanged(int level, bool charging);
    void BatteryHealthChanged(int healthPercent, int learnedMah, int designMah);
    void ChargeLimitChanged(bool enabled, int percent);
    void SensorAccessGranted(const QString &sensorName, int effectiveIntervalMs);
    void SensorAccessReleased(const QString &sensorName);
    void HealthSettingsChanged(const QString &settingsJson);
    void HealthDataUpdated(const QString &metric);

private Q_SLOTS:
    void onProfileManagerActiveProfileChanged(const QString &id, const QString &name);
    void onProfileManagerProfilesChanged();

private:
    ProfileManager *m_profileManager;
    BatteryMonitor *m_batteryMonitor;
    HealthMonitor *m_healthMonitor;
    HealthStore *m_healthStore;
    QString m_previousProfileId;
    QString m_activeWorkoutType;
    bool m_workoutActive;
    QString m_cachedProfilesJson;  // Invalidated on ProfilesChanged

    // ── Sensor access tracking ─────────────────────────────────────────
    struct SensorGrant {
        int minIntervalMs;
    };
    // sensor name → list of grants (one per RequestSensorAccess call)
    QMap<QString, QVector<SensorGrant>> m_sensorGrants;
    int effectiveInterval(const QString &sensorName) const;
    void applySensorMode(const QString &sensorName);
};

#endif // DBUSINTERFACE_H
