#ifndef DBUSINTERFACE_H
#define DBUSINTERFACE_H

#include <QObject>
#include <QString>
#include <QDBusAbstractAdaptor>
#include "profilemanager.h"
#include "batterymonitor.h"
#include "common.h"

class DBusInterface : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", POWERD_INTERFACE)

public:
    explicit DBusInterface(ProfileManager *pm, BatteryMonitor *bm);

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

Q_SIGNALS:
    void ActiveProfileChanged(const QString &id, const QString &name);
    void ProfilesChanged();
    void WorkoutStarted(const QString &workoutType, const QString &profileId);
    void WorkoutStopped();
    void BatteryLevelChanged(int level, bool charging);
    void BatteryHealthChanged(int healthPercent, int learnedMah, int designMah);

private Q_SLOTS:
    void onProfileManagerActiveProfileChanged(const QString &id, const QString &name);
    void onProfileManagerProfilesChanged();

private:
    ProfileManager *m_profileManager;
    BatteryMonitor *m_batteryMonitor;
    QString m_previousProfileId;
    QString m_activeWorkoutType;
    bool m_workoutActive;
    QString m_cachedProfilesJson;  // Invalidated on ProfilesChanged
};

#endif // DBUSINTERFACE_H
