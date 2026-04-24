#ifndef PROFILEMODEL_H
#define PROFILEMODEL_H

#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVector>

// ─── Sensor enums ───────────────────────────────────────────────────────────

enum class SensorMode { Off, Low, Medium, High, Workout };

inline QString sensorModeToString(SensorMode m)
{
    switch (m) {
    case SensorMode::Off:     return QStringLiteral("off");
    case SensorMode::Low:     return QStringLiteral("low");
    case SensorMode::Medium:  return QStringLiteral("medium");
    case SensorMode::High:    return QStringLiteral("high");
    case SensorMode::Workout: return QStringLiteral("workout");
    }
    return QStringLiteral("off");
}

inline SensorMode sensorModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("low"))     return SensorMode::Low;
    if (v == QLatin1String("medium"))  return SensorMode::Medium;
    if (v == QLatin1String("high"))    return SensorMode::High;
    if (v == QLatin1String("workout")) return SensorMode::Workout;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown SensorMode '%s', defaulting to Off", qPrintable(s));
    return SensorMode::Off;
}

enum class HrvMode { Off, SleepOnly, Always };

inline QString hrvModeToString(HrvMode m)
{
    switch (m) {
    case HrvMode::Off:       return QStringLiteral("off");
    case HrvMode::SleepOnly: return QStringLiteral("sleep_only");
    case HrvMode::Always:    return QStringLiteral("always");
    }
    return QStringLiteral("off");
}

inline HrvMode hrvModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("sleep_only")) return HrvMode::SleepOnly;
    if (v == QLatin1String("always"))     return HrvMode::Always;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown HrvMode '%s', defaulting to Off", qPrintable(s));
    return HrvMode::Off;
}

enum class Spo2Mode { Off, Periodic, Continuous };

inline QString spo2ModeToString(Spo2Mode m)
{
    switch (m) {
    case Spo2Mode::Off:        return QStringLiteral("off");
    case Spo2Mode::Periodic:   return QStringLiteral("periodic");
    case Spo2Mode::Continuous: return QStringLiteral("continuous");
    }
    return QStringLiteral("off");
}

inline Spo2Mode spo2ModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("periodic"))   return Spo2Mode::Periodic;
    if (v == QLatin1String("continuous")) return Spo2Mode::Continuous;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown Spo2Mode '%s', defaulting to Off", qPrintable(s));
    return Spo2Mode::Off;
}

enum class BaroMode { Off, Low, High };

inline QString baroModeToString(BaroMode m)
{
    switch (m) {
    case BaroMode::Off:  return QStringLiteral("off");
    case BaroMode::Low:  return QStringLiteral("low");
    case BaroMode::High: return QStringLiteral("high");
    }
    return QStringLiteral("off");
}

inline BaroMode baroModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("low"))  return BaroMode::Low;
    if (v == QLatin1String("high")) return BaroMode::High;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown BaroMode '%s', defaulting to Off", qPrintable(s));
    return BaroMode::Off;
}

enum class CompassMode { Off, OnDemand, Continuous };

inline QString compassModeToString(CompassMode m)
{
    switch (m) {
    case CompassMode::Off:        return QStringLiteral("off");
    case CompassMode::OnDemand:   return QStringLiteral("on_demand");
    case CompassMode::Continuous: return QStringLiteral("continuous");
    }
    return QStringLiteral("off");
}

inline CompassMode compassModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("on_demand"))  return CompassMode::OnDemand;
    if (v == QLatin1String("continuous")) return CompassMode::Continuous;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown CompassMode '%s', defaulting to Off", qPrintable(s));
    return CompassMode::Off;
}

enum class AmbientLightMode { Off, Low, High };

inline QString ambientLightModeToString(AmbientLightMode m)
{
    switch (m) {
    case AmbientLightMode::Off:  return QStringLiteral("off");
    case AmbientLightMode::Low:  return QStringLiteral("low");
    case AmbientLightMode::High: return QStringLiteral("high");
    }
    return QStringLiteral("off");
}

inline AmbientLightMode ambientLightModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("low"))  return AmbientLightMode::Low;
    if (v == QLatin1String("high")) return AmbientLightMode::High;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown AmbientLightMode '%s', defaulting to Off", qPrintable(s));
    return AmbientLightMode::Off;
}

enum class GpsMode { Off, Periodic, Continuous };

inline QString gpsModeToString(GpsMode m)
{
    switch (m) {
    case GpsMode::Off:        return QStringLiteral("off");
    case GpsMode::Periodic:   return QStringLiteral("periodic");
    case GpsMode::Continuous: return QStringLiteral("continuous");
    }
    return QStringLiteral("off");
}

inline GpsMode gpsModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("periodic"))   return GpsMode::Periodic;
    if (v == QLatin1String("continuous")) return GpsMode::Continuous;
    if (v != QLatin1String("off") && !v.isEmpty())
        qWarning("Unknown GpsMode '%s', defaulting to Off", qPrintable(s));
    return GpsMode::Off;
}

// ─── Radio enums ────────────────────────────────────────────────────────────

enum class RadioState { Off, On };

inline QString radioStateToString(RadioState s)
{
    return s == RadioState::On ? QStringLiteral("on") : QStringLiteral("off");
}

inline RadioState radioStateFromString(const QString &s)
{
    if (s.toLower().trimmed() == QLatin1String("on")) return RadioState::On;
    return RadioState::Off;
}

enum class SyncMode { Manual, TimeWindow, Interval };

inline QString syncModeToString(SyncMode m)
{
    switch (m) {
    case SyncMode::Manual:     return QStringLiteral("manual");
    case SyncMode::TimeWindow: return QStringLiteral("time_window");
    case SyncMode::Interval:   return QStringLiteral("interval");
    }
    return QStringLiteral("manual");
}

inline SyncMode syncModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("time_window")) return SyncMode::TimeWindow;
    if (v == QLatin1String("interval"))    return SyncMode::Interval;
    return SyncMode::Manual;
}

enum class LteState { Off, CallsOnly, Always };

inline QString lteStateToString(LteState s)
{
    switch (s) {
    case LteState::Off:       return QStringLiteral("off");
    case LteState::CallsOnly: return QStringLiteral("calls_only");
    case LteState::Always:    return QStringLiteral("always");
    }
    return QStringLiteral("off");
}

inline LteState lteStateFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("calls_only")) return LteState::CallsOnly;
    if (v == QLatin1String("always"))     return LteState::Always;
    return LteState::Off;
}

// ─── System enums ───────────────────────────────────────────────────────────

enum class BackgroundSyncMode { Auto, WhenRadiosOn, Off };

inline QString bgSyncModeToString(BackgroundSyncMode m)
{
    switch (m) {
    case BackgroundSyncMode::Auto:         return QStringLiteral("auto");
    case BackgroundSyncMode::WhenRadiosOn: return QStringLiteral("when_radios_on");
    case BackgroundSyncMode::Off:          return QStringLiteral("off");
    }
    return QStringLiteral("auto");
}

inline BackgroundSyncMode bgSyncModeFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("when_radios_on")) return BackgroundSyncMode::WhenRadiosOn;
    if (v == QLatin1String("off"))            return BackgroundSyncMode::Off;
    return BackgroundSyncMode::Auto;
}

// ─── CPU enums ──────────────────────────────────────────────────────────────

enum class CpuGovernor { Auto, Performance, Schedutil, Ondemand, Powersave };

inline QString cpuGovernorToString(CpuGovernor g)
{
    switch (g) {
    case CpuGovernor::Auto:        return QStringLiteral("auto");
    case CpuGovernor::Performance: return QStringLiteral("performance");
    case CpuGovernor::Schedutil:   return QStringLiteral("schedutil");
    case CpuGovernor::Ondemand:    return QStringLiteral("ondemand");
    case CpuGovernor::Powersave:   return QStringLiteral("powersave");
    }
    return QStringLiteral("auto");
}

inline CpuGovernor cpuGovernorFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("performance")) return CpuGovernor::Performance;
    if (v == QLatin1String("schedutil"))   return CpuGovernor::Schedutil;
    if (v == QLatin1String("ondemand"))    return CpuGovernor::Ondemand;
    if (v == QLatin1String("powersave"))   return CpuGovernor::Powersave;
    return CpuGovernor::Auto;
}

// ─── Process control enums ──────────────────────────────────────────────────

enum class ServiceState { Auto, Started, Stopped };

inline QString serviceStateToString(ServiceState s)
{
    switch (s) {
    case ServiceState::Auto:    return QStringLiteral("auto");
    case ServiceState::Started: return QStringLiteral("started");
    case ServiceState::Stopped: return QStringLiteral("stopped");
    }
    return QStringLiteral("auto");
}

inline ServiceState serviceStateFromString(const QString &s)
{
    const QString v = s.toLower().trimmed();
    if (v == QLatin1String("started")) return ServiceState::Started;
    if (v == QLatin1String("stopped")) return ServiceState::Stopped;
    return ServiceState::Auto;
}

// ─── Config structs ─────────────────────────────────────────────────────────

struct SensorConfig {
    SensorMode accelerometer = SensorMode::Off;
    SensorMode gyroscope     = SensorMode::Off;
    SensorMode heart_rate    = SensorMode::Off;
    HrvMode    hrv           = HrvMode::Off;
    Spo2Mode   spo2          = Spo2Mode::Off;
    BaroMode   barometer     = BaroMode::Off;
    CompassMode compass      = CompassMode::Off;
    AmbientLightMode ambient_light = AmbientLightMode::Off;
    GpsMode    gps           = GpsMode::Off;

    QJsonObject toJson() const;
    static SensorConfig fromJson(const QJsonObject &obj);
    bool operator==(const SensorConfig &o) const;
    bool operator!=(const SensorConfig &o) const { return !(*this == o); }
};

struct RadioEntry {
    RadioState state = RadioState::Off;
    SyncMode   sync_mode = SyncMode::Manual;
    int        interval_hours = 1;
    QString    start_time;
    int        max_duration_minutes = 30;
    bool       disable_during_sleep = false;

    QJsonObject toJson() const;
    static RadioEntry fromJson(const QJsonObject &obj);
    bool operator==(const RadioEntry &o) const;
    bool operator!=(const RadioEntry &o) const { return !(*this == o); }
};

struct LteEntry {
    LteState state = LteState::Off;

    QJsonObject toJson() const;
    static LteEntry fromJson(const QJsonObject &obj);
    bool operator==(const LteEntry &o) const { return state == o.state; }
    bool operator!=(const LteEntry &o) const { return !(*this == o); }
};

struct NfcEntry {
    RadioState state = RadioState::Off;

    QJsonObject toJson() const;
    static NfcEntry fromJson(const QJsonObject &obj);
    bool operator==(const NfcEntry &o) const { return state == o.state; }
    bool operator!=(const NfcEntry &o) const { return !(*this == o); }
};

struct RadioConfig {
    RadioEntry ble;
    RadioEntry wifi;
    LteEntry   lte;
    NfcEntry   nfc;

    QJsonObject toJson() const;
    static RadioConfig fromJson(const QJsonObject &obj);
    bool operator==(const RadioConfig &o) const;
    bool operator!=(const RadioConfig &o) const { return !(*this == o); }
};

struct SystemConfig {
    BackgroundSyncMode background_sync = BackgroundSyncMode::Auto;
    bool always_on_display = false;
    bool tilt_to_wake      = true;

    QJsonObject toJson() const;
    static SystemConfig fromJson(const QJsonObject &obj);
    bool operator==(const SystemConfig &o) const;
    bool operator!=(const SystemConfig &o) const { return !(*this == o); }
};

struct CpuConfig {
    CpuGovernor governor = CpuGovernor::Auto;
    int max_cores = 0;          // 0 = auto (use boot-time default), 1-4 = limit
    bool screen_boost = true;   // Temporarily boost CPU on screen wake

    QJsonObject toJson() const;
    static CpuConfig fromJson(const QJsonObject &obj);
    bool operator==(const CpuConfig &o) const;
    bool operator!=(const CpuConfig &o) const { return !(*this == o); }
};

struct ProcessConfig {
    bool audio_enabled = true;       // Load/unload audio kernel modules
    ServiceState pulseaudio = ServiceState::Auto;
    ServiceState btsyncd    = ServiceState::Auto;
    ServiceState sensorfwd  = ServiceState::Auto;
    ServiceState mce        = ServiceState::Auto;

    QJsonObject toJson() const;
    static ProcessConfig fromJson(const QJsonObject &obj);
    bool operator==(const ProcessConfig &o) const;
    bool operator!=(const ProcessConfig &o) const { return !(*this == o); }
};

// ─── Automation structs ─────────────────────────────────────────────────────

struct BatteryRule {
    int     threshold = 0;
    QString switch_to_profile;

    QJsonObject toJson() const;
    static BatteryRule fromJson(const QJsonObject &obj);
    bool operator==(const BatteryRule &o) const;
    bool operator!=(const BatteryRule &o) const { return !(*this == o); }
};

struct TimeRule {
    QString start;
    QString end;
    QString switch_to_profile;

    QJsonObject toJson() const;
    static TimeRule fromJson(const QJsonObject &obj);
    bool operator==(const TimeRule &o) const;
    bool operator!=(const TimeRule &o) const { return !(*this == o); }
};

struct AutomationConfig {
    QVector<BatteryRule>     battery_rules;
    QVector<TimeRule>        time_rules;
    QMap<QString, QString>   workout_profiles;

    QJsonObject toJson() const;
    static AutomationConfig fromJson(const QJsonObject &obj);
    bool operator==(const AutomationConfig &o) const;
    bool operator!=(const AutomationConfig &o) const { return !(*this == o); }
};

// ─── PowerProfile ───────────────────────────────────────────────────────────

class PowerProfile {
public:
    QString          id;
    QString          name;
    QString          icon;
    QString          color;
    bool             builtin = false;
    SensorConfig     sensors;
    RadioConfig      radios;
    SystemConfig     system;
    CpuConfig        cpu;
    ProcessConfig    processes;
    AutomationConfig automation;

    QJsonObject toJson() const;
    static PowerProfile fromJson(const QJsonObject &obj);
    bool isValid() const;
    bool operator==(const PowerProfile &o) const;
    bool operator!=(const PowerProfile &o) const { return !(*this == o); }
};

Q_DECLARE_METATYPE(PowerProfile)

#endif // PROFILEMODEL_H
