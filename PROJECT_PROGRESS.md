# IAP/OTA Remote Upgrade Project Progress

Last updated: 2026-04-27

## Project Goal

Build an STM32F407 based IAP/OTA remote upgrade system with:

- Bootloader + APP split design.
- Five Flash regions: boot / parameter / run / slot A / slot B.
- A/B redundant firmware storage and boot copy flow.
- UART, Ethernet TCP, and WiFi module over SPI OTA upgrade paths.
- APP running on FreeRTOS, with visible runtime tasks for verification.

## Current Project Structure

- `IAP_boot/`: Bootloader project.
- `IAP_APP_V3/`: FreeRTOS APP project with lwIP Ethernet OTA.
- `APP/`: Qt upper-computer tool, currently focused on UART upgrade.

## Flash Layout

Defined consistently in:

- `IAP_boot/User/iap/iap_layout.h`
- `IAP_APP_V3/USER/App/iap_layout.h`

Current layout:

- Boot: `0x08000000`, size `0x0000C000` 48 KB.
- Parameter/flag: `0x0800C000`, size `0x00004000` 16 KB.
- Run region: `0x08010000`, size `0x00030000` 192 KB.
- Slot A: `0x08040000`, size `0x00040000` 256 KB.
- Slot B: `0x08080000`, size `0x00040000` 256 KB.

Important Keil scatter outputs:

- Boot target should use `IAP_boot/Output/IAP_Bootloadert.sct`.
- APP target should use `IAP_APP_V3/Output/IAP_APP_V3.sct`.

Do not use old test outputs that link everything at `0x08000000`, otherwise the partition design will be broken.

## Completed

### Bootloader

Implemented in:

- `IAP_boot/User/main.c`
- `IAP_boot/User/iap/iap_uart.c`
- `IAP_boot/User/iap/iap_boot_partition.c`
- `IAP_boot/User/iap/iap_layout.h`

Current behavior:

- Boot waits about 3 seconds after reset.
- During countdown, host can send `ENTER_IAP` command `0x0002`.
- If `ENTER_IAP` is received, boot enters permanent UART IAP mode.
- UART IAP writes firmware to the inactive A/B slot.
- After receiving firmware, boot writes the flag area and resets.
- On normal timeout, boot reads the flag, copies slot A or B into the run region, then jumps to `0x08010000`.
- Boot validates the vector table before jumping.

UART frame format implemented:

- Header: `0x5A 0xA5`
- Command: 2 bytes
- Payload length N: 2 bytes, big endian
- Payload: N bytes
- Reserved: 4 bytes, must be zero
- CRC16-Modbus: 2 bytes, low byte first

Current UART commands:

- `0x0001`: firmware data frame.
- `0x0002`: enter IAP mode.

### APP

Implemented in:

- `IAP_APP_V3/USER/main.c`
- `IAP_APP_V3/USER/App/ota_update.c`
- `IAP_APP_V3/USER/App/netconf.c`

Current behavior:

- APP runs on FreeRTOS.
- Creates Ethernet polling task.
- Creates two print tasks:
  - `task1 alive`
  - `task2 alive`
- Initializes LAN8742A Ethernet and lwIP.
- Starts TCP OTA server.

Current TCP OTA details:

- TCP server port: `6000`.
- Uses the same basic IAP frame protocol style.
- Writes received firmware to inactive A/B slot.
- Writes boot flag after idle timeout.
- Calls `NVIC_SystemReset()` so bootloader can copy slot A/B to run region.

### Qt Upper-Computer Tool

Implemented in:

- `APP/dialog.cpp`
- `APP/dialog.h`
- `APP/APP.pro`

Current behavior:

- Uses Qt SerialPort.
- Can open serial port.
- Can select `.bin` firmware.
- Can send `ENTER_IAP`.
- Can split firmware into 256-byte payload frames.
- Can build frames with CRC16-Modbus and send them automatically.
- Has progress display for UART upgrade.

## Not Finished Yet

### WiFi Module over SPI OTA

This is the biggest missing requirement.

Current search only shows STM32 SPI library support and reserved SPI-related files. There is no complete WiFi module driver, SPI transport protocol, OTA receive task, or firmware write path for WiFi.

Need to add:

- WiFi module selection and interface definition.
- SPI driver wrapper for the module.
- WiFi OTA protocol handling.
- Firmware download/write to inactive slot.
- Flag update and reset flow.

### PC Tool Ethernet/TCP Upgrade

MCU-side TCP OTA exists, but the Qt tool currently appears focused on UART upgrade.

Need to add if required by the exam demonstration:

- TCP connection UI: target IP and port.
- TCP frame sender using the same IAP frame format.
- Firmware file split and progress logic reused from UART path.

### Whole-Image Integrity Check

Current implementation has per-frame CRC16 and vector-table validation.

For a stronger answer to the exam requirement, add whole-image verification:

- Include total firmware size and image CRC in an upgrade start/end command.
- Boot/app writes flag only after whole-image CRC passes.
- Store image CRC in parameter area.
- Boot optionally verifies source slot before copying to run region.

### Documentation and Test Evidence

Need to collect final demonstration evidence:

- UART upgrade log.
- Ethernet TCP upgrade log.
- WiFi OTA upgrade log.
- Boot countdown log.
- A/B slot switch log.
- APP task print log after successful jump.
- Ping/TCP connection screenshot if needed.

## Recommended Next Work Order

1. Confirm Boot and APP can build with the correct Keil targets.
2. Burn bootloader to `0x08000000`.
3. Generate APP bin linked at `0x08010000`.
4. Test UART upgrade from Qt tool.
5. Test boot copy A/B to run and APP task prints.
6. Test Ethernet TCP OTA on port `6000`.
7. Add Qt TCP sender if needed.
8. Add WiFi SPI OTA path.
9. Add whole-image CRC and update flag structure.
10. Finalize README/demo procedure.

## Current Risk Notes

- Do not overwrite the boot region when flashing APP manually.
- Do not use APP binaries linked at `0x08000000` for IAP testing.
- Current flag structure is simple: magic/version/boot_from/image_size. It does not yet store image CRC.
- TCP OTA commits after idle timeout; this is workable for testing but should be documented clearly.
- WiFi/SPI OTA is not implemented yet and must be handled before claiming all three communication methods are complete.

## Git/GitHub Workflow Requirement

From now on, after each code/documentation change:

1. Check changed files with `git status --short`.
2. Review important diffs with `git diff`.
3. Commit with a clear message.
4. Push to GitHub with `git push`.

If GitHub remote is missing, configure it first:

```bash
git remote add origin <github-repo-url>
git branch -M main
git push -u origin main
```

