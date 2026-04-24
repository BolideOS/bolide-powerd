#include "batterymonitor.h"
#include "common.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
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
    , m_chargeLimitEnabled(false)
    , m_chargeLimitPercent(90)
    , m_chargingSuspended(false)
    , m_usbUnplugTimer(new QTimer(this))
    , m_usbWakeupDisabled(false)
    , m_screenOn(true)
{
    discoverPowerSupplySources();

    if (m_powerSupplyPath.isEmpty()) {
        qWarning() << "BatteryMonitor: No battery power supply found, using defaults";
    }

    // Discover input_suspend path for charge limiting
    if (!m_powerSupplyPath.isEmpty()) {
        QString isp = m_powerSupplyPath + QStringLiteral("/input_suspend");
        QFileInfo fi(isp);
        if (fi.exists() && fi.isWritable()) {
            m_inputSuspendPath = isp;
            qDebug() << "BatteryMonitor: Charge limit available via" << isp;
        }
    }

    m_pollTimer->setTimerType(Qt::CoarseTimer);
    m_heartbeatTimer->setTimerType(Qt::CoarseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, &BatteryMonitor::pollBattery);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &BatteryMonitor::heartbeat);

    // USB unplug timer: disable USB wakeup 15s after cable removal
    m_usbUnplugTimer->setSingleShot(true);
    m_usbUnplugTimer->setInterval(15000);
    m_usbUnplugTimer->setTimerType(Qt::CoarseTimer);
    connect(m_usbUnplugTimer, &QTimer::timeout, this, &BatteryMonitor::onUsbUnplugTimeout);

    loadHistory();
    loadHealthData();
    loadSettings();
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
    m_usbUnplugTimer->stop();

    // Always resume charging on shutdown so a crashed/stopped powerd
    // doesn't leave the battery unable to charge.
    if (m_chargingSuspended) {
        writeInputSuspend(false);
        m_chargingSuspended = false;
    }

    // Always re-enable USB wakeup so the system can wake on cable insertion
    if (m_usbWakeupDisabled) {
        setUsbWakeup(true);
    }

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

        // USB wakeup control: when cable is unplugged, start timer to disable
        // the USB controller wakelock. When plugged back in, cancel and re-enable.
        if (!m_charging) {
            // Unplugged — start countdown to disable USB wakeup
            if (!m_usbUnplugTimer->isActive()) {
                qDebug() << "BatteryMonitor: USB unplugged, will disable wakeup in 15s";
                m_usbUnplugTimer->start();
            }
        } else {
            // Plugged in — cancel pending disable and re-enable USB wakeup
            m_usbUnplugTimer->stop();
            if (m_usbWakeupDisabled) {
                setUsbWakeup(true);
            }
        }
    }

    if (qAbs(m_level - m_lastRecordedLevel) >= BATTERY_CHANGE_THRESHOLD) {
        recordEntry();
        m_lastRecordedLevel = m_level;
    }

    if (m_level != prevLevel || m_charging != prevCharging) {
        emit significantChange(m_level, m_charging);
    }

    // Enforce charge limit after level/charging updates
    enforceChargeLimit();
}

void BatteryMonitor::heartbeat()
{
    // Skip the heartbeat entirely if screen is off, not charging, and not
    // in a workout. The next screen-on or charge-plug event will trigger
    // a full poll + record anyway, so we lose at most one 2-hour data
    // point — acceptable to let the CPU stay in deep sleep.
    if (!m_screenOn && !m_charging && !m_workoutActive) {
        qDebug() << "BatteryMonitor: Heartbeat skipped (screen off, idle)";
        return;
    }

    readBatteryHealth();
    recordEntry();
    m_lastRecordedLevel = m_level;
    saveHistory();
    saveHealthData();
}

// ─── Screen-aware battery polling ───────────────────────────────────────────

void BatteryMonitor::onDisplayStateChanged(const QString &state)
{
    bool wasOn = m_screenOn;
    m_screenOn = (state == QLatin1String("on"));

    if (m_screenOn && !wasOn) {
        // Screen just turned on — do an immediate poll so UI has fresh data,
        // then resume the timer
        pollBattery();
        if (!m_pollTimer->isActive()) {
            int interval = m_workoutActive ? BATTERY_POLL_ACTIVE_MS : BATTERY_POLL_IDLE_MS;
            m_pollTimer->start(interval);
            qDebug() << "BatteryMonitor: Screen on — resumed polling at" << interval << "ms";
        }
    } else if (!m_screenOn && wasOn && !m_workoutActive && !m_charging) {
        // Screen off, not in workout, not charging — stop polling to let CPU suspend.
        // Charge limit enforcement is not needed when not charging.
        // Heartbeat timer (2h) still runs for history recording.
        m_pollTimer->stop();
        qDebug() << "BatteryMonitor: Screen off — stopped polling (heartbeat continues)";
    }
    // If charging or workout active, keep polling even with screen off
    // so charge limit and coulomb counting continue working.
}

// ─── USB wakeup control ────────────────────────────────────────────────────

void BatteryMonitor::onUsbUnplugTimeout()
{
    // Double-check we're still unplugged before disabling
    if (!m_charging) {
        setUsbWakeup(false);
    }
}

void BatteryMonitor::setUsbWakeup(bool enabled)
{
    QFile f(QString::fromLatin1(USB_WAKEUP_PATH));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    f.write(enabled ? "enabled" : "disabled");
    f.close();
    m_usbWakeupDisabled = !enabled;
    qDebug() << "BatteryMonitor: USB wakeup" << (enabled ? "enabled" : "disabled");
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

int BatteryMonitor::readSysfsIntFrom(const QString &basePath, const QString &filename) const
{
    if (basePath.isEmpty())
        return 0;

    QFile file(basePath + "/" + filename);
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

    // Read design capacity.
    // Strategy: prefer hardware charge_full_design when it looks trustworthy.
    // Some fuel gauges (e.g. PMI632/QG) report charge_full_design = charge_full
    // (the learned FCC) making health always appear 100%.  In that case, fall back
    // to our known-device table.  But if the hardware reports a real design value
    // that differs from charge_full, trust it — it knows the actual cell rating.
    int hwDesignUah = readBestSource("charge_full_design");
    if (hwDesignUah <= 0) {
        // Some drivers use energy-based (µWh) instead of charge-based (µAh)
        int designUwh = readBestSource("energy_full_design");
        int voltageUv = readBestSource("voltage_max_design");
        if (voltageUv <= 0) voltageUv = readBestSource("voltage_now");
        if (designUwh > 0 && voltageUv > 0)
            hwDesignUah = (int)((double)designUwh / voltageUv * 1000000.0);
    }

    int knownUah = knownDesignCapacityUah();
    int designUah = 0;

    if (hwDesignUah > 100000) {
        // Hardware provides a value.  Check if it's just echoing charge_full.
        int chargeFullSniff = readBestSource("charge_full");
        bool likelyEcho = (chargeFullSniff > 0 && hwDesignUah == chargeFullSniff);

        if (likelyEcho && knownUah > 0) {
            // FG reports charge_full_design == charge_full.  This could mean:
            // (a) FG echoing learned FCC as design (common on PMI632/QG), or
            // (b) Battery is at full health and hasn't degraded yet.
            // If the known table value is close (within 20%), use the table
            // since it may account for a larger actual cell.  If the table value
            // is far off (>20%), it's probably for a different hardware variant
            // (e.g. 41mm vs 46mm) — trust the hardware.
            double ratio = (double)knownUah / hwDesignUah;
            if (ratio >= 0.8 && ratio <= 1.2) {
                designUah = knownUah;
            } else {
                designUah = hwDesignUah;
            }
        } else {
            // Hardware charge_full_design looks real — trust it
            designUah = hwDesignUah;
        }
    } else if (knownUah > 0) {
        designUah = knownUah;
    }
    if (designUah > 0)
        m_designCapacityUah = designUah;

    // Read current learned/full capacity — scan all source nodes
    int chargeFullUah = readBestSource("charge_full");
    if (chargeFullUah <= 0) {
        // Energy-based fallback
        int energyFullUwh = readBestSource("energy_full");
        int voltageUv = readBestSource("voltage_now");
        if (energyFullUwh > 0 && voltageUv > 0)
            chargeFullUah = (int)((double)energyFullUwh / voltageUv * 1000000.0);
    }
    if (chargeFullUah > 0) {
        m_chargeFullAvailable = true;
        m_learnedCapacityUah = chargeFullUah;
        updateLearnedCapacity(chargeFullUah);
    } else {
        m_chargeFullAvailable = false;
        coulombTryEstimate();
    }

    // Instantaneous readings
    m_voltageNowUv = readBestSource("voltage_now");
    m_currentNowUa = readBestSource("current_now", true);  // current can be negative (discharge)
    m_temperatureDeci = readBestSource("temp", true);       // temp can be negative (sub-zero)

    // Cycle count — try standard file first, then Qualcomm bucket format
    m_cycleCount = readBestSource("cycle_count");
    if (m_cycleCount <= 0) {
        for (const QString &srcPath : m_healthSourcePaths) {
            QFile f(srcPath + "/cycle_counts");
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromLatin1(f.readAll().trimmed());
                f.close();
                QStringList parts = content.split(QLatin1Char(' '), QString::SkipEmptyParts);
                if (!parts.isEmpty()) {
                    int total = 0;
                    for (const QString &p : parts) {
                        bool ok = false;
                        int v = p.toInt(&ok);
                        if (ok) total += v;
                    }
                    if (total > 0) {
                        m_cycleCount = total;
                        break;
                    }
                }
            }
        }
    }

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

// Discover all power supply nodes and rank them by usefulness for health data.
// Priority: Battery > BMS > any other type that has charge_full_design.
void BatteryMonitor::discoverPowerSupplySources()
{
    QDir powerSupplyDir(QStringLiteral("/sys/class/power_supply"));
    if (!powerSupplyDir.exists())
        return;

    QStringList candidates = powerSupplyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Read hostname for known-capacity fallback
    QFile hostnameFile(QStringLiteral("/etc/hostname"));
    if (hostnameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_hostname = QString::fromLatin1(hostnameFile.readAll().trimmed());
        hostnameFile.close();
    }

    // Categorize nodes by type
    QStringList batteryNodes, bmsNodes, otherNodes;

    for (const QString &candidate : candidates) {
        QString path = powerSupplyDir.absoluteFilePath(candidate);
        QFile typeFile(path + QStringLiteral("/type"));

        if (!typeFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString type = QString::fromLatin1(typeFile.readAll().trimmed());
        typeFile.close();

        if (type.compare(QLatin1String("Battery"), Qt::CaseInsensitive) == 0) {
            batteryNodes.append(path);
        } else if (type.compare(QLatin1String("BMS"), Qt::CaseInsensitive) == 0) {
            bmsNodes.append(path);
        } else if (type.compare(QLatin1String("USB"), Qt::CaseInsensitive) != 0 &&
                   type.compare(QLatin1String("Mains"), Qt::CaseInsensitive) != 0) {
            // Skip USB/Mains charger nodes, keep everything else (fuel gauges, etc.)
            otherNodes.append(path);
        }
    }

    // Primary node for level/status: prefer Battery nodes that have 'capacity'
    for (const QString &path : batteryNodes) {
        if (QFile::exists(path + QStringLiteral("/capacity"))) {
            m_powerSupplyPath = path;
            break;
        }
    }
    // If no Battery node with capacity, try BMS
    if (m_powerSupplyPath.isEmpty()) {
        for (const QString &path : bmsNodes) {
            if (QFile::exists(path + QStringLiteral("/capacity"))) {
                m_powerSupplyPath = path;
                break;
            }
        }
    }

    // Build ranked health source list: Battery nodes first, then BMS, then others.
    // Within each group, prefer nodes that have charge_full_design.
    auto sortByHealthData = [](QStringList &nodes) {
        std::stable_sort(nodes.begin(), nodes.end(), [](const QString &a, const QString &b) {
            bool aHasDesign = QFile::exists(a + QStringLiteral("/charge_full_design")) ||
                              QFile::exists(a + QStringLiteral("/energy_full_design"));
            bool bHasDesign = QFile::exists(b + QStringLiteral("/charge_full_design")) ||
                              QFile::exists(b + QStringLiteral("/energy_full_design"));
            return aHasDesign && !bHasDesign;
        });
    };
    sortByHealthData(batteryNodes);
    sortByHealthData(bmsNodes);
    sortByHealthData(otherNodes);

    m_healthSourcePaths = batteryNodes + bmsNodes + otherNodes;

    // Log discovery results
    qInfo() << "BatteryMonitor: Primary node:" << m_powerSupplyPath;
    qInfo() << "BatteryMonitor: Health sources:" << m_healthSourcePaths;
    if (!m_hostname.isEmpty())
        qInfo() << "BatteryMonitor: Device hostname:" << m_hostname;
}

// Try reading a sysfs integer file from the ranked health source list.
// Returns the first valid value found, or 0 if none.
// When allowNegative is false (default), skips zero/negative values.
// When allowNegative is true, returns the first successfully-read value (even if negative).
int BatteryMonitor::readBestSource(const QString &filename, bool allowNegative) const
{
    auto tryRead = [&](const QString &basePath) -> QPair<bool, int> {
        if (basePath.isEmpty())
            return {false, 0};
        QFile file(basePath + "/" + filename);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromLatin1(file.readAll().trimmed());
            file.close();
            bool ok = false;
            int val = content.toInt(&ok);
            if (ok)
                return {true, val};
        }
        return {false, 0};
    };

    for (const QString &srcPath : m_healthSourcePaths) {
        auto result = tryRead(srcPath);
        if (result.first) {
            if (allowNegative || result.second > 0)
                return result.second;
        }
    }
    // Fallback: try primary power supply path directly
    if (!m_powerSupplyPath.isEmpty() && !m_healthSourcePaths.contains(m_powerSupplyPath)) {
        auto result = tryRead(m_powerSupplyPath);
        if (result.first) {
            if (allowNegative || result.second > 0)
                return result.second;
        }
    }
    return 0;
}

// Known battery design capacities in µAh for AsteroidOS-supported watches.
// Used as a last resort when the kernel driver doesn't expose charge_full_design.
// Source: device specs, teardowns, and battery markings.
int BatteryMonitor::knownDesignCapacityUah() const
{
    if (m_hostname.isEmpty())
        return 0;

    // Map of device codename → design capacity in µAh
    // Qualcomm Snapdragon Wear 2100 / 3100 devices
    static const struct { const char *host; int capacityUah; } knownDevices[] = {
        // --- Qualcomm SDW2100 (msm8909w) ---
        { "beluga",     430000 },  // Oppo Watch (46mm) — 430mAh
        { "skipjack",   370000 },  // Mobvoi TicWatch Pro 2020 — 415mAh (conservative)
        { "ray",        300000 },  // Mobvoi TicWatch S/E — 300mAh
        { "firefish",   415000 },  // Mobvoi TicWatch Pro 3 — 577mAh (conservative)
        { "hoki",       300000 },  // Xiaomi Mi Watch — 570mAh (conservative)
        { "pike",       370000 },  // Casio WSD-F30 — 370mAh
        { "koi",        310000 },  // Fossil Gen 5E — 310mAh
        { "rubyfish",   310000 },  // Fossil Gen 5 Carlyle — 310mAh
        { "sawfish",    310000 },  // Fossil Gen 5 Julianna — 310mAh

        // --- Qualcomm APQ8026 / Snapdragon 400 ---
        { "catfish",    300000 },  // LG G Watch R — 410mAh (conservative)
        { "bass",       300000 },  // LG Watch Urbane — 410mAh (conservative)
        { "dory",       300000 },  // LG G Watch — 400mAh (conservative)
        { "lenok",      300000 },  // Moto 360 (1st gen) — 320mAh
        { "sparrow",    300000 },  // ASUS ZenWatch 2 — 300mAh
        { "swift",      340000 },  // ASUS ZenWatch 3 — 340mAh
        { "sturgeon",   370000 },  // Huawei Watch 1 — 300mAh
        { "smelt",      300000 },  // Moto 360 (2nd gen) — 300mAh
        { "mooneye",    340000 },  // ASUS ZenWatch 2 (WI501Q) — 400mAh (conservative)
        { "narwhal",    350000 },  // Huawei Watch 2 — 420mAh (conservative)
        { "triggerfish", 380000 }, // Sony Smartwatch 3 — 420mAh (conservative)
        { "tetra",      300000 },  // LG Watch Style — 240mAh

        // --- MediaTek MT6580 ---
        { "harmony",    350000 },  // Generic MTK6580 watch — 350mAh typical
        { "inharmony",  350000 },  // Generic MTK6580 watch variant

        // --- Samsung Exynos ---
        { "rinato",     250000 },  // Samsung Gear 2 — 300mAh (conservative)
        { "nemo",       250000 },  // Samsung Gear S — 300mAh (conservative)

        // --- Other ---
        { "anthias",    300000 },  // LG Watch Urbane 2 — 570mAh (conservative)
        { "sprat",      300000 },  // LG G Watch — 400mAh (conservative)
        { "minnow",     300000 },  // TAG Heuer Connected — 410mAh (conservative)
    };

    for (const auto &dev : knownDevices) {
        if (m_hostname == QLatin1String(dev.host))
            return dev.capacityUah;
    }

    // Unknown device — return 0, software coulomb counting will try to estimate
    return 0;
}

// ─── Charge limit (battery protection) ──────────────────────────────────────

bool BatteryMonitor::chargeLimitEnabled() const { return m_chargeLimitEnabled; }
int BatteryMonitor::chargeLimitPercent() const { return m_chargeLimitPercent; }

void BatteryMonitor::setChargeLimitEnabled(bool enabled)
{
    if (enabled == m_chargeLimitEnabled) return;
    m_chargeLimitEnabled = enabled;
    qInfo() << "BatteryMonitor: Charge limit" << (enabled ? "enabled" : "disabled")
            << "at" << m_chargeLimitPercent << "%";

    // If disabling, immediately resume charging
    if (!enabled && m_chargingSuspended) {
        writeInputSuspend(false);
        m_chargingSuspended = false;
    }

    saveSettings();
    emit chargeLimitChanged(m_chargeLimitEnabled, m_chargeLimitPercent);
    // Re-evaluate immediately
    if (enabled) enforceChargeLimit();
}

void BatteryMonitor::setChargeLimitPercent(int percent)
{
    percent = qBound(50, percent, 100);
    if (percent == m_chargeLimitPercent) return;
    m_chargeLimitPercent = percent;
    qInfo() << "BatteryMonitor: Charge limit set to" << percent << "%";

    saveSettings();
    emit chargeLimitChanged(m_chargeLimitEnabled, m_chargeLimitPercent);
    enforceChargeLimit();
}

void BatteryMonitor::enforceChargeLimit()
{
    if (!m_chargeLimitEnabled || m_inputSuspendPath.isEmpty()) return;

    if (m_level >= m_chargeLimitPercent && !m_chargingSuspended) {
        // Battery at or above limit — suspend charging
        if (writeInputSuspend(true)) {
            m_chargingSuspended = true;
            qInfo() << "BatteryMonitor: Charge limit reached (" << m_level
                    << "% >= " << m_chargeLimitPercent << "%), charging suspended";
        }
    } else if (m_level <= (m_chargeLimitPercent - CHARGE_LIMIT_HYSTERESIS) && m_chargingSuspended) {
        // Battery dropped below hysteresis threshold — resume charging
        if (writeInputSuspend(false)) {
            m_chargingSuspended = false;
            qInfo() << "BatteryMonitor: Below threshold (" << m_level
                    << "% <= " << (m_chargeLimitPercent - CHARGE_LIMIT_HYSTERESIS)
                    << "%), charging resumed";
        }
    }
}

bool BatteryMonitor::writeInputSuspend(bool suspend)
{
    QFile f(m_inputSuspendPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "BatteryMonitor: Cannot open" << m_inputSuspendPath;
        return false;
    }
    QByteArray val = suspend ? QByteArrayLiteral("1") : QByteArrayLiteral("0");
    bool ok = (f.write(val) == val.size()) && f.flush();
    if (!ok || f.error() != QFile::NoError) {
        qWarning() << "BatteryMonitor: Failed to write" << val << "to" << m_inputSuspendPath;
        return false;
    }
    return true;
}

void BatteryMonitor::loadSettings()
{
    QString path = m_configDir + QLatin1Char('/') + QLatin1String(POWERD_SETTINGS_FILE);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QJsonObject cl = obj.value(QStringLiteral("charge_limit")).toObject();
    m_chargeLimitEnabled = cl.value(QStringLiteral("enabled")).toBool(false);
    m_chargeLimitPercent = qBound(50, cl.value(QStringLiteral("percent")).toInt(90), 100);

    if (m_chargeLimitEnabled) {
        qInfo() << "BatteryMonitor: Loaded charge limit:" << m_chargeLimitPercent << "% (enabled)";
    }
}

void BatteryMonitor::saveSettings()
{
    QString path = m_configDir + QLatin1Char('/') + QLatin1String(POWERD_SETTINGS_FILE);

    // Read existing settings to preserve other fields
    QJsonObject obj;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) obj = doc.object();
        }
    }

    QJsonObject cl;
    cl[QStringLiteral("enabled")] = m_chargeLimitEnabled;
    cl[QStringLiteral("percent")] = m_chargeLimitPercent;
    obj[QStringLiteral("charge_limit")] = cl;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "BatteryMonitor: Cannot save settings to" << path;
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    f.commit();
}
