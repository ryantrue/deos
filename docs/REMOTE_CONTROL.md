# Remote control and OTA

DEOS exposes a small local control plane once `Network/wifi` is reconciled.

This document describes the **developer profile** implemented during the ESP32-P4 bring-up phase. It is intentionally local-first and dependency-light. It is not yet the final production security profile.

## First network provisioning

Wi-Fi credentials are not compiled into firmware and must not be committed to the repository.

If no profile exists in the DEOS NVS namespace, the device creates a WPA2 setup network:

```text
DEOS-SETUP-XXXX
```

The generated setup password and DEOS API token are printed to the developer serial console.

1. Join the setup network.
2. Open `http://192.168.4.1/`.
3. Enter the target Wi-Fi SSID and password.
4. DEOS stores them in NVS and reboots.
5. The device reconnects as a station and advertises `deos.local` over mDNS.

The setup AP is not intended to remain enabled after provisioning.

## Device API

Read-only status does not require a token:

```http
GET /api/v1/status
```

State-changing developer endpoints require:

```http
X-DEOS-Token: <token>
```

Current endpoints:

```text
GET  /api/v1/status
POST /api/v1/reboot
POST /api/v1/ota
```

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

python3 tools/deosctl/deosctl.py reboot
python3 tools/deosctl/deosctl.py ota firmware/esp32p4/build/deos_esp32p4.bin
```

The token can also be passed with `--token`.

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
mark application valid
```

The local health check currently requires:

- `System/device = Ready`
- `Display/primary = Ready`
- `Shell/home = Ready`
- `Input/touch = Ready`

External Wi-Fi reachability is deliberately not part of rollback health. A router outage must not make a valid OS image roll back.

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
