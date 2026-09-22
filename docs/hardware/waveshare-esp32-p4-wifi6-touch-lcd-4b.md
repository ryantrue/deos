# Waveshare ESP32-P4-WIFI6-Touch-LCD-4B hardware notes

Last reviewed: 2026-09-22

This file records board-specific facts, upstream behavior, known failure modes, and
DEOS decisions. Treat it as a validation log, not as a claim that every community
report applies to every board revision.

## Hardware baseline

- ESP32-P4 primary MCU, 32 MB flash and 32 MB PSRAM.
- 4-inch 720x720 IPS panel, ST7703 over 2-lane MIPI DSI.
- GT911 capacitive touch over I2C: SDA GPIO7, SCL GPIO8.
- Touch reset is routed to GPIO23. TP_INT is routed to test point TP2 rather than
  being a normal application interrupt input.
- ESP32-C6 is a wireless coprocessor connected to the P4 over 4-bit SDIO.
- Backlight PWM is GPIO26; LCD reset is GPIO27.
- Waveshare currently recommends ESP-IDF 5.5.1 through 5.5.4 in its FAQ. DEOS
  currently builds with IDF 6.1, so every display/touch change must continue to
  be physically validated on the target board.

Primary references:
- https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B
- https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B/FAQ
- https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
- https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_p4_wifi6_touch_lcd_4b

## GT911: important board-specific behavior

The current Waveshare BSP does **not** drive GT911 INT/RST during normal touch
initialization. It probes both legal GT911 I2C addresses, 0x5D and 0x14, then
uses whichever responds. Touch is polled.

This matters because GT911 uses reset/interrupt sequencing to select its I2C
address. Blindly toggling those lines can make a previously visible controller
move to the other address or disappear from the address the firmware expects.

DEOS follows the current BSP behavior:
- probe 0x5D, then 0x14;
- use the responding address;
- configure RST and INT as GPIO_NC;
- poll touch data;
- no forced reset/address strap sequence.

Do not "fix" touch by hard-coding 0x5D or by adding a GPIO23 reset pulse unless a
scope/logic-analyzer test proves it is required on a particular hardware
revision.

Upstream BSP reference:
https://github.com/waveshareteam/Waveshare-ESP32-components/blob/master/bsp/esp32_p4_wifi6_touch_lcd_4b/esp32_p4_wifi6_touch_lcd_4b.c

## Why the current DEOS symptom is probably not a GT911 hardware fault

Physical DEOS logs have already shown valid GT911 press coordinates, for
example a press around x=160/y=340. The task watchdog then fires with
`deos_lvgl` running. That proves the I2C controller and GT911 data path can
produce valid input. It does not prove every gesture is perfect, but it moves
the primary investigation downstream: LVGL rendering, flush completion, task
scheduling, and navigation callbacks.

Therefore:
1. raw GT911 coordinates are evidence, not the current primary suspect;
2. a WDT immediately after interaction should be investigated as a display/UI
   scheduling problem first;
3. coordinate transforms should only be changed if a calibration grid proves
   swap/mirror/offset errors.

## Display/LVGL buffering

The current Waveshare BSP 3.x uses Espressif's `esp_lvgl_adapter` with:
- MIPI DSI;
- `TRIPLE_PARTIAL` tear avoidance;
- three panel frame buffers;
- a 50-line LVGL partial buffer;
- LVGL draw buffer outside PSRAM (`use_psram = false`);
- no PPA acceleration by default;
- rotation 0.

Espressif documents `TRIPLE_PARTIAL` as the high-resolution smooth-UI mode for
RGB/MIPI DSI. It requires three panel frame buffers and uses partial LVGL
rendering.

DEOS previously used two panel frame buffers plus two 40-line LVGL buffers in
PSRAM. That was a custom configuration and differed from the now-published BSP
reference. Starting with commit a6517337, DEOS moves toward the upstream stable
profile: 3 panel frame buffers, one 50-line partial LVGL buffer in internal RAM,
rotation 0, and no PPA.

Reference:
https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html

### Known upstream display hazards

Espressif documents a PPA freeze affecting ESP32-P4 when all of these are true:
TRIPLE_PARTIAL, screen rotation (90/270 degrees), and PPA acceleration. DEOS
currently uses rotation 0 and does not enable PPA, so that specific erratum
should not apply. If rotation is added later, re-check the IDF/adapter patch
status before enabling it.

There is also an upstream report of visual corruption in TRIPLE_PARTIAL when
LVGL draw-buffer stride alignment is greater than one in affected adapter
versions. DEOS currently has a custom flush path rather than that adapter copy
path, but this is a useful regression reference if shifted/misaligned stripes
appear.

Reference:
https://github.com/espressif/esp-iot-solution/issues/659

## PSRAM reports

A community report attributes freezes on this board to running PSRAM/MSPI at
500 MHz and recommends reducing the memory clock. This is not sufficient
evidence to call the PCB defective, and DEOS must not encode that claim as a
hardware fact.

DEOS already configures:
- `CONFIG_SPIRAM_SPEED_200M=y`
- `CONFIG_SPIRAM_SPEED=200`

So the reported 500 MHz condition is not the explanation for the current DEOS
WDT symptom.

Community report for reference:
https://github.com/78/xiaozhi-esp32/discussions/2129

## Other useful projects

Projects known to target this exact board are valuable as implementation
cross-checks, not as authoritative hardware specifications:

- Waveshare official examples:
  https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
- p3a pixel-art player:
  https://github.com/fabkury/p3a
- ESP32-P4 NINA Display:
  https://github.com/chvvkumar/ESP32-P4-NINA-Display
- Kern:
  https://github.com/odudex/Kern
- Xiaozhi board support:
  https://github.com/78/xiaozhi-esp32
- ESPHome/LVGL configuration:
  https://github.com/jtenniswood/esphome-lvgl

Waveshare's resource page also maintains a community showcase:
https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B/Resources-And-Documents

## DEOS physical validation checklist

After every display/touch transport change:

1. Boot for at least 60 seconds without touching the screen; no task WDT.
2. Verify Home is visually stable: no periodic full-screen flicker.
3. Tap all four Home destinations repeatedly; every press must produce exactly
   one navigation transition.
4. Hold and drag across the screen; LVGL must continue servicing input and
   rendering without WDT.
5. Verify raw touch corners and center against a calibration overlay before
   changing swap/mirror flags.
6. Exercise repeated screen transitions for at least five minutes.
7. Verify brightness changes while UI is active.
8. Bring ESP32-C6/Wi-Fi up and down; local UI and touch must remain responsive.
9. Test with SD absent, valid, and unsupported filesystem. Never auto-format.
10. Capture serial logs for any WDT, including MEPC/RA, and symbolize addresses
    against the exact CI ELF before changing timing/priority/watchdog settings.

## Rules for future fixes

- Never disable or feed the task watchdog to hide a rendering stall.
- Never format SD automatically.
- Never hard-code a single GT911 address.
- Keep Wi-Fi/C6 failure isolated from local display/touch.
- Prefer published Waveshare/Espressif BSP behavior over ad-hoc timing changes,
  then document deliberate deviations.
- Treat community hardware-defect claims as hypotheses until reproduced.
