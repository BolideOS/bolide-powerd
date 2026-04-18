# bolide-powerd

Power management daemon for [AsteroidOS](https://asteroidos.org/). Gives you profile-based control over sensors, radios, and system settings on your watch.

<p align="center">
  <img src="docs/screenshots/01-settings-menu.png" width="200" alt="Settings menu showing Power Manager entry"/>
  <img src="docs/screenshots/02-power-manager-page.png" width="200" alt="Power Manager main page"/>
  <img src="docs/screenshots/03-profiles-list.png" width="200" alt="Profile list with all available profiles"/>
  <img src="docs/screenshots/04-profile-editor.png" width="200" alt="Profile editor with fill-bar controls"/>
</p>

## What is this?

`bolide-powerd` is a system daemon that manages power on AsteroidOS watches. Instead of just having one "battery saver" toggle, you get multiple profiles that are each tuned for a specific situation (indoor, outdoor, health tracking, workouts, etc). Each profile has its own config for every sensor, radio, and system setting on the watch. The daemon can also switch profiles automatically based on battery level or time of day.

Everything is exposed over D-Bus (`org.bolideos.powerd`) so any UI can talk to it. The settings UI lives in [asteroid-settings](https://github.com/AsteroidOS/asteroid-settings).

## Features

- 8 built-in profiles for common use cases (see [Profiles](#built-in-profiles))
- Custom profiles with per-sensor, per-radio control
- Automation: auto-switch on battery thresholds or time-of-day
- Workout mode: quick profile switching for treadmill, running, cycling, hiking
- Battery telemetry: history ring buffer, drain rate, time-remaining prediction
- Two sensor backends: SensorFW (D-Bus) with sysfs fallback
- Radio control for BLE, Wi-Fi, LTE, NFC with sync scheduling
- System settings: always-on display, tilt-to-wake, background sync (via MCE/ConnMan)
- D-Bus API with 14 methods and 5 signals (see [D-Bus Interface](#d-bus-interface))
- Systemd service with D-Bus activation
- 60 language translation templates
- 6 unit test programs

## Sensor Control

The daemon manages 9 sensors. Each one has multiple operating modes that control sampling rate and power draw.

### Accelerometer & Gyroscope

These share the same set of modes:

| Mode | Sampling Interval | Frequency | Typical use |
|------|-------------------|-----------|-------------|
| Off | n/a | n/a | Disabled |
| Low | 200 ms | 5 Hz | Step counting, basic motion |
| Medium | 40 ms | 25 Hz | Gesture recognition, tilt-to-wake |
| High | 20 ms | 50 Hz | Activity tracking, navigation |
| Workout | 10 ms | 100 Hz | Full speed for exercise tracking |

So for example, the "Health Indoor" profile runs accelerometer at medium (25 Hz for gesture detection) and gyroscope at low (5 Hz, just for basic orientation). That's enough for step counting without burning through the battery like workout mode would.

### Heart Rate

| Mode | Sampling Interval | Typical use |
|------|-------------------|-------------|
| Off | n/a | Disabled |
| Low | Every 30 min | Resting HR spot checks |
| Medium | Every 5 min | All-day monitoring |
| High | Every 1 min | Active health tracking |
| Workout | Every 1 sec | Continuous during exercise |

### HRV (Heart Rate Variability)

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| Sleep Only | Only active during detected sleep |
| Always | Continuous tracking |

### SpO2 (Blood Oxygen)

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| Periodic | Spot checks at intervals |
| Continuous | Always reading |

### Barometer

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| Low | Infrequent readings, good for weather trends |
| High | Frequent readings for altitude/floor counting |

### Compass (Magnetometer)

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| On-Demand | Only active when an app asks for heading |
| Continuous | Always on, for navigation and maps |

### Ambient Light Sensor

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| Low | Infrequent, for auto-brightness |
| High | Frequent, for outdoor sunlight adaptation |

### GPS

| Mode | What it does |
|------|--------------|
| Off | Disabled |
| Periodic | 60-second duty cycle: get a fix, sleep, repeat |
| Continuous | Always on for runs, hikes, cycling |

## The Fill-Bar UI

In the settings app, each sensor and radio shows up as a row with a vertical green bar on the right. It's a multi-state toggle: tap the row to cycle through modes.

```
 ┌─────────────────────────────┬───┐
 │ Accelerometer          Low  │▐  │  <- 25% filled (green from bottom)
 ├─────────────────────────────┼───┤
 │ Gyroscope              Off  │▐  │  <- 0% filled (all grey)
 ├─────────────────────────────┼───┤
 │ Heart Rate             High │▐  │  <- 75% filled
 ├─────────────────────────────┼───┤
 │ GPS              Continuous │▐  │  <- 100% filled (all green)
 └─────────────────────────────┴───┘
```

The fill height maps to where the mode sits in the list:
- 0% (grey) = Off
- 25% = Low
- 50% = Medium
- 75% = High
- 100% (green) = Workout / Continuous / Always

For simple on/off toggles like BLE or NFC, the bar is just fully grey or fully green. You can glance at the whole profile and immediately see which sensors are cranked up and which are off.

## Radio Control

Four radios, each with independent power state and sync scheduling:

### BLE (Bluetooth Low Energy)
- State: Off / On
- Sync mode: Manual, Interval (1h to 24h), or Time Window
- Can optionally turn off during sleep

### Wi-Fi
- State: Off / On
- Sync mode: Manual, Interval, or Time Window
- Optional sleep disable

### LTE (Cellular)
- State: Off / Calls Only / Always
- "Calls Only" keeps the radio in low-power standby, only waking for incoming calls

### NFC
- State: Off / On

## System Settings

| Setting | Options | What it does |
|---------|---------|--------------|
| Background Sync | Auto / When Radios On / Off | Whether apps can sync data in the background |
| Always-On Display | On / Off | Keep screen dimly lit (MCE `display/use-low-power-mode`) |
| Tilt to Wake | On / Off | Wake screen on wrist raise (MCE `display/enable-gesture-actions`) |

Applied through MCE (Mode Control Entity) and ConnMan D-Bus interfaces.

## Built-in Profiles

| Profile | Accel | Gyro | HR | HRV | SpO2 | Baro | Compass | ALS | GPS | BLE | Wi-Fi | AOD | What it's for |
|---------|-------|------|----|-----|------|------|---------|-----|-----|-----|-------|-----|---------------|
| Ultra Saver Indoor | Low | Off | Off | Off | Off | Off | Off | Off | Off | Off | Off | Off | Max battery life at home |
| Ultra Saver Outdoor | Low | Off | Low | Off | Off | Low | On-Demand | Low | Periodic | On (10h) | Off | Off | Light outdoor use, low battery |
| Health Indoor | Med | Low | High | Sleep | Off | Off | Off | Low | Off | On (2h) | Off | Off | Gym, indoor health tracking |
| Health Outdoor | Med | Med | High | Sleep | Off | Low | On-Demand | Low | Continuous | On (1h) | Off | Off | Running, walking outdoors |
| Smartwatch Indoor | Med | Med | Med | Off | Off | Off | Off | Low | Off | On | On (2h) | On | Daily wear at home/office |
| Smartwatch Outdoor | Med | Med | Med | Off | Off | Low | On-Demand | High | Periodic | On | Off | On | Daily wear outside |
| Performance Outdoor | Max | Max | Max | Always | Continuous | High | Continuous | High | Continuous | On | Off | On | Full workout tracking |

## Automation

### Battery Rules

Switch profiles automatically when battery drops below a level:

```json
{
  "threshold": 20,
  "switch_to_profile": "ultra_saver_indoor"
}
```

When battery hits 20%, it switches to Ultra Saver to stretch the remaining charge.

### Time Rules

Schedule profile switches by time of day:

```json
{
  "start": "23:00",
  "end": "07:00",
  "switch_to_profile": "ultra_saver_indoor"
}
```

Go into Ultra Saver overnight, switch back in the morning.

### Workout Mapping

Map workout types to profiles so you can switch with one tap:

```json
{
  "treadmill": "health_indoor",
  "outdoor_run": "health_outdoor",
  "cycling": "performance_outdoor",
  "hiking": "health_outdoor"
}
```

## D-Bus Interface

Service: `org.bolideos.powerd`
Object: `/org/bolideos/powerd`
Interface: `org.bolideos.powerd.ProfileManager`

### Methods

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `GetProfiles` | none | JSON string | List all profiles |
| `GetActiveProfile` | none | Profile ID | Currently active profile |
| `SetActiveProfile` | `id: string` | `bool` | Switch to a profile |
| `GetProfile` | `id: string` | JSON string | Get one profile's full config |
| `UpdateProfile` | `json: string` | `bool` | Update an existing profile |
| `AddProfile` | `json: string` | Profile ID | Create a new profile, returns its ID |
| `DeleteProfile` | `id: string` | `bool` | Remove a profile (built-ins are protected) |
| `StartWorkout` | `type: string` | `bool` | Start workout, switches to mapped profile |
| `StopWorkout` | none | `bool` | End workout, go back to previous profile |
| `GetWorkoutProfiles` | none | JSON string | Get workout-to-profile mapping |
| `SetWorkoutProfile` | `type, id: string` | `bool` | Map a workout type to a profile |
| `GetBatteryHistory` | `hours: int` | JSON string | Battery level history |
| `GetBatteryPrediction` | none | JSON string | Drain rate and time remaining estimate |
| `GetCurrentState` | none | JSON string | Full daemon state snapshot |

### Signals

| Signal | Args | When |
|--------|------|------|
| `ActiveProfileChanged` | `id, name` | Profile switch happens |
| `ProfilesChanged` | none | A profile is added, updated, or deleted |
| `WorkoutStarted` | `type, profileId` | Workout begins |
| `WorkoutStopped` | none | Workout ends |
| `BatteryLevelChanged` | `level, charging` | Battery level changes |

## Building

### Dependencies

- Qt 5.12+ (Core, DBus, Test)
- CMake 3.10+
- Systemd (for service install)

### Compile

```bash
mkdir build && cd build
cmake ..
make
```

### Run tests

```bash
cd build
ctest --output-on-failure
```

### Install (on device)

```bash
make install
systemctl enable bolide-powerd
systemctl start bolide-powerd
```

Or through the AsteroidOS Yocto build system:

```bash
source oe-core/oe-init-build-env build
bitbake bolide-powerd
```

## Architecture

```
┌──────────────────────────────────────────────┐
│              D-Bus Interface                 │
│    org.bolideos.powerd.ProfileManager      │
└────────────────────┬─────────────────────────┘
                     │
         ┌───────────┴───────────┐
         │    Profile Manager    │
         │  (JSON load/save)     │
         └───────────┬───────────┘
                     │
    ┌────────────────┼────────────────┐
    │                │                │
┌───┴───┐    ┌──────┴──────┐   ┌─────┴─────┐
│Sensor │    │   Radio     │   │  System   │
│Control│    │  Controller │   │ Controller│
└───┬───┘    └──────┬──────┘   └─────┬─────┘
    │               │                │
┌───┴───┐    ┌──────┴──────┐   ┌─────┴─────┐
│SensorFW│   │BlueZ/ConnMan│   │    MCE     │
│ sysfs  │   │  D-Bus      │   │   D-Bus    │
└────────┘   └─────────────┘   └───────────┘
                     │
         ┌───────────┴───────────┐
         │  Automation Engine    │
         │ (battery/time rules)  │
         └───────────┬───────────┘
                     │
         ┌───────────┴───────────┐
         │   Battery Monitor     │
         │ (ring buffer, predict)│
         └───────────────────────┘
```

## Known Issues

This is a first version. It works but there are things to fix:

- **SensorFW race condition**: The `availableSensors()` D-Bus call is async, so the sensor list might be empty on first check. Falls back to sysfs which works fine in practice.
- **Workout state sync**: The workout active/inactive state between `DBusInterface` and `AutomationEngine` can get out of sync in edge cases.
- **GPS timer lifecycle**: The periodic GPS timer doesn't get cleanly stopped in all code paths.
- **Screen-on detection**: `screenOn` state isn't wired up yet.
- **TimeWindow sync mode**: Only fires once instead of repeating.

## License

GPLv3. See [LICENSE](LICENSE).

## Contributing

This is early days. Issues and PRs welcome.

If you're from the AsteroidOS project and want to bring this repo into the org, let us know. We'd be happy to transfer it.
