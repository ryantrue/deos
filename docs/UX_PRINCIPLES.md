# DEOS UX/UI principles

DEOS is an operating environment for standalone touchscreen devices, not a collection of engineering demos. UX behavior is part of the platform contract.

## 1. Device-first

The primary interface is the physical 720x720 touchscreen.

Remote control, web setup, MQTT, Home Assistant and AI are integrations. They must not be required for the shell, settings, storage or local device operation to remain usable.

A failed optional service must degrade independently.

Examples:

- Wi-Fi failure must not prevent Home from loading.
- AI failure must not break Control or Settings.
- Missing SD must not prevent the OS from booting.
- A router outage must not make a valid OTA image roll back.

## 2. One foreground surface

DEOS does not emulate a desktop window manager.

The interaction model is:

```text
one foreground screen/app
+ background services
+ system overlays/dialogs
```

Navigation should be shallow and predictable:

```text
Home
 ├─ Settings
 │   ├─ Network
 │   ├─ Display
 │   ├─ Storage
 │   ├─ Developer
 │   └─ About
 ├─ System
 ├─ Storage
 ├─ AI
 ├─ Control
 └─ Apps
```

## 3. Touch ergonomics

For the 720x720 reference device:

- primary touch target: at least 52x52 px;
- destructive actions should be visually separated from ordinary actions;
- cards and rows must be tappable as a whole;
- no tiny desktop-style controls;
- important actions should be reachable without precision tapping;
- pressed state must be visible immediately.

Animations are optional. Input latency is not.

## 4. Immediate feedback

A touch should produce visible feedback in the same frame whenever possible.

Operations longer than roughly 300 ms must expose state such as:

- Connecting
- Scanning
- Formatting
- Updating
- Rebooting

The UI must not freeze while slow storage/network work is running. Blocking operations belong on worker tasks/services.

## 5. State is explicit

The UI presents state, not guesses.

Examples:

```text
Network
  SETUP
  CONNECTING
  CONNECTED
  OFFLINE

SD
  absent
  foreign
  ready
  needs-format
  busy
  error
```

The same system state should eventually be available to widgets, automations, remote APIs and AI through the common DEOS State + Actions model.

## 6. No destructive surprises

DEOS never automatically formats removable storage.

Storage rules:

### Readable card without DEOS marker

State: `foreign`.

Offer:

- Leave unchanged
- Initialize for DEOS
- Format for DEOS...

`Initialize for DEOS` is non-destructive. It creates the DEOS directory tree and volume marker while preserving existing files.

### Unsupported filesystem or layout

State: `needs-format`.

The card remains untouched.

Offer an explicit `Format for DEOS...` action.

### Generic hardware/driver error

State: `error`.

Do **not** offer formatting because DEOS has not safely classified the media.

Offer `Rescan`.

### Format confirmation

Formatting requires a dedicated confirmation screen stating that all data will be erased.

The canonical DEOS SD layout is:

- one partition occupying the card;
- FAT filesystem for broad host interoperability;
- DEOS marker and directory structure.

Current directory structure:

```text
/DEOS/
    Apps/
    AppData/
    Packages/
    Backups/
    Logs/

/Media/
    Music/
    Pictures/
    Video/

/Documents/
/Downloads/
```

Marker:

```text
/DEOS/.volume
```

The OS must remain fully usable without SD.

## 7. Network configuration UX

Wi-Fi credentials are never compiled into firmware.

With no saved profile:

1. DEOS boots directly to Home in local-only mode.
2. The user opens `Settings -> Network`.
3. The device scans only after an explicit touch action.
4. The user selects an SSID and enters the password on the display.
5. Credentials are stored in device NVS and DEOS reboots into station mode.

The screen should be understandable without opening a serial console.

Forgetting Wi-Fi:

- removes SSID/password only;
- preserves DEOS device identity/API token;
- reboots back into local-only mode with Network settings available.

## 8. Developer Mode

Normal operation has one visible mode.

Developer Mode is hidden and opt-in, activated from build/about information rather than exposed as a normal user mode.

Initial activation:

```text
Settings -> About/System -> tap build 7 times
```

Developer surfaces may expose:

- FPS / frame time;
- internal RAM / PSRAM;
- task information;
- Wi-Fi diagnostics;
- SD diagnostics;
- I2C scan;
- touch coordinates;
- logs;
- hardware tests;
- sideload/debug controls.

Recovery/rollback is an implementation mechanism, not a day-to-day user mode.

## 9. Local-first system actions

System actions should eventually use the same action registry as widgets, automations and AI.

Examples:

```text
network.profile.forget
storage.sd.rescan
storage.sd.initialize
storage.sd.format
display.brightness.set
system.reboot
update.install
```

AI must call permission-gated actions rather than receiving unrestricted low-level system access.

## 10. Visual language

Reference direction:

- dark, restrained system chrome;
- information hierarchy before decoration;
- large rounded cards;
- limited simultaneous accent colors;
- readable state labels;
- no dense developer-dashboard aesthetic in normal mode;
- square-screen-native layouts rather than scaled phone UI.

The UI should feel like a finished appliance, not an ESP-IDF demo.

## 11. Performance budget is a UX requirement

Every new visual/runtime feature must be evaluated against:

- input latency;
- frame time;
- internal RAM;
- PSRAM bandwidth;
- flash size;
- background CPU load.

A feature that makes normal interaction visibly sluggish is not ready for the default shell.

The target is stable 30/60 FPS where the screen content permits it, with immediate touch response and minimal full-screen redraws.

## 12. Product rule

When choosing between broader feature coverage and a coherent responsive device experience, DEOS chooses the coherent responsive experience.
