#include "radiocontroller.h"
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusError>
#include <QDebug>
#include <QDateTime>

RadioController::RadioController(QObject *parent)
    : QObject(parent)
    , m_bluezAdapter(nullptr)
    , m_bluezProperties(nullptr)
    , m_connmanManager(nullptr)
    , m_connmanTechnology(nullptr)
    , m_neardManager(nullptr)
    , m_syncTimer(new QTimer(this))
    , m_sleepMode(false)
    , m_bluezAvailable(false)
    , m_connmanAvailable(false)
    , m_neardAvailable(false)
{
    qDebug() << "RadioController: Initializing radio control subsystem";
    
    QDBusConnection systemBus = QDBusConnection::systemBus();
    
    // Initialize BlueZ adapter interface
    m_bluezAdapter = new QDBusInterface(
        "org.bluez",
        "/org/bluez/hci0",
        "org.bluez.Adapter1",
        systemBus,
        this
    );
    
    // Initialize BlueZ properties interface (for setting Powered property)
    m_bluezProperties = new QDBusInterface(
        "org.bluez",
        "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties",
        systemBus,
        this
    );
    
    if (m_bluezAdapter->isValid() && m_bluezProperties->isValid()) {
        m_bluezAvailable = true;
        qDebug() << "RadioController: BlueZ adapter available";
    } else {
        qWarning() << "RadioController: BlueZ not available:" << m_bluezAdapter->lastError().message();
    }
    
    // Initialize ConnMan manager interface
    m_connmanManager = new QDBusInterface(
        "net.connman",
        "/",
        "net.connman.Manager",
        systemBus,
        this
    );
    
    if (m_connmanManager->isValid()) {
        m_connmanAvailable = true;
        qDebug() << "RadioController: ConnMan available";
        
        // Get WiFi technology interface
        m_connmanTechnology = new QDBusInterface(
            "net.connman",
            "/net/connman/technology/wifi",
            "net.connman.Technology",
            systemBus,
            this
        );
        
        if (!m_connmanTechnology->isValid()) {
            qWarning() << "RadioController: WiFi technology interface not available";
        }
    } else {
        qWarning() << "RadioController: ConnMan not available:" << m_connmanManager->lastError().message();
    }
    
    // Initialize Neard (NFC) interface
    m_neardManager = new QDBusInterface(
        "org.neard",
        "/",
        "org.neard.Manager",
        systemBus,
        this
    );
    
    if (m_neardManager->isValid()) {
        m_neardAvailable = true;
        qDebug() << "RadioController: Neard (NFC) available";
    } else {
        qDebug() << "RadioController: Neard not available (NFC not supported)";
    }
    
    // Connect sync timer
    connect(m_syncTimer, &QTimer::timeout, this, &RadioController::onSyncTimer);
}

RadioController::~RadioController()
{
    stopSyncSchedule();
}

bool RadioController::applyConfig(const RadioConfig &config)
{
    qDebug() << "RadioController: Applying radio configuration";
    
    bool success = true;
    
    // Apply BLE configuration
    bool bleEnabled = (config.ble.state == RadioState::On);
    success &= setBleState(bleEnabled);
    
    // Apply WiFi configuration
    bool wifiEnabled = (config.wifi.state == RadioState::On);
    success &= setWifiState(wifiEnabled);
    
    // LTE
    success &= setLteState(lteStateToString(config.lte.state));
    
    // Apply NFC configuration
    bool nfcEnabled = (config.nfc.state == RadioState::On);
    success &= setNfcState(nfcEnabled);
    
    if (success) {
        m_currentConfig = config;
        
        // Setup sync scheduling based on BLE and WiFi configs
        setupSyncSchedule(config.ble, config.wifi);
    }
    
    return success;
}

RadioConfig RadioController::currentConfig() const
{
    return m_currentConfig;
}

bool RadioController::setBleState(bool enabled)
{
    if (!m_bluezAvailable || !m_bluezProperties || !m_bluezProperties->isValid()) {
        qWarning() << "RadioController: BlueZ not available, cannot set BLE state";
        return false;
    }
    
    qDebug() << "RadioController: Setting BLE to" << (enabled ? "enabled" : "disabled");
    
    // Set Powered property via org.freedesktop.DBus.Properties interface
    QDBusReply<void> reply = m_bluezProperties->call(
        "Set",
        "org.bluez.Adapter1",
        "Powered",
        QVariant::fromValue(QDBusVariant(enabled))
    );
    
    if (!reply.isValid()) {
        qWarning() << "RadioController: Failed to set BLE state:" << reply.error().message();
        emit radioError("ble", reply.error().message());
        return false;
    }
    
    qDebug() << "RadioController: BLE" << (enabled ? "enabled" : "disabled");
    return true;
}

bool RadioController::setWifiState(bool enabled)
{
    if (!m_connmanAvailable || !m_connmanTechnology || !m_connmanTechnology->isValid()) {
        qWarning() << "RadioController: ConnMan WiFi technology not available";
        return false;
    }
    
    qDebug() << "RadioController: Setting WiFi to" << (enabled ? "enabled" : "disabled");
    
    // Set Powered property on WiFi technology
    QDBusReply<void> reply = m_connmanTechnology->call(
        "SetProperty",
        "Powered",
        QVariant::fromValue(QDBusVariant(enabled))
    );
    
    if (!reply.isValid()) {
        qWarning() << "RadioController: Failed to set WiFi state:" << reply.error().message();
        emit radioError("wifi", reply.error().message());
        return false;
    }
    
    qDebug() << "RadioController: WiFi" << (enabled ? "enabled" : "disabled");
    return true;
}

bool RadioController::setLteState(const QString &state)
{
    // LTE/modem control varies by hardware — no generic solution exists yet
    qDebug() << "RadioController: LTE control not available (requested:" << state << ")";
    return true;
}

bool RadioController::setNfcState(bool enabled)
{
    if (!m_neardAvailable || !m_neardManager || !m_neardManager->isValid()) {
        qDebug() << "RadioController: Neard not available, NFC control not supported";
        return true;  // Not an error - NFC might not be present
    }
    
    qDebug() << "RadioController: Setting NFC to" << (enabled ? "enabled" : "disabled");
    
    // Neard adapter management is device-specific; log and continue
    qDebug() << "RadioController: NFC adapter management not yet supported";
    
    return true;
}

void RadioController::triggerSync()
{
    qDebug() << "RadioController: Triggering sync";
    emit syncTriggered();
    
    // Also send D-Bus signal that companion apps can listen to
    QDBusMessage signal = QDBusMessage::createSignal(
        "/org/bolideos/powerd",
        "org.bolideos.powerd.ProfileManager",
        "SyncTriggered"
    );
    QDBusConnection::systemBus().send(signal);
}

bool RadioController::isBleAvailable() const
{
    return m_bluezAvailable;
}

bool RadioController::isWifiAvailable() const
{
    return m_connmanAvailable;
}

void RadioController::setSleepMode(bool sleeping)
{
    qDebug() << "RadioController: Sleep mode" << (sleeping ? "enabled" : "disabled");
    m_sleepMode = sleeping;
}

void RadioController::onSyncTimer()
{
    // Check if we should skip sync due to sleep mode
    bool bleDisableDuringSleep = m_currentConfig.ble.disable_during_sleep;
    bool wifiDisableDuringSleep = m_currentConfig.wifi.disable_during_sleep;
    
    if (m_sleepMode && (bleDisableDuringSleep || wifiDisableDuringSleep)) {
        qDebug() << "RadioController: Skipping sync - sleep mode enabled and disable_during_sleep is true";
        return;
    }
    
    // Trigger the sync
    triggerSync();
}

void RadioController::setupSyncSchedule(const RadioEntry &bleConfig, const RadioEntry &wifiConfig)
{
    stopSyncSchedule();
    
    // Determine which sync mode to use (prefer BLE if both are configured)
    const RadioEntry *activeConfig = nullptr;
    
    if (bleConfig.state == RadioState::On && bleConfig.sync_mode != SyncMode::Manual) {
        activeConfig = &bleConfig;
        qDebug() << "RadioController: Using BLE sync configuration";
    } else if (wifiConfig.state == RadioState::On && wifiConfig.sync_mode != SyncMode::Manual) {
        activeConfig = &wifiConfig;
        qDebug() << "RadioController: Using WiFi sync configuration";
    }
    
    if (!activeConfig) {
        qDebug() << "RadioController: No automatic sync configured";
        return;
    }
    
    if (activeConfig->sync_mode == SyncMode::Interval) {
        // Interval-based sync
        int intervalMs = calculateIntervalMs(activeConfig->interval_hours);
        m_syncTimer->setInterval(intervalMs);
        m_syncTimer->start();
        qDebug() << "RadioController: Sync interval set to" << activeConfig->interval_hours << "hours";
        
    } else if (activeConfig->sync_mode == SyncMode::TimeWindow) {
        // Time window sync
        QTime nextSync = calculateNextWindowStart(activeConfig->start_time);
        QTime now = QTime::currentTime();
        int msUntilSync = now.msecsTo(nextSync);
        
        if (msUntilSync < 0) {
            msUntilSync += 24 * 60 * 60 * 1000;  // Add 24 hours
        }
        
        m_syncTimer->setSingleShot(true);
        m_syncTimer->setInterval(msUntilSync);
        m_syncTimer->start();
        
        qDebug() << "RadioController: Next sync window at" << nextSync.toString()
                 << "(" << (msUntilSync / 1000 / 60) << "minutes from now)";
    }
}

void RadioController::stopSyncSchedule()
{
    if (m_syncTimer->isActive()) {
        m_syncTimer->stop();
        qDebug() << "RadioController: Sync schedule stopped";
    }
}

QTime RadioController::calculateNextWindowStart(const QString &startTime)
{
    QTime target = QTime::fromString(startTime, "HH:mm");
    if (!target.isValid()) {
        qWarning() << "RadioController: Invalid start time" << startTime << ", using 00:00";
        target = QTime(0, 0);
    }
    return target;
}

int RadioController::calculateIntervalMs(int hours)
{
    // Convert hours to milliseconds
    return hours * 60 * 60 * 1000;
}
