#include "dbusinterface.h"
#include "profilemodel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDebug>

DBusInterface::DBusInterface(ProfileManager *pm, BatteryMonitor *bm)
    : QDBusAbstractAdaptor(pm)
    , m_profileManager(pm)
    , m_batteryMonitor(bm)
    , m_workoutActive(false)
{
    // Connect ProfileManager signals to D-Bus signals
    connect(m_profileManager, &ProfileManager::activeProfileChanged,
            this, &DBusInterface::onProfileManagerActiveProfileChanged);
    connect(m_profileManager, &ProfileManager::profilesChanged,
            this, &DBusInterface::onProfileManagerProfilesChanged);

    // Forward battery health changes
    if (m_batteryMonitor) {
        connect(m_batteryMonitor, &BatteryMonitor::healthChanged, this, [this]() {
            emit BatteryHealthChanged(
                m_batteryMonitor->healthPercent(),
                m_batteryMonitor->learnedCapacityMah(),
                m_batteryMonitor->designCapacityMah());
        });
    }
}

QString DBusInterface::GetProfiles()
{
    if (m_cachedProfilesJson.isEmpty()) {
        QJsonArray profilesArray;
        const QList<PowerProfile> profiles = m_profileManager->profiles();
        for (const PowerProfile &profile : profiles) {
            profilesArray.append(profile.toJson());
        }
        QJsonDocument doc(profilesArray);
        m_cachedProfilesJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    return m_cachedProfilesJson;
}

QString DBusInterface::GetActiveProfile()
{
    return m_profileManager->activeProfileId();
}

bool DBusInterface::SetActiveProfile(const QString &id)
{
    if (id.isEmpty()) {
        qWarning() << "SetActiveProfile: empty profile ID";
        return false;
    }
    
    bool success = m_profileManager->setActiveProfile(id);
    if (success) {
        m_profileManager->saveProfiles();
    }
    return success;
}

QString DBusInterface::GetProfile(const QString &id)
{
    if (id.isEmpty()) {
        qWarning() << "GetProfile: empty profile ID";
        return QString();
    }
    
    PowerProfile profile = m_profileManager->profile(id);
    if (!profile.isValid()) {
        qWarning() << "GetProfile: profile not found:" << id;
        return QString();
    }
    
    QJsonDocument doc(profile.toJson());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool DBusInterface::UpdateProfile(const QString &profileJson)
{
    if (profileJson.isEmpty()) {
        qWarning() << "UpdateProfile: empty JSON";
        return false;
    }
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "UpdateProfile: JSON parse error:" << parseError.errorString();
        return false;
    }
    
    if (!doc.isObject()) {
        qWarning() << "UpdateProfile: JSON is not an object";
        return false;
    }
    
    PowerProfile profile = PowerProfile::fromJson(doc.object());
    if (!profile.isValid()) {
        qWarning() << "UpdateProfile: invalid profile data";
        return false;
    }
    
    bool success = m_profileManager->updateProfile(profile);
    if (success) {
        m_profileManager->saveProfiles();
    }
    return success;
}

QString DBusInterface::AddProfile(const QString &profileJson)
{
    if (profileJson.isEmpty()) {
        qWarning() << "AddProfile: empty JSON";
        return QString();
    }
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "AddProfile: JSON parse error:" << parseError.errorString();
        return QString();
    }
    
    if (!doc.isObject()) {
        qWarning() << "AddProfile: JSON is not an object";
        return QString();
    }
    
    PowerProfile profile = PowerProfile::fromJson(doc.object());
    
    // ID will be auto-generated if empty
    bool success = m_profileManager->addProfile(profile);
    if (!success) {
        qWarning() << "AddProfile: failed to add profile";
        return QString();
    }
    
    m_profileManager->saveProfiles();
    
    // Find the newly added profile to get the (possibly auto-generated) ID
    QList<PowerProfile> allProfiles = m_profileManager->profiles();
    if (!allProfiles.isEmpty()) {
        return allProfiles.last().id;
    }
    return profile.id;
}

bool DBusInterface::DeleteProfile(const QString &id)
{
    if (id.isEmpty()) {
        qWarning() << "DeleteProfile: empty profile ID";
        return false;
    }
    
    bool success = m_profileManager->deleteProfile(id);
    if (success) {
        m_profileManager->saveProfiles();
    }
    return success;
}

bool DBusInterface::StartWorkout(const QString &workoutType)
{
    if (workoutType.isEmpty()) {
        qWarning() << "StartWorkout: empty workout type";
        return false;
    }
    
    if (m_workoutActive) {
        qWarning() << "StartWorkout: workout already active";
        return false;
    }
    
    // Look up the profile assigned to this workout type
    QString profileId = m_profileManager->workoutProfileId(workoutType);
    if (profileId.isEmpty()) {
        qWarning() << "StartWorkout: no profile assigned to workout type:" << workoutType;
        return false;
    }
    
    if (!m_profileManager->hasProfile(profileId)) {
        qWarning() << "StartWorkout: assigned profile does not exist:" << profileId;
        return false;
    }
    
    // Save the current profile so we can restore it later
    m_previousProfileId = m_profileManager->activeProfileId();
    m_activeWorkoutType = workoutType;
    
    // Switch to the workout profile
    if (!m_profileManager->setActiveProfile(profileId)) {
        qWarning() << "StartWorkout: failed to switch to profile:" << profileId;
        m_previousProfileId.clear();
        m_activeWorkoutType.clear();
        return false;
    }
    
    m_workoutActive = true;
    m_profileManager->saveProfiles();
    
    emit WorkoutStarted(workoutType, profileId);
    qInfo() << "Workout started:" << workoutType << "-> profile:" << profileId;
    
    return true;
}

bool DBusInterface::StopWorkout()
{
    if (!m_workoutActive) {
        qWarning() << "StopWorkout: no active workout";
        return false;
    }
    
    // Restore the previous profile
    if (!m_previousProfileId.isEmpty()) {
        if (!m_profileManager->setActiveProfile(m_previousProfileId)) {
            qWarning() << "StopWorkout: failed to restore profile:" << m_previousProfileId;
            // Continue anyway to clear workout state
        }
    }
    
    m_workoutActive = false;
    QString workoutType = m_activeWorkoutType;
    m_activeWorkoutType.clear();
    m_previousProfileId.clear();
    
    m_profileManager->saveProfiles();
    
    emit WorkoutStopped();
    qInfo() << "Workout stopped:" << workoutType;
    
    return true;
}

QString DBusInterface::GetWorkoutProfiles()
{
    QJsonObject workoutObj;
    const QMap<QString, QString> workoutProfiles = m_profileManager->workoutProfiles();
    
    for (auto it = workoutProfiles.constBegin(); it != workoutProfiles.constEnd(); ++it) {
        workoutObj[it.key()] = it.value();
    }
    
    QJsonDocument doc(workoutObj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool DBusInterface::SetWorkoutProfile(const QString &workoutType, const QString &profileId)
{
    if (workoutType.isEmpty()) {
        qWarning() << "SetWorkoutProfile: empty workout type";
        return false;
    }
    
    bool success = m_profileManager->setWorkoutProfile(workoutType, profileId);
    if (success) {
        m_profileManager->saveProfiles();
    }
    return success;
}

QString DBusInterface::GetBatteryHistory(int hours)
{
    if (!m_batteryMonitor)
        return QStringLiteral("[]");

    QVector<BatteryMonitor::BatteryEntry> entries = m_batteryMonitor->history(hours);
    QJsonArray arr;
    for (const auto &e : entries) {
        QJsonObject obj;
        obj[QLatin1String("timestamp")] = e.timestamp;
        obj[QLatin1String("level")] = e.level;
        obj[QLatin1String("charging")] = e.charging;
        obj[QLatin1String("profile")] = e.activeProfile;
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString DBusInterface::GetBatteryPrediction()
{
    if (!m_batteryMonitor) {
        QJsonObject obj;
        obj[QLatin1String("hours_remaining")] = -1;
        obj[QLatin1String("confidence")] = QLatin1String("low");
        QJsonDocument doc(obj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    QJsonDocument doc(m_batteryMonitor->prediction());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString DBusInterface::GetBatteryHealth()
{
    if (!m_batteryMonitor) {
        QJsonObject obj;
        obj[QLatin1String("health_percent")] = -1;
        obj[QLatin1String("confidence")] = QLatin1String("unavailable");
        QJsonDocument doc(obj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    QJsonDocument doc(m_batteryMonitor->healthInfo());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString DBusInterface::GetCurrentState()
{
    QJsonObject state;
    state[QLatin1String("active_profile")] = m_profileManager->activeProfileId();

    QJsonObject battery;
    if (m_batteryMonitor) {
        battery[QLatin1String("level")] = m_batteryMonitor->level();
        battery[QLatin1String("charging")] = m_batteryMonitor->charging();
        battery[QLatin1String("health_percent")] = m_batteryMonitor->healthPercent();
        battery[QLatin1String("learned_capacity_mah")] = m_batteryMonitor->learnedCapacityMah();
        battery[QLatin1String("design_capacity_mah")] = m_batteryMonitor->designCapacityMah();
        battery[QLatin1String("cycle_count")] = m_batteryMonitor->cycleCount();
        battery[QLatin1String("health_confidence")] = m_batteryMonitor->healthConfidence();
    }
    state[QLatin1String("battery")] = battery;

    state[QLatin1String("workout_active")] = m_workoutActive;
    state[QLatin1String("workout_type")] = m_activeWorkoutType;

    QJsonDocument doc(state);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

void DBusInterface::onProfileManagerActiveProfileChanged(const QString &id, const QString &name)
{
    emit ActiveProfileChanged(id, name);
}

void DBusInterface::onProfileManagerProfilesChanged()
{
    m_cachedProfilesJson.clear();  // Invalidate cache
    emit ProfilesChanged();
}

// ═══════════════════════════════════════════════════════════════════════════
// Sensor access coordination
// ═══════════════════════════════════════════════════════════════════════════

bool DBusInterface::RequestSensorAccess(const QString &sensorName, int minIntervalMs)
{
    if (sensorName.isEmpty() || minIntervalMs <= 0) {
        qWarning() << "RequestSensorAccess: invalid parameters:" << sensorName << minIntervalMs;
        return false;
    }

    SensorGrant grant;
    grant.minIntervalMs = minIntervalMs;
    m_sensorGrants[sensorName].append(grant);

    int eff = effectiveInterval(sensorName);
    applySensorMode(sensorName);

    qInfo() << "Sensor access granted:" << sensorName
            << "interval:" << minIntervalMs << "ms"
            << "effective:" << eff << "ms"
            << "grants:" << m_sensorGrants[sensorName].size();

    emit SensorAccessGranted(sensorName, eff);
    return true;
}

bool DBusInterface::ReleaseSensorAccess(const QString &sensorName)
{
    if (sensorName.isEmpty()) {
        qWarning() << "ReleaseSensorAccess: empty sensor name";
        return false;
    }

    auto it = m_sensorGrants.find(sensorName);
    if (it == m_sensorGrants.end() || it->isEmpty()) {
        qWarning() << "ReleaseSensorAccess: no active grants for:" << sensorName;
        return false;
    }

    // Remove one grant (LIFO – last request released first)
    it->removeLast();

    if (it->isEmpty()) {
        m_sensorGrants.erase(it);
        qInfo() << "Sensor fully released:" << sensorName;
    } else {
        int eff = effectiveInterval(sensorName);
        qInfo() << "Sensor grant released:" << sensorName
                << "remaining:" << it->size()
                << "effective:" << eff << "ms";
    }

    applySensorMode(sensorName);
    emit SensorAccessReleased(sensorName);
    return true;
}

QString DBusInterface::GetAvailableSensors()
{
    // Report sensors known to the platform.
    // In Phase 2 this will auto-discover from SensorFW and IIO.
    QJsonArray arr;
    static const QStringList knownSensors = {
        QStringLiteral("heart_rate"),
        QStringLiteral("accelerometer"),
        QStringLiteral("gyroscope"),
        QStringLiteral("barometer"),
        QStringLiteral("gps"),
        QStringLiteral("step_count"),
        QStringLiteral("ppg_raw"),
        QStringLiteral("spo2"),
        QStringLiteral("temperature"),
        QStringLiteral("compass"),
        QStringLiteral("ambient_light")
    };
    for (const QString &s : knownSensors) {
        QJsonObject obj;
        obj[QLatin1String("name")] = s;
        obj[QLatin1String("active_grants")] = m_sensorGrants.value(s).size();
        obj[QLatin1String("effective_interval_ms")] = effectiveInterval(s);
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString DBusInterface::GetActiveSensorGrants()
{
    QJsonObject obj;
    for (auto it = m_sensorGrants.constBegin(); it != m_sensorGrants.constEnd(); ++it) {
        QJsonObject sensorObj;
        sensorObj[QLatin1String("grant_count")] = it->size();
        sensorObj[QLatin1String("effective_interval_ms")] = effectiveInterval(it.key());
        QJsonArray intervals;
        for (const SensorGrant &g : *it)
            intervals.append(g.minIntervalMs);
        sensorObj[QLatin1String("intervals")] = intervals;
        obj[it.key()] = sensorObj;
    }
    QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

int DBusInterface::effectiveInterval(const QString &sensorName) const
{
    const auto grants = m_sensorGrants.value(sensorName);
    if (grants.isEmpty())
        return 0;

    // Effective interval = minimum of all requested intervals
    int minMs = grants.first().minIntervalMs;
    for (int i = 1; i < grants.size(); ++i) {
        if (grants[i].minIntervalMs < minMs)
            minMs = grants[i].minIntervalMs;
    }
    return minMs;
}

void DBusInterface::applySensorMode(const QString &sensorName)
{
    // TODO: Phase 2 – actually communicate with SensorFW / IIO to
    // set the sensor sampling rate or enable/disable it.
    // For now, we just track the grants and log the effective rate.
    int eff = effectiveInterval(sensorName);
    if (eff > 0) {
        qDebug() << "applySensorMode:" << sensorName
                 << "→ active at" << eff << "ms";
    } else {
        qDebug() << "applySensorMode:" << sensorName
                 << "→ no grants, profile-based mode";
    }
}
