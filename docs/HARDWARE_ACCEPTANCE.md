# ESP32-P4 hardware acceptance

This checklist is the merge gate for the first interactive/network/OTA DEOS milestone on the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.

The goal is not only "it boots". The build should demonstrate that the product architecture, UX and failure isolation work together on real hardware.

## Reference hardware

- Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
- ESP32-P4 rev 1.x
- 32 MB flash
- 32 MB PSRAM
- ST7703 720x720 MIPI-DSI
- GT911 touch
- ESP32-C6 Wi-Fi coprocessor
- optional microSD

## 1. Boot and local-first behavior

Pass when:

- bootloader recognizes 32 MB flash;
- 32 MB PSRAM initializes at the configured speed;
- `System/device`, `Display/primary`, `Storage/sd`, `Shell/home` and `Input/touch` reconcile independently;
- Home becomes usable before optional network connectivity is required;
- a missing router, missing SD card or unavailable AI provider does not prevent the shell from loading;
- the screen remains responsive after `app_main()` returns.

Expected architecture:

```text
stage 1
System -> Display -> Storage -> Shell + Touch

stage 2
System -> Network -> Update
```

## 2. Touch and navigation

Pass when:

- GT911 is detected at either supported address;
- tap coordinates match the visible control under the finger;
- Home tiles are tappable as whole cards;
- Back returns to the previous system surface;
- Home clears navigation history;
- Quick Settings returns to its caller rather than always forcing Home;
- repeated navigation for several minutes does not crash or progressively lose internal RAM;
- visible pressed state begins immediately.

Check at least:

```text
Home -> Settings -> Storage -> Back -> Settings -> Back -> Home
Home -> System -> Back
Home -> Quick Settings -> Network -> Back -> Quick Settings
Apps -> Control -> Back -> Apps
```

## 3. Motion and feedback

Pass when:

- short screen fades remain visually smooth;
- fades do not delay touch handling;
- brightness/storage actions show a transient result toast;
- navigation while a toast is visible does not crash or leave stale LVGL timers;
- long storage operations expose durable Busy state instead of relying on a toast.

If transitions make interaction visibly slower, disable/reduce them rather than accepting degraded input latency.

## 4. First-run flow

With NVS cleared, verify:

```text
Welcome -> Network -> Storage -> Ready -> Home
```

Pass when:

- every step can be skipped;
- `Use now` enters Home without requiring network/SD;
- setup completion persists across reboot;
- Wi-Fi configuration can reboot the device and resume onboarding at Storage;
- browser-based Wi-Fi provisioning and on-device Wi-Fi provisioning resume at the same persisted next step;
- completing first-run does not create a separate permanent operating mode.

## 5. Wi-Fi and local control

Pass when:

- ESP32-C6 transport initializes over 4-bit SDIO;
- ESP-Hosted creates its SDIO mempool in PSRAM without an early-boot assert;
- logs do not warn that `CONFIG_FREERTOS_HZ` is below the recommended 1000 Hz;
- nearby Wi-Fi networks can be scanned from the touchscreen without freezing LVGL;
- SSID selection opens the system keyboard;
- credentials are stored only in device NVS;
- station mode obtains an IP address;
- `deos.local` resolves on a compatible LAN;
- the setup AP remains a fallback path when no station profile exists;
- setup identity generation never aborts when the P4 has no local Wi-Fi MAC;
- forgetting Wi-Fi preserves device/API identity but removes SSID/password;
- after forget + reboot, provisioning is available again.

## 6. State + Actions

Pass when local UI shows live typed state for at least:

- system ready;
- uptime;
- internal RAM;
- PSRAM;
- task count;
- display brightness;
- SD state/capacity;
- network mode/IP/SSID according to permission policy.

Pass when these Actions work through the registry:

- `display.brightness.set`;
- `storage.sd.rescan`;
- `storage.sd.initialize`;
- `storage.sd.format` from the local destructive-confirm flow only;
- `network.wifi.configure`;
- `network.wifi.forget`.

The UI must not directly bypass the Action/Resource model for durable configuration.

## 7. SD: absent

Boot with no card.

Pass when:

- `Storage/sd` reports `absent` rather than blocking boot;
- Home and Settings remain usable;
- Storage offers Rescan;
- no Format action is shown for an absent card.

Insert a card and use Rescan.

## 8. SD: readable foreign card

Use a FAT card containing ordinary user files but no `DEOS/.volume`.

Pass when:

- DEOS classifies it as `foreign`;
- existing files are visible to the filesystem and remain unchanged;
- UI offers:
  - Leave unchanged;
  - Initialize for DEOS;
  - Format instead...;
- `Initialize for DEOS` creates the DEOS directories and marker without deleting the pre-existing test files;
- after initialization, state becomes `ready`.

## 9. SD: unsupported/foreign partition layout

Use a disposable test card with an unsupported filesystem or stale/multi-partition/GPT layout.

Pass when:

- the normal probe never formats the media;
- state is `needs-format` only when the media was classified sufficiently for a destructive option;
- generic hardware/driver error does not expose Format;
- formatting requires the dedicated destructive confirmation screen;
- explicit formatting clears stale primary and backup partition metadata;
- the resulting layout is one portable FAT partition occupying the card;
- desktop tools no longer report the old GPT/partition layout;
- `DEOS/.volume` and the standard directory tree are created.

Never perform this test on a card containing data that has not been backed up.

## 10. Display settings

Pass when:

- Quick Settings brightness slider responds smoothly;
- Display settings slider represents the same desired state;
- brightness is constrained to 10–100%;
- a change reconciles through `Display/primary`;
- brightness persists across reboot/OTA;
- UI reports the actual Action result.

## 11. Developer Mode

Pass when:

- Developer Mode is hidden initially;
- seven build taps enable it;
- enabling shows a result toast;
- state persists across reboot/OTA;
- it can be disabled explicitly;
- disabling does not change normal boot behavior;
- normal users never see recovery/safe-mode choices.

## 12. Remote API

On the trusted development LAN:

```bash
python3 tools/deosctl/deosctl.py status
python3 tools/deosctl/deosctl.py entities
python3 tools/deosctl/deosctl.py actions
```

With `DEOS_TOKEN` configured:

```bash
python3 tools/deosctl/deosctl.py invoke display.brightness.set --arg value=65
```

Pass when:

- status reports `firmware.slot` (`ota_0` or `ota_1`) and `firmware.ota_state`;
- read-only/status behavior follows the documented policy;
- authenticated mutations require the per-device token;
- generic remote Action invocation is capability-gated;
- remote actor cannot invoke `storage.destructive`;
- malformed JSON/action arguments do not crash the HTTP service.

## 13. A/B OTA

The first migration from the old factory partition table is done by one full USB bootstrap.

After station networking is working:

1. note the current OTA slot;
2. build/download a newer CI application image;
3. run `deosctl ota <image>`;
4. observe reboot;
5. confirm the alternate OTA slot is active;
6. confirm the serial log reports the 10 second stability window and status
   reports `valid` after it expires;
7. confirm settings/NVS survive;
8. repeat in the opposite direction later.

Pass when OTA never requires rewriting the partition table/bootloader during an ordinary app update.

## 14. Resource budget

Record after the acceptance run:

- application binary size;
- internal RAM free after idle Home;
- largest internal free block;
- PSRAM free;
- task count;
- boot time until Home visible;
- approximate touch-to-feedback latency;
- UI frame-rate/frame-time observation.

These values become the baseline for future regressions.

## Merge criterion

PR #3 can merge when:

- Host CI is green;
- ESP32-P4 ESP-IDF 6.1 CI is green;
- the complete flash package passes hashes;
- the local shell/touch/storage tests pass;
- Wi-Fi provisioning and remote API pass;
- at least one real OTA slot switch succeeds;
- destructive SD behavior is verified on disposable media or explicitly deferred with the implementation kept disabled.

Hardware evidence is more important than preserving an architectural assumption.
