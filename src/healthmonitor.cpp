#include "healthmonitor.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

// ─── HealthSettings serialization ───────────────────────────────────────────

static QJsonObject metricConfigToJson(const HealthMetricConfig &c)
{
    QJsonObject obj;
    obj[QLatin1String("enabled")] = c.enabled;
    obj[QLatin1String("minimum_mode")] = sensorModeToString(c.minimumMode);
    return obj;
}

static HealthMetricConfig metricConfigFromJson(const QJsonObject &obj)
{
    HealthMetricConfig c;
    c.enabled = obj.value(QLatin1String("enabled")).toBool(false);
    QString modeStr = obj.value(QLatin1String("minimum_mode")).toString(QStringLiteral("low"));
    c.minimumMode = sensorModeFromString(modeStr);
    if (c.minimumMode == SensorMode::Off)
        c.minimumMode = SensorMode::Low;  // minimum_mode should never be Off
    return c;
}

QJsonObject HealthSettings::toJson() const
{
    QJsonObject obj;
    obj[QLatin1String("enabled")] = enabled;
    obj[QLatin1String("heart_rate")] = metricConfigToJson(heartRate);
    obj[QLatin1String("steps")] = metricConfigToJson(steps);
    obj[QLatin1String("hrv")] = metricConfigToJson(hrv);
    obj[QLatin1String("spo2")] = metricConfigToJson(spo2);
    obj[QLatin1String("retention_days")] = retentionDays;
    return obj;
}

HealthSettings HealthSettings::fromJson(const QJsonObject &obj)
{
    HealthSettings s;
    s.enabled = obj.value(QLatin1String("enabled")).toBool(false);
    s.heartRate = metricConfigFromJson(obj.value(QLatin1String("heart_rate")).toObject());
    s.steps = metricConfigFromJson(obj.value(QLatin1String("steps")).toObject());
    s.hrv = metricConfigFromJson(obj.value(QLatin1String("hrv")).toObject());
    s.spo2 = metricConfigFromJson(obj.value(QLatin1String("spo2")).toObject());
    s.retentionDays = obj.value(QLatin1String("retention_days")).toInt(30);
    return s;
}

// Helper: return the higher of two sensor modes
static SensorMode maxSensorMode(SensorMode a, SensorMode b)
{
    return static_cast<SensorMode>(qMax(static_cast<int>(a), static_cast<int>(b)));
}

SensorConfig HealthSettings::effectiveSensorConfig(const SensorConfig &profileConfig) const
{
    SensorConfig eff = profileConfig;

    if (!enabled) return eff;

    // Heart rate: enforce minimum mode
    if (heartRate.enabled && eff.heart_rate < heartRate.minimumMode) {
        eff.heart_rate = heartRate.minimumMode;
    }

    // Steps: need accelerometer at least Low
    if (steps.enabled && eff.accelerometer == SensorMode::Off) {
        eff.accelerometer = SensorMode::Low;
    }

    // HRV: needs heart_rate at Medium minimum (5-min intervals for ultra-short HRV)
    if (hrv.enabled) {
        SensorMode minHr = maxSensorMode(SensorMode::Medium, heartRate.minimumMode);
        if (eff.heart_rate < minHr) {
            eff.heart_rate = minHr;
        }
        if (eff.hrv == HrvMode::Off) {
            eff.hrv = HrvMode::Always;
        }
    }

    // SpO2: hardware not present on beluga — always unavailable
    // spo2 setting is ignored regardless of user config

    return eff;
}

// ─── HealthMonitor implementation ───────────────────────────────────────────

// Flush buffer to DB every 5 minutes (or on screen wake)
static constexpr int FLUSH_INTERVAL_MS = 5 * 60 * 1000;

HealthMonitor::HealthMonitor(HealthStore *store, SensorController *sensors,
                             const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_sensors(sensors)
    , m_configDir(configDir)
    , m_sampleTimer(new QTimer(this))
    , m_flushTimer(new QTimer(this))
    , m_screenOn(true)
    , m_workoutActive(false)
    , m_running(false)
    , m_lastStepCount(-1)
    , m_lastStepTs(0)
{
    m_sampleTimer->setTimerType(Qt::CoarseTimer);
    m_flushTimer->setTimerType(Qt::CoarseTimer);

    connect(m_sampleTimer, &QTimer::timeout, this, &HealthMonitor::onSampleTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &HealthMonitor::onFlushTimer);

    loadSettings();
}

void HealthMonitor::start()
{
    if (m_running) return;
    m_running = true;

    if (!m_settings.enabled) {
        qInfo() << "HealthMonitor: Health tracking is disabled";
        return;
    }

    qInfo() << "HealthMonitor: Starting health monitoring";
    qInfo() << "  HR:" << m_settings.heartRate.enabled
            << "Steps:" << m_settings.steps.enabled
            << "HRV:" << m_settings.hrv.enabled
            << "SpO2:" << m_settings.spo2.enabled;

    // Open database
    if (!m_store->isOpen()) {
        m_store->open();
    }

    // Prune old data
    m_store->pruneOlderThan(m_settings.retentionDays);

    // Flush timer is NOT started here — it starts on-demand when the
    // sample buffer first becomes non-empty (see ensureFlushTimer()).
    // This avoids a 5-minute wakeup cycle when there is nothing to flush.

    // Start sampling based on effective config
    scheduleSampling();
}

void HealthMonitor::stop()
{
    if (!m_running) return;
    m_running = false;

    qInfo() << "HealthMonitor: Stopping";

    stopSampling();
    m_flushTimer->stop();

    // Flush any remaining buffered samples
    flushBuffer();
}

void HealthMonitor::setSettings(const HealthSettings &settings)
{
    bool wasEnabled = m_settings.enabled;
    m_settings = settings;
    saveSettings();

    qInfo() << "HealthMonitor: Settings updated"
            << "enabled:" << settings.enabled
            << "HR:" << settings.heartRate.enabled
            << "steps:" << settings.steps.enabled;

    if (m_running) {
        if (!wasEnabled && settings.enabled) {
            // Just turned on
            if (!m_store->isOpen()) m_store->open();
            m_store->pruneOlderThan(m_settings.retentionDays);
            // Flush timer will start on-demand when buffer fills
            scheduleSampling();
        } else if (wasEnabled && !settings.enabled) {
            // Just turned off
            stopSampling();
            m_flushTimer->stop();
            flushBuffer();
        } else if (settings.enabled) {
            // Settings changed while enabled — reschedule
            scheduleSampling();
        }
    }

    // Notify that effective sensor config may have changed
    emit effectiveSensorConfigChanged(
        m_settings.effectiveSensorConfig(m_profileSensorConfig));
}

void HealthMonitor::onDisplayStateChanged(const QString &state)
{
    bool wasOn = m_screenOn;
    m_screenOn = (state == QLatin1String("on"));

    if (!m_settings.enabled || !m_running) return;

    if (m_screenOn && !wasOn) {
        // Screen woke up — flush buffered data so UI has fresh readings
        flushBuffer();
    }
    // Sampling continues regardless of screen state — we want background HR.
    // The profile's sensor mode controls the interval (Low=30min, Medium=5min).
}

void HealthMonitor::onProfileChanged(const SensorConfig &profileSensorConfig)
{
    m_profileSensorConfig = profileSensorConfig;

    if (m_settings.enabled) {
        SensorConfig effective = m_settings.effectiveSensorConfig(profileSensorConfig);
        emit effectiveSensorConfigChanged(effective);
    }
}

void HealthMonitor::setWorkoutActive(bool active)
{
    if (m_workoutActive == active) return;
    m_workoutActive = active;

    if (!m_settings.enabled || !m_running) return;

    if (active) {
        // During workout, the workout profile handles sensor config.
        // We keep collecting samples but don't override sensor modes.
        qDebug() << "HealthMonitor: Workout active — deferring sensor control";
    } else {
        // Workout ended — re-apply health effective config
        qDebug() << "HealthMonitor: Workout ended — re-applying health config";
        emit effectiveSensorConfigChanged(
            m_settings.effectiveSensorConfig(m_profileSensorConfig));
    }
}

// ─── Sampling ───────────────────────────────────────────────────────────────

void HealthMonitor::scheduleSampling()
{
    if (!m_settings.enabled) return;

    // Determine sample interval from effective HR mode.
    // The sample timer fires to record the current sensor reading.
    // The actual sensor hardware interval is set by the sensor controller
    // based on the effective sensor config.
    SensorConfig eff = m_settings.effectiveSensorConfig(m_profileSensorConfig);

    int intervalMs = 0;
    if (m_settings.heartRate.enabled || m_settings.hrv.enabled) {
        switch (eff.heart_rate) {
        case SensorMode::Low:     intervalMs = 30 * 60 * 1000; break;  // 30 min
        case SensorMode::Medium:  intervalMs = 5 * 60 * 1000;  break;  // 5 min
        case SensorMode::High:    intervalMs = 60 * 1000;       break;  // 1 min
        case SensorMode::Workout: intervalMs = 1000;            break;  // 1s
        default:                  intervalMs = 10 * 60 * 1000;  break;  // fallback 10 min
        }
    }

    // Steps: check every 5 minutes (hardware step counter is always-on)
    if (m_settings.steps.enabled && (intervalMs == 0 || intervalMs > 5 * 60 * 1000)) {
        intervalMs = 5 * 60 * 1000;
    }

    if (intervalMs > 0) {
        m_sampleTimer->setInterval(intervalMs);
        m_sampleTimer->start();
        qInfo() << "HealthMonitor: Sampling every" << (intervalMs / 1000) << "seconds";
    } else {
        m_sampleTimer->stop();
    }
}

void HealthMonitor::stopSampling()
{
    m_sampleTimer->stop();
}

void HealthMonitor::onSampleTimer()
{
    if (m_settings.heartRate.enabled || m_settings.hrv.enabled) {
        collectHeartRateSample();
    }

    if (m_settings.steps.enabled) {
        collectStepSample();
    }
}

void HealthMonitor::collectHeartRateSample()
{
    int hr = m_sensors->lastHeartRate();

    if (hr <= 0 || hr > 300) return; // invalid reading

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    HealthStore::Sample s;
    s.ts = now;
    s.metric = QStringLiteral("hr");
    s.value = hr;
    s.quality = 0;

    m_sampleBuffer.append(s);
    ensureFlushTimer();

    emit healthDataUpdated(QStringLiteral("hr"));
}

void HealthMonitor::collectStepSample()
{
    qint64 steps = m_sensors->lastStepCount();

    if (steps < 0) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_lastStepCount >= 0 && steps >= m_lastStepCount) {
        qint64 delta = steps - m_lastStepCount;
        if (delta > 0) {
            HealthStore::Sample s;
            s.ts = now;
            s.metric = QStringLiteral("steps");
            s.value = delta;  // steps since last read
            s.quality = 0;
            m_sampleBuffer.append(s);
            ensureFlushTimer();

            emit healthDataUpdated(QStringLiteral("steps"));
        }
    }

    m_lastStepCount = steps;
    m_lastStepTs = now;
}

// ─── Flush ──────────────────────────────────────────────────────────────────

void HealthMonitor::onFlushTimer()
{
    flushBuffer();

    // Periodic maintenance: prune old data once a day
    // (approximated by checking every flush whether we've crossed midnight)
    static qint64 lastPruneDay = 0;
    qint64 today = QDateTime::currentMSecsSinceEpoch() / (24LL * 3600 * 1000);
    if (today != lastPruneDay) {
        lastPruneDay = today;
        m_store->pruneOlderThan(m_settings.retentionDays);
    }
}

void HealthMonitor::flushBuffer()
{
    if (m_sampleBuffer.isEmpty()) {
        // Nothing to write — stop the flush timer so it doesn't wake CPU
        m_flushTimer->stop();
        return;
    }

    if (!m_store->isOpen()) {
        m_store->open();
    }

    m_store->writeSamples(m_sampleBuffer);
    m_sampleBuffer.clear();

    // Buffer is now empty — stop flush timer until new samples arrive
    m_flushTimer->stop();

    // Close DB to release file handles and allow full CPU suspend
    // DB will be re-opened on next flush or query
    m_store->close();
}

void HealthMonitor::ensureFlushTimer()
{
    if (!m_flushTimer->isActive() && m_running && m_settings.enabled) {
        m_flushTimer->start(FLUSH_INTERVAL_MS);
    }
}

// ─── Settings persistence ───────────────────────────────────────────────────

void HealthMonitor::loadSettings()
{
    QString path = m_configDir + QLatin1String("/health_settings.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "HealthMonitor: No settings file, using defaults";
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "HealthMonitor: Settings parse error:" << err.errorString();
        return;
    }

    m_settings = HealthSettings::fromJson(doc.object());
    qInfo() << "HealthMonitor: Loaded settings — enabled:" << m_settings.enabled;
}

void HealthMonitor::saveSettings()
{
    QString path = m_configDir + QLatin1String("/health_settings.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "HealthMonitor: Failed to save settings to" << path;
        return;
    }

    QJsonDocument doc(m_settings.toJson());
    f.write(doc.toJson(QJsonDocument::Compact));
    qDebug() << "HealthMonitor: Saved settings";
}
