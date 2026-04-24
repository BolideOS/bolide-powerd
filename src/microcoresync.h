/*
 * Copyright (C) 2026 BolideOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * microcoresync — BLE GATT server that pairs with the Microcore phone app.
 *
 * Implements the "AsteroidOS Microcore Sync" protocol defined on the phone
 * side at:
 *   mc/microcore/lib/modules/watch/data/asteroid_sync_protocol.dart
 *
 *   Service     0000a001-0000-0000-0000-00a57e401d05  (custom)
 *   Control     0000a002-…                            (write + notify)
 *   Data        0000a003-…                            (write + notify)
 *   State       0000a004-…                            (read)
 *
 * Design goals — all non-negotiable:
 *
 *   1. ZERO sensor side-effects. Subscribing/syncing must never raise a
 *      sensor's duty cycle above what the active power profile has already
 *      allowed. We only READ from HealthStore and serialize whatever is
 *      already there. No RequestSensorAccess calls. No forced high-rate HR.
 *
 *   2. Profile-gated advertising. The GATT server is only advertised when
 *      the active profile has BLE radio state == On. When the active
 *      profile switches BLE off, we immediately tear down advertising and
 *      drop the client connection. Set by calling setBleAllowed(bool).
 *
 *   3. Batched, intermittent. Designed for once-a-day sync sessions. No
 *      keep-alive, no streaming by default. Real-time HR is intentionally
 *      NOT exposed here (use a separate standard HRS 0x180D server if
 *      ever needed, similarly profile-gated).
 *
 *   4. Read-only against the health database by default. Push_* messages
 *      (profile / workouts / weather from the phone) are persisted as
 *      JSON blobs in configDir/microcore-inbox/ for bolide-fitness or
 *      other components to consume asynchronously. We do NOT interpret
 *      them here.
 */

#ifndef MICROCORESYNC_H
#define MICROCORESYNC_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLowEnergyController>
#include <QLowEnergyServiceData>
#include <QLowEnergyAdvertisingData>
#include <QLowEnergyAdvertisingParameters>
#include <QLowEnergyService>

class HealthStore;
class BatteryMonitor;

class MicrocoreSync : public QObject
{
    Q_OBJECT

public:
    explicit MicrocoreSync(HealthStore *store,
                           BatteryMonitor *battery,
                           const QString &configDir,
                           QObject *parent = nullptr);
    ~MicrocoreSync() override;

    /** Start GATT peripheral. Will only advertise once setBleAllowed(true). */
    bool start();
    void stop();

    /**
     * Called when the active profile's BLE radio state changes.
     * true  → advertise the sync service.
     * false → stop advertising and disconnect any client.
     * This is the single choke point that enforces the power-profile
     * contract: we never advertise when BLE is off in the current profile.
     */
    void setBleAllowed(bool allowed);

    /** Pending-items count exposed via the WatchState characteristic. */
    int pendingItems() const;

signals:
    void clientConnected();
    void clientDisconnected();
    void inboxMessageReceived(const QString &category);

private slots:
    void onControllerError(QLowEnergyController::Error e);
    void onControllerStateChanged(QLowEnergyController::ControllerState s);
    void onControlWritten(const QLowEnergyCharacteristic &c, const QByteArray &v);
    void onDataWritten(const QLowEnergyCharacteristic &c, const QByteArray &v);

private:
    // ── Transport ─────────────────────────────────────────────────────────
    bool buildServer();
    void startAdvertising();
    void stopAdvertising();
    void sendFramedOnControl(const QJsonObject &msg);
    void sendFramedOnData(const QJsonObject &msg);
    void sendFramed(QLowEnergyService *svc,
                    const QLowEnergyCharacteristic &ch,
                    const QJsonObject &msg);

    struct Reassembler {
        QByteArray buffer;
        bool active = false;
        void reset() { buffer.clear(); active = false; }
    };
    // Return a parsed JSON object if the frame completed a message.
    QJsonObject feedFrame(Reassembler &r, const QByteArray &frame);

    // ── Message dispatch ─────────────────────────────────────────────────
    void handleControlMessage(const QJsonObject &msg);
    void handleDataMessage(const QJsonObject &msg);

    void handleRequestData(const QJsonObject &msg);
    void handleAck(const QJsonObject &msg);

    void persistInbox(const QString &category, const QJsonObject &msg);

    // ── Responses (DB-backed, no sensor side-effects) ────────────────────
    QJsonObject buildHrResponse(qint64 sinceMs) const;
    QJsonObject buildStepsResponse(qint64 sinceMs) const;
    QJsonObject buildSleepResponse(qint64 sinceMs) const;
    QJsonObject buildMeasurementsResponse(qint64 sinceMs) const;
    QJsonObject buildWorkoutSessionsResponse(qint64 sinceMs) const;

    // WatchState characteristic payload (quick-read summary).
    QByteArray buildWatchStateBlob() const;
    void refreshWatchState();

    // ── Members ───────────────────────────────────────────────────────────
    HealthStore    *m_store;
    BatteryMonitor *m_battery;
    QString         m_configDir;
    QString         m_inboxDir;

    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService    *m_service    = nullptr;
    QLowEnergyCharacteristic m_controlCh;
    QLowEnergyCharacteristic m_dataCh;
    QLowEnergyCharacteristic m_stateCh;

    Reassembler m_controlReasm;
    Reassembler m_dataReasm;

    bool m_bleAllowed = false;
    bool m_running    = false;
    bool m_advertising = false;
    bool m_clientConnected = false;

    int  m_pendingItems = 0;
    qint64 m_lastClientAckMs = 0;

    // Effective ATT MTU − 3 (opcode + handle) − 1 (our frame header).
    // Default to a conservative value; will be updated on MTU negotiation.
    int m_effectivePayload = 182; // 185 default MTU on many stacks.
};

#endif // MICROCORESYNC_H
