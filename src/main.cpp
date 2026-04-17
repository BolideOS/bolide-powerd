#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDir>
#include <QDebug>
#include <csignal>
#include <unistd.h>
#include "profilemanager.h"
#include "dbusinterface.h"
#include "batterymonitor.h"
#include "sensorcontroller.h"
#include "sensorfwbackend.h"
#include "sysfsbackend.h"
#include "radiocontroller.h"
#include "systemcontroller.h"
#include "automationengine.h"
#include "common.h"

// Global pointer for signal handler
static QCoreApplication *g_app = nullptr;

void unixSignalHandler(int /* sig */)
{
    // Only use async-signal-safe functions here
    static const char msg[] = "Received signal, shutting down...\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    if (g_app) {
        g_app->quit();
    }
}

void setupUnixSignalHandlers()
{
    struct sigaction sa;
    sa.sa_handler = unixSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa, nullptr) != 0) {
        qWarning() << "Failed to set SIGTERM handler";
    }
    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        qWarning() << "Failed to set SIGINT handler";
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("asteroid-powerd"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    
    g_app = &app;
    setupUnixSignalHandlers();

    // Config directory: /home/ceres/.config/asteroid-powerd/
    // Use HOME env, fallback to /home/ceres
    QString home = qEnvironmentVariable("HOME");
    if (home.isEmpty()) {
        home = QStringLiteral("/home/ceres");
    }
    QString configDir = home + QLatin1Char('/') + QLatin1String(POWERD_CONFIG_DIR);

    // Ensure config dir exists
    QDir dir;
    if (!dir.mkpath(configDir)) {
        qWarning() << "Failed to create config directory:" << configDir;
    }

    // Core components
    ProfileManager profileManager(configDir);
    if (!profileManager.loadProfiles()) {
        qWarning() << "Failed to load profiles, using defaults";
    }

    // Battery monitor
    BatteryMonitor batteryMonitor(configDir);
    batteryMonitor.start();
    qInfo() << "Battery monitor started, level:" << batteryMonitor.level()
            << "% charging:" << batteryMonitor.charging();

    // Initialize controllers
    // Try SensorFW first, fall back to sysfs if unavailable
    SensorController *sensorController = nullptr;
    {
        SensorFwBackend *sfw = new SensorFwBackend();
        if (!sfw->availableSensors().isEmpty()) {
            qInfo() << "Using SensorFW backend for sensor control";
            sensorController = sfw;
        } else {
            qInfo() << "SensorFW not available, using sysfs fallback";
            delete sfw;
            sensorController = new SysfsBackend();
        }
    }

    RadioController radioController(&app);
    qInfo() << "Radio controller initialized, BLE available:" << radioController.isBleAvailable()
            << "WiFi available:" << radioController.isWifiAvailable();

    SystemController systemController(&app);
    qInfo() << "System controller initialized, MCE available:" << systemController.isMceAvailable()
            << "systemd available:" << systemController.isSystemdAvailable();

    // Automation engine
    AutomationEngine automationEngine(&profileManager, &batteryMonitor);

    // Connect automation engine profile switch to setActiveProfile
    QObject::connect(&automationEngine, &AutomationEngine::profileSwitchRequested,
                     [&profileManager](const QString &profileId, const QString &reason) {
        qInfo() << "Automation switching to profile:" << profileId << "reason:" << reason;
        profileManager.setActiveProfile(profileId);
        profileManager.saveProfiles();
    });

    // Let radio controller know about sleep mode changes
    QObject::connect(&automationEngine, &AutomationEngine::sleepModeChanged,
                     &radioController, &RadioController::setSleepMode);

    // Connect ProfileManager signals to controllers
    QObject::connect(&profileManager, &ProfileManager::activeProfileChanged,
                     [&](const QString &id, const QString &name) {
        qInfo() << "Active profile changed to:" << name << "(" << id << ")";
        
        PowerProfile profile = profileManager.profile(id);
        if (profile.id.isEmpty()) {
            qWarning() << "Failed to get profile" << id;
            return;
        }

        // Apply sensor config
        if (!sensorController->applyConfig(profile.sensors)) {
            qWarning() << "Failed to apply sensor config for profile" << name;
        } else {
            qInfo() << "Applied sensor config for profile" << name;
        }

        // Apply radio config
        if (!radioController.applyConfig(profile.radios)) {
            qWarning() << "Failed to apply radio config for profile" << name;
        } else {
            qInfo() << "Applied radio config for profile" << name;
        }

        // Apply system config
        if (!systemController.applyConfig(profile.system)) {
            qWarning() << "Failed to apply system config for profile" << name;
        } else {
            qInfo() << "Applied system config for profile" << name;
        }

        // Notify SystemController of radio state so WhenRadiosOn btsyncd mode works
        bool radiosEnabled = (profile.radios.ble.state == RadioState::On);
        systemController.updateBackgroundSyncServices(profile.system.background_sync, radiosEnabled);

        // Update battery monitor with active profile
        batteryMonitor.setActiveProfile(id);
    });

    // Connect controller error signals
    QObject::connect(sensorController, &SensorController::sensorError,
                     [](const QString &sensor, const QString &error) {
        qWarning() << "Sensor error:" << sensor << "-" << error;
    });

    QObject::connect(&radioController, &RadioController::radioError,
                     [](const QString &radio, const QString &error) {
        qWarning() << "Radio error:" << radio << "-" << error;
    });

    QObject::connect(&systemController, &SystemController::systemError,
                     [](const QString &component, const QString &error) {
        qWarning() << "System error:" << component << "-" << error;
    });

    // Apply initial profile configuration
    QString activeProfileId = profileManager.activeProfileId();
    PowerProfile activeProfile = profileManager.profile(activeProfileId);
    if (!activeProfile.id.isEmpty()) {
        qInfo() << "Applying initial profile:" << activeProfile.name;
        sensorController->applyConfig(activeProfile.sensors);
        radioController.applyConfig(activeProfile.radios);
        systemController.applyConfig(activeProfile.system);

        // Sync radio state to SystemController for WhenRadiosOn btsyncd mode
        bool radiosOn = (activeProfile.radios.ble.state == RadioState::On);
        systemController.updateBackgroundSyncServices(activeProfile.system.background_sync, radiosOn);

        batteryMonitor.setActiveProfile(activeProfileId);
    }

    // D-Bus registration on system bus
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCritical() << "Cannot connect to system D-Bus:" << bus.lastError().message();
        return 1;
    }

    DBusInterface dbusIface(&profileManager, &batteryMonitor);

    // Relay battery changes to D-Bus signal (must be after dbusIface construction)
    QObject::connect(&batteryMonitor, &BatteryMonitor::significantChange,
                     [&dbusIface](int level, bool charging) {
        qInfo() << "Battery significant change: level=" << level << "% charging=" << charging;
        emit dbusIface.BatteryLevelChanged(level, charging);
    });

    if (!bus.registerService(QLatin1String(POWERD_SERVICE))) {
        qCritical() << "Failed to register D-Bus service:" << bus.lastError().message();
        return 1;
    }

    if (!bus.registerObject(QLatin1String(POWERD_PATH), &profileManager,
            QDBusConnection::ExportAdaptors)) {
        qCritical() << "Failed to register D-Bus object:" << bus.lastError().message();
        bus.unregisterService(QLatin1String(POWERD_SERVICE));
        return 1;
    }

    qInfo() << "asteroid-powerd started";
    qInfo() << "Active profile:" << profileManager.activeProfileId();
    qInfo() << "Total profiles:" << profileManager.profiles().size();
    qInfo() << "Battery level:" << batteryMonitor.level() << "%";

    // Start automation after everything is wired up
    automationEngine.start();

    int ret = app.exec();

    // Cleanup
    automationEngine.stop();
    batteryMonitor.stop();
    
    bus.unregisterObject(QLatin1String(POWERD_PATH));
    bus.unregisterService(QLatin1String(POWERD_SERVICE));

    // Save profiles on exit
    if (!profileManager.saveProfiles()) {
        qWarning() << "Failed to save profiles on exit";
    }

    delete sensorController;
    sensorController = nullptr;

    qInfo() << "asteroid-powerd stopped";
    return ret;
}
