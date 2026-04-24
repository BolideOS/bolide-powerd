#include "sensorfwbackend.h"
#include <QDBusReply>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDBusError>
#include <QTime>
#include <QDebug>

SensorFwBackend::SensorFwBackend(QObject *parent)
    : SensorController(parent)
    , m_gpsPeriodicTimer(new QTimer(this))
    , m_gpsActiveInCycle(false)
    , m_sensorfwAvailable(false)
{
    // Initialize sensor name to SensorFW ID mapping
    m_sensorIdMap[QStringLiteral("accelerometer")] = QStringLiteral("accelerometersensor");
    m_sensorIdMap[QStringLiteral("gyroscope")] = QStringLiteral("gyroscopesensor");
    m_sensorIdMap[QStringLiteral("heart_rate")] = QStringLiteral("heartrate");
    m_sensorIdMap[QStringLiteral("barometer")] = QStringLiteral("pressuresensor");
    m_sensorIdMap[QStringLiteral("compass")] = QStringLiteral("compasssensor");
    m_sensorIdMap[QStringLiteral("ambient_light")] = QStringLiteral("alssensor");

    // GPS periodic timer setup
    m_gpsPeriodicTimer->setInterval(60000); // 60 seconds cycle
    m_gpsPeriodicTimer->setTimerType(Qt::CoarseTimer);
    connect(m_gpsPeriodicTimer, &QTimer::timeout, this, &SensorFwBackend::onGpsTimerTick);

    // Initialize SensorFW connection
    if (!initializeSensorFw()) {
        qWarning() << "SensorFW not available, sensor control will be limited";
    }
}

SensorFwBackend::~SensorFwBackend()
{
    // Stop all active sensors
    for (auto it = m_activeSensors.begin(); it != m_activeSensors.end(); ++it) {
        stopSensor(it.key());
    }
    
    // Stop GPS
    stopGps();
}

bool SensorFwBackend::initializeSensorFw()
{
    // Try to connect to SensorFW manager
    m_sensorManager = QSharedPointer<QDBusInterface>::create(
        QStringLiteral("local.SensorManager"),
        QStringLiteral("/SensorManager"),
        QStringLiteral("local.SensorManager"),
        QDBusConnection::systemBus()
    );

    if (!m_sensorManager->isValid()) {
        qWarning() << "Failed to connect to SensorFW:" << m_sensorManager->lastError().message();
        return false;
    }

    // Query available sensors asynchronously — don't block startup
    QDBusPendingCall pendingCall = m_sensorManager->asyncCall(QStringLiteral("availableSensors"));
    auto *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isValid()) {
            QStringList sensorIds = reply.value();
            for (auto it = m_sensorIdMap.begin(); it != m_sensorIdMap.end(); ++it) {
                if (sensorIds.contains(it.value())) {
                    m_availableSensors.append(it.key());
                }
            }
            qDebug() << "SensorFW available sensors:" << m_availableSensors;
        } else {
            qWarning() << "Failed to query available sensors:" << reply.error().message();
        }
        w->deleteLater();
    });

    m_sensorfwAvailable = true;
    return true;
}

bool SensorFwBackend::applyConfig(const SensorConfig &config)
{
    qDebug() << "Applying sensor configuration";

    bool success = true;

    // Apply each sensor configuration
    success &= setAccelerometerMode(config.accelerometer);
    success &= setGyroscopeMode(config.gyroscope);
    success &= setHeartRateMode(config.heart_rate);
    success &= setHrvMode(config.hrv);
    success &= setSpo2Mode(config.spo2);
    success &= setBarometerMode(config.barometer);
    success &= setCompassMode(config.compass);
    success &= setAmbientLightMode(config.ambient_light);
    success &= setGpsMode(config.gps);

    // Step counter: start/stop alongside accelerometer (steps derived from accel)
    const QString stepSensorId = QStringLiteral("stepcountersensor");
    if (config.accelerometer != SensorMode::Off) {
        // 5-minute polling for step count reads
        startSensor(stepSensorId, 300000);
    } else {
        stopSensor(stepSensorId);
    }

    // Update current config
    m_currentConfig = config;

    return success;
}

bool SensorFwBackend::setSensorMode(const QString &sensorName, const QString &mode)
{
    qDebug() << "Setting sensor" << sensorName << "to mode" << mode;

    if (sensorName == QLatin1String("accelerometer")) {
        return setAccelerometerMode(sensorModeFromString(mode));
    } else if (sensorName == QLatin1String("gyroscope")) {
        return setGyroscopeMode(sensorModeFromString(mode));
    } else if (sensorName == QLatin1String("heart_rate")) {
        return setHeartRateMode(sensorModeFromString(mode));
    } else if (sensorName == QLatin1String("hrv")) {
        return setHrvMode(hrvModeFromString(mode));
    } else if (sensorName == QLatin1String("spo2")) {
        return setSpo2Mode(spo2ModeFromString(mode));
    } else if (sensorName == QLatin1String("barometer")) {
        return setBarometerMode(baroModeFromString(mode));
    } else if (sensorName == QLatin1String("compass")) {
        return setCompassMode(compassModeFromString(mode));
    } else if (sensorName == QLatin1String("ambient_light")) {
        return setAmbientLightMode(ambientLightModeFromString(mode));
    } else if (sensorName == QLatin1String("gps")) {
        return setGpsMode(gpsModeFromString(mode));
    }

    qWarning() << "Unknown sensor name:" << sensorName;
    return false;
}

SensorConfig SensorFwBackend::currentConfig() const
{
    return m_currentConfig;
}

bool SensorFwBackend::isAvailable(const QString &sensorName) const
{
    if (sensorName == QLatin1String("gps")) {
        // GPS availability checked via geoclue
        return true; // Assume available, will fail gracefully if not
    }
    
    if (sensorName == QLatin1String("hrv") || sensorName == QLatin1String("spo2")) {
        // HRV and SpO2 may not be available on all devices
        // For now, return false (will be logged as warning when attempted)
        return false;
    }

    return m_availableSensors.contains(sensorName);
}

QStringList SensorFwBackend::availableSensors() const
{
    return m_availableSensors;
}

int SensorFwBackend::lastHeartRate() const
{
    const QString sensorId = QStringLiteral("heartrate");
    if (!m_activeSensors.contains(sensorId))
        return -1;

    const SensorSession &sess = m_activeSensors[sensorId];
    if (!sess.interface || !sess.interface->isValid() || !sess.running)
        return -1;

    // SensorFW exposes last reading via the "reading" property on the sensor session
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("local.SensorManager"),
        sess.interface->path(),
        QStringLiteral("local.heartrate"),
        QStringLiteral("reading"));
    QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 500);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "Failed to read heartrate:" << reply.errorMessage();
        return -1;
    }

    // SensorFW heartrate reading returns (unsigned, {timestamp, bpm})
    // Try to extract BPM from the reply arguments
    if (reply.arguments().isEmpty())
        return -1;

    // The reply format depends on the SensorFW version/adaptor.
    // hybrishrmadaptor typically returns a struct with timestamp + bpm.
    // Try extracting as int directly first, then dig into structs.
    QVariant arg = reply.arguments().first();
    if (arg.canConvert<int>()) {
        int bpm = arg.toInt();
        return (bpm > 0 && bpm <= 300) ? bpm : -1;
    }

    // Try as QDBusArgument (complex type)
    if (arg.canConvert<QDBusArgument>()) {
        QDBusArgument dbusArg = arg.value<QDBusArgument>();
        dbusArg.beginStructure();
        quint64 timestamp;
        int bpm;
        dbusArg >> timestamp >> bpm;
        dbusArg.endStructure();
        return (bpm > 0 && bpm <= 300) ? bpm : -1;
    }

    return -1;
}

qint64 SensorFwBackend::lastStepCount() const
{
    // Step counter in SensorFW is software-derived from accelerometer.
    // It may not be available as a separate sensor.
    // Try reading from stepcountersensor if it has a session.
    const QString sensorId = QStringLiteral("stepcountersensor");
    if (!m_activeSensors.contains(sensorId))
        return -1;

    const SensorSession &sess = m_activeSensors[sensorId];
    if (!sess.interface || !sess.interface->isValid() || !sess.running)
        return -1;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("local.SensorManager"),
        sess.interface->path(),
        QStringLiteral("local.stepcountersensor"),
        QStringLiteral("reading"));
    QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 500);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "Failed to read step counter:" << reply.errorMessage();
        return -1;
    }

    if (reply.arguments().isEmpty())
        return -1;

    QVariant arg = reply.arguments().first();
    if (arg.canConvert<qint64>())
        return arg.toLongLong();
    if (arg.canConvert<int>())
        return arg.toInt();

    return -1;
}

bool SensorFwBackend::setAccelerometerMode(SensorMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("accelerometer")];
    
    if (mode == SensorMode::Off) {
        m_currentConfig.accelerometer = mode;
        return stopSensor(sensorId);
    }

    int interval = getAccelGyroInterval(mode);
    m_currentConfig.accelerometer = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setGyroscopeMode(SensorMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("gyroscope")];
    
    if (mode == SensorMode::Off) {
        m_currentConfig.gyroscope = mode;
        return stopSensor(sensorId);
    }

    int interval = getAccelGyroInterval(mode);
    m_currentConfig.gyroscope = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setHeartRateMode(SensorMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("heart_rate")];
    
    if (mode == SensorMode::Off) {
        m_currentConfig.heart_rate = mode;
        return stopSensor(sensorId);
    }

    int interval = getHeartRateInterval(mode);
    m_currentConfig.heart_rate = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setHrvMode(HrvMode mode)
{
    m_currentConfig.hrv = mode;

    if (!isAvailable(QStringLiteral("hrv"))) {
        qWarning() << "HRV sensor not available on this device";
        return true; // Not a failure, just not supported
    }

    // HRV implementation would go here
    // For now, just log
    if (mode == HrvMode::Off) {
        qDebug() << "HRV disabled";
    } else if (mode == HrvMode::SleepOnly) {
        qDebug() << "HRV enabled for sleep hours only (22:00-06:00)";
        // Would need timer to enable/disable based on time
    } else if (mode == HrvMode::Always) {
        qDebug() << "HRV enabled continuously";
    }

    return true;
}

bool SensorFwBackend::setSpo2Mode(Spo2Mode mode)
{
    m_currentConfig.spo2 = mode;

    if (!isAvailable(QStringLiteral("spo2"))) {
        qWarning() << "SpO2 sensor not available on this device";
        return true; // Not a failure, just not supported
    }

    // SpO2 implementation would go here
    if (mode == Spo2Mode::Off) {
        qDebug() << "SpO2 disabled";
    } else if (mode == Spo2Mode::Periodic) {
        qDebug() << "SpO2 enabled periodically";
    } else if (mode == Spo2Mode::Continuous) {
        qDebug() << "SpO2 enabled continuously";
    }

    return true;
}

bool SensorFwBackend::setBarometerMode(BaroMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("barometer")];
    
    if (mode == BaroMode::Off) {
        m_currentConfig.barometer = mode;
        return stopSensor(sensorId);
    }

    int interval = getBarometerInterval(mode);
    m_currentConfig.barometer = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setCompassMode(CompassMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("compass")];
    
    if (mode == CompassMode::Off) {
        m_currentConfig.compass = mode;
        return stopSensor(sensorId);
    }

    // For on_demand, we start the sensor but with a longer interval
    // Apps can request faster updates when needed
    int interval = (mode == CompassMode::OnDemand) ? 5000 : 1000;
    m_currentConfig.compass = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setAmbientLightMode(AmbientLightMode mode)
{
    const QString sensorId = m_sensorIdMap[QStringLiteral("ambient_light")];
    
    if (mode == AmbientLightMode::Off) {
        m_currentConfig.ambient_light = mode;
        return stopSensor(sensorId);
    }

    int interval = getAmbientLightInterval(mode);
    m_currentConfig.ambient_light = mode;
    return startSensor(sensorId, interval);
}

bool SensorFwBackend::setGpsMode(GpsMode mode)
{
    m_currentConfig.gps = mode;

    if (mode == GpsMode::Off) {
        return stopGps();
    } else if (mode == GpsMode::Periodic) {
        return startGpsPeriodic();
    } else if (mode == GpsMode::Continuous) {
        return startGpsContinuous();
    }

    return false;
}

bool SensorFwBackend::startSensor(const QString &sensorId, int intervalMs)
{
    if (!m_sensorfwAvailable) {
        qWarning() << "SensorFW not available, cannot start sensor" << sensorId;
        return false;
    }

    // Get or create sensor session
    QSharedPointer<QDBusInterface> session = getSensorSession(sensorId);
    if (!session || !session->isValid()) {
        emit sensorError(sensorId, QStringLiteral("Failed to get sensor session"));
        return false;
    }

    // Set interval (fire-and-forget with error logging)
    QDBusPendingCall intervalCall = session->asyncCall(QStringLiteral("setInterval"), intervalMs);
    auto *intervalWatcher = new QDBusPendingCallWatcher(intervalCall, this);
    connect(intervalWatcher, &QDBusPendingCallWatcher::finished, this, [sensorId](QDBusPendingCallWatcher *w) {
        if (w->isError())
            qWarning() << "Failed to set interval for" << sensorId << ":" << w->error().message();
        w->deleteLater();
    });

    // Start the sensor (fire-and-forget with error logging)
    QDBusPendingCall startCall = session->asyncCall(QStringLiteral("start"));
    auto *startWatcher = new QDBusPendingCallWatcher(startCall, this);
    connect(startWatcher, &QDBusPendingCallWatcher::finished, this, [this, sensorId](QDBusPendingCallWatcher *w) {
        if (w->isError()) {
            qWarning() << "Failed to start sensor" << sensorId << ":" << w->error().message();
            emit sensorError(sensorId, w->error().message());
        }
        w->deleteLater();
    });

    // Track active sensor
    SensorSession &sess = m_activeSensors[sensorId];
    sess.interface = session;
    sess.sensorId = sensorId;
    sess.running = true;
    sess.currentInterval = intervalMs;

    qDebug() << "Started sensor" << sensorId << "with interval" << intervalMs << "ms";
    return true;
}

bool SensorFwBackend::stopSensor(const QString &sensorId)
{
    if (!m_activeSensors.contains(sensorId)) {
        // Not running, nothing to do
        return true;
    }

    SensorSession &session = m_activeSensors[sensorId];
    if (session.interface && session.interface->isValid()) {
        QDBusPendingCall stopCall = session.interface->asyncCall(QStringLiteral("stop"));
        auto *watcher = new QDBusPendingCallWatcher(stopCall, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [sensorId](QDBusPendingCallWatcher *w) {
            if (w->isError())
                qWarning() << "Failed to stop sensor" << sensorId << ":" << w->error().message();
            w->deleteLater();
        });
    }

    m_activeSensors.remove(sensorId);
    qDebug() << "Stopped sensor" << sensorId;
    return true;
}

bool SensorFwBackend::setSensorInterval(const QString &sensorId, int intervalMs)
{
    if (!m_activeSensors.contains(sensorId)) {
        qWarning() << "Cannot set interval for inactive sensor" << sensorId;
        return false;
    }

    SensorSession &session = m_activeSensors[sensorId];
    if (!session.interface || !session.interface->isValid()) {
        return false;
    }

    QDBusPendingCall call = session.interface->asyncCall(QStringLiteral("setInterval"), intervalMs);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [sensorId](QDBusPendingCallWatcher *w) {
        if (w->isError())
            qWarning() << "Failed to set interval for" << sensorId << ":" << w->error().message();
        w->deleteLater();
    });

    session.currentInterval = intervalMs;
    return true;
}

QSharedPointer<QDBusInterface> SensorFwBackend::getSensorSession(const QString &sensorId)
{
    // Check if we already have a session
    if (m_activeSensors.contains(sensorId)) {
        return m_activeSensors[sensorId].interface;
    }

    // Create new session via SensorManager
    if (!m_sensorManager || !m_sensorManager->isValid()) {
        qWarning() << "SensorManager not available";
        return nullptr;
    }

    QDBusReply<QDBusObjectPath> reply = m_sensorManager->call(QStringLiteral("loadPlugin"), sensorId);
    if (!reply.isValid()) {
        qWarning() << "Failed to load sensor plugin" << sensorId << ":" << reply.error().message();
        return nullptr;
    }

    QString sessionPath = reply.value().path();
    QSharedPointer<QDBusInterface> session = QSharedPointer<QDBusInterface>::create(
        QStringLiteral("local.SensorManager"),
        sessionPath,
        QStringLiteral("local.") + sensorId,
        QDBusConnection::systemBus()
    );

    if (!session->isValid()) {
        qWarning() << "Failed to create sensor interface for" << sensorId << ":" << session->lastError().message();
        return nullptr;
    }

    return session;
}

bool SensorFwBackend::startGpsContinuous()
{
    // Stop periodic timer if running
    m_gpsPeriodicTimer->stop();
    m_gpsActiveInCycle = false;

    // Use geoclue2 for GPS control
    if (!m_geoclueManager) {
        m_geoclueManager = QSharedPointer<QDBusInterface>::create(
            QStringLiteral("org.freedesktop.GeoClue2"),
            QStringLiteral("/org/freedesktop/GeoClue2/Manager"),
            QStringLiteral("org.freedesktop.GeoClue2.Manager"),
            QDBusConnection::systemBus()
        );
    }

    if (!m_geoclueManager->isValid()) {
        qWarning() << "GeoClue2 not available:" << m_geoclueManager->lastError().message();
        emit sensorError(QStringLiteral("gps"), QStringLiteral("GeoClue2 not available"));
        return false;
    }

    // Get client
    QDBusReply<QDBusObjectPath> clientReply = m_geoclueManager->call(QStringLiteral("GetClient"));
    if (!clientReply.isValid()) {
        qWarning() << "Failed to get GeoClue2 client:" << clientReply.error().message();
        return false;
    }

    QString clientPath = clientReply.value().path();
    m_geoclueClient = QSharedPointer<QDBusInterface>::create(
        QStringLiteral("org.freedesktop.GeoClue2"),
        clientPath,
        QStringLiteral("org.freedesktop.GeoClue2.Client"),
        QDBusConnection::systemBus()
    );

    if (!m_geoclueClient->isValid()) {
        qWarning() << "Failed to create GeoClue2 client interface";
        return false;
    }

    // Set desired accuracy to EXACT (GPS)
    m_geoclueClient->setProperty("DesiredAccuracy", 8); // EXACT = 8

    // Start location updates (async — don't block main loop)
    QDBusPendingCall startCall = m_geoclueClient->asyncCall(QStringLiteral("Start"));
    auto *watcher = new QDBusPendingCallWatcher(startCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        if (w->isError()) {
            qWarning() << "Failed to start GPS:" << w->error().message();
            emit sensorError(QStringLiteral("gps"), w->error().message());
        }
        w->deleteLater();
    });

    qDebug() << "GPS started in continuous mode";
    return true;
}

bool SensorFwBackend::startGpsPeriodic()
{
    // Periodic mode: GPS on for 10 seconds every 60 seconds
    m_gpsPeriodicTimer->stop();
    m_gpsActiveInCycle = false;

    // Start first cycle immediately
    startGpsContinuous();
    m_gpsActiveInCycle = true;

    // Set timer to turn off after 10 seconds, then cycle every 60 seconds
    QTimer::singleShot(10000, this, [this]() {
        stopGps();
        m_gpsActiveInCycle = false;
        m_gpsPeriodicTimer->start(); // Start 60-second cycle timer
    });

    qDebug() << "GPS started in periodic mode (10s on / 50s off)";
    return true;
}

bool SensorFwBackend::stopGps()
{
    m_gpsPeriodicTimer->stop();
    m_gpsActiveInCycle = false;

    if (m_geoclueClient && m_geoclueClient->isValid()) {
        QDBusPendingCall stopCall = m_geoclueClient->asyncCall(QStringLiteral("Stop"));
        auto *watcher = new QDBusPendingCallWatcher(stopCall, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
            if (w->isError())
                qWarning() << "Failed to stop GPS:" << w->error().message();
            w->deleteLater();
        });
    }

    m_geoclueClient.clear();
    qDebug() << "GPS stopped";
    return true;
}

void SensorFwBackend::onGpsTimerTick()
{
    // Periodic timer fired - start GPS for 10 seconds
    if (!m_gpsActiveInCycle) {
        startGpsContinuous();
        m_gpsActiveInCycle = true;

        QTimer::singleShot(10000, this, [this]() {
            stopGps();
            m_gpsActiveInCycle = false;
        });
    }
}

int SensorFwBackend::getAccelGyroInterval(SensorMode mode) const
{
    switch (mode) {
    case SensorMode::Low:     return 200;  // 5 Hz
    case SensorMode::Medium:  return 40;   // 25 Hz
    case SensorMode::High:    return 20;   // 50 Hz
    case SensorMode::Workout: return 10;   // 100 Hz (max)
    case SensorMode::Off:
    default:                  return 1000;
    }
}

int SensorFwBackend::getHeartRateInterval(SensorMode mode) const
{
    switch (mode) {
    case SensorMode::Low:     return 1800000; // 30 minutes
    case SensorMode::Medium:  return 300000;  // 5 minutes
    case SensorMode::High:    return 60000;   // 1 minute
    case SensorMode::Workout: return 1000;    // Continuous (1 second)
    case SensorMode::Off:
    default:                  return 1800000;
    }
}

int SensorFwBackend::getBarometerInterval(BaroMode mode) const
{
    switch (mode) {
    case BaroMode::Low:  return 600000; // 10 minutes
    case BaroMode::High: return 60000;  // 1 minute
    case BaroMode::Off:
    default:             return 600000;
    }
}

int SensorFwBackend::getAmbientLightInterval(AmbientLightMode mode) const
{
    switch (mode) {
    case AmbientLightMode::Low:  return 30000; // 30 seconds
    case AmbientLightMode::High: return 5000;  // 5 seconds
    case AmbientLightMode::Off:
    default:                     return 30000;
    }
}

bool SensorFwBackend::isNightTime() const
{
    QTime now = QTime::currentTime();
    // Night hours: 22:00 to 06:00
    return now >= QTime(22, 0) || now < QTime(6, 0);
}
