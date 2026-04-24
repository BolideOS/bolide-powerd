#include "healthstore.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>

HealthStore::HealthStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_db(nullptr)
{
}

HealthStore::~HealthStore()
{
    close();
}

bool HealthStore::open()
{
    if (m_db) return true;

    int rc = sqlite3_open(m_dbPath.toUtf8().constData(), &m_db);
    if (rc != SQLITE_OK) {
        qWarning() << "HealthStore: Failed to open database:" << sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // WAL mode — readers don't block writers, minimal fsync
    exec("PRAGMA journal_mode=WAL");
    // Synchronous NORMAL — safe for WAL, avoids fsync on every commit
    exec("PRAGMA synchronous=NORMAL");
    // Temp store in memory
    exec("PRAGMA temp_store=MEMORY");
    // Small cache (watch has limited RAM)
    exec("PRAGMA cache_size=-512"); // 512KB

    if (!createTables()) {
        qWarning() << "HealthStore: Failed to create tables";
        close();
        return false;
    }

    qInfo() << "HealthStore: Opened" << m_dbPath;
    return true;
}

void HealthStore::close()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        qDebug() << "HealthStore: Closed";
    }
}

bool HealthStore::createTables()
{
    bool ok = true;

    ok &= exec(
        "CREATE TABLE IF NOT EXISTS health_samples ("
        "  ts INTEGER NOT NULL,"
        "  metric TEXT NOT NULL,"
        "  value REAL NOT NULL,"
        "  quality INTEGER DEFAULT 0"
        ")");

    ok &= exec(
        "CREATE INDEX IF NOT EXISTS idx_metric_ts "
        "ON health_samples(metric, ts)");

    ok &= exec(
        "CREATE TABLE IF NOT EXISTS health_sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type TEXT NOT NULL,"
        "  start_ts INTEGER NOT NULL,"
        "  end_ts INTEGER,"
        "  metadata TEXT"
        ")");

    ok &= exec(
        "CREATE INDEX IF NOT EXISTS idx_session_type_ts "
        "ON health_sessions(type, start_ts)");

    // Sync watermark — tracks the last-acknowledged timestamp per category
    // so the phone can do incremental (delta) sync instead of re-fetching.
    ok &= exec(
        "CREATE TABLE IF NOT EXISTS sync_watermarks ("
        "  category TEXT PRIMARY KEY,"
        "  acked_ts INTEGER NOT NULL DEFAULT 0"
        ")");

    return ok;
}

// ─── Write ──────────────────────────────────────────────────────────────────

bool HealthStore::writeSamples(const QVector<Sample> &samples)
{
    if (!m_db || samples.isEmpty()) return false;

    exec("BEGIN TRANSACTION");

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "INSERT INTO health_samples (ts, metric, value, quality) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        qWarning() << "HealthStore: prepare failed:" << sqlite3_errmsg(m_db);
        exec("ROLLBACK");
        return false;
    }

    for (const Sample &s : samples) {
        sqlite3_bind_int64(stmt, 1, s.ts);
        sqlite3_bind_text(stmt, 2, s.metric.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, s.value);
        sqlite3_bind_int(stmt, 4, s.quality);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            qWarning() << "HealthStore: insert failed:" << sqlite3_errmsg(m_db);
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    exec("COMMIT");

    qDebug() << "HealthStore: Wrote" << samples.size() << "samples";
    return true;
}

// ─── Read ───────────────────────────────────────────────────────────────────

QJsonArray HealthStore::querySamples(const QString &metric, qint64 fromTs, qint64 toTs,
                                     int maxResults) const
{
    QJsonArray result;
    if (!m_db) return result;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT ts, value, quality FROM health_samples "
        "WHERE metric = ? AND ts BETWEEN ? AND ? "
        "ORDER BY ts ASC LIMIT ?",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, metric.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, fromTs);
    sqlite3_bind_int64(stmt, 3, toTs);
    sqlite3_bind_int(stmt, 4, maxResults);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QJsonObject obj;
        obj[QLatin1String("ts")] = (qint64)sqlite3_column_int64(stmt, 0);
        obj[QLatin1String("value")] = sqlite3_column_double(stmt, 1);
        obj[QLatin1String("quality")] = sqlite3_column_int(stmt, 2);
        result.append(obj);
    }

    sqlite3_finalize(stmt);
    return result;
}

QJsonArray HealthStore::queryLatest(const QString &metric, int count) const
{
    QJsonArray result;
    if (!m_db) return result;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT ts, value, quality FROM health_samples "
        "WHERE metric = ? ORDER BY ts DESC LIMIT ?",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, metric.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, count);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QJsonObject obj;
        obj[QLatin1String("ts")] = (qint64)sqlite3_column_int64(stmt, 0);
        obj[QLatin1String("value")] = sqlite3_column_double(stmt, 1);
        obj[QLatin1String("quality")] = sqlite3_column_int(stmt, 2);
        result.append(obj);
    }

    sqlite3_finalize(stmt);
    return result;
}

QJsonArray HealthStore::queryAggregated(const QString &metric, qint64 fromTs, qint64 toTs,
                                        int bucketMinutes) const
{
    QJsonArray result;
    if (!m_db) return result;

    qint64 bucketMs = (qint64)bucketMinutes * 60 * 1000;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT (ts / ?) * ? AS bucket_ts, "
        "  MIN(value), MAX(value), AVG(value), COUNT(*) "
        "FROM health_samples "
        "WHERE metric = ? AND ts BETWEEN ? AND ? AND quality = 0 "
        "GROUP BY bucket_ts ORDER BY bucket_ts ASC",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int64(stmt, 1, bucketMs);
    sqlite3_bind_int64(stmt, 2, bucketMs);
    sqlite3_bind_text(stmt, 3, metric.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, fromTs);
    sqlite3_bind_int64(stmt, 5, toTs);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QJsonObject obj;
        obj[QLatin1String("ts")] = (qint64)sqlite3_column_int64(stmt, 0);
        obj[QLatin1String("min")] = sqlite3_column_double(stmt, 1);
        obj[QLatin1String("max")] = sqlite3_column_double(stmt, 2);
        obj[QLatin1String("avg")] = sqlite3_column_double(stmt, 3);
        obj[QLatin1String("count")] = sqlite3_column_int(stmt, 4);
        result.append(obj);
    }

    sqlite3_finalize(stmt);
    return result;
}

// ─── Sessions ───────────────────────────────────────────────────────────────

qint64 HealthStore::startSession(const QString &type)
{
    if (!m_db) return -1;

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "INSERT INTO health_sessions (type, start_ts) VALUES (?, ?)",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;

    return sqlite3_last_insert_rowid(m_db);
}

bool HealthStore::endSession(qint64 sessionId, const QJsonObject &metadata)
{
    if (!m_db) return false;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QByteArray metaJson = QJsonDocument(metadata).toJson(QJsonDocument::Compact);

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "UPDATE health_sessions SET end_ts = ?, metadata = ? WHERE id = ?",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, metaJson.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, sessionId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

QJsonArray HealthStore::querySessions(const QString &type, qint64 fromTs, qint64 toTs) const
{
    QJsonArray result;
    if (!m_db) return result;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT id, type, start_ts, end_ts, metadata FROM health_sessions "
        "WHERE type = ? AND start_ts BETWEEN ? AND ? "
        "ORDER BY start_ts DESC",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, fromTs);
    sqlite3_bind_int64(stmt, 3, toTs);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QJsonObject obj;
        obj[QLatin1String("id")] = (qint64)sqlite3_column_int64(stmt, 0);
        obj[QLatin1String("type")] = QString::fromUtf8(
            (const char *)sqlite3_column_text(stmt, 1));
        obj[QLatin1String("start_ts")] = (qint64)sqlite3_column_int64(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            obj[QLatin1String("end_ts")] = (qint64)sqlite3_column_int64(stmt, 3);
        }
        const char *meta = (const char *)sqlite3_column_text(stmt, 4);
        if (meta) {
            obj[QLatin1String("metadata")] = QJsonDocument::fromJson(
                QByteArray(meta)).object();
        }
        result.append(obj);
    }

    sqlite3_finalize(stmt);
    return result;
}

// ─── Sync watermarks ────────────────────────────────────────────────────────

qint64 HealthStore::syncWatermark(const QString &category) const
{
    if (!m_db) return 0;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT acked_ts FROM sync_watermarks WHERE category = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, category.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    qint64 ts = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        ts = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);
    return ts;
}

bool HealthStore::setSyncWatermark(const QString &category, qint64 ackedTs)
{
    if (!m_db) return false;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "INSERT INTO sync_watermarks (category, acked_ts) VALUES (?, ?)"
        " ON CONFLICT(category) DO UPDATE SET acked_ts = excluded.acked_ts",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, category.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, ackedTs);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ─── Maintenance ────────────────────────────────────────────────────────────

bool HealthStore::pruneOlderThan(int days)
{
    if (!m_db) return false;

    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - (qint64)days * 24 * 3600 * 1000;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "DELETE FROM health_samples WHERE ts < ?",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, cutoff);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(m_db);
    sqlite3_finalize(stmt);

    if (deleted > 0) {
        qInfo() << "HealthStore: Pruned" << deleted << "samples older than" << days << "days";
    }

    return rc == SQLITE_DONE;
}

qint64 HealthStore::databaseSizeBytes() const
{
    QFileInfo fi(m_dbPath);
    return fi.exists() ? fi.size() : 0;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

bool HealthStore::exec(const char *sql)
{
    if (!m_db) return false;
    char *err = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        qWarning() << "HealthStore: SQL error:" << (err ? err : "unknown") << "in:" << sql;
        sqlite3_free(err);
        return false;
    }
    return true;
}
