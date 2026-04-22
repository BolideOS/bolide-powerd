#include <QtTest/QtTest>
#include "profilemodel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class TestProfileModel : public QObject
{
    Q_OBJECT

private slots:
    // Enum conversion tests
    void testSensorModeConversion();
    void testHrvModeConversion();
    void testSpo2ModeConversion();
    void testBaroModeConversion();
    void testCompassModeConversion();
    void testAmbientLightModeConversion();
    void testGpsModeConversion();
    void testRadioStateConversion();
    void testSyncModeConversion();
    void testLteStateConversion();
    void testBackgroundSyncModeConversion();

    // Struct serialization tests
    void testSensorConfigDefaults();
    void testSensorConfigJsonRoundTrip();
    void testSensorConfigEquality();
    
    void testRadioEntryDefaults();
    void testRadioEntryJsonRoundTrip();
    void testRadioEntryEquality();
    
    void testLteEntryJsonRoundTrip();
    void testNfcEntryJsonRoundTrip();
    
    void testRadioConfigDefaults();
    void testRadioConfigJsonRoundTrip();
    void testRadioConfigEquality();
    
    void testSystemConfigDefaults();
    void testSystemConfigJsonRoundTrip();
    void testSystemConfigEquality();
    
    void testBatteryRuleJsonRoundTrip();
    void testTimeRuleJsonRoundTrip();
    
    void testAutomationConfigDefaults();
    void testAutomationConfigJsonRoundTrip();
    void testAutomationConfigEquality();
    
    // PowerProfile tests
    void testPowerProfileDefaults();
    void testPowerProfileJsonRoundTrip();
    void testPowerProfileValidation();
    void testPowerProfileEquality();
    void testPowerProfileComplexJson();
    
    // Edge cases
    void testUnknownEnumValues();
    void testEmptyJsonObjects();
};

// ─── Enum Conversion Tests ──────────────────────────────────────────────────

void TestProfileModel::testSensorModeConversion()
{
    QCOMPARE(sensorModeToString(SensorMode::Off), QStringLiteral("off"));
    QCOMPARE(sensorModeToString(SensorMode::Low), QStringLiteral("low"));
    QCOMPARE(sensorModeToString(SensorMode::Medium), QStringLiteral("medium"));
    QCOMPARE(sensorModeToString(SensorMode::High), QStringLiteral("high"));
    QCOMPARE(sensorModeToString(SensorMode::Workout), QStringLiteral("workout"));
    
    QCOMPARE(sensorModeFromString(QStringLiteral("off")), SensorMode::Off);
    QCOMPARE(sensorModeFromString(QStringLiteral("low")), SensorMode::Low);
    QCOMPARE(sensorModeFromString(QStringLiteral("medium")), SensorMode::Medium);
    QCOMPARE(sensorModeFromString(QStringLiteral("high")), SensorMode::High);
    QCOMPARE(sensorModeFromString(QStringLiteral("workout")), SensorMode::Workout);
    
    // Case insensitive
    QCOMPARE(sensorModeFromString(QStringLiteral("LOW")), SensorMode::Low);
    QCOMPARE(sensorModeFromString(QStringLiteral("Medium")), SensorMode::Medium);
    
    // Unknown defaults to Off
    QCOMPARE(sensorModeFromString(QStringLiteral("invalid")), SensorMode::Off);
    QCOMPARE(sensorModeFromString(QString()), SensorMode::Off);
}

void TestProfileModel::testHrvModeConversion()
{
    QCOMPARE(hrvModeToString(HrvMode::Off), QStringLiteral("off"));
    QCOMPARE(hrvModeToString(HrvMode::SleepOnly), QStringLiteral("sleep_only"));
    QCOMPARE(hrvModeToString(HrvMode::Always), QStringLiteral("always"));
    
    QCOMPARE(hrvModeFromString(QStringLiteral("off")), HrvMode::Off);
    QCOMPARE(hrvModeFromString(QStringLiteral("sleep_only")), HrvMode::SleepOnly);
    QCOMPARE(hrvModeFromString(QStringLiteral("always")), HrvMode::Always);
    
    QCOMPARE(hrvModeFromString(QStringLiteral("SLEEP_ONLY")), HrvMode::SleepOnly);
    QCOMPARE(hrvModeFromString(QStringLiteral("invalid")), HrvMode::Off);
}

void TestProfileModel::testSpo2ModeConversion()
{
    QCOMPARE(spo2ModeToString(Spo2Mode::Off), QStringLiteral("off"));
    QCOMPARE(spo2ModeToString(Spo2Mode::Periodic), QStringLiteral("periodic"));
    QCOMPARE(spo2ModeToString(Spo2Mode::Continuous), QStringLiteral("continuous"));
    
    QCOMPARE(spo2ModeFromString(QStringLiteral("off")), Spo2Mode::Off);
    QCOMPARE(spo2ModeFromString(QStringLiteral("periodic")), Spo2Mode::Periodic);
    QCOMPARE(spo2ModeFromString(QStringLiteral("continuous")), Spo2Mode::Continuous);
    
    QCOMPARE(spo2ModeFromString(QStringLiteral("invalid")), Spo2Mode::Off);
}

void TestProfileModel::testBaroModeConversion()
{
    QCOMPARE(baroModeToString(BaroMode::Off), QStringLiteral("off"));
    QCOMPARE(baroModeToString(BaroMode::Low), QStringLiteral("low"));
    QCOMPARE(baroModeToString(BaroMode::High), QStringLiteral("high"));
    
    QCOMPARE(baroModeFromString(QStringLiteral("off")), BaroMode::Off);
    QCOMPARE(baroModeFromString(QStringLiteral("low")), BaroMode::Low);
    QCOMPARE(baroModeFromString(QStringLiteral("high")), BaroMode::High);
    
    QCOMPARE(baroModeFromString(QStringLiteral("invalid")), BaroMode::Off);
}

void TestProfileModel::testCompassModeConversion()
{
    QCOMPARE(compassModeToString(CompassMode::Off), QStringLiteral("off"));
    QCOMPARE(compassModeToString(CompassMode::OnDemand), QStringLiteral("on_demand"));
    QCOMPARE(compassModeToString(CompassMode::Continuous), QStringLiteral("continuous"));
    
    QCOMPARE(compassModeFromString(QStringLiteral("off")), CompassMode::Off);
    QCOMPARE(compassModeFromString(QStringLiteral("on_demand")), CompassMode::OnDemand);
    QCOMPARE(compassModeFromString(QStringLiteral("continuous")), CompassMode::Continuous);
    
    QCOMPARE(compassModeFromString(QStringLiteral("invalid")), CompassMode::Off);
}

void TestProfileModel::testAmbientLightModeConversion()
{
    QCOMPARE(ambientLightModeToString(AmbientLightMode::Off), QStringLiteral("off"));
    QCOMPARE(ambientLightModeToString(AmbientLightMode::Low), QStringLiteral("low"));
    QCOMPARE(ambientLightModeToString(AmbientLightMode::High), QStringLiteral("high"));
    
    QCOMPARE(ambientLightModeFromString(QStringLiteral("off")), AmbientLightMode::Off);
    QCOMPARE(ambientLightModeFromString(QStringLiteral("low")), AmbientLightMode::Low);
    QCOMPARE(ambientLightModeFromString(QStringLiteral("high")), AmbientLightMode::High);
    
    QCOMPARE(ambientLightModeFromString(QStringLiteral("invalid")), AmbientLightMode::Off);
}

void TestProfileModel::testGpsModeConversion()
{
    QCOMPARE(gpsModeToString(GpsMode::Off), QStringLiteral("off"));
    QCOMPARE(gpsModeToString(GpsMode::Periodic), QStringLiteral("periodic"));
    QCOMPARE(gpsModeToString(GpsMode::Continuous), QStringLiteral("continuous"));
    
    QCOMPARE(gpsModeFromString(QStringLiteral("off")), GpsMode::Off);
    QCOMPARE(gpsModeFromString(QStringLiteral("periodic")), GpsMode::Periodic);
    QCOMPARE(gpsModeFromString(QStringLiteral("continuous")), GpsMode::Continuous);
    
    QCOMPARE(gpsModeFromString(QStringLiteral("invalid")), GpsMode::Off);
}

void TestProfileModel::testRadioStateConversion()
{
    QCOMPARE(radioStateToString(RadioState::Off), QStringLiteral("off"));
    QCOMPARE(radioStateToString(RadioState::On), QStringLiteral("on"));
    
    QCOMPARE(radioStateFromString(QStringLiteral("off")), RadioState::Off);
    QCOMPARE(radioStateFromString(QStringLiteral("on")), RadioState::On);
    
    QCOMPARE(radioStateFromString(QStringLiteral("ON")), RadioState::On);
    QCOMPARE(radioStateFromString(QStringLiteral("invalid")), RadioState::Off);
}

void TestProfileModel::testSyncModeConversion()
{
    QCOMPARE(syncModeToString(SyncMode::Manual), QStringLiteral("manual"));
    QCOMPARE(syncModeToString(SyncMode::TimeWindow), QStringLiteral("time_window"));
    QCOMPARE(syncModeToString(SyncMode::Interval), QStringLiteral("interval"));
    
    QCOMPARE(syncModeFromString(QStringLiteral("manual")), SyncMode::Manual);
    QCOMPARE(syncModeFromString(QStringLiteral("time_window")), SyncMode::TimeWindow);
    QCOMPARE(syncModeFromString(QStringLiteral("interval")), SyncMode::Interval);
    
    QCOMPARE(syncModeFromString(QStringLiteral("invalid")), SyncMode::Manual);
}

void TestProfileModel::testLteStateConversion()
{
    QCOMPARE(lteStateToString(LteState::Off), QStringLiteral("off"));
    QCOMPARE(lteStateToString(LteState::CallsOnly), QStringLiteral("calls_only"));
    QCOMPARE(lteStateToString(LteState::Always), QStringLiteral("always"));
    
    QCOMPARE(lteStateFromString(QStringLiteral("off")), LteState::Off);
    QCOMPARE(lteStateFromString(QStringLiteral("calls_only")), LteState::CallsOnly);
    QCOMPARE(lteStateFromString(QStringLiteral("always")), LteState::Always);
    
    QCOMPARE(lteStateFromString(QStringLiteral("invalid")), LteState::Off);
}

void TestProfileModel::testBackgroundSyncModeConversion()
{
    QCOMPARE(bgSyncModeToString(BackgroundSyncMode::Auto), QStringLiteral("auto"));
    QCOMPARE(bgSyncModeToString(BackgroundSyncMode::WhenRadiosOn), QStringLiteral("when_radios_on"));
    QCOMPARE(bgSyncModeToString(BackgroundSyncMode::Off), QStringLiteral("off"));
    
    QCOMPARE(bgSyncModeFromString(QStringLiteral("auto")), BackgroundSyncMode::Auto);
    QCOMPARE(bgSyncModeFromString(QStringLiteral("when_radios_on")), BackgroundSyncMode::WhenRadiosOn);
    QCOMPARE(bgSyncModeFromString(QStringLiteral("off")), BackgroundSyncMode::Off);
    
    QCOMPARE(bgSyncModeFromString(QStringLiteral("invalid")), BackgroundSyncMode::Auto);
}

// ─── SensorConfig Tests ─────────────────────────────────────────────────────

void TestProfileModel::testSensorConfigDefaults()
{
    SensorConfig c;
    QCOMPARE(c.accelerometer, SensorMode::Off);
    QCOMPARE(c.gyroscope, SensorMode::Off);
    QCOMPARE(c.heart_rate, SensorMode::Off);
    QCOMPARE(c.hrv, HrvMode::Off);
    QCOMPARE(c.spo2, Spo2Mode::Off);
    QCOMPARE(c.barometer, BaroMode::Off);
    QCOMPARE(c.compass, CompassMode::Off);
    QCOMPARE(c.ambient_light, AmbientLightMode::Off);
    QCOMPARE(c.gps, GpsMode::Off);
}

void TestProfileModel::testSensorConfigJsonRoundTrip()
{
    SensorConfig original;
    original.accelerometer = SensorMode::Medium;
    original.gyroscope = SensorMode::High;
    original.heart_rate = SensorMode::Workout;
    original.hrv = HrvMode::SleepOnly;
    original.spo2 = Spo2Mode::Periodic;
    original.barometer = BaroMode::Low;
    original.compass = CompassMode::OnDemand;
    original.ambient_light = AmbientLightMode::High;
    original.gps = GpsMode::Continuous;
    
    QJsonObject json = original.toJson();
    SensorConfig restored = SensorConfig::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.accelerometer, SensorMode::Medium);
    QCOMPARE(restored.gyroscope, SensorMode::High);
    QCOMPARE(restored.heart_rate, SensorMode::Workout);
    QCOMPARE(restored.hrv, HrvMode::SleepOnly);
    QCOMPARE(restored.spo2, Spo2Mode::Periodic);
    QCOMPARE(restored.barometer, BaroMode::Low);
    QCOMPARE(restored.compass, CompassMode::OnDemand);
    QCOMPARE(restored.ambient_light, AmbientLightMode::High);
    QCOMPARE(restored.gps, GpsMode::Continuous);
}

void TestProfileModel::testSensorConfigEquality()
{
    SensorConfig c1, c2;
    QCOMPARE(c1, c2);
    
    c1.heart_rate = SensorMode::High;
    QVERIFY(c1 != c2);
    
    c2.heart_rate = SensorMode::High;
    QCOMPARE(c1, c2);
}

// ─── RadioEntry Tests ───────────────────────────────────────────────────────

void TestProfileModel::testRadioEntryDefaults()
{
    RadioEntry e;
    QCOMPARE(e.state, RadioState::Off);
    QCOMPARE(e.sync_mode, SyncMode::Manual);
    QCOMPARE(e.interval_hours, 1);
    QVERIFY(e.start_time.isEmpty());
    QCOMPARE(e.max_duration_minutes, 30);
    QCOMPARE(e.disable_during_sleep, false);
}

void TestProfileModel::testRadioEntryJsonRoundTrip()
{
    RadioEntry original;
    original.state = RadioState::On;
    original.sync_mode = SyncMode::Interval;
    original.interval_hours = 5;
    original.start_time = QStringLiteral("22:00");
    original.max_duration_minutes = 60;
    original.disable_during_sleep = true;
    
    QJsonObject json = original.toJson();
    RadioEntry restored = RadioEntry::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.state, RadioState::On);
    QCOMPARE(restored.sync_mode, SyncMode::Interval);
    QCOMPARE(restored.interval_hours, 5);
    QCOMPARE(restored.start_time, QStringLiteral("22:00"));
    QCOMPARE(restored.max_duration_minutes, 60);
    QCOMPARE(restored.disable_during_sleep, true);
}

void TestProfileModel::testRadioEntryEquality()
{
    RadioEntry e1, e2;
    QCOMPARE(e1, e2);
    
    e1.state = RadioState::On;
    QVERIFY(e1 != e2);
    
    e2.state = RadioState::On;
    QCOMPARE(e1, e2);
    
    e1.interval_hours = 10;
    QVERIFY(e1 != e2);
}

// ─── LteEntry and NfcEntry Tests ────────────────────────────────────────────

void TestProfileModel::testLteEntryJsonRoundTrip()
{
    LteEntry original;
    original.state = LteState::CallsOnly;
    
    QJsonObject json = original.toJson();
    LteEntry restored = LteEntry::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.state, LteState::CallsOnly);
}

void TestProfileModel::testNfcEntryJsonRoundTrip()
{
    NfcEntry original;
    original.state = RadioState::On;
    
    QJsonObject json = original.toJson();
    NfcEntry restored = NfcEntry::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.state, RadioState::On);
}

// ─── RadioConfig Tests ──────────────────────────────────────────────────────

void TestProfileModel::testRadioConfigDefaults()
{
    RadioConfig c;
    QCOMPARE(c.ble.state, RadioState::Off);
    QCOMPARE(c.wifi.state, RadioState::Off);
    QCOMPARE(c.lte.state, LteState::Off);
    QCOMPARE(c.nfc.state, RadioState::Off);
}

void TestProfileModel::testRadioConfigJsonRoundTrip()
{
    RadioConfig original;
    original.ble.state = RadioState::On;
    original.ble.sync_mode = SyncMode::Interval;
    original.ble.interval_hours = 2;
    original.wifi.state = RadioState::On;
    original.wifi.sync_mode = SyncMode::TimeWindow;
    original.wifi.start_time = QStringLiteral("06:00");
    original.lte.state = LteState::Always;
    original.nfc.state = RadioState::On;
    
    QJsonObject json = original.toJson();
    RadioConfig restored = RadioConfig::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.ble.state, RadioState::On);
    QCOMPARE(restored.ble.interval_hours, 2);
    QCOMPARE(restored.wifi.start_time, QStringLiteral("06:00"));
    QCOMPARE(restored.lte.state, LteState::Always);
    QCOMPARE(restored.nfc.state, RadioState::On);
}

void TestProfileModel::testRadioConfigEquality()
{
    RadioConfig c1, c2;
    QCOMPARE(c1, c2);
    
    c1.ble.state = RadioState::On;
    QVERIFY(c1 != c2);
    
    c2.ble.state = RadioState::On;
    QCOMPARE(c1, c2);
}

// ─── SystemConfig Tests ─────────────────────────────────────────────────────

void TestProfileModel::testSystemConfigDefaults()
{
    SystemConfig c;
    QCOMPARE(c.background_sync, BackgroundSyncMode::Auto);
    QCOMPARE(c.always_on_display, false);
    QCOMPARE(c.tilt_to_wake, true);
}

void TestProfileModel::testSystemConfigJsonRoundTrip()
{
    SystemConfig original;
    original.background_sync = BackgroundSyncMode::WhenRadiosOn;
    original.always_on_display = true;
    original.tilt_to_wake = false;
    
    QJsonObject json = original.toJson();
    SystemConfig restored = SystemConfig::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.background_sync, BackgroundSyncMode::WhenRadiosOn);
    QCOMPARE(restored.always_on_display, true);
    QCOMPARE(restored.tilt_to_wake, false);
}

void TestProfileModel::testSystemConfigEquality()
{
    SystemConfig c1, c2;
    QCOMPARE(c1, c2); // Two default-constructed configs are equal

    // Changing one field makes them unequal
    c2.always_on_display = true;
    QVERIFY(c1 != c2);

    // Make them equal again
    c1.always_on_display = true;
    QCOMPARE(c1, c2);

    // Different background_sync makes them unequal
    c2.background_sync = BackgroundSyncMode::Off;
    QVERIFY(c1 != c2);
}

// ─── BatteryRule and TimeRule Tests ─────────────────────────────────────────

void TestProfileModel::testBatteryRuleJsonRoundTrip()
{
    BatteryRule original;
    original.threshold = 30;
    original.switch_to_profile = QStringLiteral("ultra_saver_indoor");
    
    QJsonObject json = original.toJson();
    BatteryRule restored = BatteryRule::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.threshold, 30);
    QCOMPARE(restored.switch_to_profile, QStringLiteral("ultra_saver_indoor"));
}

void TestProfileModel::testTimeRuleJsonRoundTrip()
{
    TimeRule original;
    original.start = QStringLiteral("22:00");
    original.end = QStringLiteral("06:00");
    original.switch_to_profile = QStringLiteral("health_indoor");
    
    QJsonObject json = original.toJson();
    TimeRule restored = TimeRule::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.start, QStringLiteral("22:00"));
    QCOMPARE(restored.end, QStringLiteral("06:00"));
    QCOMPARE(restored.switch_to_profile, QStringLiteral("health_indoor"));
}

// ─── AutomationConfig Tests ─────────────────────────────────────────────────

void TestProfileModel::testAutomationConfigDefaults()
{
    AutomationConfig c;
    QVERIFY(c.battery_rules.isEmpty());
    QVERIFY(c.time_rules.isEmpty());
    QVERIFY(c.workout_profiles.isEmpty());
}

void TestProfileModel::testAutomationConfigJsonRoundTrip()
{
    AutomationConfig original;
    
    BatteryRule br1;
    br1.threshold = 30;
    br1.switch_to_profile = QStringLiteral("health_indoor");
    original.battery_rules.append(br1);
    
    BatteryRule br2;
    br2.threshold = 15;
    br2.switch_to_profile = QStringLiteral("ultra_saver_indoor");
    original.battery_rules.append(br2);
    
    TimeRule tr;
    tr.start = QStringLiteral("22:00");
    tr.end = QStringLiteral("06:00");
    tr.switch_to_profile = QStringLiteral("health_indoor");
    original.time_rules.append(tr);
    
    original.workout_profiles[QStringLiteral("treadmill")] = QStringLiteral("health_indoor");
    original.workout_profiles[QStringLiteral("outdoor_run")] = QStringLiteral("health_outdoor");
    
    QJsonObject json = original.toJson();
    AutomationConfig restored = AutomationConfig::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.battery_rules.size(), 2);
    QCOMPARE(restored.battery_rules[0].threshold, 30);
    QCOMPARE(restored.battery_rules[1].threshold, 15);
    QCOMPARE(restored.time_rules.size(), 1);
    QCOMPARE(restored.time_rules[0].start, QStringLiteral("22:00"));
    QCOMPARE(restored.workout_profiles.size(), 2);
    QCOMPARE(restored.workout_profiles[QStringLiteral("treadmill")], QStringLiteral("health_indoor"));
}

void TestProfileModel::testAutomationConfigEquality()
{
    AutomationConfig c1, c2;
    QCOMPARE(c1, c2);
    
    BatteryRule br;
    br.threshold = 30;
    br.switch_to_profile = QStringLiteral("test");
    c1.battery_rules.append(br);
    QVERIFY(c1 != c2);
    
    c2.battery_rules.append(br);
    QCOMPARE(c1, c2);
}

// ─── PowerProfile Tests ─────────────────────────────────────────────────────

void TestProfileModel::testPowerProfileDefaults()
{
    PowerProfile p;
    QVERIFY(p.id.isEmpty());
    QVERIFY(p.name.isEmpty());
    QVERIFY(p.icon.isEmpty());
    QVERIFY(p.color.isEmpty());
    QCOMPARE(p.sensors.accelerometer, SensorMode::Off);
    QCOMPARE(p.radios.ble.state, RadioState::Off);
    QCOMPARE(p.system.background_sync, BackgroundSyncMode::Auto);
    QVERIFY(p.automation.battery_rules.isEmpty());
}

void TestProfileModel::testPowerProfileJsonRoundTrip()
{
    PowerProfile original;
    original.id = QStringLiteral("test_profile");
    original.name = QStringLiteral("Test Profile");
    original.icon = QStringLiteral("ios-heart");
    original.color = QStringLiteral("#4CAF50");
    
    original.sensors.accelerometer = SensorMode::Medium;
    original.sensors.heart_rate = SensorMode::High;
    original.sensors.gps = GpsMode::Continuous;
    
    original.radios.ble.state = RadioState::On;
    original.radios.ble.sync_mode = SyncMode::Interval;
    original.radios.ble.interval_hours = 2;
    
    original.system.always_on_display = true;
    original.system.tilt_to_wake = true;
    original.system.background_sync = BackgroundSyncMode::WhenRadiosOn;
    
    original.cpu.governor = CpuGovernor::Powersave;
    original.cpu.max_cores = 2;
    original.cpu.screen_boost = false;
    
    original.processes.audio_enabled = false;
    original.processes.pulseaudio = ServiceState::Stopped;
    original.processes.btsyncd = ServiceState::Auto;
    original.processes.mce = ServiceState::Auto;
    
    BatteryRule br;
    br.threshold = 20;
    br.switch_to_profile = QStringLiteral("ultra_saver");
    original.automation.battery_rules.append(br);
    
    QJsonObject json = original.toJson();
    PowerProfile restored = PowerProfile::fromJson(json);
    
    QCOMPARE(restored, original);
    QCOMPARE(restored.id, QStringLiteral("test_profile"));
    QCOMPARE(restored.name, QStringLiteral("Test Profile"));
    QCOMPARE(restored.icon, QStringLiteral("ios-heart"));
    QCOMPARE(restored.color, QStringLiteral("#4CAF50"));
    QCOMPARE(restored.sensors.accelerometer, SensorMode::Medium);
    QCOMPARE(restored.sensors.heart_rate, SensorMode::High);
    QCOMPARE(restored.sensors.gps, GpsMode::Continuous);
    QCOMPARE(restored.radios.ble.state, RadioState::On);
    QCOMPARE(restored.radios.ble.interval_hours, 2);
    QCOMPARE(restored.system.always_on_display, true);
    QCOMPARE(restored.cpu.governor, CpuGovernor::Powersave);
    QCOMPARE(restored.cpu.max_cores, 2);
    QCOMPARE(restored.cpu.screen_boost, false);
    QCOMPARE(restored.processes.audio_enabled, false);
    QCOMPARE(restored.processes.pulseaudio, ServiceState::Stopped);
    QCOMPARE(restored.automation.battery_rules.size(), 1);
    QCOMPARE(restored.automation.battery_rules[0].threshold, 20);
}

void TestProfileModel::testPowerProfileValidation()
{
    PowerProfile p;
    QVERIFY(!p.isValid()); // Empty id and name
    
    p.id = QStringLiteral("test");
    QVERIFY(!p.isValid()); // Empty name
    
    p.name = QStringLiteral("Test");
    QVERIFY(p.isValid()); // Both set
    
    p.id.clear();
    QVERIFY(!p.isValid()); // Empty id
}

void TestProfileModel::testPowerProfileEquality()
{
    PowerProfile p1, p2;
    p1.id = QStringLiteral("test1");
    p1.name = QStringLiteral("Test 1");
    p2.id = QStringLiteral("test1");
    p2.name = QStringLiteral("Test 1");
    
    QCOMPARE(p1, p2);
    
    // ID comparison is case-insensitive
    p2.id = QStringLiteral("TEST1");
    QCOMPARE(p1, p2);
    
    p2.name = QStringLiteral("Different");
    QVERIFY(p1 != p2);
    
    p2.name = p1.name;
    p2.sensors.heart_rate = SensorMode::High;
    QVERIFY(p1 != p2);
}

void TestProfileModel::testPowerProfileComplexJson()
{
    // Test with a complex, realistic profile
    QString jsonStr = R"({
        "id": "health_outdoor",
        "name": "Health – Outdoor",
        "icon": "ios-walk",
        "color": "#4CAF50",
        "sensors": {
            "accelerometer": "medium",
            "gyroscope": "medium",
            "heart_rate": "high",
            "hrv": "sleep_only",
            "spo2": "off",
            "barometer": "low",
            "compass": "on_demand",
            "ambient_light": "low",
            "gps": "continuous"
        },
        "radios": {
            "ble": {
                "state": "on",
                "sync_mode": "interval",
                "interval_hours": 1,
                "max_duration_minutes": 30,
                "disable_during_sleep": true
            },
            "wifi": {
                "state": "off",
                "sync_mode": "manual",
                "interval_hours": 1,
                "max_duration_minutes": 30,
                "disable_during_sleep": false
            },
            "lte": {
                "state": "off"
            },
            "nfc": {
                "state": "off"
            }
        },
        "system": {
            "background_sync": "when_radios_on",
            "always_on_display": false,
            "tilt_to_wake": true
        },
        "automation": {
            "battery_rules": [
                { "threshold": 30, "switch_to_profile": "health_indoor" },
                { "threshold": 15, "switch_to_profile": "ultra_saver_indoor" }
            ],
            "time_rules": [
                {
                    "start": "22:00",
                    "end": "06:00",
                    "switch_to_profile": "health_indoor"
                }
            ],
            "workout_profiles": {
                "treadmill": "health_indoor",
                "outdoor_run": "health_outdoor"
            }
        }
    })";
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QVERIFY(!doc.isNull());
    
    PowerProfile p = PowerProfile::fromJson(doc.object());
    
    QVERIFY(p.isValid());
    QCOMPARE(p.id, QStringLiteral("health_outdoor"));
    QCOMPARE(p.name, QStringLiteral("Health – Outdoor"));
    QCOMPARE(p.sensors.accelerometer, SensorMode::Medium);
    QCOMPARE(p.sensors.heart_rate, SensorMode::High);
    QCOMPARE(p.sensors.hrv, HrvMode::SleepOnly);
    QCOMPARE(p.sensors.gps, GpsMode::Continuous);
    QCOMPARE(p.radios.ble.state, RadioState::On);
    QCOMPARE(p.radios.ble.interval_hours, 1);
    QCOMPARE(p.radios.ble.disable_during_sleep, true);
    QCOMPARE(p.system.background_sync, BackgroundSyncMode::WhenRadiosOn);
    QCOMPARE(p.automation.battery_rules.size(), 2);
    QCOMPARE(p.automation.time_rules.size(), 1);
    QCOMPARE(p.automation.workout_profiles.size(), 2);
    
    // Round-trip test
    QJsonObject json = p.toJson();
    PowerProfile restored = PowerProfile::fromJson(json);
    QCOMPARE(restored, p);
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

void TestProfileModel::testUnknownEnumValues()
{
    QString jsonStr = R"({
        "id": "test",
        "name": "Test",
        "sensors": {
            "accelerometer": "ultra_high",
            "heart_rate": "unknown_mode",
            "hrv": "sometimes"
        }
    })";
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    PowerProfile p = PowerProfile::fromJson(doc.object());
    
    // Unknown values should default to Off
    QCOMPARE(p.sensors.accelerometer, SensorMode::Off);
    QCOMPARE(p.sensors.heart_rate, SensorMode::Off);
    QCOMPARE(p.sensors.hrv, HrvMode::Off);
}

void TestProfileModel::testEmptyJsonObjects()
{
    QJsonObject empty;
    
    SensorConfig sc = SensorConfig::fromJson(empty);
    QCOMPARE(sc.accelerometer, SensorMode::Off);
    
    RadioEntry re = RadioEntry::fromJson(empty);
    QCOMPARE(re.state, RadioState::Off);
    QCOMPARE(re.interval_hours, 1); // Default value
    
    SystemConfig sysc = SystemConfig::fromJson(empty);
    QCOMPARE(sysc.background_sync, BackgroundSyncMode::Auto);
    QCOMPARE(sysc.always_on_display, false);
    QCOMPARE(sysc.tilt_to_wake, true);
    
    PowerProfile p = PowerProfile::fromJson(empty);
    QVERIFY(!p.isValid()); // Missing required fields
}

QTEST_MAIN(TestProfileModel)
#include "tst_profilemodel.moc"
