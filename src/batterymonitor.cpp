#include "batterymonitor.h"
#include "common.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <cmath>

BatteryMonitor::BatteryMonitor(const QString &configDir, QObject *parent)
    : QObject(parent)
    , m_configDir(configDir)
    , m_level(100)
    , m_charging(false)
    , m_lastRecordedLevel(100)
    , m_workoutActive(false)
    , m_historyDays(DEFAULT_BATTERY_HISTORY_DAYS)
    , m_pollTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_designCapacityUah(0)
    , m_learnedCapacityUah(0)
    , m_voltageNowUv(0)
    , m_currentNowUa(0)
    , m_temperatureDeci(0)
    , m_cycleCount(0)
    , m_emaCapacityUah(0.0)
    , m_emaSampleCount(0)
    , m_coulombAccUah(0.0)
    , m_coulombStartSoc(-1)
    , m_coulombLastMs(0)
    , m_coulombEstimateCount(0)
    , m_chargeFullAvailable(false)
{
    m_powerSupplyPath = findPowerSupplyPath();
    
    if (m_powerSupplyPath.isEmpty()) {
        qWarning() << "BatteryMonitor: No battery power supply found, using defaults";
    }

    connect(m_pollTimer, &QTimer::timeout, this, &BatteryMonitor::pollBattery);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &BatteryMonitor::heartbeat);

    loadHistory();
    loadHealthData();
}

void BatteryMonitor::start()
{
    readBatteryLevel();
    readBatteryHealth();
    m_lastRecordedLevel = m_level;
    recordEntry();

    // Use adaptive poll interval: slower during idle, faster during workouts
    m_pollTimer->start(m_workoutActive ? BATTERY_POLL_ACTIVE_MS : BATTERY_POLL_IDLE_MS);
    m_heartbeatTimer->start(BATTERY_HEARTBEAT_MINUTES * 60 * 1000);
}

void BatteryMonitor::stop()
{
    m_pollTimer->stop();
    m_heartbeatTimer->stop();
    saveHistory();
    saveHealthData();
}

int BatteryMonitor::level() const
{
    return m_level;
}

bool BatteryMonitor::charging() const
{
    return m_charging;
}

int BatteryMonitor::healthPercent() const
{
    if (m_designCapacityUah <= 0)
        return -1; // unavailable

    int learned = m_emaSampleCount > 0
        ? qRound(m_emaCapacityUah)
        : m_learnedCapacityUah;

    if (learned <= 0)
        return -1;

    int pct = qRound(learned * 100.0 / m_designCapacityUah);
    return qBound(0, pct, 150); // cap at 150% to catch gauge calibration quirks
}

int BatteryMonitor::learnedCapacityMah() const
{
    int learned = m_emaSampleCount > 0
        ? qRound(m_emaCapacityUah)
        : m_learnedCapacityUah;

    return learned > 0 ? qRound(learned / 1000.0) : -1;
}

int BatteryMonitor::designCapacityMah() const
{
    return m_designCapacityUah > 0 ? qRound(m_designCapacityUah / 1000.0) : -1;
}

int BatteryMonitor::cycleCount() const
{
    return m_cycleCount;
}

int BatteryMonitor::voltageNowMv() const
{
    return m_voltageNowUv > 0 ? qRound(m_voltageNowUv / 1000.0) : -1;
}

int BatteryMonitor::currentNowMa() const
{
    return m_currentNowUa != 0 ? qRound(m_currentNowUa / 1000.0) : 0;
}

int BatteryMonitor::temperatureDeci() const
{
    return m_temperatureDeci;
}

QString BatteryMonitor::healthConfidence() const
{
    if (m_emaSampleCount >= 20)
        return QStringLiteral("high");
    if (m_emaSampleCount >= 5)
        return QStringLiteral("medium");
    if (m_emaSampleCount >= 1 || m_learnedCapacityUah > 0)
        return QStringLiteral("low");
    return QStringLiteral("unavailable");
}

QJsonObject BatteryMonitor::healthInfo() const
{
    QJsonObject obj;
    obj[QLatin1String("health_percent")] = healthPercent();
    obj[QLatin1String("learned_capacity_mah")] = learnedCapacityMah();
    obj[QLatin1String("design_capacity_mah")] = designCapacityMah();
    obj[QLatin1String("cycle_count")] = m_cycleCount;
    obj[QLatin1String("voltage_mv")] = voltageNowMv();
    obj[QLatin1String("current_ma")] = currentNowMa();
    obj[QLatin1String("temperature_deci")] = m_temperatureDeci;
    obj[QLatin1String("confidence")] = healthConfidence();
    obj[QLatin1String("sample_count")] = m_emaSampleCount;

    // Include raw history for trend analysis on the UI side
    QJsonArray samplesArr;
    int count = qMin(m_healthTimestamps.size(), m_healthSamples.size());
    for (int i = 0; i < count; ++i) {
        QJsonObject s;
        s[QLatin1String("t")] = m_healthTimestamps[i];
        s[QLatin1String("mah")] = qRound(m_healthSamples[i] / 1000.0);
        samplesArr.append(s);
    }
    obj[QLatin1String("history")] = samplesArr;

    return obj;
}

QVector<BatteryMonitor::BatteryEntry> BatteryMonitor::history(int hours) const
{
    qint64 cutoffTime = QDateTime::currentSecsSinceEpoch() - (hours * 3600);
    QVector<BatteryEntry> result;

    for (const BatteryEntry &entry : m_history) {
        if (entry.timestamp >= cutoffTime) {
            result.append(entry);
        }
    }

    return result;
}

QJsonObject BatteryMonitor::prediction() const
{
    QJsonObject result;
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7200; // last 2 hours

    if (m_charging) {
        int chargeGain = 0;
        qint64 timeSpan = 0;
        int chargingSamples = 0;

        // Iterate m_history directly — avoid copying the vector
        for (int i = m_history.size() - 1; i > 0; --i) {
            if (m_history[i].timestamp < cutoff)
                break;
            if (m_history[i - 1].timestamp < cutoff)
                continue; // pair spans the cutoff boundary, skip it
            if (m_history[i].charging && m_history[i-1].charging) {
                chargeGain += m_history[i].level - m_history[i-1].level;
                timeSpan += m_history[i].timestamp - m_history[i-1].timestamp;
                chargingSamples++;
            }
        }

        if (chargingSamples > 0 && timeSpan > 0 && chargeGain > 0) {
            double chargeRatePerHour = (chargeGain * 3600.0) / timeSpan;
            int remaining = 100 - m_level;
            double hoursToFull = remaining / chargeRatePerHour;
            result["charging"] = true;
            result["hours_to_full"] = qRound(hoursToFull * 10.0) / 10.0;
        } else {
            result["charging"] = true;
            result["hours_to_full"] = -1;
        }

        return result;
    }

    int drainTotal = 0;
    qint64 timeSpan = 0;
    int drainingSamples = 0;

    for (int i = m_history.size() - 1; i > 0; --i) {
        if (m_history[i].timestamp < cutoff)
            break;
        if (m_history[i - 1].timestamp < cutoff)
            continue; // pair spans the cutoff boundary, skip it
        if (!m_history[i].charging && !m_history[i-1].charging) {
            int levelDrop = m_history[i-1].level - m_history[i].level;
            if (levelDrop > 0) {
                drainTotal += levelDrop;
                timeSpan += m_history[i].timestamp - m_history[i-1].timestamp;
                drainingSamples++;
            }
        }
    }

    if (drainingSamples == 0 || timeSpan == 0) {
        result["hours_remaining"] = -1;
        result["drain_rate_per_hour"] = 0.0;
        result["confidence"] = "low";
        return result;
    }

    double drainRatePerHour = (drainTotal * 3600.0) / timeSpan;
    
    if (drainRatePerHour <= 0) {
        result["hours_remaining"] = -1;
        result["drain_rate_per_hour"] = 0.0;
        result["confidence"] = "low";
        return result;
    }

    double hoursRemaining = m_level / drainRatePerHour;

    double dataHours = timeSpan / 3600.0;
    QString confidence;
    if (dataHours < 1.0) {
        confidence = "low";
    } else if (dataHours < 4.0) {
        confidence = "medium";
    } else {
        confidence = "high";
    }

    result["hours_remaining"] = qRound(hoursRemaining * 10.0) / 10.0;
    result["drain_rate_per_hour"] = qRound(drainRatePerHour * 10.0) / 10.0;
    result["confidence"] = confidence;

    return result;
}

int BatteryMonitor::historyDays() const
{
    return m_historyDays;
}

void BatteryMonitor::setHistoryDays(int days)
{
    if (days > 0 && days != m_historyDays) {
        m_historyDays = days;
        trimHistory();
        saveHistory();
    }
}

void BatteryMonitor::setActiveProfile(const QString &profileId)
{
    m_activeProfile = profileId;
}

void BatteryMonitor::setWorkoutActive(bool active)
{
    if (m_workoutActive == active)
        return;

    m_workoutActive = active;

    // Adjust poll frequency: faster during workouts, slower during idle
    if (m_pollTimer->isActive()) {
        m_pollTimer->setInterval(active ? BATTERY_POLL_ACTIVE_MS : BATTERY_POLL_IDLE_MS);
    }
}

void BatteryMonitor::pollBattery()
{
    int prevLevel = m_level;
    bool prevCharging = m_charging;

    readBatteryLevel();

    // Software coulomb counting: accumulate current during discharge
    // when hardware charge_full is not available
    if (!m_chargeFullAvailable && !m_charging) {
        coulombAccumulate();
    }

    // Reset accumulator when charging starts (a new discharge cycle)
    if (m_charging && !prevCharging) {
        coulombReset();
    }

    if (m_level != prevLevel) {
        emit levelChanged(m_level);
    }

    if (m_charging != prevCharging) {
        emit chargingChanged(m_charging);
    }

    if (qAbs(m_level - m_lastRecordedLevel) >= BATTERY_CHANGE_THRESHOLD) {
        recordEntry();
        m_lastRecordedLevel = m_level;
    }

    if (m_level != prevLevel || m_charging != prevCharging) {
        emit significantChange(m_level, m_charging);
    }
}

void BatteryMonitor::heartbeat()
{
    readBatteryHealth();
    recordEntry();
    m_lastRecordedLevel = m_level;
    saveHistory();
    saveHealthData();
}

void BatteryMonitor::readBatteryLevel()
{
    if (m_powerSupplyPath.isEmpty()) {
        m_level = 100;
        m_charging = false;
        return;
    }

    QFile capacityFile(m_powerSupplyPath + "/capacity");
    if (capacityFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromLatin1(capacityFile.readAll().trimmed());
        bool ok = false;
        int level = content.toInt(&ok);
        if (ok && level >= 0 && level <= 100) {
            m_level = level;
        }
        capacityFile.close();
    }

    QFile statusFile(m_powerSupplyPath + "/status");
    if (statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString status = QString::fromLatin1(statusFile.readAll().trimmed());
        m_charging = (status.compare(QLatin1String("Charging"), Qt::CaseInsensitive) == 0 ||
                      status.compare(QLatin1String("Full"), Qt::CaseInsensitive) == 0);
        statusFile.close();
    }
}

int BatteryMonitor::readSysfsInt(const QString &filename) const
{
    if (m_powerSupplyPath.isEmpty())
        return 0;

    QFile file(m_powerSupplyPath + "/" + filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromLatin1(file.readAll().trimmed());
        file.close();
        bool ok = false;
        int val = content.toInt(&ok);
        return ok ? val : 0;
    }
    return 0;
}

void BatteryMonitor::readBatteryHealth()
{
    if (m_powerSupplyPath.isEmpty())
        return;

    int prevHealth = healthPercent();

    // Read design capacity (µAh) — typically constant
    int designUah = readSysfsInt("charge_full_design");
    if (designUah > 0)
        m_designCapacityUah = designUah;

    // Read current learned capacity (µAh) — updated by fuel gauge
    int chargeFullUah = readSysfsInt("charge_full");
    if (chargeFullUah > 0) {
        m_chargeFullAvailable = true;
        m_learnedCapacityUah = chargeFullUah;
        updateLearnedCapacity(chargeFullUah);
    } else {
        // Hardware charge_full not available — try software estimate
        m_chargeFullAvailable = false;
        coulombTryEstimate();
    }

    // Instantaneous readings
    m_voltageNowUv = readSysfsInt("voltage_now");
    m_currentNowUa = readSysfsInt("current_now");
    m_temperatureDeci = readSysfsInt("temp");
    m_cycleCount = readSysfsInt("cycle_count");

    if (healthPercent() != prevHealth)
        emit healthChanged();
}

void BatteryMonitor::updateLearnedCapacity(int chargeFullUah)
{
    if (chargeFullUah <= 0)
        return;

    // EMA smoothing: alpha starts high (quick initial convergence) and
    // decreases as we accumulate samples, down to a floor of 0.05
    double alpha;
    if (m_emaSampleCount == 0) {
        m_emaCapacityUah = chargeFullUah;
        alpha = 1.0;
    } else {
        alpha = qMax(0.05, 2.0 / (m_emaSampleCount + 1));
        m_emaCapacityUah = alpha * chargeFullUah + (1.0 - alpha) * m_emaCapacityUah;
    }
    m_emaSampleCount++;

    // Record timestamped sample for trend analysis
    qint64 now = QDateTime::currentSecsSinceEpoch();
    m_healthTimestamps.append(now);
    m_healthSamples.append(chargeFullUah);

    // Trim oldest if exceeding cap
    while (m_healthTimestamps.size() > MAX_HEALTH_SAMPLES) {
        m_healthTimestamps.remove(0);
        m_healthSamples.remove(0);
    }
}

void BatteryMonitor::coulombAccumulate()
{
    // Read current_now (µA) — skip if not available
    int currentUa = readSysfsInt("current_now");
    if (currentUa == 0)
        return;

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // First sample of this discharge cycle: just record the starting point
    if (m_coulombStartSoc < 0) {
        m_coulombStartSoc = m_level;
        m_coulombLastMs = nowMs;
        m_coulombAccUah = 0.0;
        return;
    }

    qint64 dtMs = nowMs - m_coulombLastMs;

    // If the gap is too large (>10min), the CPU was likely suspended.
    // We can't account for discharge during sleep so just advance the
    // timestamp and keep going — the current samples we DO have are still
    // valid for the intervals we measured them.
    if (dtMs <= 0 || dtMs > COULOMB_MAX_GAP_MS) {
        m_coulombLastMs = nowMs;
        return;
    }

    // Integrate: I(µA) * dt(ms) / 3,600,000 = µAh
    // current_now is negative during discharge on most drivers, take abs
    double dischargedUah = qAbs(currentUa) * (dtMs / 3600000.0);
    m_coulombAccUah += dischargedUah;
    m_coulombLastMs = nowMs;
}

void BatteryMonitor::coulombReset()
{
    m_coulombAccUah = 0.0;
    m_coulombStartSoc = -1;
    m_coulombLastMs = 0;
}

void BatteryMonitor::coulombTryEstimate()
{
    if (m_coulombStartSoc < 0 || m_coulombAccUah <= 0)
        return;

    // Need design capacity to compute health %
    if (m_designCapacityUah <= 0)
        return;

    int socDrop = m_coulombStartSoc - m_level;
    if (socDrop < COULOMB_MIN_SOC_SPAN)
        return; // not enough span yet

    // Estimate full capacity: accumulatedUah / (socDrop / 100)
    double estimatedCapacityUah = m_coulombAccUah * 100.0 / socDrop;

    // Sanity check: should be within 30%-200% of design capacity
    if (estimatedCapacityUah < m_designCapacityUah * 0.3 ||
        estimatedCapacityUah > m_designCapacityUah * 2.0) {
        qWarning() << "BatteryMonitor: Coulomb estimate out of range:"
                    << qRound(estimatedCapacityUah / 1000.0) << "mAh, discarding";
        coulombReset();
        return;
    }

    qInfo() << "BatteryMonitor: Software coulomb estimate:"
            << qRound(estimatedCapacityUah / 1000.0) << "mAh"
            << "(SoC" << m_coulombStartSoc << "% ->" << m_level << "%,"
            << qRound(m_coulombAccUah / 1000.0) << "mAh discharged)";

    m_coulombEstimateCount++;
    updateLearnedCapacity(qRound(estimatedCapacityUah));

    // Reset for the next accumulation window — continue from current level
    m_coulombStartSoc = m_level;
    m_coulombAccUah = 0.0;
}

void BatteryMonitor::loadHealthData()
{
    QString path = m_configDir + "/" + QString::fromLatin1(POWERD_HEALTH_FILE);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject obj = doc.object();
    m_designCapacityUah = obj.value(QLatin1String("design_uah")).toInt(0);
    m_emaCapacityUah = obj.value(QLatin1String("ema_uah")).toDouble(0.0);
    m_emaSampleCount = obj.value(QLatin1String("ema_count")).toInt(0);

    QJsonArray samples = obj.value(QLatin1String("samples")).toArray();
    m_healthTimestamps.clear();
    m_healthSamples.clear();
    m_healthTimestamps.reserve(samples.size());
    m_healthSamples.reserve(samples.size());

    for (int i = 0; i < samples.size(); ++i) {
        QJsonObject s = samples[i].toObject();
        m_healthTimestamps.append(static_cast<qint64>(s.value(QLatin1String("t")).toDouble()));
        m_healthSamples.append(s.value(QLatin1String("uah")).toInt());
    }

    if (m_emaCapacityUah > 0 && m_emaSampleCount > 0)
        m_learnedCapacityUah = qRound(m_emaCapacityUah);

    // Restore coulomb counting fallback state
    m_coulombAccUah = obj.value(QLatin1String("coulomb_acc_uah")).toDouble(0.0);
    m_coulombStartSoc = obj.value(QLatin1String("coulomb_start_soc")).toInt(-1);
    m_coulombEstimateCount = obj.value(QLatin1String("coulomb_estimates")).toInt(0);
    m_chargeFullAvailable = obj.value(QLatin1String("charge_full_available")).toBool(false);
}

void BatteryMonitor::saveHealthData()
{
    // Only save if we have actual data
    if (m_emaSampleCount == 0 && m_designCapacityUah == 0 && m_coulombEstimateCount == 0)
        return;

    QJsonObject obj;
    obj[QLatin1String("design_uah")] = m_designCapacityUah;
    obj[QLatin1String("ema_uah")] = m_emaCapacityUah;
    obj[QLatin1String("ema_count")] = m_emaSampleCount;

    // Coulomb counting fallback state
    obj[QLatin1String("coulomb_acc_uah")] = m_coulombAccUah;
    obj[QLatin1String("coulomb_start_soc")] = m_coulombStartSoc;
    obj[QLatin1String("coulomb_estimates")] = m_coulombEstimateCount;
    obj[QLatin1String("charge_full_available")] = m_chargeFullAvailable;

    QJsonArray samples;
    int count = qMin(m_healthTimestamps.size(), m_healthSamples.size());
    for (int i = 0; i < count; ++i) {
        QJsonObject s;
        s[QLatin1String("t")] = m_healthTimestamps[i];
        s[QLatin1String("uah")] = m_healthSamples[i];
        samples.append(s);
    }
    obj[QLatin1String("samples")] = samples;

    QString path = m_configDir + "/" + QString::fromLatin1(POWERD_HEALTH_FILE);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to save battery health data to:" << path;
        return;
    }

    QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning() << "Failed to commit battery health file";
}

void BatteryMonitor::recordEntry()
{
    BatteryEntry entry(
        QDateTime::currentSecsSinceEpoch(),
        m_level,
        m_charging,
        m_activeProfile,
        false,
        m_workoutActive
    );

    m_history.append(entry);
    trimHistory();
}

void BatteryMonitor::loadHistory()
{
    QString historyPath = m_configDir + "/" + QString::fromLatin1(POWERD_BATTERY_FILE);
    QFile file(historyPath);

    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open battery history file:" << historyPath;
        return;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    char magic[4];
    if (stream.readRawData(magic, 4) != 4 || qstrncmp(magic, "BATT", 4) != 0) {
        qWarning() << "Invalid battery history file magic";
        file.close();
        return;
    }

    quint16 version;
    stream >> version;

    if (version != 1) {
        qWarning() << "Unsupported battery history version:" << version;
        file.close();
        return;
    }

    quint32 entryCount;
    quint32 maxEntries;
    stream >> entryCount >> maxEntries;

    m_history.clear();
    m_history.reserve(entryCount);

    for (quint32 i = 0; i < entryCount; ++i) {
        qint64 timestamp;
        qint32 level;
        quint8 charging;
        quint8 screenOn;
        quint8 workoutActive;
        quint32 profileLen;

        stream >> timestamp >> level >> charging >> screenOn >> workoutActive >> profileLen;

        QByteArray profileData(profileLen, '\0');
        if (profileLen > 0) {
            stream.readRawData(profileData.data(), profileLen);
        }

        BatteryEntry entry;
        entry.timestamp = timestamp;
        entry.level = level;
        entry.charging = (charging != 0);
        entry.screenOn = (screenOn != 0);
        entry.workoutActive = (workoutActive != 0);
        entry.activeProfile = QString::fromUtf8(profileData);

        m_history.append(entry);
    }

    file.close();
    trimHistory();
}

void BatteryMonitor::saveHistory()
{
    QString historyPath = m_configDir + "/" + QString::fromLatin1(POWERD_BATTERY_FILE);
    QSaveFile file(historyPath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to save battery history to:" << historyPath;
        return;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream.writeRawData("BATT", 4);
    stream << quint16(1);
    stream << quint32(m_history.size());
    stream << quint32(m_history.size());

    for (const BatteryEntry &entry : m_history) {
        QByteArray profileUtf8 = entry.activeProfile.toUtf8();

        stream << qint64(entry.timestamp);
        stream << qint32(entry.level);
        stream << quint8(entry.charging ? 1 : 0);
        stream << quint8(entry.screenOn ? 1 : 0);
        stream << quint8(entry.workoutActive ? 1 : 0);
        stream << quint32(profileUtf8.size());

        if (!profileUtf8.isEmpty()) {
            stream.writeRawData(profileUtf8.constData(), profileUtf8.size());
        }
    }

    if (!file.commit()) {
        qWarning() << "Failed to commit battery history file";
    }
}

void BatteryMonitor::trimHistory()
{
    qint64 cutoffTime = QDateTime::currentSecsSinceEpoch() - (m_historyDays * 86400);

    int removeCount = 0;
    while (removeCount < m_history.size() && m_history[removeCount].timestamp < cutoffTime) {
        ++removeCount;
    }

    if (removeCount > 0) {
        m_history.remove(0, removeCount);
    }
}

QString BatteryMonitor::findPowerSupplyPath() const
{
    QDir powerSupplyDir("/sys/class/power_supply");
    if (!powerSupplyDir.exists()) {
        return QString();
    }

    QStringList candidates = powerSupplyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &candidate : candidates) {
        QString path = powerSupplyDir.absoluteFilePath(candidate);
        QFile typeFile(path + "/type");

        if (typeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString type = QString::fromLatin1(typeFile.readAll().trimmed());
            typeFile.close();

            if (type.compare(QLatin1String("Battery"), Qt::CaseInsensitive) == 0) {
                return path;
            }
        }
    }

    return QString();
}
