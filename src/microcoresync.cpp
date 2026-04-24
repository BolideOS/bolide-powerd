/*
 * Copyright (C) 2026 BolideOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "microcoresync.h"
#include "healthstore.h"
#include "batterymonitor.h"

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBluetoothUuid>
#include <QBluetoothAddress>
#include <QLowEnergyDescriptorData>
#include <QLowEnergyCharacteristicData>
#include <QDebug>

// ─── UUIDs — must match mc/microcore/lib/modules/watch/data/asteroid_sync_protocol.dart ───
static const QString kBase       = QStringLiteral("0000%1-0000-0000-0000-00a57e401d05");
static QBluetoothUuid svcUuid()   { return QBluetoothUuid(kBase.arg(QStringLiteral("a001"))); }
static QBluetoothUuid ctrlUuid()  { return QBluetoothUuid(kBase.arg(QStringLiteral("a002"))); }
static QBluetoothUuid dataUuid()  { return QBluetoothUuid(kBase.arg(QStringLiteral("a003"))); }
static QBluetoothUuid stateUuid() { return QBluetoothUuid(kBase.arg(QStringLiteral("a004"))); }

// Chunked framing headers.
static const quint8 kFrameSingle = 0x00;
static const quint8 kFrameStart  = 0x01;
static const quint8 kFrameCont   = 0x02;
static const quint8 kFrameEnd    = 0x03;

// ─── Construction / lifecycle ───────────────────────────────────────────────

MicrocoreSync::MicrocoreSync(HealthStore *store,
                             BatteryMonitor *battery,
                             const QString &configDir,
                             QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_battery(battery)
    , m_configDir(configDir)
    , m_inboxDir(configDir + QLatin1String("/microcore-inbox"))
{
    QDir().mkpath(m_inboxDir);
}

MicrocoreSync::~MicrocoreSync()
{
    stop();
}

bool MicrocoreSync::start()
{
    if (m_running) return true;

    if (!buildServer()) {
        qWarning() << "MicrocoreSync: buildServer failed";
        return false;
    }
    m_running = true;

    // Advertising is gated on setBleAllowed(). Don't auto-start.
    qInfo() << "MicrocoreSync: started (advertising gated on active profile BLE state)";
    return true;
}

void MicrocoreSync::stop()
{
    if (!m_running) return;
    stopAdvertising();
    if (m_service)    { m_service->deleteLater();    m_service = nullptr; }
    if (m_controller) { m_controller->deleteLater(); m_controller = nullptr; }
    m_running = false;
}

void MicrocoreSync::setBleAllowed(bool allowed)
{
    if (m_bleAllowed == allowed) return;
    m_bleAllowed = allowed;
    if (!m_running) return;

    if (allowed) {
        qInfo() << "MicrocoreSync: BLE allowed by profile — starting advertising";
        startAdvertising();
    } else {
        qInfo() << "MicrocoreSync: BLE disallowed by profile — stopping advertising + disconnect";
        stopAdvertising();
        if (m_controller &&
            m_controller->state() != QLowEnergyController::UnconnectedState) {
            m_controller->disconnectFromDevice();
        }
    }
}

int MicrocoreSync::pendingItems() const
{
    return m_pendingItems;
}

// ─── GATT server construction ───────────────────────────────────────────────

bool MicrocoreSync::buildServer()
{
    // Peripheral role on the local Bluetooth adapter.
    m_controller = QLowEnergyController::createPeripheral(this);
    if (!m_controller) {
        qWarning() << "MicrocoreSync: createPeripheral returned null";
        return false;
    }

    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &MicrocoreSync::onControllerError);
    connect(m_controller, &QLowEnergyController::stateChanged,
            this, &MicrocoreSync::onControllerStateChanged);
    connect(m_controller, &QLowEnergyController::mtuChanged, this,
            [this](int mtu) {
        // Usable payload = MTU − 3 (ATT opcode + handle) − 1 (our frame header).
        m_effectivePayload = qMax(20, mtu - 4);
        qInfo() << "MicrocoreSync: MTU=" << mtu
                << "effective payload=" << m_effectivePayload;
    });

    // ── Characteristics ──────────────────────────────────────────────────
    // Client Characteristic Configuration Descriptor (CCCD) — BLE requires this
    // for every notifiable characteristic so the central can enable notifications.
    QLowEnergyDescriptorData cccd(
        QBluetoothUuid::ClientCharacteristicConfiguration,
        QByteArray(2, 0));
    cccd.setWritePermissions(true);

    // Control characteristic (write + notify).
    QLowEnergyCharacteristicData ctrlData;
    ctrlData.setUuid(ctrlUuid());
    ctrlData.setProperties(QLowEnergyCharacteristic::Write
                         | QLowEnergyCharacteristic::WriteNoResponse
                         | QLowEnergyCharacteristic::Notify);
    ctrlData.setValueLength(0, 512);
    ctrlData.addDescriptor(cccd);

    // Data channel (write + notify).
    QLowEnergyCharacteristicData dataData;
    dataData.setUuid(dataUuid());
    dataData.setProperties(QLowEnergyCharacteristic::Write
                         | QLowEnergyCharacteristic::WriteNoResponse
                         | QLowEnergyCharacteristic::Notify);
    dataData.setValueLength(0, 512);
    dataData.addDescriptor(cccd);

    // Watch state (read-only snapshot).
    QLowEnergyCharacteristicData stateData;
    stateData.setUuid(stateUuid());
    stateData.setProperties(QLowEnergyCharacteristic::Read);
    stateData.setValue(buildWatchStateBlob());

    // ── Service ──────────────────────────────────────────────────────────
    QLowEnergyServiceData svcData;
    svcData.setType(QLowEnergyServiceData::ServiceTypePrimary);
    svcData.setUuid(svcUuid());
    svcData.addCharacteristic(ctrlData);
    svcData.addCharacteristic(dataData);
    svcData.addCharacteristic(stateData);

    m_service = m_controller->addService(svcData, this);
    if (!m_service) {
        qWarning() << "MicrocoreSync: addService failed";
        return false;
    }

    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, [this](const QLowEnergyCharacteristic &c, const QByteArray &v) {
        // On a peripheral this fires when the central writes to a characteristic.
        if (c.uuid() == ctrlUuid())  onControlWritten(c, v);
        else if (c.uuid() == dataUuid()) onDataWritten(c, v);
    });

    m_controlCh = m_service->characteristic(ctrlUuid());
    m_dataCh    = m_service->characteristic(dataUuid());
    m_stateCh   = m_service->characteristic(stateUuid());
    return true;
}

void MicrocoreSync::startAdvertising()
{
    if (!m_controller || m_advertising) return;
    if (!m_bleAllowed) return;

    QLowEnergyAdvertisingData adv;
    adv.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
    adv.setLocalName(QStringLiteral("BolideOS Watch"));
    adv.setServices({svcUuid()});

    QLowEnergyAdvertisingParameters params;
    params.setMode(QLowEnergyAdvertisingParameters::AdvInd);
    // Low-duty-cycle advertising to keep the radio budget small when no phone
    // is around. 1000–2000 ms intervals are conservative; BlueZ will cap.
    params.setInterval(1000, 2000);

    m_controller->startAdvertising(params, adv, adv);
    m_advertising = true;
}

void MicrocoreSync::stopAdvertising()
{
    if (!m_controller || !m_advertising) return;
    m_controller->stopAdvertising();
    m_advertising = false;
}

// ─── Controller events ──────────────────────────────────────────────────────

void MicrocoreSync::onControllerError(QLowEnergyController::Error e)
{
    qWarning() << "MicrocoreSync: controller error" << e
               << m_controller->errorString();
}

void MicrocoreSync::onControllerStateChanged(QLowEnergyController::ControllerState s)
{
    const bool wasConnected = m_clientConnected;
    m_clientConnected = (s == QLowEnergyController::ConnectedState);
    if (m_clientConnected && !wasConnected) {
        qInfo() << "MicrocoreSync: phone connected";
        m_controlReasm.reset();
        m_dataReasm.reset();
        refreshWatchState();
        emit clientConnected();
    } else if (!m_clientConnected && wasConnected) {
        qInfo() << "MicrocoreSync: phone disconnected";
        emit clientDisconnected();
        // Re-advertise only if still allowed.
        if (m_bleAllowed && m_running) startAdvertising();
    }
}

// ─── Framing ────────────────────────────────────────────────────────────────

QJsonObject MicrocoreSync::feedFrame(Reassembler &r, const QByteArray &frame)
{
    if (frame.isEmpty()) return {};
    const quint8 header  = static_cast<quint8>(frame.at(0));
    const QByteArray pay = frame.mid(1);

    auto decode = [](const QByteArray &bytes) -> QJsonObject {
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "MicrocoreSync: bad JSON —" << err.errorString();
            return {};
        }
        return doc.object();
    };

    static constexpr int kMaxReassemblyBytes = 65536; // 64 KB

    switch (header) {
    case kFrameSingle:
        return decode(pay);
    case kFrameStart:
        r.buffer = pay;
        r.active = true;
        return {};
    case kFrameCont:
        if (!r.active) { qWarning() << "CONT without START"; return {}; }
        if (r.buffer.size() + pay.size() > kMaxReassemblyBytes) {
            qWarning() << "MicrocoreSync: reassembly buffer overflow, dropping";
            r.reset();
            return {};
        }
        r.buffer.append(pay);
        return {};
    case kFrameEnd: {
        if (!r.active) { qWarning() << "END without START"; return {}; }
        r.buffer.append(pay);
        const auto obj = decode(r.buffer);
        r.reset();
        return obj;
    }
    default:
        qWarning() << "MicrocoreSync: unknown frame header" << header;
        return {};
    }
}

void MicrocoreSync::sendFramedOnControl(const QJsonObject &msg)
{
    if (!m_service || !m_clientConnected) return;
    sendFramed(m_service, m_controlCh, msg);
}

void MicrocoreSync::sendFramedOnData(const QJsonObject &msg)
{
    if (!m_service || !m_clientConnected) return;
    sendFramed(m_service, m_dataCh, msg);
}

void MicrocoreSync::sendFramed(QLowEnergyService *svc,
                               const QLowEnergyCharacteristic &ch,
                               const QJsonObject &msg)
{
    const QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    const int maxPayload = qMax(1, m_effectivePayload);

    if (payload.size() <= maxPayload) {
        QByteArray frame;
        frame.reserve(1 + payload.size());
        frame.append(static_cast<char>(kFrameSingle));
        frame.append(payload);
        svc->writeCharacteristic(ch, frame); // notifies subscribed central
        return;
    }

    int offset = 0;
    while (offset < payload.size()) {
        const int remaining = payload.size() - offset;
        const int chunkSize = qMin(remaining, maxPayload);
        const bool isFirst = (offset == 0);
        const bool isLast  = (offset + chunkSize) >= payload.size();
        const quint8 h = isFirst ? kFrameStart : (isLast ? kFrameEnd : kFrameCont);

        QByteArray frame;
        frame.reserve(1 + chunkSize);
        frame.append(static_cast<char>(h));
        frame.append(payload.mid(offset, chunkSize));
        svc->writeCharacteristic(ch, frame);
        offset += chunkSize;
    }
}

// ─── Incoming characteristic writes ────────────────────────────────────────

void MicrocoreSync::onControlWritten(const QLowEnergyCharacteristic &, const QByteArray &v)
{
    const QJsonObject msg = feedFrame(m_controlReasm, v);
    if (!msg.isEmpty()) handleControlMessage(msg);
}

void MicrocoreSync::onDataWritten(const QLowEnergyCharacteristic &, const QByteArray &v)
{
    const QJsonObject msg = feedFrame(m_dataReasm, v);
    if (!msg.isEmpty()) handleDataMessage(msg);
}

// ─── Message dispatch ──────────────────────────────────────────────────────

void MicrocoreSync::handleControlMessage(const QJsonObject &msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();
    qInfo() << "MicrocoreSync: control msg type=" << type;

    if (type == QLatin1String("request_data")) {
        handleRequestData(msg);
    } else if (type == QLatin1String("ack")) {
        handleAck(msg);
    } else if (type == QLatin1String("sync_complete")) {
        refreshWatchState(); // pendingItems may have changed after prune.
    } else if (type == QLatin1String("push_weather")
            || type == QLatin1String("push_profile")) {
        persistInbox(type.mid(5), msg); // "push_xxx" → "xxx"
        sendFramedOnControl(QJsonObject{
            {QStringLiteral("type"),  QStringLiteral("watch_ack")},
            {QStringLiteral("of"),    type},
            {QStringLiteral("ts"),    QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    } else if (type == QLatin1String("sync_start")) {
        sendFramedOnControl(QJsonObject{{QStringLiteral("type"), QStringLiteral("sync_ready")}});
    } else {
        qInfo() << "MicrocoreSync: ignoring unknown control msg" << type;
    }
}

void MicrocoreSync::handleDataMessage(const QJsonObject &msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();
    qInfo() << "MicrocoreSync: data msg type=" << type
            << "size=" << QJsonDocument(msg).toJson(QJsonDocument::Compact).size();

    if (type == QLatin1String("push_workouts")) {
        persistInbox(QStringLiteral("workouts"), msg);
        sendFramedOnData(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("watch_ack")},
            {QStringLiteral("of"),   type},
            {QStringLiteral("ts"),   QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    } else {
        qInfo() << "MicrocoreSync: ignoring unknown data msg" << type;
    }
}

void MicrocoreSync::handleRequestData(const QJsonObject &msg)
{
    const QString category = msg.value(QStringLiteral("category")).toString();
    const QString sinceStr = msg.value(QStringLiteral("since")).toString();

    qint64 sinceMs = 0;
    if (!sinceStr.isEmpty()) {
        const auto dt = QDateTime::fromString(sinceStr, Qt::ISODate);
        if (dt.isValid()) sinceMs = dt.toMSecsSinceEpoch();
    }

    // Use the stored sync watermark as a floor so we never re-send
    // data the phone has already acknowledged.
    if (m_store) {
        if (!m_store->isOpen()) m_store->open();
        qint64 watermark = m_store->syncWatermark(category);
        if (watermark > sinceMs) sinceMs = watermark;
    }

    QJsonObject resp;
    if      (category == QLatin1String("hr"))                resp = buildHrResponse(sinceMs);
    else if (category == QLatin1String("steps"))             resp = buildStepsResponse(sinceMs);
    else if (category == QLatin1String("sleep"))             resp = buildSleepResponse(sinceMs);
    else if (category == QLatin1String("measurements"))      resp = buildMeasurementsResponse(sinceMs);
    else if (category == QLatin1String("workout_sessions"))  resp = buildWorkoutSessionsResponse(sinceMs);
    else {
        sendFramedOnControl(QJsonObject{
            {QStringLiteral("type"),     QStringLiteral("error")},
            {QStringLiteral("category"), category},
            {QStringLiteral("reason"),   QStringLiteral("unknown_category")}});
        return;
    }

    // Large payloads always go on the data channel.
    sendFramedOnData(resp);
}

void MicrocoreSync::handleAck(const QJsonObject &msg)
{
    // Phone has durably stored category data up to `before` ISO time.
    // Record the watermark so next sync is incremental (delta only).
    const QString category = msg.value(QStringLiteral("category")).toString();
    const QString beforeStr = msg.value(QStringLiteral("before")).toString();
    if (!category.isEmpty() && !beforeStr.isEmpty()) {
        const auto dt = QDateTime::fromString(beforeStr, Qt::ISODate);
        if (dt.isValid() && m_store) {
            if (!m_store->isOpen()) m_store->open();
            m_store->setSyncWatermark(category, dt.toMSecsSinceEpoch());
            qInfo() << "MicrocoreSync: watermark" << category << "->" << beforeStr;
        }
    }
    m_lastClientAckMs = QDateTime::currentMSecsSinceEpoch();
    refreshWatchState();
}

// ─── Inbox persistence ─────────────────────────────────────────────────────

void MicrocoreSync::persistInbox(const QString &category, const QJsonObject &msg)
{
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    const QString path = QStringLiteral("%1/%2-%3.json")
                             .arg(m_inboxDir, category, QString::number(ts));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "MicrocoreSync: cannot write inbox file" << path;
        return;
    }
    f.write(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    f.close();
    emit inboxMessageReceived(category);
}

// ─── DB-backed response builders ───────────────────────────────────────────
//
// These ONLY READ from HealthStore. They never touch SensorController.
// If HealthMonitor hasn't written any samples because the active profile
// disables that metric, the response is legitimately empty — the phone
// should accept that as "no data in the requested window".

QJsonObject MicrocoreSync::buildHrResponse(qint64 sinceMs) const
{
    QJsonArray samples;
    if (m_store && m_store->isOpen()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 fromMs = (sinceMs > 0) ? sinceMs : nowMs - qint64(7) * 86400 * 1000;
        // Cap at 5000 samples per sync to keep BLE transfer bounded.
        const QJsonArray raw = m_store->querySamples(
            QStringLiteral("hr"), fromMs, nowMs, 5000);
        for (const auto &v : raw) {
            const auto obj = v.toObject();
            samples.append(QJsonObject{
                {QStringLiteral("t"),
                 QDateTime::fromMSecsSinceEpoch(obj.value(QStringLiteral("ts")).toVariant().toLongLong())
                     .toUTC().toString(Qt::ISODate)},
                {QStringLiteral("v"), obj.value(QStringLiteral("value")).toDouble()}});
        }
    }
    return QJsonObject{
        {QStringLiteral("type"),    QStringLiteral("data_hr")},
        {QStringLiteral("samples"), samples}};
}

QJsonObject MicrocoreSync::buildStepsResponse(qint64 sinceMs) const
{
    // Aggregate the "steps" metric into per-day totals. HealthStore stores
    // step counter deltas as samples; we sum within each local calendar day.
    QJsonArray days;
    if (m_store && m_store->isOpen()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 fromMs = (sinceMs > 0) ? sinceMs : nowMs - qint64(30) * 86400 * 1000;
        const QJsonArray raw = m_store->querySamples(
            QStringLiteral("steps"), fromMs, nowMs, 10000);

        QMap<QString, double> byDate;
        for (const auto &v : raw) {
            const auto obj = v.toObject();
            const qint64 ts = obj.value(QStringLiteral("ts")).toVariant().toLongLong();
            const QString date = QDateTime::fromMSecsSinceEpoch(ts)
                                     .toLocalTime().date().toString(Qt::ISODate);
            byDate[date] += obj.value(QStringLiteral("value")).toDouble();
        }
        for (auto it = byDate.cbegin(); it != byDate.cend(); ++it) {
            days.append(QJsonObject{
                {QStringLiteral("date"),  it.key()},
                {QStringLiteral("steps"), qRound(it.value())}});
        }
    }
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("data_steps")},
        {QStringLiteral("days"), days}};
}

QJsonObject MicrocoreSync::buildSleepResponse(qint64) const
{
    // bolide-powerd does not compute sleep — that's bolide-fitness's
    // SleepTracker via its own D-Bus. Return empty; the phone accepts this.
    return QJsonObject{
        {QStringLiteral("type"),   QStringLiteral("data_sleep")},
        {QStringLiteral("nights"), QJsonArray{}}};
}

QJsonObject MicrocoreSync::buildMeasurementsResponse(qint64) const
{
    // Manual measurements are entered via bolide-fitness on the watch.
    // Not available from HealthStore. Return empty.
    return QJsonObject{
        {QStringLiteral("type"),    QStringLiteral("data_measurements")},
        {QStringLiteral("entries"), QJsonArray{}}};
}

QJsonObject MicrocoreSync::buildWorkoutSessionsResponse(qint64) const
{
    // Workout sessions are owned by bolide-fitness. Return empty here; a
    // future enhancement can proxy over the session bus to
    // org.bolide.fitness.Companion.GetRecentWorkouts.
    return QJsonObject{
        {QStringLiteral("type"),     QStringLiteral("data_workout_session")},
        {QStringLiteral("sessions"), QJsonArray{}}};
}

// ─── WatchState characteristic ─────────────────────────────────────────────

QByteArray MicrocoreSync::buildWatchStateBlob() const
{
    QJsonObject state{
        {QStringLiteral("bat"),    m_battery ? m_battery->level() : -1},
        {QStringLiteral("uptime"), static_cast<qint64>(
            QDateTime::currentMSecsSinceEpoch() / 1000)},
        {QStringLiteral("pending"), QJsonObject{
            {QStringLiteral("hr"),    m_pendingItems}}}};
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

void MicrocoreSync::refreshWatchState()
{
    if (!m_service || !m_stateCh.isValid()) return;
    m_service->writeCharacteristic(m_stateCh, buildWatchStateBlob());
}
