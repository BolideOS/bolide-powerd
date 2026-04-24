#ifndef HEALTHSTORE_H
#define HEALTHSTORE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <sqlite3.h>

class HealthStore : public QObject
{
    Q_OBJECT

public:
    explicit HealthStore(const QString &dbPath, QObject *parent = nullptr);
    ~HealthStore();

    bool open();
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // ─── Write (called by HealthMonitor during batch flush) ─────────────

    struct Sample {
        qint64  ts;         // unix epoch ms
        QString metric;     // "hr", "steps", "hrv_rmssd", "spo2", "rr_interval"
        double  value;
        int     quality;    // 0=good, 1=motion artifact, 2=weak signal
    };

    bool writeSamples(const QVector<Sample> &samples);

    // ─── Read (called by D-Bus for UI) ──────────────────────────────────

    // Returns JSON array of {ts, value, quality} for a metric in time range
    QJsonArray querySamples(const QString &metric, qint64 fromTs, qint64 toTs,
                            int maxResults = 1000) const;

    // Returns latest N samples for a metric
    QJsonArray queryLatest(const QString &metric, int count = 1) const;

    // Returns aggregated stats: {min, max, avg, count} per time bucket
    QJsonArray queryAggregated(const QString &metric, qint64 fromTs, qint64 toTs,
                               int bucketMinutes = 60) const;

    // ─── Sessions ───────────────────────────────────────────────────────

    qint64 startSession(const QString &type);
    bool endSession(qint64 sessionId, const QJsonObject &metadata = QJsonObject());
    QJsonArray querySessions(const QString &type, qint64 fromTs, qint64 toTs) const;

    // ─── Sync watermarks (for incremental phone sync) ────────────────────

    qint64 syncWatermark(const QString &category) const;
    bool setSyncWatermark(const QString &category, qint64 ackedTs);

    // ─── Maintenance ────────────────────────────────────────────────────

    bool pruneOlderThan(int days);
    qint64 databaseSizeBytes() const;

private:
    bool createTables();
    bool exec(const char *sql);
    QString m_dbPath;
    sqlite3 *m_db;
};

#endif // HEALTHSTORE_H
