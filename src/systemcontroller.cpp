#include "systemcontroller.h"
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QProcess>

// MCE D-Bus constants
static const char *MCE_SERVICE = "com.nokia.mce";
static const char *MCE_REQUEST_PATH = "/com/nokia/mce/request";
static const char *MCE_REQUEST_INTERFACE = "com.nokia.mce.request";

// MCE configuration paths
static const char *MCE_LOW_POWER_MODE_PATH = "/system/osso/dsm/display/use_low_power_mode";
static const char *MCE_TILT_TO_WAKE_PATH = "/system/osso/dsm/display/tilt_to_wake";

// systemd D-Bus constants
static const char *SYSTEMD_SERVICE = "org.freedesktop.systemd1";
static const char *SYSTEMD_PATH = "/org/freedesktop/systemd1";
static const char *SYSTEMD_MANAGER_INTERFACE = "org.freedesktop.systemd1.Manager";

// Background sync services
static const char *BTSYNCD_SERVICE = "asteroid-btsyncd.service";

SystemController::SystemController(QObject *parent)
    : QObject(parent)
    , m_mceRequest(nullptr)
    , m_systemd(nullptr)
    , m_mceAvailable(false)
    , m_systemdAvailable(false)
    , m_radiosEnabled(false)
    , m_preBoostGovernor(CpuGovernor::Auto)
    , m_preBoostCores(0)
    , m_boosting(false)
{
    // Screen boost timer — restore CPU settings after boost period
    m_boostTimer.setSingleShot(true);
    m_boostTimer.setInterval(3000); // 3 seconds of boost on screen wake
    m_boostTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_boostTimer, &QTimer::timeout, this, [this]() {
        if (m_boosting) {
            m_boosting = false;
            qDebug() << "SystemController: Screen boost ended, restoring governor"
                     << cpuGovernorToString(m_preBoostGovernor);
            setCpuGovernor(m_preBoostGovernor);
            setCpuMaxCores(m_preBoostCores);
        }
    });
    // Initialize MCE D-Bus interface
    m_mceRequest = new QDBusInterface(
        MCE_SERVICE,
        MCE_REQUEST_PATH,
        MCE_REQUEST_INTERFACE,
        QDBusConnection::systemBus(),
        this
    );

    if (m_mceRequest->isValid()) {
        m_mceAvailable = true;
        qDebug() << "SystemController: MCE interface available";
    } else {
        qWarning() << "SystemController: MCE not available:" << m_mceRequest->lastError().message();
        qWarning() << "SystemController: Display control features will be unavailable";
    }

    // Initialize system systemd D-Bus interface
    m_systemd = new QDBusInterface(
        SYSTEMD_SERVICE,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        QDBusConnection::systemBus(),
        this
    );

    if (m_systemd->isValid()) {
        m_systemdAvailable = true;
        qDebug() << "SystemController: systemd interface available";
    } else {
        qWarning() << "SystemController: systemd not available:" << m_systemd->lastError().message();
        qWarning() << "SystemController: Service control features will be unavailable";
    }

    // User service control via busctl (QDBus cannot connect to user bus from root)
    // Verified: busctl --user --address=unix:path=/run/user/1000/bus works from root
    qDebug() << "SystemController: User service control via busctl";

    // Initialize current config to defaults
    m_currentConfig.background_sync = BackgroundSyncMode::Auto;
    m_currentConfig.always_on_display = false;
    m_currentConfig.tilt_to_wake = true;
}

SystemController::~SystemController()
{
    // QObject parent will clean up interfaces
}

bool SystemController::applyConfig(const SystemConfig &config)
{
    qDebug() << "SystemController: Applying system config";
    
    bool success = true;

    // Apply always-on display setting
    if (config.always_on_display != m_currentConfig.always_on_display) {
        if (!setAlwaysOnDisplay(config.always_on_display)) {
            qWarning() << "SystemController: Failed to set always-on display";
            success = false;
        }
    }

    // Apply tilt-to-wake setting
    if (config.tilt_to_wake != m_currentConfig.tilt_to_wake) {
        if (!setTiltToWake(config.tilt_to_wake)) {
            qWarning() << "SystemController: Failed to set tilt-to-wake";
            success = false;
        }
    }

    // Apply background sync setting
    if (config.background_sync != m_currentConfig.background_sync) {
        if (!setBackgroundSync(config.background_sync)) {
            qWarning() << "SystemController: Failed to set background sync mode";
            success = false;
        }
    }

    if (success) {
        m_currentConfig = config;
        emit configApplied(config);
        qDebug() << "SystemController: Config applied successfully";
    }

    return success;
}

SystemConfig SystemController::currentConfig() const
{
    return m_currentConfig;
}

bool SystemController::setAlwaysOnDisplay(bool enabled)
{
    qDebug() << "SystemController: Setting always-on display to" << enabled;

    if (!setMceConfig(MCE_LOW_POWER_MODE_PATH, enabled)) {
        emit systemError(QStringLiteral("always_on_display"), 
                        QStringLiteral("Failed to set MCE low power mode"));
        return false;
    }

    m_currentConfig.always_on_display = enabled;
    return true;
}

bool SystemController::setTiltToWake(bool enabled)
{
    qDebug() << "SystemController: Setting tilt-to-wake to" << enabled;

    if (!setMceConfig(MCE_TILT_TO_WAKE_PATH, enabled)) {
        emit systemError(QStringLiteral("tilt_to_wake"), 
                        QStringLiteral("Failed to set MCE tilt-to-wake"));
        return false;
    }

    m_currentConfig.tilt_to_wake = enabled;
    return true;
}

bool SystemController::setBackgroundSync(BackgroundSyncMode mode)
{
    qDebug() << "SystemController: Setting background sync to" << bgSyncModeToString(mode);

    if (!m_systemdAvailable) {
        qWarning() << "SystemController: Cannot control background sync - systemd unavailable";
        // Don't treat this as fatal - update our state and continue
        m_currentConfig.background_sync = mode;
        return true;
    }

    bool shouldRun = false;

    switch (mode) {
    case BackgroundSyncMode::Auto:
        // Always run in auto mode
        shouldRun = true;
        break;

    case BackgroundSyncMode::WhenRadiosOn:
        // Only run if radios are enabled
        shouldRun = m_radiosEnabled;
        break;

    case BackgroundSyncMode::Off:
        // Never run
        shouldRun = false;
        break;
    }

    // Control the btsyncd service (user service)
    if (!controlUserService(BTSYNCD_SERVICE, shouldRun)) {
        emit systemError(QStringLiteral("background_sync"),
                        QStringLiteral("Failed to control sync service"));
        return false;
    }

    m_currentConfig.background_sync = mode;
    return true;
}

bool SystemController::isMceAvailable() const
{
    return m_mceAvailable;
}

bool SystemController::isSystemdAvailable() const
{
    return m_systemdAvailable;
}

void SystemController::updateBackgroundSyncServices(BackgroundSyncMode mode, bool radiosEnabled)
{
    // Store radio state for WhenRadiosOn mode
    m_radiosEnabled = radiosEnabled;

    // If we're in WhenRadiosOn mode, update service state based on radio state
    if (mode == BackgroundSyncMode::WhenRadiosOn) {
        setBackgroundSync(mode);
    }
}

bool SystemController::setMceConfig(const QString &path, bool value)
{
    if (!m_mceAvailable) {
        qWarning() << "SystemController: MCE not available, cannot set" << path;
        return false;
    }

    if (!m_mceRequest->isValid()) {
        qWarning() << "SystemController: MCE interface invalid";
        return false;
    }

    // MCE set_config method signature: set_config(string path, variant value)
    QDBusReply<void> reply = m_mceRequest->call(
        QStringLiteral("set_config"),
        path,
        QVariant::fromValue(QDBusVariant(value))
    );

    if (!reply.isValid()) {
        qWarning() << "SystemController: MCE set_config failed for" << path
                   << ":" << reply.error().message();
        return false;
    }

    qDebug() << "SystemController: MCE config" << path << "set to" << value;
    return true;
}

bool SystemController::controlSystemdService(const QString &service, bool start)
{
    if (!m_systemdAvailable) {
        qWarning() << "SystemController: systemd not available, cannot control" << service;
        return false;
    }

    if (!m_systemd->isValid()) {
        qWarning() << "SystemController: systemd interface invalid";
        return false;
    }

    const QString method = start ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit");
    const QString mode = QStringLiteral("replace");

    qDebug() << "SystemController:" << (start ? "Starting" : "Stopping") << service;

    QDBusReply<QDBusObjectPath> reply = m_systemd->call(
        method,
        service,
        mode
    );

    if (!reply.isValid()) {
        // Check if the error is because the service is already in the desired state
        const QString errorName = m_systemd->lastError().name();
        if (errorName == QLatin1String("org.freedesktop.systemd1.NoSuchUnit")) {
            qWarning() << "SystemController: Service" << service << "does not exist";
            // Don't treat as fatal - service might not be installed
            return true;
        }
        
        qWarning() << "SystemController: Failed to" << method << service
                   << ":" << m_systemd->lastError().message();
        return false;
    }

    qDebug() << "SystemController: Service" << service << (start ? "started" : "stopped");
    return true;
}

bool SystemController::controlUserService(const QString &service, bool start)
{
    const QString method = start ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit");
    const QString mode = QStringLiteral("replace");

    qDebug() << "SystemController:" << (start ? "Starting" : "Stopping") << "user service" << service;

    // When stopping a service, also stop its companion .socket unit
    // to prevent socket-activation from restarting it.
    if (!start && service.endsWith(QLatin1String(".service"))) {
        QString socketUnit = service;
        socketUnit.replace(QLatin1String(".service"), QLatin1String(".socket"));
        callUserSystemd(QStringLiteral("StopUnit"), socketUnit, mode);
        // Ignore errors — socket may not exist
    }

    bool ok = callUserSystemd(method, service, mode);
    if (!ok) {
        qWarning() << "SystemController: Failed to" << method << "user service" << service;
        return false;
    }

    // When starting, also re-enable the companion socket
    if (start && service.endsWith(QLatin1String(".service"))) {
        QString socketUnit = service;
        socketUnit.replace(QLatin1String(".service"), QLatin1String(".socket"));
        callUserSystemd(QStringLiteral("StartUnit"), socketUnit, mode);
    }

    qDebug() << "SystemController: User service" << service << (start ? "started" : "stopped");
    return true;
}

bool SystemController::callUserSystemd(const QString &method, const QString &unit, const QString &mode)
{
    // Use busctl to talk to the user bus from root
    // busctl --user --address=unix:path=/run/user/1000/bus call \
    //   org.freedesktop.systemd1 /org/freedesktop/systemd1 \
    //   org.freedesktop.systemd1.Manager StartUnit ss "unit" "mode"
    QProcess proc;
    proc.setProgram(QStringLiteral("/usr/bin/busctl"));
    proc.setArguments({
        QStringLiteral("--user"),
        QStringLiteral("--address=unix:path=/run/user/1000/bus"),
        QStringLiteral("call"),
        QStringLiteral("org.freedesktop.systemd1"),
        QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"),
        method,
        QStringLiteral("ss"),
        unit,
        mode
    });
    proc.start();
    if (!proc.waitForFinished(5000)) {
        qWarning() << "SystemController: busctl timed out for" << method << unit;
        return false;
    }
    if (proc.exitCode() != 0) {
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        // NoSuchUnit is not a failure — service simply doesn't exist
        if (err.contains(QLatin1String("NoSuchUnit"))) {
            qDebug() << "SystemController: User unit" << unit << "does not exist (ok)";
            return true;
        }
        qWarning() << "SystemController: busctl failed:" << err;
        return false;
    }
    return true;
}

// ─── Display state handling ─────────────────────────────────────────────────

void SystemController::onDisplayStateChanged(const QString &state)
{
    if (state == QLatin1String("on")) {
        triggerScreenBoost();
    }
}

// ─── Sysfs helpers ──────────────────────────────────────────────────────────

bool SystemController::writeSysfs(const QString &path, const QString &value)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "SystemController: Failed to open" << path << "for writing";
        return false;
    }
    qint64 written = f.write(value.toUtf8());
    bool ok = f.flush();
    if (written < 0 || !ok || f.error() != QFile::NoError) {
        qWarning() << "SystemController: Failed to write" << value << "to" << path
                   << "error:" << f.errorString();
        f.close();
        return false;
    }
    f.close();
    return true;
}

QString SystemController::readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

// ─── CPU Governor control ───────────────────────────────────────────────────

QString SystemController::detectBestGovernor()
{
    QString avail = readSysfs(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors"));
    if (avail.isEmpty()) return QStringLiteral("schedutil");

    // Prefer schedutil > ondemand > conservative > powersave
    for (const auto &gov : {QStringLiteral("schedutil"), QStringLiteral("ondemand"),
                            QStringLiteral("conservative"), QStringLiteral("powersave")}) {
        if (avail.contains(gov)) return gov;
    }
    return QStringLiteral("schedutil");
}

bool SystemController::setCpuGovernor(CpuGovernor governor)
{
    QString govStr;
    switch (governor) {
    case CpuGovernor::Performance: govStr = QStringLiteral("performance"); break;
    case CpuGovernor::Schedutil:   govStr = QStringLiteral("schedutil"); break;
    case CpuGovernor::Ondemand:    govStr = QStringLiteral("ondemand"); break;
    case CpuGovernor::Powersave:   govStr = QStringLiteral("powersave"); break;
    case CpuGovernor::Auto:        govStr = detectBestGovernor(); break;
    }
    
    // Validate that requested governor is actually available on this hardware
    QString avail = readSysfs(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors"));
    if (!avail.isEmpty() && !avail.contains(govStr)) {
        QString fallback = detectBestGovernor();
        qWarning() << "SystemController: Governor" << govStr << "not available (have:" << avail.trimmed()
                   << "), falling back to" << fallback;
        govStr = fallback;
    }

    qDebug() << "SystemController: Setting CPU governor to" << govStr;

    bool success = true;
    QDir cpuDir(QStringLiteral("/sys/devices/system/cpu/"));
    const auto entries = cpuDir.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs);
    for (const QString &cpu : entries) {
        QString path = QStringLiteral("/sys/devices/system/cpu/%1/cpufreq/scaling_governor").arg(cpu);
        if (QFile::exists(path)) {
            if (!writeSysfs(path, govStr)) {
                qWarning() << "SystemController: Failed to set governor for" << cpu;
                success = false;
            }
        }
    }

    if (success) {
        m_currentCpuConfig.governor = governor;
    }
    return success;
}

bool SystemController::setCpuMaxCores(int maxCores)
{
    // 0 = auto (core_ctl manages hotplug), 1-4 = force exactly N cores
    qDebug() << "SystemController: Setting max cores to" << maxCores
             << (maxCores == 0 ? "(core_ctl auto)" : "(manual)");

    bool success = true;
    const QString coreCtlBase = QStringLiteral("/sys/devices/system/cpu/cpu0/core_ctl/");
    bool hasCoreCtl = QFile::exists(coreCtlBase + QStringLiteral("enable"));

    if (maxCores == 0 && hasCoreCtl) {
        // Auto mode: let core_ctl dynamically hotplug cores based on load
        // First bring all cores online so core_ctl can manage them
        QDir cpuDir(QStringLiteral("/sys/devices/system/cpu/"));
        const auto entries = cpuDir.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &cpuName : entries) {
            QString onlinePath = QStringLiteral("/sys/devices/system/cpu/%1/online").arg(cpuName);
            if (!QFile::exists(onlinePath)) continue;
            writeSysfs(onlinePath, QStringLiteral("1"));
        }

        // Configure core_ctl thresholds for responsive auto-scaling
        writeSysfs(coreCtlBase + QStringLiteral("min_cpus"), QStringLiteral("1"));
        writeSysfs(coreCtlBase + QStringLiteral("max_cpus"), QStringLiteral("4"));
        writeSysfs(coreCtlBase + QStringLiteral("busy_up_thres"), QStringLiteral("60 60 60 60"));
        writeSysfs(coreCtlBase + QStringLiteral("busy_down_thres"), QStringLiteral("25 25 25 25"));
        writeSysfs(coreCtlBase + QStringLiteral("offline_delay_ms"), QStringLiteral("100"));
        writeSysfs(coreCtlBase + QStringLiteral("enable"), QStringLiteral("1"));
        qDebug() << "SystemController: core_ctl enabled (min=1, max=4, up=60%, down=25%)";
    } else {
        // Manual mode: disable core_ctl and force exactly N cores
        if (hasCoreCtl) {
            writeSysfs(coreCtlBase + QStringLiteral("enable"), QStringLiteral("0"));
        }

        QDir cpuDir(QStringLiteral("/sys/devices/system/cpu/"));
        const auto entries = cpuDir.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &cpuName : entries) {
            QString onlinePath = QStringLiteral("/sys/devices/system/cpu/%1/online").arg(cpuName);
            if (!QFile::exists(onlinePath)) continue;

            int cpuNum = cpuName.midRef(3).toInt();
            if (cpuNum == 0) continue;

            bool shouldBeOnline = (maxCores <= 0) || (cpuNum < maxCores);
            QString value = shouldBeOnline ? QStringLiteral("1") : QStringLiteral("0");

            if (!writeSysfs(onlinePath, value)) {
                qWarning() << "SystemController: Failed to set" << cpuName << "online=" << value;
                success = false;
            }
        }
    }

    if (success) {
        m_currentCpuConfig.max_cores = maxCores;
    }
    return success;
}

bool SystemController::applyCpuConfig(const CpuConfig &config)
{
    qDebug() << "SystemController: Applying CPU config: governor="
             << cpuGovernorToString(config.governor)
             << "max_cores=" << config.max_cores
             << "screen_boost=" << config.screen_boost;

    bool success = true;

    if (config.governor != m_currentCpuConfig.governor || config.governor == CpuGovernor::Auto) {
        if (!setCpuGovernor(config.governor)) {
            success = false;
        }
    }

    if (config.max_cores != m_currentCpuConfig.max_cores || config.max_cores == 0) {
        if (!setCpuMaxCores(config.max_cores)) {
            success = false;
        }
    }

    m_currentCpuConfig.screen_boost = config.screen_boost;

    if (success) {
        emit cpuConfigApplied(config);
    }
    return success;
}

void SystemController::triggerScreenBoost()
{
    if (!m_currentCpuConfig.screen_boost) return;
    if (m_boosting) {
        // Already boosting — just extend the timer
        m_boostTimer.start();
        return;
    }

    qDebug() << "SystemController: Screen boost triggered";
    m_boosting = true;
    m_preBoostGovernor = m_currentCpuConfig.governor;
    m_preBoostCores = m_currentCpuConfig.max_cores;

    // Temporarily boost: performance governor, all cores forced online
    setCpuGovernor(CpuGovernor::Performance);
    setCpuMaxCores(4); // force all 4 cores online during boost
    m_boostTimer.start();
}

// ─── Audio module control ───────────────────────────────────────────────────

bool SystemController::setAudioModules(bool enabled)
{
    qDebug() << "SystemController: Setting audio modules" << (enabled ? "enabled" : "disabled");

    static const QStringList audioModules = {
        QStringLiteral("audio_tfa98xx"),
        QStringLiteral("audio_machine_msm8909_bg"),
        QStringLiteral("audio_wcd_cpe"),
        QStringLiteral("audio_mbhc"),
        QStringLiteral("audio_wcd9xxx"),
        QStringLiteral("audio_stub"),
        QStringLiteral("audio_swr_ctrl"),
        QStringLiteral("audio_swr"),
        QStringLiteral("audio_pinctrl_wcd"),
        QStringLiteral("audio_machine_ext"),
        QStringLiteral("audio_usf"),
        QStringLiteral("audio_cpe_lsm"),
        QStringLiteral("audio_wcd9335"),
    };

    bool success = true;
    for (const QString &mod : audioModules) {
        QProcess proc;
        if (enabled) {
            proc.start(QStringLiteral("modprobe"), {mod});
        } else {
            proc.start(QStringLiteral("rmmod"), {mod});
        }
        proc.waitForFinished(1000);
        // Don't treat individual module failures as fatal
    }

    return success;
}

// ─── Process config ─────────────────────────────────────────────────────────

bool SystemController::applyProcessConfig(const ProcessConfig &config)
{
    qDebug() << "SystemController: Applying process config";

    bool success = true;

    // Audio modules
    if (config.audio_enabled != m_currentProcessConfig.audio_enabled) {
        setAudioModules(config.audio_enabled);
    }

    // PulseAudio (user service)
    if (config.pulseaudio != m_currentProcessConfig.pulseaudio) {
        bool shouldRun = (config.pulseaudio == ServiceState::Auto) || 
                         (config.pulseaudio == ServiceState::Started);
        controlUserService(QStringLiteral("pulseaudio.service"), shouldRun);
        if (!shouldRun) {
            // Also kill any autospawned PA instances (libpulse --start bypasses systemd)
            QProcess::execute(QStringLiteral("/usr/bin/killall"), {QStringLiteral("-q"), QStringLiteral("pulseaudio")});
        }
    }

    // btsyncd — note: background_sync in SystemConfig also controls this,
    // but ProcessConfig gives explicit override (user service)
    if (config.btsyncd != m_currentProcessConfig.btsyncd) {
        if (config.btsyncd == ServiceState::Stopped) {
            controlUserService(QStringLiteral("asteroid-btsyncd.service"), false);
        } else if (config.btsyncd == ServiceState::Started) {
            controlUserService(QStringLiteral("asteroid-btsyncd.service"), true);
        }
        // Auto = let background_sync handle it
    }

    // sensorfwd (system service — controls sensor hub hardware)
    if (config.sensorfwd != m_currentProcessConfig.sensorfwd) {
        bool shouldRun = (config.sensorfwd == ServiceState::Auto) ||
                         (config.sensorfwd == ServiceState::Started);

        if (shouldRun) {
            // Unmask first so systemd can start it, then start
            QProcess::execute(QStringLiteral("/bin/systemctl"),
                {QStringLiteral("unmask"), QStringLiteral("sensorfwd.service")});
            controlSystemdService(QStringLiteral("sensorfwd.service"), true);
        } else {
            // sensorfwd ignores SIGTERM, so kill it directly and mask to prevent restart
            QProcess::execute(QStringLiteral("/usr/bin/killall"), {QStringLiteral("-9"), QStringLiteral("sensorfwd")});
            QProcess::execute(QStringLiteral("/bin/systemctl"),
                {QStringLiteral("mask"), QStringLiteral("sensorfwd.service")});
            qDebug() << "SystemController: sensorfwd killed and masked";
        }

        // Also control Oppo's deamonserver (sensor hub HAL daemon) which holds
        // /dev/snshub_data open and generates ~1 wakeup/sec via ws_std_sns.
        // It's an Android init service, controlled via setprop ctl.start/stop.
        QProcess::execute(QStringLiteral("/usr/bin/setprop"),
            {shouldRun ? QStringLiteral("ctl.start") : QStringLiteral("ctl.stop"),
             QStringLiteral("deamonserver")});
        qDebug() << "SystemController: deamonserver" << (shouldRun ? "started" : "stopped") << "via setprop";
    }

    m_currentProcessConfig = config;
    emit processConfigApplied(config);
    return success;
}
