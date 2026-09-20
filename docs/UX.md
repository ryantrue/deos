# DEOS UX model

DEOS uses a mobile-style system shell with an adaptive tile home surface. The goal is not to clone iOS or Windows Phone; it combines their useful interaction ideas with DEOS Resources, State and Actions.

## Interaction model

- one foreground surface at a time;
- explicit Home and Back navigation;
- no desktop-style floating windows;
- Home is a live workspace, not only an icon launcher;
- tiles may display state and invoke Actions;
- system screens use one visual language for rows, cards, dialogs and destructive confirmation;
- touch targets are intentionally large for a 720x720 embedded display;
- normal operation has no collection of recovery/safe modes;
- Developer Mode is hidden from normal users and is enabled from build information;
- once enabled, Developer Mode is stored in device preferences and survives reboot/OTA;
- Developer Mode has an explicit disable action and never changes the normal boot path.

## First-run experience

The first boot uses the normal system shell, not a separate recovery/setup mode.

If `setup_done` is absent from device preferences, DEOS presents:

```text
Welcome
  ↓
Network
  ↓
Storage
  ↓
Ready
  ↓
Home
```

Rules:

- every step is optional;
- **Use now** exits setup immediately and marks first-run complete;
- networking may still be starting or unavailable without blocking setup;
- storage is summarized but never modified automatically;
- destructive SD formatting remains in the regular Storage confirmation flow;
- completion is stored in NVS and survives OTA;
- all setup functions remain available later in Settings.

## Current navigation

```text
Home
├── AI
├── Control
├── Automations
├── Storage
├── System
├── Apps
└── Settings
    ├── Network
    ├── Display
    ├── Storage
    ├── Software Update
    ├── About / System
    └── Developer  (hidden until enabled)
```

The Home dock provides direct access to:

```text
Home  ·  Control  ·  AI  ·  Apps
```

The top-right system status pill is interactive and opens **Quick Settings**. Its label reflects the current connectivity state:

```text
LOCAL  ·  SETUP  ·  WIFI
```

Quick Settings provides brightness plus direct Network, Storage and Settings entry points. Brightness still updates `Display/primary` desired state through the reconciler; system chrome never bypasses the resource model.

A control that looks interactive must have a meaningful action. Placeholder features should open an explanatory system surface rather than silently ignore touch.

## Declarative settings

Durable device configuration must not bypass the resource model.

Example:

```text
brightness slider
      ↓
Display/primary.spec.brightness
      ↓
ResourceRuntime
      ↓
Reconciler
      ↓
DisplayController
      ↓
LEDC / backlight
```

The UI does not directly manipulate the hardware driver.

User preferences that must survive reboot and OTA are stored in device NVS and are used to build the initial desired state at boot.

## AI UX

AI is presented as an optional intelligence layer, not as the operating system itself.

When no provider is configured:

- Home, Settings, Storage, Control and Automations remain available;
- the AI surface clearly reports that no provider is configured;
- the OS does not degrade into an error state.

When AI is added, it will consume the same typed State and capability-scoped Actions as other DEOS clients.

## Destructive actions

Destructive operations require a dedicated confirmation surface.

Rules:

1. No destructive action runs because a mount, network or app operation failed.
2. The screen must say what will be destroyed.
3. The safe action and destructive action must be visually distinct.
4. The destructive action begins only after explicit confirmation.
5. Long-running operations run outside the LVGL task and expose Busy state.

The SD formatting flow follows these rules.

## Performance goals

These are targets, not yet hardware-certified guarantees:

- visible input response should begin within one frame where possible;
- avoid expensive work in LVGL callbacks;
- storage formatting and similar operations run on worker tasks;
- redraw stateful screens only when their underlying snapshot changes;
- prefer partial rendering and bounded object trees;
- keep the shell useful even if optional network or AI services fail.

## Visual direction

The system chrome is calm and mobile-like:

- dark neutral base;
- consistent rounded geometry;
- restrained status colors;
- clear typography hierarchy;
- minimal decoration.

The Home surface is tile-centric:

- mixed tile sizes;
- state shown directly on the tile;
- tiles evolve into projections of State + Actions rather than static shortcuts.

This gives DEOS its own product language while keeping the interaction model familiar.


## Software update surface

Settings exposes a read-only **Software Update** system surface. It shows:

- current firmware build;
- ESP-IDF version;
- active OTA slot;
- next inactive OTA slot;
- rollback policy;
- whether `Update/system` is currently Ready.

The shell does not write flash directly. Developer uploads still flow through the `Update/system` resource/control-plane endpoint, and the UI only exposes system state and guidance.
