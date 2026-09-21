# DEOS — Declarative Edge OS

> Working name. The project is an experiment in building a **declarative, local-first operating environment for interactive edge devices**.

DEOS is not intended to be another ESP32 launcher, Home Assistant panel, or thin AI chat client. Its core hypothesis is that a physical device can be managed like modern infrastructure:

- **desired state** instead of imperative setup code;
- **plan/apply** before changes;
- **event-driven reconciliation** between desired and actual state;
- **Resources, State, Actions and Events** as separate primitives;
- one control model for UI, CLI, automations, remote management and optional AI;
- reproducible device configuration and observable drift;
- AI as an optional reasoning layer, never a requirement for basic operation.

The first hardware target is **Waveshare ESP32-P4-WIFI6-Touch-LCD-4B** (ESP32-P4, 720×720 MIPI-DSI, touch, audio, SD, USB, ESP32-C6 networking). The architecture is deliberately not tied to that board.

## Why this exists

Most embedded products eventually accumulate imperative callbacks, one-off configuration paths and duplicated control logic. The same setting may be changed differently from UI, OTA code, a mobile app and an AI agent.

DEOS experiments with a different rule:

> Every durable system change goes through the same typed resource model.

A UI slider, `deosctl`, an automation or an AI model does not directly poke hardware. It proposes a state change. The planner validates it, the reconciler applies it, and status records what actually happened.

```text
UI ─────────┐
CLI ────────┤
Automation ─┼──> Resource API ──> Plan ──> Reconciler ──> Controllers ──> ESP-IDF / hardware
AI ─────────┤                        │
Remote ─────┘                        └──────────────> Status / Events
```

## Four primitives

- **Resource** — durable desired configuration: Wi-Fi profile, installed app, dashboard, AI provider, storage policy.
- **State** — observed facts: temperature, free space, connectivity, playback position.
- **Action** — a one-shot operation: capture photo, play track, publish MQTT message.
- **Event** — something happened: SD inserted, network lost, button pressed, alert raised.

Not everything is a Resource. This boundary is intentional.

## Prototype status

The project now has two working layers.

### Portable core

- deterministic `CREATE / UPDATE / DELETE` planning;
- idempotent `apply`;
- event-driven reconciliation queue;
- resource dependencies and dependent re-reconciliation;
- explicit status (`Pending / Waiting / Ready / Error`);
- event bus;
- host tests;
- zero-dependency `deosctl`.

### ESP32-P4 milestone

The reference firmware currently includes:

- 720x720 ST7703 MIPI-DSI display + LVGL 9.5;
- GT911 touch input;
- adaptive tile Home and system navigation;
- Network, Display, Storage, System and Developer surfaces;
- runtime desired-state mutation through the same Reconciler used at boot;
- persistent display preferences in NVS;
- safe SD-card classification, initialization, rescan and explicit formatting;
- ESP32-C6 networking through ESP-Hosted / Wi-Fi Remote;
- on-device Wi-Fi configuration and `deos.local`;
- remote status, reboot and application OTA through `deosctl`;
- 32 MB A/B application layout with rollback.

CI validates the host core and ESP-IDF 6.1 build. Hardware validation remains mandatory before feature branches are merged.

## Try the architecture on a desktop

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/deos_host_demo
```

Try the IaC-style CLI:

```bash
python3 tools/deosctl/deosctl.py validate config/system.example.json
python3 tools/deosctl/deosctl.py plan config/system.example.json --actual config/system.actual.json
```

## Product principles

1. **Useful without AI.** UI, apps, automations, storage, integrations and actions continue to work when no model is configured.
2. **AI proposes; the OS applies.** A model may create a plan or automation, but deterministic system code validates and executes it.
3. **Local-first, not local-only.** Local Qwen/Ollama/vLLM can be first-class, while remote providers remain optional.
4. **No hidden destructive behavior.** For example, an unknown SD filesystem is never silently formatted.
5. **One developer mode.** Debugging tools are for developers; normal operation stays simple.
6. **Hardware is a provider, not the architecture.** ESP-IDF and existing drivers are reused; DEOS owns the system model above them.
7. **Reproducibility matters.** A device should be exportable, inspectable and reconstructable from declared state plus secrets/data.

## Repository layout

```text
core/                portable declarative core
boards/              declarative board descriptions
docs/                product, architecture and ADRs
config/              example desired-state manifests
examples/host-demo/  desktop proof of concept
firmware/esp32p4/    ESP-IDF 6.1 bootstrap
tests/               host tests
tools/deosctl/       developer control-plane CLI prototype
```

## Roadmap

- `P0` — host core: plan/apply/reconcile/dependencies/tests ✅
- `P1` — same core under ESP-IDF 6.1 on ESP32-P4 ✅
- `P2` — Display / Touch / Storage / Network controllers — implemented, hardware validation in progress
- `P3` — resource-backed interactive LVGL shell and runtime settings — implemented, hardware validation in progress
- `P4` — safe SD provisioning + remote control + A/B OTA — implemented, hardware validation in progress
- `P5` — typed State + Actions registry and local OpenAI-compatible/Qwen provider
- `P6` — local automation engine over the same Action registry
- `P7` — installable package/application model after the control plane proves stable

Product and engineering references:

- [Product definition](docs/PRODUCT.md)
- [Architecture](docs/ARCHITECTURE.md)
- [UX model](docs/UX.md)
- [Storage / SD policy](docs/STORAGE.md)
- [Remote control and OTA](docs/REMOTE_CONTROL.md)

## License

DEOS-owned code is licensed under the **Apache License 2.0**. Third-party components retain their own licenses. See `LICENSE` and `THIRD_PARTY.md`.
