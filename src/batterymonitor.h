#ifndef BATTERYMONITOR_H
#define BATTERYMONITOR_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

class BatteryMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int level READ level NOTIFY levelChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)
    Q_PROPERTY(int healthPercent READ healthPercent NOTIFY healthChanged)
    Q_PROPERTY(int learnedCapacityMah READ learnedCapacityMah NOTIFY healthChanged)
    Q_PROPERTY(int designCapacityMah READ designCapacityMah NOTIFY healthChanged)
    Q_PROPERTY(int cycleCount READ cycleCount NOTIFY healthChanged)

    friend class TestBatteryMonitor;
    friend class TestDBusInterface;
    friend class TestAutomationEngine;

public:
    explicit BatteryMonitor(const QString &configDir, QObject *parent = nullptr);

    void start();
    void stop();

    int level() const;
    bool charging() const;
    int healthPercent() const;
    int learnedCapacityMah() const;
    int designCapacityMah() const;
    int cycleCount() const;
    int voltageNowMv() const;
    int currentNowMa() const;
    int temperatureDeci() const;
    QString healthConfidence() const;
    QJsonObject healthInfo() const;

    struct BatteryEntry {
        qint64 timestamp;
        int level;
        bool charging;
        QString activeProfile;
        bool screenOn;
        bool workoutActive;

        BatteryEntry()
            : timestamp(0), level(0), charging(false), screenOn(false), workoutActive(false) {}

        BatteryEntry(qint64 ts, int lvl, bool chrg, const QString &profile, bool screen, bool workout)
            : timestamp(ts), level(lvl), charging(chrg), activeProfile(profile),
              screenOn(screen), workoutActive(workout) {}
    };

    QVector<BatteryEntry> history(int hours) const;
    QJsonObject prediction() const;

    int historyDays() const;
    void setHistoryDays(int days);

signals:
    void levelChanged(int level);
    void chargingChanged(bool charging);
    void significantChange(int level, bool charging);
    void healthChanged();

public slots:
    void setActiveProfile(const QString &profileId);
    void setWorkoutActive(bool active);

private slots:
    void pollBattery();
    void heartbeat();

private:
    void readBatteryLevel();
    void readBatteryHealth();
    void recordEntry();
    void loadHistory();
    void saveHistory();
    void loadHealthData();
    void saveHealthData();
    void updateLearnedCapacity(int chargeFullUah);
    void trimHistory();
    int readSysfsInt(const QString &filename) const;
    int readSysfsIntFrom(const QString &basePath, const QString &filename) const;
    void discoverPowerSupplySources();
    int readBestSource(const QString &filename, bool allowNegative = false) const;
    int knownDesignCapacityUah() const;

    // Software coulomb counting fallback
    void coulombAccumulate();
    void coulombReset();
    void coulombTryEstimate();
    bool m_chargeFullAvailable;

    QString m_configDir;
    QString m_powerSupplyPath;          // Primary node for level/status (type=Battery)
    QStringList m_healthSourcePaths;    // Ranked list of nodes to try for health data
    QString m_hostname;                 // Device codename for known-capacity fallback
    int m_level;
    bool m_charging;
    int m_lastRecordedLevel;
    QString m_activeProfile;
    bool m_workoutActive;
    int m_historyDays;

    QVector<BatteryEntry> m_history;
    QTimer *m_pollTimer;
    QTimer *m_heartbeatTimer;

    // Battery health tracking
    int m_designCapacityUah;     // charge_full_design in µAh (0 = unavailable)
    int m_learnedCapacityUah;    // latest charge_full in µAh
    int m_voltageNowUv;          // voltage_now in µV
    int m_currentNowUa;          // current_now in µA
    int m_temperatureDeci;       // temp in 0.1°C
    int m_cycleCount;            // cycle_count

    // Exponential moving average for smoothing
    double m_emaCapacityUah;     // smoothed learned capacity
    int m_emaSampleCount;        // number of samples averaged
    QVector<qint64> m_healthTimestamps; // timestamps of health samples
    QVector<int> m_healthSamples;       // charge_full values (µAh) at those times
    static const int MAX_HEALTH_SAMPLES = 200; // ~400 days of 2/day samples

    // Software coulomb counting fallback (active when charge_full unavailable)
    double m_coulombAccUah;      // accumulated discharge in µAh (positive = discharged)
    int m_coulombStartSoc;       // SoC% when accumulation started
    qint64 m_coulombLastMs;      // timestamp (ms) of last accumulation step
    int m_coulombEstimateCount;  // how many fallback estimates produced
};

#endif // BATTERYMONITOR_H
