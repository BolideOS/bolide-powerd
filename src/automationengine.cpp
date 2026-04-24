#include "automationengine.h"
#include <QDateTime>
#include <QTime>
#include <QDebug>
#include <algorithm>

AutomationEngine::AutomationEngine(ProfileManager *pm, BatteryMonitor *bm, QObject *parent)
    : QObject(parent)
    , m_profileManager(pm)
    , m_batteryMonitor(bm)
    , m_timeCheckTimer(new QTimer(this))
    , m_workoutActive(false)
    , m_lastBatteryLevel(-1)
    , m_inSleepWindow(false)
    , m_currentSource(SourceManual)
{
    // Connect battery monitor signals
    connect(m_batteryMonitor, &BatteryMonitor::levelChanged,
            this, &AutomationEngine::onBatteryLevelChanged);

    // Time check timer — only used for time_rules (sleep schedules etc).
    // Disabled: all profiles currently have empty time_rules, and polling
    // every 60s prevents CPU suspend. Re-enable when time rules are needed.
    m_timeCheckTimer->setInterval(60000);
    m_timeCheckTimer->setTimerType(Qt::CoarseTimer);
    connect(m_timeCheckTimer, &QTimer::timeout,
            this, &AutomationEngine::onTimeCheck);
}

void AutomationEngine::start()
{
    qInfo() << "AutomationEngine: starting";
    
    // Initialize battery level tracking
    m_lastBatteryLevel = m_batteryMonitor->level();
    
    // Time check timer is NOT started — no profiles use time_rules.
    // Battery rules are event-driven via onBatteryLevelChanged.
    // m_timeCheckTimer->start();
    
    // Perform initial evaluation (battery rules only)
    evaluateRules();
}

void AutomationEngine::stop()
{
    qInfo() << "AutomationEngine: stopping";
    m_timeCheckTimer->stop();
}

bool AutomationEngine::isWorkoutActive() const
{
    return m_workoutActive;
}

QString AutomationEngine::activeWorkoutType() const
{
    return m_workoutType;
}

bool AutomationEngine::startWorkout(const QString &workoutType)
{
    if (workoutType.isEmpty()) {
        qWarning() << "AutomationEngine::startWorkout: empty workout type";
        return false;
    }
    
    if (m_workoutActive) {
        qWarning() << "AutomationEngine::startWorkout: workout already active";
        return false;
    }
    
    // Save current profile to restore later
    m_preWorkoutProfileId = m_profileManager->activeProfileId();
    
    // Look up workout profile - first check active profile's automation config
    PowerProfile activeProfile = m_profileManager->profile(m_preWorkoutProfileId);
    QString profileId;
    
    if (activeProfile.automation.workout_profiles.contains(workoutType)) {
        profileId = activeProfile.automation.workout_profiles[workoutType];
        qInfo() << "AutomationEngine: using per-profile workout mapping:" << workoutType << "->" << profileId;
    } else {
        // Fall back to global workout profiles
        profileId = m_profileManager->workoutProfileId(workoutType);
        if (!profileId.isEmpty()) {
            qInfo() << "AutomationEngine: using global workout mapping:" << workoutType << "->" << profileId;
        }
    }
    
    if (profileId.isEmpty()) {
        qWarning() << "AutomationEngine::startWorkout: no profile assigned to workout type:" << workoutType;
        return false;
    }
    
    if (!m_profileManager->hasProfile(profileId)) {
        qWarning() << "AutomationEngine::startWorkout: assigned profile does not exist:" << profileId;
        return false;
    }
    
    // Switch to workout profile
    m_workoutActive = true;
    m_workoutType = workoutType;
    m_currentSource = SourceWorkout;
    
    qInfo() << "AutomationEngine: starting workout:" << workoutType << "switching to profile:" << profileId;
    emit profileSwitchRequested(profileId, QStringLiteral("workout:%1").arg(workoutType));
    emit workoutStarted(workoutType, profileId);
    
    return true;
}

bool AutomationEngine::stopWorkout()
{
    if (!m_workoutActive) {
        qWarning() << "AutomationEngine::stopWorkout: no active workout";
        return false;
    }
    
    QString workoutType = m_workoutType;
    m_workoutActive = false;
    m_workoutType.clear();
    
    // Restore pre-workout profile
    if (!m_preWorkoutProfileId.isEmpty()) {
        qInfo() << "AutomationEngine: stopping workout:" << workoutType << "restoring profile:" << m_preWorkoutProfileId;
        
        // Reset to manual source (user's choice before workout)
        m_currentSource = SourceManual;
        
        emit profileSwitchRequested(m_preWorkoutProfileId, QStringLiteral("workout_end"));
    }
    
    m_preWorkoutProfileId.clear();
    
    // Re-evaluate rules now that workout is over
    evaluateRules();
    
    emit workoutStopped();
    
    return true;
}

bool AutomationEngine::isSleepTime() const
{
    return m_inSleepWindow;
}

void AutomationEngine::onBatteryLevelChanged(int level)
{
    bool currentlyCharging = m_batteryMonitor->charging();

    if (currentlyCharging && m_lastBatteryLevel >= 0 && !m_batteryMonitor->charging()) {
        // Transitioning to charging — but this is detected via charging state,
        // not level. Clear battery rule tracking when we detect charging started.
    }

    // Only evaluate battery rules when discharging
    if (!currentlyCharging) {
        evaluateBatteryRules(level);
    } else if (m_lastBatteryLevel >= 0) {
        // Charging started, clear battery rule state
        m_lastBatteryTriggeredProfile.clear();
    }

    m_lastBatteryLevel = level;
}

void AutomationEngine::onTimeCheck()
{
    evaluateTimeRules();
}

void AutomationEngine::evaluateRules()
{
    // Don't evaluate rules if workout is active (highest priority)
    if (m_workoutActive) {
        return;
    }
    
    // Check battery rules first (higher priority than time rules)
    if (!m_batteryMonitor->charging()) {
        evaluateBatteryRules(m_batteryMonitor->level());
    }
    
    // Check time rules
    evaluateTimeRules();
}

void AutomationEngine::evaluateBatteryRules(int batteryLevel)
{
    // Don't evaluate if workout is active
    if (m_workoutActive) {
        return;
    }
    
    // Get current active profile
    QString activeProfileId = m_profileManager->activeProfileId();
    PowerProfile activeProfile = m_profileManager->profile(activeProfileId);
    
    if (activeProfile.id.isEmpty()) {
        return;
    }
    
    // Find the highest matching threshold
    QString targetProfile = findBatteryRuleProfile(batteryLevel, activeProfile.automation.battery_rules);
    
    if (targetProfile.isEmpty()) {
        // No rule matches, but if we previously triggered a battery rule and level went up,
        // we might want to clear tracking (already handled in onBatteryLevelChanged when charging)
        return;
    }
    
    // Don't re-trigger if we already switched for this rule
    if (targetProfile == m_lastBatteryTriggeredProfile) {
        return;
    }
    
    // Don't switch if already on the target profile
    if (targetProfile == activeProfileId) {
        m_lastBatteryTriggeredProfile = targetProfile;
        return;
    }
    
    // Verify target profile exists
    if (!m_profileManager->hasProfile(targetProfile)) {
        qWarning() << "AutomationEngine: battery rule target profile does not exist:" << targetProfile;
        return;
    }
    
    qInfo() << "AutomationEngine: battery rule triggered at" << batteryLevel << "% -> profile:" << targetProfile;
    
    m_lastBatteryTriggeredProfile = targetProfile;
    m_currentSource = SourceBatteryRule;
    
    emit profileSwitchRequested(targetProfile, QStringLiteral("battery:%1%").arg(batteryLevel));
}

void AutomationEngine::evaluateTimeRules()
{
    // Don't evaluate if workout is active
    if (m_workoutActive) {
        return;
    }
    
    // Get current active profile
    QString activeProfileId = m_profileManager->activeProfileId();
    PowerProfile activeProfile = m_profileManager->profile(activeProfileId);
    
    if (activeProfile.id.isEmpty()) {
        return;
    }
    
    bool nowInWindow = false;
    QString targetProfile;
    
    // Check all time rules
    for (const TimeRule &rule : activeProfile.automation.time_rules) {
        if (isInTimeWindow(rule.start, rule.end)) {
            nowInWindow = true;
            targetProfile = rule.switch_to_profile;
            break; // Use first matching window
        }
    }
    
    // Track sleep mode state changes
    bool wasInSleepWindow = m_inSleepWindow;
    m_inSleepWindow = nowInWindow;
    
    if (wasInSleepWindow != nowInWindow) {
        emit sleepModeChanged(nowInWindow);
    }
    
    if (nowInWindow) {
        // We're in a time window
        if (targetProfile.isEmpty() || targetProfile == activeProfileId) {
            // Already on target profile or invalid target
            m_lastTimeTriggeredProfile = targetProfile;
            return;
        }
        
        // Don't re-trigger if we already switched for this time rule
        if (targetProfile == m_lastTimeTriggeredProfile) {
            return;
        }
        
        // Battery rules have higher priority - don't override if we're in a battery-triggered profile
        if (m_currentSource == SourceBatteryRule) {
            qInfo() << "AutomationEngine: time rule suppressed by active battery rule";
            return;
        }
        
        // Verify target profile exists
        if (!m_profileManager->hasProfile(targetProfile)) {
            qWarning() << "AutomationEngine: time rule target profile does not exist:" << targetProfile;
            return;
        }
        
        qInfo() << "AutomationEngine: time rule triggered -> profile:" << targetProfile;
        
        m_lastTimeTriggeredProfile = targetProfile;
        m_currentSource = SourceTimeRule;
        
        emit profileSwitchRequested(targetProfile, QStringLiteral("time_window"));
    } else {
        // We're outside all time windows
        if (wasInSleepWindow && m_currentSource == SourceTimeRule) {
            // We just exited a time window - clear time rule tracking
            qInfo() << "AutomationEngine: exited time window";
            m_lastTimeTriggeredProfile.clear();
            // Don't automatically restore profile here - let user's manual selection or battery rules take over
        }
    }
}

QString AutomationEngine::findBatteryRuleProfile(int level, const QVector<BatteryRule> &rules) const
{
    if (rules.isEmpty()) {
        return QString();
    }
    
    // Sort rules by threshold ascending (lowest first)
    // When multiple rules match, the lowest threshold is the most aggressive
    QVector<BatteryRule> sortedRules = rules;
    std::sort(sortedRules.begin(), sortedRules.end(),
              [](const BatteryRule &a, const BatteryRule &b) {
                  return a.threshold < b.threshold;
              });
    
    // Find the highest threshold that the current level is at or below
    for (const BatteryRule &rule : sortedRules) {
        if (level <= rule.threshold) {
            return rule.switch_to_profile;
        }
    }
    
    return QString();
}

bool AutomationEngine::isInTimeWindow(const QString &start, const QString &end) const
{
    if (start.isEmpty() || end.isEmpty()) {
        return false;
    }
    
    QTime startTime = QTime::fromString(start, QStringLiteral("HH:mm"));
    QTime endTime = QTime::fromString(end, QStringLiteral("HH:mm"));
    QTime currentTime = QTime::currentTime();
    
    if (!startTime.isValid() || !endTime.isValid()) {
        qWarning() << "AutomationEngine: invalid time format in time rule:" << start << "-" << end;
        return false;
    }
    
    if (startTime < endTime) {
        // Same-day window: e.g., 08:00 - 17:00
        return currentTime >= startTime && currentTime < endTime;
    } else {
        // Overnight window: e.g., 22:00 - 06:00
        return currentTime >= startTime || currentTime < endTime;
    }
}

bool AutomationEngine::isInSleepWindow() const
{
    // Get current active profile
    QString activeProfileId = m_profileManager->activeProfileId();
    PowerProfile activeProfile = m_profileManager->profile(activeProfileId);
    
    if (activeProfile.id.isEmpty()) {
        return false;
    }
    
    // Check if we're in any time window
    for (const TimeRule &rule : activeProfile.automation.time_rules) {
        if (isInTimeWindow(rule.start, rule.end)) {
            return true;
        }
    }
    
    return false;
}
