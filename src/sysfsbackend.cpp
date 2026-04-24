#include "sysfsbackend.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

SysfsBackend::SysfsBackend(QObject *parent)
    : SensorController(parent)
{
    discoverSensors();
}

SysfsBackend::~SysfsBackend()
{
}

void SysfsBackend::discoverSensors()
{
    // Discover IIO devices
    QStringList iioDevices = findIioDevices();
    
    for (const QString &devicePath : iioDevices) {
        QString namePath = devicePath + "/name";
        QString name = readSysfsNode(namePath).trimmed().toLower();
        
        if (name.isEmpty()) {
            continue;
        }
        
        SysfsNode node;
        node.path = devicePath;
        node.available = true;
        
        // Try common enable paths
        QStringList enablePaths = {
            devicePath + "/enable",
            devicePath + "/power/control",
            devicePath + "/buffer/enable"
        };
        
        for (const QString &enablePath : enablePaths) {
            if (QFile::exists(enablePath)) {
                node.enablePath = enablePath;
                break;
            }
        }
        
        // Map IIO device names to our sensor names
        if (name.contains("accel")) {
            m_sensors["accelerometer"] = node;
            qDebug() << "SysfsBackend: Found accelerometer at" << devicePath;
        } else if (name.contains("gyro")) {
            m_sensors["gyroscope"] = node;
            qDebug() << "SysfsBackend: Found gyroscope at" << devicePath;
        } else if (name.contains("magn") || name.contains("compass")) {
            m_sensors["compass"] = node;
            qDebug() << "SysfsBackend: Found compass at" << devicePath;
        } else if (name.contains("pressure") || name.contains("baro")) {
            m_sensors["barometer"] = node;
            qDebug() << "SysfsBackend: Found barometer at" << devicePath;
        } else if (name.contains("light") || name.contains("als")) {
            m_sensors["ambient_light"] = node;
            qDebug() << "SysfsBackend: Found ambient light sensor at" << devicePath;
        }
    }
    
    // Check for heart rate sensor (device-specific paths)
    QStringList hrPaths = {
        "/sys/class/misc/heartrate",
        "/sys/devices/virtual/misc/heartrate",
        "/sys/class/heart_rate/device"
    };
    
    for (const QString &hrPath : hrPaths) {
        if (QFile::exists(hrPath)) {
            SysfsNode node;
            node.path = hrPath;
            node.available = true;
            
            if (QFile::exists(hrPath + "/enable")) {
                node.enablePath = hrPath + "/enable";
            }
            
            m_sensors["heart_rate"] = node;
            qDebug() << "SysfsBackend: Found heart rate sensor at" << hrPath;
            break;
        }
    }
    
    // GPS is typically a character device, not sysfs
    // Check for common GPS device nodes
    QStringList gpsDevices = {
        "/dev/gps0",
        "/dev/ttymxc2",  // Common on some smartwatches
        "/dev/ttyUSB0"
    };
    
    for (const QString &gpsDevice : gpsDevices) {
        if (QFile::exists(gpsDevice)) {
            SysfsNode node;
            node.path = gpsDevice;
            node.available = true;
            // GPS doesn't have a simple enable file
            m_sensors["gps"] = node;
            qDebug() << "SysfsBackend: Found GPS device at" << gpsDevice;
            break;
        }
    }
    
    if (m_sensors.isEmpty()) {
        qWarning() << "SysfsBackend: No sensors found via sysfs";
    }
}

QStringList SysfsBackend::findIioDevices()
{
    QStringList devices;
    
    // Check /sys/bus/iio/devices/
    QDir iioDir("/sys/bus/iio/devices");
    if (iioDir.exists()) {
        QStringList entries = iioDir.entryList(QStringList() << "iio:device*", QDir::Dirs);
        for (const QString &entry : entries) {
            devices.append(iioDir.absoluteFilePath(entry));
        }
    }
    
    // Also check /sys/class/iio/
    QDir classDir("/sys/class/iio");
    if (classDir.exists()) {
        QStringList entries = classDir.entryList(QStringList() << "iio:device*", QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            QString path = classDir.absoluteFilePath(entry);
            if (!devices.contains(path)) {
                devices.append(path);
            }
        }
    }
    
    return devices;
}

QString SysfsBackend::findIioDeviceByName(const QString &name)
{
    QStringList devices = findIioDevices();
    
    for (const QString &devicePath : devices) {
        QString namePath = devicePath + "/name";
        QString deviceName = readSysfsNode(namePath).trimmed().toLower();
        
        if (deviceName.contains(name.toLower())) {
            return devicePath;
        }
    }
    
    return QString();
}

bool SysfsBackend::applyConfig(const SensorConfig &config)
{
    qDebug() << "SysfsBackend: Applying sensor configuration";
    
    bool success = true;
    
    // Accelerometer
    if (config.accelerometer == SensorMode::Off) {
        success &= enableSensor("accelerometer", false);
    } else {
        success &= enableSensor("accelerometer", true);
    }
    
    // Gyroscope
    if (config.gyroscope == SensorMode::Off) {
        success &= enableSensor("gyroscope", false);
    } else {
        success &= enableSensor("gyroscope", true);
    }
    
    // Heart rate
    if (config.heart_rate == SensorMode::Off) {
        success &= enableSensor("heart_rate", false);
    } else {
        success &= enableSensor("heart_rate", true);
    }
    
    // Barometer
    if (config.barometer == BaroMode::Off) {
        success &= enableSensor("barometer", false);
    } else {
        success &= enableSensor("barometer", true);
    }
    
    // Compass
    if (config.compass == CompassMode::Off) {
        success &= enableSensor("compass", false);
    } else {
        success &= enableSensor("compass", true);
    }
    
    // Ambient light
    if (config.ambient_light == AmbientLightMode::Off) {
        success &= enableSensor("ambient_light", false);
    } else {
        success &= enableSensor("ambient_light", true);
    }
    
    // GPS - just log, can't control easily via sysfs
    if (config.gps != GpsMode::Off) {
        qDebug() << "SysfsBackend: GPS control not supported via sysfs, use geoclue2";
    }
    
    // HRV, SpO2 - log warnings
    if (config.hrv != HrvMode::Off) {
        qWarning() << "SysfsBackend: HRV control not supported via sysfs";
    }
    if (config.spo2 != Spo2Mode::Off) {
        qWarning() << "SysfsBackend: SpO2 control not supported via sysfs";
    }
    
    if (success) {
        m_currentConfig = config;
    }
    
    return success;
}

bool SysfsBackend::setSensorMode(const QString &sensorName, const QString &mode)
{
    qDebug() << "SysfsBackend: Setting sensor" << sensorName << "to mode" << mode;
    
    bool enable = (mode.toLower() != "off");
    bool result = enableSensor(sensorName, enable);
    
    if (result) {
        // Update current config
        if (sensorName == "accelerometer") {
            m_currentConfig.accelerometer = sensorModeFromString(mode);
        } else if (sensorName == "gyroscope") {
            m_currentConfig.gyroscope = sensorModeFromString(mode);
        } else if (sensorName == "heart_rate") {
            m_currentConfig.heart_rate = sensorModeFromString(mode);
        } else if (sensorName == "barometer") {
            m_currentConfig.barometer = baroModeFromString(mode);
        } else if (sensorName == "compass") {
            m_currentConfig.compass = compassModeFromString(mode);
        } else if (sensorName == "ambient_light") {
            m_currentConfig.ambient_light = ambientLightModeFromString(mode);
        } else if (sensorName == "gps") {
            m_currentConfig.gps = gpsModeFromString(mode);
        }
    }
    
    return result;
}

bool SysfsBackend::enableSensor(const QString &sensorName, bool enable)
{
    if (!m_sensors.contains(sensorName)) {
        qDebug() << "SysfsBackend: Sensor" << sensorName << "not available";
        return false;
    }
    
    const SysfsNode &node = m_sensors[sensorName];
    
    if (node.enablePath.isEmpty()) {
        qDebug() << "SysfsBackend: No enable path for sensor" << sensorName;
        // Not an error - sensor might always be on
        return true;
    }
    
    QString value = enable ? "1" : "0";
    bool result = writeSysfsNode(node.enablePath, value);
    
    if (result) {
        qDebug() << "SysfsBackend: Sensor" << sensorName << (enable ? "enabled" : "disabled");
    } else {
        qWarning() << "SysfsBackend: Failed to" << (enable ? "enable" : "disable") << "sensor" << sensorName;
        emit sensorError(sensorName, QString("Failed to write to %1").arg(node.enablePath));
    }
    
    return result;
}

bool SysfsBackend::writeSysfsNode(const QString &path, const QString &value)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "SysfsBackend: Cannot open" << path << "for writing:" << file.errorString();
        return false;
    }
    
    QTextStream out(&file);
    out << value;
    file.close();
    
    if (file.error() != QFile::NoError) {
        qWarning() << "SysfsBackend: Error writing to" << path << ":" << file.errorString();
        return false;
    }
    
    return true;
}

QString SysfsBackend::readSysfsNode(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    
    return content;
}

SensorConfig SysfsBackend::currentConfig() const
{
    return m_currentConfig;
}

bool SysfsBackend::isAvailable(const QString &sensorName) const
{
    return m_sensors.contains(sensorName) && m_sensors[sensorName].available;
}

QStringList SysfsBackend::availableSensors() const
{
    return m_sensors.keys();
}

int SysfsBackend::lastHeartRate() const
{
    // Sysfs backend has no HR sensor paths on beluga (HR is via SensorFW only)
    return -1;
}

qint64 SysfsBackend::lastStepCount() const
{
    // Sysfs backend has no step counter paths on beluga (steps via SensorFW only)
    return -1;
}
