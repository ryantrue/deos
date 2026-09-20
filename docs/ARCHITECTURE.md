# Architecture v0.1

## Layers

```text
+--------------------------------------------------------------+
| UI / CLI / Automation / Remote / AI                          |
+---------------------------+----------------------------------+
                            |
                     Resource + Action API
                            |
+---------------------------v----------------------------------+
| Desired State Store | Planner | Dependency Graph | Status    |
+---------------------------+----------------------------------+
                            |
                  Event-driven Reconciler
                            |
+---------------------------v----------------------------------+
| Controllers: Display / Storage / Network / App / AI / ...    |
+---------------------------+----------------------------------+
                            |
| Providers / HAL / existing drivers                           |
+---------------------------+----------------------------------+
| ESP-IDF / FreeRTOS / SoC                                     |
+--------------------------------------------------------------+
```

DEOS owns the architecture above the platform layer. It does not attempt to replace FreeRTOS scheduling, ESP-IDF networking stacks or vendor peripheral drivers merely to claim implementation purity.

## Core primitives

### Resource
A durable object with identity, desired `spec`, dependencies and observed `status`.

```text
ResourceKey = kind + name
```

Examples: `Network/wifi`, `Storage/sd0`, `AIProvider/qwen`, `Dashboard/home`.

### State
Ephemeral or observed data. It is not automatically reconciled.

Examples: room temperature, RSSI, playback position, available storage.

### Action
A one-shot command with an explicit result. Actions do not become desired state unless a higher layer intentionally records a resource change.

Examples: take a photo, play next track, send an MQTT message.

### Event
An immutable occurrence consumed by interested components.

Examples: `sd.inserted`, `network.disconnected`, `touch.gesture`, `app.crashed`.

## Reconciliation

The reconciler is intentionally event-driven. DEOS does **not** run one polling loop per resource/controller.

A resource is queued when:

- its desired specification changes;
- one of its dependencies changes status;
- a relevant platform event indicates possible drift;
- an explicit refresh is requested.

The controller receives desired state and current applied state and returns an observed status.

This makes the control plane conceptually similar to controller/reconciliation systems while respecting MCU constraints.

## Plan/apply

Before durable changes are applied, the planner computes a deterministic set of operations:

```text
CREATE
UPDATE
DELETE
```

Future versions will extend plan steps with:

- destructive/non-destructive classification;
- permission requirements;
- estimated storage/download cost;
- restart/reboot requirements;
- dependency impact;
- rollback metadata.

A formatting operation for removable storage is an example of a destructive plan step that must require explicit confirmation.

## Desired state storage

The current PoC stores objects in memory. Planned persistent design:

1. developer-facing YAML/JSON;
2. schema validation and normalization in `deosctl`;
3. compile to a versioned typed binary IR;
4. atomically store desired-state revisions;
5. boot from the last committed revision.

The MCU should not need a full YAML implementation for normal operation.

## Status and drift

Each controller owns observed status. A resource may be:

- `Pending`
- `Waiting`
- `Ready`
- `Error`

Drift is visible when desired spec remains valid but observed reality is unavailable or different. Examples:

- desired SD mounted, actual card removed;
- desired AI provider enabled, endpoint unreachable;
- desired network connected, actual link down.

Drift is not always “fixed” blindly. Policies decide whether reconciliation should retry, wait or request user action.

## AI boundary

Models do not receive unrestricted access to hardware or a root shell.

Preferred flow:

```text
natural language
      ↓
AI provider
      ↓
proposed typed Resource/Action changes
      ↓
schema + policy validation
      ↓
plan
      ↓
user/policy approval
      ↓
deterministic execution
```

A local Qwen server can therefore become a reasoning coprocessor while DEOS remains authoritative for device state.

## UI boundary

UI components must use the same Resource/Action APIs as non-UI clients. A brightness slider changes `Display/primary.spec.brightness`; it does not bypass the control plane and call a backlight driver directly.

High-rate UI state (touch coordinates, animation progress, waveform samples) is not modeled as resources and stays in specialized runtime paths.

## Storage

Storage controllers will distinguish:

- media detection;
- filesystem probing;
- mounting;
- DEOS directory initialization;
- destructive formatting.

A supported foreign card may be used in place and optionally initialized with DEOS directories. Unsupported formats are never silently reformatted.

## Failure philosophy

DEOS should prefer explicit status and debuggability over a proliferation of end-user boot modes. A developer mode exposes internal state. Normal boot behavior should remain simple and deterministic.
