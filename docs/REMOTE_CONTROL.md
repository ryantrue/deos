# Remote control and OTA

DEOS exposes a small local control plane once `Network/wifi` is reconciled.

This document describes the **developer profile** implemented during the ESP32-P4 bring-up phase. It is intentionally local-first and dependency-light. It is not yet the final production security profile.

## Local network configuration

Wi-Fi credentials are not compiled into firmware and must not be committed to the repository.

If no profile exists in the DEOS NVS namespace, DEOS stays local-only and does
not create an access point. Configure Wi-Fi on the display:

```text
Home -> Settings -> Network -> Choose Wi-Fi
```

Choose an SSID, enter its password with the system keyboard, and confirm.
DEOS stores the profile in NVS, reboots, connects as a station, and advertises
`deos.local` over mDNS. The API token is shown only in Developer settings; it
is never printed in the serial log.

## Device API

Read-only status does not require a token:

```http
GET /api/v1/status
```

Status includes the active OTA slot, firmware/ESP-IDF versions and the current
OTA image state. This makes slot switches and rollback acceptance observable
without relying only on serial logs.

Typed state/action introspection and state-changing developer endpoints require:

```http
X-DEOS-Token: <token>
```

Current endpoints:

```text
GET  /api/v1/status
GET  /api/v1/entities
GET  /api/v1/actions
POST /api/v1/action
POST /api/v1/reboot
POST /api/v1/ota
```

The generic action endpoint uses the same `ActionRegistry` as the local UI. A remote request cannot bypass action capability checks.

Developer LAN requests currently receive only these capabilities:

```text
display.control
storage.control
network.control
```

`storage.destructive` is deliberately **not** granted to the remote API. SD formatting therefore remains an on-device, explicitly confirmed action in this profile.

## deosctl

The control CLI uses only the Python standard library.

```bash
python3 tools/deosctl/deosctl.py status
```

The default device is `http://deos.local`. An IP address can be supplied explicitly:

```bash
python3 tools/deosctl/deosctl.py status --device http://192.168.1.50
```

For authenticated operations:

```bash
export DEOS_TOKEN='<token from the device>'

python3 tools/deosctl/deosctl.py entities
python3 tools/deosctl/deosctl.py actions

python3 tools/deosctl/deosctl.py invoke display.brightness.set --arg value=65

python3 tools/deosctl/deosctl.py reboot
python3 tools/deosctl/deosctl.py ota firmware/esp32p4/build/deos_esp32p4.bin
```

The token can also be passed with `--token`.

Examples:

```bash
# Typed state
python3 tools/deosctl/deosctl.py entities

# Discover callable actions/capabilities
python3 tools/deosctl/deosctl.py actions

# Safe system mutation through the same ActionRegistry used by the UI
python3 tools/deosctl/deosctl.py invoke display.brightness.set --arg value=72

# This is intentionally denied remotely because the developer LAN actor
# does not have storage.destructive.
python3 tools/deosctl/deosctl.py invoke storage.sd.format
```

## A/B application update

The 32 MB ESP32-P4 flash layout contains two 6 MiB application slots:

```text
ota_0  6 MiB
ota_1  6 MiB
```

An OTA upload always targets the inactive application slot.

The update path is:

```text
upload application image
        ↓
esp_ota_begin
        ↓
stream into inactive slot
        ↓
esp_ota_end / image validation
        ↓
select new boot partition
        ↓
reboot
        ↓
local DEOS health checks
        ↓
10 second runtime stability window
        ↓
mark application valid
```

The local health check currently requires:

- `System/device = Ready`
- `Display/primary = Ready`
- `Shell/home = Ready`
- `Input/touch = Ready`

External Wi-Fi reachability is deliberately not part of rollback health. A router outage must not make a valid OS image roll back.

If the local health checks fail, the stability task cannot be started, or the
new image resets during the stability window, the image remains pending and
the ESP-IDF bootloader can roll it back on the next boot.

## First migration to the OTA partition table

The older DEOS bring-up builds used a factory application at `0x10000`. The A/B layout moves the first application to `ota_0` at `0x20000` and introduces `otadata`.

For the one-time migration, the ESP32-P4 flash should be erased before writing the complete CI flash package. This prevents data from the old partition layout from being interpreted as new OTA metadata.

The ESP32-C6 is a separate chip with separate flash; erasing the P4 flash does not erase C6 firmware.

## Security status

The current OTA endpoint is for trusted developer LANs:

- per-device random API token;
- A/B application slots;
- ESP-IDF application image validation;
- rollback after an unhealthy new boot.

Before DEOS declares a production security profile, remote management will add at least:

- authenticated encrypted transport (HTTPS);
- signed firmware policy;
- secure-boot / flash-encryption deployment profile;
- token rotation/revocation;
- explicit resource permissions for remote operations.

The architecture should not depend on insecure transport; only the current bring-up adapter does.
