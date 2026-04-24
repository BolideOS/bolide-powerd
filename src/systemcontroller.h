#ifndef SYSTEMCONTROLLER_H
#define SYSTEMCONTROLLER_H

#include <QObject>
#include <QDBusInterface>
#include <QTimer>
#include "profilemodel.h"

class SystemController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SystemController)

public:
    explicit SystemController(QObject *parent = nullptr);
    ~SystemController() override;

    bool applyConfig(const SystemConfig &config);
    bool applyCpuConfig(const CpuConfig &config);
    bool applyProcessConfig(const ProcessConfig &config);
    SystemConfig currentConfig() const;

    bool setAlwaysOnDisplay(bool enabled);
    bool setTiltToWake(bool enabled);
    bool setBackgroundSync(BackgroundSyncMode mode);

    // CPU control
    bool setCpuGovernor(CpuGovernor governor);
    bool setCpuMaxCores(int maxCores);
    void triggerScreenBoost();

    // Process/service control
    bool setAudioModules(bool enabled);

    // Query availability
    bool isMceAvailable() const;
    bool isSystemdAvailable() const;

    // Called by RadioController when radio state changes
    void updateBackgroundSyncServices(BackgroundSyncMode mode, bool radiosEnabled);

public slots:
    // MCE display state change — triggers screen boost
    void onDisplayStateChanged(const QString &state);

signals:
    void systemError(const QString &component, const QString &error);
    void configApplied(const SystemConfig &config);
    void cpuConfigApplied(const CpuConfig &config);
    void processConfigApplied(const ProcessConfig &config);

private:
    bool setMceConfig(const QString &path, bool value);
    bool controlSystemdService(const QString &service, bool start);
    bool controlUserService(const QString &service, bool start);
    bool callUserSystemd(const QString &method, const QString &unit, const QString &mode);
    bool writeSysfs(const QString &path, const QString &value);
    QString readSysfs(const QString &path);
    QString detectBestGovernor();

    // MCE D-Bus interface for display control
    QDBusInterface *m_mceRequest;

    // systemd D-Bus for system service control
    QDBusInterface *m_systemd;

    SystemConfig m_currentConfig;
    CpuConfig m_currentCpuConfig;
    ProcessConfig m_currentProcessConfig;
    
    // State tracking
    bool m_mceAvailable;
    bool m_systemdAvailable;
    bool m_radiosEnabled;

    // Screen boost
    QTimer m_boostTimer;
    CpuGovernor m_preBoostGovernor;
    int m_preBoostCores;
    bool m_boosting;
};

#endif // SYSTEMCONTROLLER_H
