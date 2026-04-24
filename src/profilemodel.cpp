#include "profilemodel.h"
#include <QJsonDocument>

// ─── SensorConfig ───────────────────────────────────────────────────────────

QJsonObject SensorConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("accelerometer")] = sensorModeToString(accelerometer);
    o[QStringLiteral("gyroscope")]     = sensorModeToString(gyroscope);
    o[QStringLiteral("heart_rate")]    = sensorModeToString(heart_rate);
    o[QStringLiteral("hrv")]           = hrvModeToString(hrv);
    o[QStringLiteral("spo2")]          = spo2ModeToString(spo2);
    o[QStringLiteral("barometer")]     = baroModeToString(barometer);
    o[QStringLiteral("compass")]       = compassModeToString(compass);
    o[QStringLiteral("ambient_light")] = ambientLightModeToString(ambient_light);
    o[QStringLiteral("gps")]           = gpsModeToString(gps);
    return o;
}

SensorConfig SensorConfig::fromJson(const QJsonObject &obj)
{
    SensorConfig c;
    c.accelerometer = sensorModeFromString(obj.value(QStringLiteral("accelerometer")).toString());
    c.gyroscope     = sensorModeFromString(obj.value(QStringLiteral("gyroscope")).toString());
    c.heart_rate    = sensorModeFromString(obj.value(QStringLiteral("heart_rate")).toString());
    c.hrv           = hrvModeFromString(obj.value(QStringLiteral("hrv")).toString());
    c.spo2          = spo2ModeFromString(obj.value(QStringLiteral("spo2")).toString());
    c.barometer     = baroModeFromString(obj.value(QStringLiteral("barometer")).toString());
    c.compass       = compassModeFromString(obj.value(QStringLiteral("compass")).toString());
    c.ambient_light = ambientLightModeFromString(obj.value(QStringLiteral("ambient_light")).toString());
    c.gps           = gpsModeFromString(obj.value(QStringLiteral("gps")).toString());
    return c;
}

bool SensorConfig::operator==(const SensorConfig &o) const
{
    return accelerometer == o.accelerometer
        && gyroscope     == o.gyroscope
        && heart_rate    == o.heart_rate
        && hrv           == o.hrv
        && spo2          == o.spo2
        && barometer     == o.barometer
        && compass       == o.compass
        && ambient_light == o.ambient_light
        && gps           == o.gps;
}

// ─── RadioEntry ─────────────────────────────────────────────────────────────

QJsonObject RadioEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("state")]     = radioStateToString(state);
    o[QStringLiteral("sync_mode")] = syncModeToString(sync_mode);
    o[QStringLiteral("interval_hours")]        = interval_hours;
    if (!start_time.isEmpty())
        o[QStringLiteral("start_time")]        = start_time;
    o[QStringLiteral("max_duration_minutes")]  = max_duration_minutes;
    o[QStringLiteral("disable_during_sleep")]  = disable_during_sleep;
    return o;
}

RadioEntry RadioEntry::fromJson(const QJsonObject &obj)
{
    RadioEntry e;
    e.state                 = radioStateFromString(obj.value(QStringLiteral("state")).toString());
    e.sync_mode             = syncModeFromString(obj.value(QStringLiteral("sync_mode")).toString());
    e.interval_hours        = obj.value(QStringLiteral("interval_hours")).toInt(1);
    e.start_time            = obj.value(QStringLiteral("start_time")).toString();
    e.max_duration_minutes  = obj.value(QStringLiteral("max_duration_minutes")).toInt(30);
    e.disable_during_sleep  = obj.value(QStringLiteral("disable_during_sleep")).toBool(false);
    return e;
}

bool RadioEntry::operator==(const RadioEntry &o) const
{
    return state == o.state
        && sync_mode == o.sync_mode
        && interval_hours == o.interval_hours
        && start_time == o.start_time
        && max_duration_minutes == o.max_duration_minutes
        && disable_during_sleep == o.disable_during_sleep;
}

// ─── LteEntry ───────────────────────────────────────────────────────────────

QJsonObject LteEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("state")] = lteStateToString(state);
    return o;
}

LteEntry LteEntry::fromJson(const QJsonObject &obj)
{
    LteEntry e;
    e.state = lteStateFromString(obj.value(QStringLiteral("state")).toString());
    return e;
}

// ─── NfcEntry ───────────────────────────────────────────────────────────────

QJsonObject NfcEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("state")] = radioStateToString(state);
    return o;
}

NfcEntry NfcEntry::fromJson(const QJsonObject &obj)
{
    NfcEntry e;
    e.state = radioStateFromString(obj.value(QStringLiteral("state")).toString());
    return e;
}

// ─── RadioConfig ────────────────────────────────────────────────────────────

QJsonObject RadioConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("ble")]  = ble.toJson();
    o[QStringLiteral("wifi")] = wifi.toJson();
    o[QStringLiteral("lte")]  = lte.toJson();
    o[QStringLiteral("nfc")]  = nfc.toJson();
    return o;
}

RadioConfig RadioConfig::fromJson(const QJsonObject &obj)
{
    RadioConfig c;
    c.ble  = RadioEntry::fromJson(obj.value(QStringLiteral("ble")).toObject());
    c.wifi = RadioEntry::fromJson(obj.value(QStringLiteral("wifi")).toObject());
    c.lte  = LteEntry::fromJson(obj.value(QStringLiteral("lte")).toObject());
    c.nfc  = NfcEntry::fromJson(obj.value(QStringLiteral("nfc")).toObject());
    return c;
}

bool RadioConfig::operator==(const RadioConfig &o) const
{
    return ble == o.ble && wifi == o.wifi && lte == o.lte && nfc == o.nfc;
}

// ─── SystemConfig ───────────────────────────────────────────────────────────

QJsonObject SystemConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("background_sync")]    = bgSyncModeToString(background_sync);
    o[QStringLiteral("always_on_display")]  = always_on_display;
    o[QStringLiteral("tilt_to_wake")]       = tilt_to_wake;
    return o;
}

SystemConfig SystemConfig::fromJson(const QJsonObject &obj)
{
    SystemConfig c;
    c.background_sync    = bgSyncModeFromString(obj.value(QStringLiteral("background_sync")).toString());
    c.always_on_display  = obj.value(QStringLiteral("always_on_display")).toBool(false);
    c.tilt_to_wake       = obj.value(QStringLiteral("tilt_to_wake")).toBool(true);
    return c;
}

bool SystemConfig::operator==(const SystemConfig &o) const
{
    return background_sync    == o.background_sync
        && always_on_display  == o.always_on_display
        && tilt_to_wake       == o.tilt_to_wake;
}

// ─── CpuConfig ──────────────────────────────────────────────────────────────

QJsonObject CpuConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("governor")]     = cpuGovernorToString(governor);
    o[QStringLiteral("max_cores")]    = max_cores;
    o[QStringLiteral("screen_boost")] = screen_boost;
    return o;
}

CpuConfig CpuConfig::fromJson(const QJsonObject &obj)
{
    CpuConfig c;
    c.governor     = cpuGovernorFromString(obj.value(QStringLiteral("governor")).toString());
    c.max_cores    = obj.value(QStringLiteral("max_cores")).toInt(0);
    c.screen_boost = obj.value(QStringLiteral("screen_boost")).toBool(true);
    return c;
}

bool CpuConfig::operator==(const CpuConfig &o) const
{
    return governor     == o.governor
        && max_cores    == o.max_cores
        && screen_boost == o.screen_boost;
}

// ─── ProcessConfig ──────────────────────────────────────────────────────────

QJsonObject ProcessConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("audio_enabled")] = audio_enabled;
    o[QStringLiteral("pulseaudio")]    = serviceStateToString(pulseaudio);
    o[QStringLiteral("btsyncd")]       = serviceStateToString(btsyncd);
    o[QStringLiteral("sensorfwd")]     = serviceStateToString(sensorfwd);
    o[QStringLiteral("mce")]           = serviceStateToString(mce);
    return o;
}

ProcessConfig ProcessConfig::fromJson(const QJsonObject &obj)
{
    ProcessConfig c;
    c.audio_enabled = obj.value(QStringLiteral("audio_enabled")).toBool(true);
    c.pulseaudio    = serviceStateFromString(obj.value(QStringLiteral("pulseaudio")).toString());
    c.btsyncd       = serviceStateFromString(obj.value(QStringLiteral("btsyncd")).toString());
    c.sensorfwd     = serviceStateFromString(obj.value(QStringLiteral("sensorfwd")).toString());
    c.mce           = serviceStateFromString(obj.value(QStringLiteral("mce")).toString());
    return c;
}

bool ProcessConfig::operator==(const ProcessConfig &o) const
{
    return audio_enabled == o.audio_enabled
        && pulseaudio    == o.pulseaudio
        && btsyncd       == o.btsyncd
        && sensorfwd     == o.sensorfwd
        && mce           == o.mce;
}

// ─── BatteryRule ────────────────────────────────────────────────────────────

QJsonObject BatteryRule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("threshold")]          = threshold;
    o[QStringLiteral("switch_to_profile")]  = switch_to_profile;
    return o;
}

BatteryRule BatteryRule::fromJson(const QJsonObject &obj)
{
    BatteryRule r;
    r.threshold         = obj.value(QStringLiteral("threshold")).toInt(0);
    r.switch_to_profile = obj.value(QStringLiteral("switch_to_profile")).toString();
    return r;
}

bool BatteryRule::operator==(const BatteryRule &o) const
{
    return threshold == o.threshold
        && switch_to_profile.compare(o.switch_to_profile, Qt::CaseInsensitive) == 0;
}

// ─── TimeRule ───────────────────────────────────────────────────────────────

QJsonObject TimeRule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("start")]              = start;
    o[QStringLiteral("end")]                = end;
    o[QStringLiteral("switch_to_profile")]  = switch_to_profile;
    return o;
}

TimeRule TimeRule::fromJson(const QJsonObject &obj)
{
    TimeRule r;
    r.start              = obj.value(QStringLiteral("start")).toString();
    r.end                = obj.value(QStringLiteral("end")).toString();
    r.switch_to_profile  = obj.value(QStringLiteral("switch_to_profile")).toString();
    return r;
}

bool TimeRule::operator==(const TimeRule &o) const
{
    return start == o.start && end == o.end
        && switch_to_profile.compare(o.switch_to_profile, Qt::CaseInsensitive) == 0;
}

// ─── AutomationConfig ──────────────────────────────────────────────────────

QJsonObject AutomationConfig::toJson() const
{
    QJsonObject o;

    QJsonArray ba;
    for (const auto &r : battery_rules)
        ba.append(r.toJson());
    o[QStringLiteral("battery_rules")] = ba;

    QJsonArray ta;
    for (const auto &r : time_rules)
        ta.append(r.toJson());
    o[QStringLiteral("time_rules")] = ta;

    QJsonObject wp;
    for (auto it = workout_profiles.constBegin(); it != workout_profiles.constEnd(); ++it)
        wp[it.key()] = it.value();
    o[QStringLiteral("workout_profiles")] = wp;

    return o;
}

AutomationConfig AutomationConfig::fromJson(const QJsonObject &obj)
{
    AutomationConfig c;

    const QJsonArray ba = obj.value(QStringLiteral("battery_rules")).toArray();
    for (const auto &v : ba)
        c.battery_rules.append(BatteryRule::fromJson(v.toObject()));

    const QJsonArray ta = obj.value(QStringLiteral("time_rules")).toArray();
    for (const auto &v : ta)
        c.time_rules.append(TimeRule::fromJson(v.toObject()));

    const QJsonObject wp = obj.value(QStringLiteral("workout_profiles")).toObject();
    for (auto it = wp.constBegin(); it != wp.constEnd(); ++it)
        c.workout_profiles[it.key()] = it.value().toString();

    return c;
}

bool AutomationConfig::operator==(const AutomationConfig &o) const
{
    return battery_rules     == o.battery_rules
        && time_rules        == o.time_rules
        && workout_profiles  == o.workout_profiles;
}

// ─── PowerProfile ───────────────────────────────────────────────────────────

QJsonObject PowerProfile::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]         = id;
    o[QStringLiteral("name")]       = name;
    o[QStringLiteral("icon")]       = icon;
    o[QStringLiteral("color")]      = color;
    if (builtin)
        o[QStringLiteral("builtin")] = true;
    o[QStringLiteral("sensors")]    = sensors.toJson();
    o[QStringLiteral("radios")]     = radios.toJson();
    o[QStringLiteral("system")]     = system.toJson();
    o[QStringLiteral("cpu")]        = cpu.toJson();
    o[QStringLiteral("processes")]  = processes.toJson();
    o[QStringLiteral("automation")] = automation.toJson();
    return o;
}

PowerProfile PowerProfile::fromJson(const QJsonObject &obj)
{
    PowerProfile p;
    p.id         = obj.value(QStringLiteral("id")).toString();
    p.name       = obj.value(QStringLiteral("name")).toString();
    p.icon       = obj.value(QStringLiteral("icon")).toString();
    p.color      = obj.value(QStringLiteral("color")).toString();
    p.builtin    = obj.value(QStringLiteral("builtin")).toBool(false);
    p.sensors    = SensorConfig::fromJson(obj.value(QStringLiteral("sensors")).toObject());
    p.radios     = RadioConfig::fromJson(obj.value(QStringLiteral("radios")).toObject());
    p.system     = SystemConfig::fromJson(obj.value(QStringLiteral("system")).toObject());
    p.cpu        = CpuConfig::fromJson(obj.value(QStringLiteral("cpu")).toObject());
    p.processes  = ProcessConfig::fromJson(obj.value(QStringLiteral("processes")).toObject());
    p.automation = AutomationConfig::fromJson(obj.value(QStringLiteral("automation")).toObject());
    return p;
}

bool PowerProfile::isValid() const
{
    return !id.isEmpty() && !name.isEmpty();
}

bool PowerProfile::operator==(const PowerProfile &o) const
{
    return id.compare(o.id, Qt::CaseInsensitive) == 0
        && name       == o.name
        && icon       == o.icon
        && color      == o.color
        && builtin    == o.builtin
        && sensors    == o.sensors
        && radios     == o.radios
        && system     == o.system
        && cpu        == o.cpu
        && processes  == o.processes
        && automation == o.automation;
}
