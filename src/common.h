#ifndef COMMON_H
#define COMMON_H

// D-Bus constants
#define POWERD_SERVICE      "org.bolideos.powerd"
#define POWERD_PATH         "/org/bolideos/powerd"
#define POWERD_INTERFACE    "org.bolideos.powerd.ProfileManager"

// Config paths
#define POWERD_CONFIG_DIR   ".config/bolide-powerd"
#define POWERD_PROFILES_FILE "profiles.json"
#define POWERD_SETTINGS_FILE "settings.json"
#define POWERD_BATTERY_FILE  "battery_history.bin"
#define POWERD_HEALTH_FILE   "battery_health.json"
#define POWERD_DEFAULT_PROFILES_PATH "/usr/share/bolide-powerd/default-profiles.json"

// Defaults
#define DEFAULT_BATTERY_HISTORY_DAYS 14
#define BATTERY_HEARTBEAT_MINUTES 120   // record history entry every 2 hours
#define BATTERY_CHANGE_THRESHOLD 2
#define BATTERY_POLL_IDLE_MS    120000  // 2 minutes during idle
#define BATTERY_POLL_ACTIVE_MS   30000  // 30 seconds during workout

// Software coulomb counting fallback
#define COULOMB_MIN_SOC_SPAN    30      // minimum SoC% drop to produce an estimate
#define COULOMB_MAX_GAP_MS      600000  // 10 min: discard accumulator if gap exceeds this

#endif // COMMON_H
