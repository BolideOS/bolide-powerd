#ifndef SYSFSBACKEND_H
#define SYSFSBACKEND_H

#include "sensorcontroller.h"
#include <QMap>
#include <QString>
#include <QStringList>

class SysfsBackend : public SensorController
{
    Q_OBJECT
    Q_DISABLE_COPY(SysfsBackend)

public:
    explicit SysfsBackend(QObject *parent = nullptr);
    ~SysfsBackend() override;

    // SensorController interface
    bool applyConfig(const SensorConfig &config) override;
    bool setSensorMode(const QString &sensorName, const QString &mode) override;
    SensorConfig currentConfig() const override;
    bool isAvailable(const QString &sensorName) const override;
    QStringList availableSensors() const override;
    int lastHeartRate() const override;
    qint64 lastStepCount() const override;

private:
    struct SysfsNode {
        QString path;
        QString enablePath;
        bool available;
    };

    void discoverSensors();
    bool enableSensor(const QString &sensorName, bool enable);
    bool writeSysfsNode(const QString &path, const QString &value);
    QString readSysfsNode(const QString &path);
    QStringList findIioDevices();
    QString findIioDeviceByName(const QString &name);

    QMap<QString, SysfsNode> m_sensors;
    SensorConfig m_currentConfig;
};

#endif // SYSFSBACKEND_H
