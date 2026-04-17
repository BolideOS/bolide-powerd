#ifndef COMMON_H
#define COMMON_H

// D-Bus constants
#define POWERD_SERVICE      "org.asteroidos.powerd"
#define POWERD_PATH         "/org/asteroidos/powerd"
#define POWERD_INTERFACE    "org.asteroidos.powerd.ProfileManager"

// Config paths
#define POWERD_CONFIG_DIR   ".config/asteroid-powerd"
#define POWERD_PROFILES_FILE "profiles.json"
#define POWERD_SETTINGS_FILE "settings.json"
#define POWERD_BATTERY_FILE  "battery_history.bin"
#define POWERD_HEALTH_FILE   "battery_health.json"
#define POWERD_DEFAULT_PROFILES_PATH "/usr/share/asteroid-powerd/default-profiles.json"

// Defaults
#define DEFAULT_BATTERY_HISTORY_DAYS 14
#define BATTERY_HEARTBEAT_MINUTES 120   // record history entry every 2 hours
#define BATTERY_CHANGE_THRESHOLD 2
#define BATTERY_POLL_IDLE_MS    120000  // 2 minutes during idle
#define BATTERY_POLL_ACTIVE_MS   30000  // 30 seconds during workout

#endif // COMMON_H
