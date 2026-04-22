#include <QtTest/QtTest>
#include "profilemanager.h"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class TestProfileManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    
    // Default profiles tests
    void testLoadDefaultProfiles();
    void testDefaultProfileCount();
    void testDefaultProfileIds();
    void testDefaultActiveProfile();
    void testDefaultWorkoutProfiles();
    
    // CRUD operations
    void testAddProfile();
    void testAddProfileAutoGenerateId();
    void testAddProfileDuplicateId();
    void testAddInvalidProfile();
    void testUpdateProfile();
    void testUpdateNonExistentProfile();
    void testDeleteProfile();
    void testDeleteNonExistentProfile();
    void testDeleteActiveProfile();
    void testHasProfile();
    void testGetProfile();
    void testGetProfiles();
    
    // Active profile management
    void testSetActiveProfile();
    void testSetActiveProfileNonExistent();
    void testSetActiveProfileSignal();
    void testActiveProfileCaseInsensitive();
    
    // Workout profile management
    void testSetWorkoutProfile();
    void testSetWorkoutProfileNonExistent();
    void testGetWorkoutProfile();
    void testClearWorkoutProfile();
    
    // Persistence tests
    void testSaveAndLoadProfiles();
    void testSavePreservesActiveProfile();
    void testSavePreservesWorkoutProfiles();
    void testLoadFromNonExistentFile();
    void testLoadInvalidJson();
    void testLoadWithMissingActiveProfile();
    void testSaveCreatesDirectory();
    
    // Builtin profile tests
    void testBuiltinProfileFlag();
    void testDeleteBuiltinProfile();
    void testBuiltinProfileJsonRoundTrip();
    
    // Signal tests
    void testActiveProfileChangedSignal();
    void testProfilesChangedSignalOnAdd();
    void testProfilesChangedSignalOnUpdate();
    void testProfilesChangedSignalOnDelete();

private:
    QTemporaryDir *m_tempDir;
    ProfileManager *m_manager;
    
    PowerProfile createTestProfile(const QString &id, const QString &name);
};

void TestProfileManager::initTestCase()
{
    qRegisterMetaType<QString>("QString");
    // Point to the source-tree default profiles for testing
    qputenv("POWERD_DEFAULT_PROFILES",
            QByteArray(CMAKE_SOURCE_DIR "/data/default-profiles.json"));
}

void TestProfileManager::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_manager = new ProfileManager(m_tempDir->path(), this);
}

void TestProfileManager::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
    delete m_tempDir;
    m_tempDir = nullptr;
}

PowerProfile TestProfileManager::createTestProfile(const QString &id, const QString &name)
{
    PowerProfile p;
    p.id = id;
    p.name = name;
    p.icon = QStringLiteral("ios-heart");
    p.color = QStringLiteral("#FF0000");
    p.sensors.heart_rate = SensorMode::Medium;
    return p;
}

// ─── Default Profiles Tests ─────────────────────────────────────────────────

void TestProfileManager::testLoadDefaultProfiles()
{
    m_manager->loadDefaultProfiles();
    QVERIFY(!m_manager->profiles().isEmpty());
}

void TestProfileManager::testDefaultProfileCount()
{
    m_manager->loadDefaultProfiles();
    QCOMPARE(m_manager->profiles().size(), 8);
}

void TestProfileManager::testDefaultProfileIds()
{
    m_manager->loadDefaultProfiles();
    
    QStringList expectedIds = {
        QStringLiteral("ultra_saver_indoor"),
        QStringLiteral("ultra_saver_outdoor"),
        QStringLiteral("health_indoor"),
        QStringLiteral("health_outdoor"),
        QStringLiteral("smartwatch_indoor"),
        QStringLiteral("smartwatch_outdoor"),
        QStringLiteral("performance_outdoor")
    };
    
    for (const QString &id : expectedIds) {
        QVERIFY2(m_manager->hasProfile(id), qPrintable(QString("Missing profile: %1").arg(id)));
        PowerProfile p = m_manager->profile(id);
        QVERIFY(p.isValid());
        QVERIFY(!p.name.isEmpty());
    }
}

void TestProfileManager::testDefaultActiveProfile()
{
    m_manager->loadDefaultProfiles();
    QCOMPARE(m_manager->activeProfileId(), QStringLiteral("smartwatch_indoor"));
    
    PowerProfile active = m_manager->activeProfile();
    QVERIFY(active.isValid());
    QCOMPARE(active.id, QStringLiteral("smartwatch_indoor"));
}

void TestProfileManager::testDefaultWorkoutProfiles()
{
    m_manager->loadDefaultProfiles();
    
    QMap<QString, QString> workouts = m_manager->workoutProfiles();
    QVERIFY(!workouts.isEmpty());
    
    QCOMPARE(workouts.value(QStringLiteral("treadmill")), QStringLiteral("health_indoor"));
    QCOMPARE(workouts.value(QStringLiteral("outdoor_run")), QStringLiteral("health_outdoor"));
    QCOMPARE(workouts.value(QStringLiteral("cycling")), QStringLiteral("performance_outdoor"));
    QCOMPARE(workouts.value(QStringLiteral("hiking")), QStringLiteral("health_outdoor"));
    QCOMPARE(workouts.value(QStringLiteral("ultra_hike")), QStringLiteral("ultra_saver_outdoor"));
}

// ─── CRUD Operations ────────────────────────────────────────────────────────

void TestProfileManager::testAddProfile()
{
    m_manager->loadDefaultProfiles();
    int initialCount = m_manager->profiles().size();
    
    PowerProfile p = createTestProfile(QStringLiteral("test_profile"), QStringLiteral("Test Profile"));
    QVERIFY(m_manager->addProfile(p));
    
    QCOMPARE(m_manager->profiles().size(), initialCount + 1);
    QVERIFY(m_manager->hasProfile(QStringLiteral("test_profile")));
    
    PowerProfile retrieved = m_manager->profile(QStringLiteral("test_profile"));
    QCOMPARE(retrieved.name, QStringLiteral("Test Profile"));
}

void TestProfileManager::testAddProfileAutoGenerateId()
{
    PowerProfile p = createTestProfile(QString(), QStringLiteral("No ID Profile"));
    QVERIFY(m_manager->addProfile(p));
    
    QCOMPARE(m_manager->profiles().size(), 1);
    PowerProfile retrieved = m_manager->profiles().first();
    QVERIFY(!retrieved.id.isEmpty());
    QVERIFY(retrieved.id.startsWith(QStringLiteral("custom_profile_")));
}

void TestProfileManager::testAddProfileDuplicateId()
{
    PowerProfile p1 = createTestProfile(QStringLiteral("duplicate"), QStringLiteral("First"));
    PowerProfile p2 = createTestProfile(QStringLiteral("duplicate"), QStringLiteral("Second"));
    
    QVERIFY(m_manager->addProfile(p1));
    QVERIFY(!m_manager->addProfile(p2)); // Should fail
    
    QCOMPARE(m_manager->profiles().size(), 1);
    QCOMPARE(m_manager->profile(QStringLiteral("duplicate")).name, QStringLiteral("First"));
}

void TestProfileManager::testAddInvalidProfile()
{
    PowerProfile p;
    // Invalid: missing id and name
    QVERIFY(!m_manager->addProfile(p));
    
    p.id = QStringLiteral("test");
    // Invalid: missing name
    QVERIFY(!m_manager->addProfile(p));
    
    QCOMPARE(m_manager->profiles().size(), 0);
}

void TestProfileManager::testUpdateProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Original"));
    m_manager->addProfile(p);
    
    p.name = QStringLiteral("Updated");
    p.sensors.heart_rate = SensorMode::High;
    QVERIFY(m_manager->updateProfile(p));
    
    PowerProfile retrieved = m_manager->profile(QStringLiteral("test"));
    QCOMPARE(retrieved.name, QStringLiteral("Updated"));
    QCOMPARE(retrieved.sensors.heart_rate, SensorMode::High);
}

void TestProfileManager::testUpdateNonExistentProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("nonexistent"), QStringLiteral("Test"));
    QVERIFY(!m_manager->updateProfile(p));
}

void TestProfileManager::testDeleteProfile()
{
    PowerProfile p1 = createTestProfile(QStringLiteral("profile1"), QStringLiteral("Profile 1"));
    PowerProfile p2 = createTestProfile(QStringLiteral("profile2"), QStringLiteral("Profile 2"));
    
    m_manager->addProfile(p1);
    m_manager->addProfile(p2);
    m_manager->setActiveProfile(QStringLiteral("profile1"));
    
    QCOMPARE(m_manager->profiles().size(), 2);
    
    // Delete non-active profile
    QVERIFY(m_manager->deleteProfile(QStringLiteral("profile2")));
    QCOMPARE(m_manager->profiles().size(), 1);
    QVERIFY(!m_manager->hasProfile(QStringLiteral("profile2")));
}

void TestProfileManager::testDeleteNonExistentProfile()
{
    QVERIFY(!m_manager->deleteProfile(QStringLiteral("nonexistent")));
}

void TestProfileManager::testDeleteActiveProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    m_manager->setActiveProfile(QStringLiteral("test"));
    
    // Cannot delete active profile
    QVERIFY(!m_manager->deleteProfile(QStringLiteral("test")));
    QVERIFY(m_manager->hasProfile(QStringLiteral("test")));
}

void TestProfileManager::testHasProfile()
{
    QVERIFY(!m_manager->hasProfile(QStringLiteral("test")));
    
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    
    QVERIFY(m_manager->hasProfile(QStringLiteral("test")));
    QVERIFY(m_manager->hasProfile(QStringLiteral("TEST"))); // Case insensitive
}

void TestProfileManager::testGetProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test Profile"));
    m_manager->addProfile(p);
    
    PowerProfile retrieved = m_manager->profile(QStringLiteral("test"));
    QVERIFY(retrieved.isValid());
    QCOMPARE(retrieved.id, QStringLiteral("test"));
    QCOMPARE(retrieved.name, QStringLiteral("Test Profile"));
    
    // Non-existent profile returns invalid profile
    PowerProfile invalid = m_manager->profile(QStringLiteral("nonexistent"));
    QVERIFY(!invalid.isValid());
}

void TestProfileManager::testGetProfiles()
{
    QVERIFY(m_manager->profiles().isEmpty());
    
    m_manager->addProfile(createTestProfile(QStringLiteral("p1"), QStringLiteral("Profile 1")));
    m_manager->addProfile(createTestProfile(QStringLiteral("p2"), QStringLiteral("Profile 2")));
    
    QList<PowerProfile> profiles = m_manager->profiles();
    QCOMPARE(profiles.size(), 2);
}

// ─── Active Profile Management ──────────────────────────────────────────────

void TestProfileManager::testSetActiveProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    
    QVERIFY(m_manager->setActiveProfile(QStringLiteral("test")));
    QCOMPARE(m_manager->activeProfileId(), QStringLiteral("test"));
    
    PowerProfile active = m_manager->activeProfile();
    QCOMPARE(active.id, QStringLiteral("test"));
}

void TestProfileManager::testSetActiveProfileNonExistent()
{
    QVERIFY(!m_manager->setActiveProfile(QStringLiteral("nonexistent")));
}

void TestProfileManager::testSetActiveProfileSignal()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test Name"));
    m_manager->addProfile(p);
    
    QSignalSpy spy(m_manager, &ProfileManager::activeProfileChanged);
    
    m_manager->setActiveProfile(QStringLiteral("test"));
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("test"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("Test Name"));
}

void TestProfileManager::testActiveProfileCaseInsensitive()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    
    QVERIFY(m_manager->setActiveProfile(QStringLiteral("TEST")));
    QCOMPARE(m_manager->activeProfileId(), QStringLiteral("TEST"));
    
    PowerProfile active = m_manager->activeProfile();
    QVERIFY(active.isValid());
}

// ─── Workout Profile Management ─────────────────────────────────────────────

void TestProfileManager::testSetWorkoutProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("running_profile"), QStringLiteral("Running"));
    m_manager->addProfile(p);
    
    QVERIFY(m_manager->setWorkoutProfile(QStringLiteral("outdoor_run"), QStringLiteral("running_profile")));
    QCOMPARE(m_manager->workoutProfileId(QStringLiteral("outdoor_run")), QStringLiteral("running_profile"));
}

void TestProfileManager::testSetWorkoutProfileNonExistent()
{
    QVERIFY(!m_manager->setWorkoutProfile(QStringLiteral("outdoor_run"), QStringLiteral("nonexistent")));
}

void TestProfileManager::testGetWorkoutProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    m_manager->setWorkoutProfile(QStringLiteral("cycling"), QStringLiteral("test"));
    
    QCOMPARE(m_manager->workoutProfileId(QStringLiteral("cycling")), QStringLiteral("test"));
    
    // Non-existent workout returns empty string
    QVERIFY(m_manager->workoutProfileId(QStringLiteral("swimming")).isEmpty());
}

void TestProfileManager::testClearWorkoutProfile()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    m_manager->setWorkoutProfile(QStringLiteral("running"), QStringLiteral("test"));
    
    QVERIFY(!m_manager->workoutProfileId(QStringLiteral("running")).isEmpty());
    
    // Clear by setting empty string
    QVERIFY(m_manager->setWorkoutProfile(QStringLiteral("running"), QString()));
    QVERIFY(m_manager->workoutProfileId(QStringLiteral("running")).isEmpty());
}

// ─── Persistence Tests ──────────────────────────────────────────────────────

void TestProfileManager::testSaveAndLoadProfiles()
{
    // Add some profiles
    m_manager->addProfile(createTestProfile(QStringLiteral("p1"), QStringLiteral("Profile 1")));
    m_manager->addProfile(createTestProfile(QStringLiteral("p2"), QStringLiteral("Profile 2")));
    m_manager->setActiveProfile(QStringLiteral("p2"));
    
    // Save
    QVERIFY(m_manager->saveProfiles());
    
    // Create new manager with same config dir
    ProfileManager *newManager = new ProfileManager(m_tempDir->path());
    QVERIFY(newManager->loadProfiles());
    
    // Verify loaded state
    QCOMPARE(newManager->profiles().size(), 2);
    QVERIFY(newManager->hasProfile(QStringLiteral("p1")));
    QVERIFY(newManager->hasProfile(QStringLiteral("p2")));
    QCOMPARE(newManager->activeProfileId(), QStringLiteral("p2"));
    
    PowerProfile p1 = newManager->profile(QStringLiteral("p1"));
    QCOMPARE(p1.name, QStringLiteral("Profile 1"));
    
    delete newManager;
}

void TestProfileManager::testSavePreservesActiveProfile()
{
    m_manager->loadDefaultProfiles();
    m_manager->setActiveProfile(QStringLiteral("health_outdoor"));
    
    QVERIFY(m_manager->saveProfiles());
    
    ProfileManager *newManager = new ProfileManager(m_tempDir->path());
    newManager->loadProfiles();
    
    QCOMPARE(newManager->activeProfileId(), QStringLiteral("health_outdoor"));
    
    delete newManager;
}

void TestProfileManager::testSavePreservesWorkoutProfiles()
{
    m_manager->loadDefaultProfiles();
    m_manager->setWorkoutProfile(QStringLiteral("custom_workout"), QStringLiteral("health_indoor"));
    
    QVERIFY(m_manager->saveProfiles());
    
    ProfileManager *newManager = new ProfileManager(m_tempDir->path());
    newManager->loadProfiles();
    
    QCOMPARE(newManager->workoutProfileId(QStringLiteral("custom_workout")), QStringLiteral("health_indoor"));
    
    delete newManager;
}

void TestProfileManager::testLoadFromNonExistentFile()
{
    // Loading from empty directory should fall back to defaults
    QVERIFY(m_manager->loadProfiles());
    QCOMPARE(m_manager->profiles().size(), 8); // Default profiles
    QCOMPARE(m_manager->activeProfileId(), QStringLiteral("smartwatch_indoor"));
}

void TestProfileManager::testLoadInvalidJson()
{
    // Write invalid JSON to file
    QString filePath = m_tempDir->path() + QStringLiteral("/profiles.json");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ invalid json");
    file.close();
    
    // Should fall back to defaults
    QVERIFY(!m_manager->loadProfiles()); // Returns false but loads defaults
    QCOMPARE(m_manager->profiles().size(), 8);
}

void TestProfileManager::testLoadWithMissingActiveProfile()
{
    // Create valid JSON but with non-existent active profile
    QJsonObject root;
    root[QStringLiteral("active_profile")] = QStringLiteral("nonexistent");
    
    QJsonArray profilesArray;
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    profilesArray.append(p.toJson());
    root[QStringLiteral("profiles")] = profilesArray;
    root[QStringLiteral("workout_profiles")] = QJsonObject();
    
    QString filePath = m_tempDir->path() + QStringLiteral("/profiles.json");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();
    
    QVERIFY(m_manager->loadProfiles());
    
    // Should fall back to first profile or smartwatch_indoor
    QVERIFY(!m_manager->activeProfileId().isEmpty());
}

void TestProfileManager::testSaveCreatesDirectory()
{
    QTemporaryDir tempRoot;
    QString nestedPath = tempRoot.path() + QStringLiteral("/nested/config/dir");
    
    ProfileManager *manager = new ProfileManager(nestedPath);
    manager->loadDefaultProfiles();
    
    QVERIFY(manager->saveProfiles());
    
    // Verify directory was created
    QDir dir(nestedPath);
    QVERIFY(dir.exists());
    
    // Verify file exists
    QString filePath = nestedPath + QStringLiteral("/profiles.json");
    QVERIFY(QFile::exists(filePath));
    
    delete manager;
}

// ─── Builtin Profile Tests ──────────────────────────────────────────────────

void TestProfileManager::testBuiltinProfileFlag()
{
    m_manager->loadDefaultProfiles();
    
    // All default profiles should be marked builtin
    const QList<PowerProfile> profiles = m_manager->profiles();
    for (const PowerProfile &p : profiles) {
        QVERIFY2(p.builtin, qPrintable(QString("Profile '%1' should be builtin").arg(p.id)));
    }
    
    // User-added profiles should NOT be builtin
    PowerProfile custom = createTestProfile(QStringLiteral("custom_user"), QStringLiteral("Custom"));
    QVERIFY(!custom.builtin);
    m_manager->addProfile(custom);
    
    PowerProfile retrieved = m_manager->profile(QStringLiteral("custom_user"));
    QVERIFY(!retrieved.builtin);
}

void TestProfileManager::testDeleteBuiltinProfile()
{
    m_manager->loadDefaultProfiles();
    int initialCount = m_manager->profiles().size();
    
    // Attempt to delete a builtin profile should fail
    QVERIFY(!m_manager->deleteProfile(QStringLiteral("smartwatch_indoor")));
    QCOMPARE(m_manager->profiles().size(), initialCount);
    QVERIFY(m_manager->hasProfile(QStringLiteral("smartwatch_indoor")));
    
    // Attempt to delete another builtin
    QVERIFY(!m_manager->deleteProfile(QStringLiteral("ultra_saver_indoor")));
    QCOMPARE(m_manager->profiles().size(), initialCount);
    
    // User-added profile CAN be deleted
    PowerProfile custom = createTestProfile(QStringLiteral("custom"), QStringLiteral("Custom"));
    m_manager->addProfile(custom);
    QCOMPARE(m_manager->profiles().size(), initialCount + 1);
    
    m_manager->setActiveProfile(QStringLiteral("smartwatch_indoor")); // ensure it's not active
    QVERIFY(m_manager->deleteProfile(QStringLiteral("custom")));
    QCOMPARE(m_manager->profiles().size(), initialCount);
}

void TestProfileManager::testBuiltinProfileJsonRoundTrip()
{
    m_manager->loadDefaultProfiles();
    m_manager->saveProfiles();
    
    ProfileManager *newManager = new ProfileManager(m_tempDir->path());
    newManager->loadProfiles();
    
    // Verify builtin flag survived serialization
    const QList<PowerProfile> profiles = newManager->profiles();
    bool foundBuiltin = false;
    for (const PowerProfile &p : profiles) {
        if (p.id == QStringLiteral("smartwatch_indoor")) {
            QVERIFY(p.builtin);
            foundBuiltin = true;
        }
    }
    QVERIFY(foundBuiltin);
    
    delete newManager;
}

// ─── Signal Tests ───────────────────────────────────────────────────────────

void TestProfileManager::testActiveProfileChangedSignal()
{
    PowerProfile p1 = createTestProfile(QStringLiteral("p1"), QStringLiteral("Profile 1"));
    PowerProfile p2 = createTestProfile(QStringLiteral("p2"), QStringLiteral("Profile 2"));
    
    m_manager->addProfile(p1);
    m_manager->addProfile(p2);
    m_manager->setActiveProfile(QStringLiteral("p1"));
    
    QSignalSpy spy(m_manager, &ProfileManager::activeProfileChanged);
    
    // Change to different profile
    m_manager->setActiveProfile(QStringLiteral("p2"));
    QCOMPARE(spy.count(), 1);
    
    // Setting to same profile should not emit signal
    m_manager->setActiveProfile(QStringLiteral("p2"));
    QCOMPARE(spy.count(), 1); // Still 1
    
    // Case-insensitive comparison should not emit
    m_manager->setActiveProfile(QStringLiteral("P2"));
    QCOMPARE(spy.count(), 1); // Still 1
}

void TestProfileManager::testProfilesChangedSignalOnAdd()
{
    QSignalSpy spy(m_manager, &ProfileManager::profilesChanged);
    
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    
    QCOMPARE(spy.count(), 1);
}

void TestProfileManager::testProfilesChangedSignalOnUpdate()
{
    PowerProfile p = createTestProfile(QStringLiteral("test"), QStringLiteral("Test"));
    m_manager->addProfile(p);
    
    QSignalSpy spy(m_manager, &ProfileManager::profilesChanged);
    
    p.name = QStringLiteral("Updated");
    m_manager->updateProfile(p);
    
    QCOMPARE(spy.count(), 1);
}

void TestProfileManager::testProfilesChangedSignalOnDelete()
{
    PowerProfile p1 = createTestProfile(QStringLiteral("p1"), QStringLiteral("Profile 1"));
    PowerProfile p2 = createTestProfile(QStringLiteral("p2"), QStringLiteral("Profile 2"));
    
    m_manager->addProfile(p1);
    m_manager->addProfile(p2);
    m_manager->setActiveProfile(QStringLiteral("p1"));
    
    QSignalSpy spy(m_manager, &ProfileManager::profilesChanged);
    
    m_manager->deleteProfile(QStringLiteral("p2"));
    
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestProfileManager)
#include "tst_profilemanager.moc"
