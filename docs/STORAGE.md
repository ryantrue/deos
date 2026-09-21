# Storage and SD policy

DEOS treats removable storage as user data first and an OS resource second.

The primary rule is:

> **Never auto-format removable media.**

A mount failure is not permission to destroy a card.

## Resource

The first board exposes removable media as:

```text
Storage/sd
```

Current board transport:

- SDMMC host slot 0;
- 4-bit bus;
- board IO-MUX routing;
- FAT support for the initial DEOS volume format.

DEOS itself must remain functional with no SD card inserted.

## Volume states

### Absent

No usable card is detected.

UX:

- show the slot as empty;
- allow **Rescan SD card**;
- do not block the rest of the OS.

### Foreign

The card is readable, but it does not contain the DEOS volume marker.

This is a valid user card, not an error.

UX offers:

- **Leave unchanged** — do nothing;
- **Initialize for DEOS** — preserve existing files and create the DEOS directory structure;
- **Format instead...** — destructive flow with separate confirmation.

### Ready

The card contains:

```text
DEOS/.volume
```

and is mounted as a DEOS-managed removable volume.

### NeedsFormat

The SD card is detected as media, but its current filesystem or partition layout cannot be mounted by the supported DEOS storage stack.

UX:

- explain that the layout is unsupported;
- offer explicit **Format for DEOS...**;
- never format automatically.

### Error

The device could not safely classify the card.

UX:

- do **not** offer destructive formatting;
- show the error;
- allow rescan / later diagnostics.

### Busy

A user-requested storage operation is executing.

Formatting and initialization run on a worker task, not inside the LVGL task.

## Initialize for DEOS

This operation is non-destructive.

It creates missing directories only:

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

and writes:

```text
/DEOS/.volume
```

Existing user files remain in place.

Current marker schema:

```text
DEOS_VOLUME=1
schema=1
filesystem=fat
portable=true
```

The removable volume is intentionally board-independent. A card initialized by one DEOS device should remain recognizable on future supported DEOS hardware. Hardware compatibility belongs to device resources and drivers, not removable-volume identity.

The marker format is versioned so later DEOS releases can migrate layout rules without guessing.

## Format for DEOS

Formatting is explicitly destructive and requires a dedicated confirmation screen.

For the first implementation DEOS creates a predictable layout:

1. initialize the card as raw SDMMC media;
2. clear stale primary MBR/GPT metadata;
3. clear the backup GPT metadata area at the end of the card;
4. create one partition consuming the card;
5. create a FAT filesystem;
6. mount it;
7. create the DEOS directory layout and marker.

Clearing stale GPT metadata matters when a card previously came from another operating system. Leaving a backup GPT header at the end of the device can cause desktop tools to report a confusing hybrid/stale layout after DEOS writes an MBR-style single-partition layout.

ESP-IDF 6.1 FatFs formatting support creates the new partition table and filesystem only when formatting was explicitly requested by DEOS.

## Safety requirements

- no `format_if_mount_failed=true` during normal probing;
- a readable foreign card is never treated as corrupt just because DEOS directories are absent;
- generic driver errors do not expose a Format action;
- format is allowed only after DEOS has classified the media appropriately and the user confirms;
- do not remove/power-cycle a card while Storage state is Busy;
- later hot-remove support must unmount and quiesce users before releasing the card.

## Future storage work

- hot insert/remove event handling;
- exFAT policy evaluation for large media files;
- Files app and shared-storage permissions;
- app quotas / per-app directories;
- backup manifests;
- storage health and throughput diagnostics in Developer Mode.
