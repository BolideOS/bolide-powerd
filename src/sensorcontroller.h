#ifndef SENSORCONTROLLER_H
#define SENSORCONTROLLER_H

#include <QObject>
#include "profilemodel.h"

class SensorController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(SensorController)

public:
    explicit SensorController(QObject *parent = nullptr);
    virtual ~SensorController();

    // Apply full sensor config from a profile
    virtual bool applyConfig(const SensorConfig &config) = 0;

    // Control individual sensors
    virtual bool setSensorMode(const QString &sensorName, const QString &mode) = 0;

    // Query current state
    virtual SensorConfig currentConfig() const = 0;
    virtual bool isAvailable(const QString &sensorName) const = 0;
    virtual QStringList availableSensors() const = 0;

    // Read last sensor value (returns -1 if unavailable)
    virtual int lastHeartRate() const = 0;
    virtual qint64 lastStepCount() const = 0;

signals:
    void sensorError(const QString &sensorName, const QString &error);
};

#endif // SENSORCONTROLLER_H
