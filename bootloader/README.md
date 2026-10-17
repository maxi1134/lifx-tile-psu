# Bootloader for XMC A-series flash

My ESP32 boot-looped the moment I flashed it, before the sketch even started:

```text
E (30) bootloader_flash: XMC flash startup fail
E (30) boot.esp32: XMC startup flow failed, reboot!
```

The chip is fine. Its flash just isn't on Espressif's list.

> [!NOTE]
> It works on my board: an ESP32-D0WDQ6 with an XM25QH64A (`20 70 17`), flashed from the PlatformIO project in `arduino/`. Other people report normal operation with the same patch on the XM25QH128A (see [sources](#sources)).

## What's going on

XMC recommends a startup sequence for its chips that come up in a bad state, so the ESP-IDF bootloader checks for XMC flash on every boot (since ESP-IDF 4.4, and 4.1.4 / 4.2.4 / 4.3.2):

1. If the flash ID is a known XMC chip, carry on.
2. Otherwise, if the flash says it's made by XMC (vendor `0x20`), run XMC's startup sequence and read the ID again.
3. Still not on the list? Reboot.

The list only has XMC's C-series. XMC's older A-series (like my XM25QH64A, `20 70 17`) is genuine XMC flash that isn't on it, so the bootloader runs the sequence, still doesn't recognise the chip, and reboots, every single time. Espressif has been asked to add it and declined, on the grounds that those chips aren't in their supply chain.

Things that **don't** fix it:

| Tried | Why not |
|---|---|
| `CONFIG_BOOTLOADER_FLASH_XMC_SUPPORT=n` | With XMC support off, the bootloader refuses *every* chip with vendor `0x20` outright |
| Arduino core 2.0.1 to 2.0.17 | Same check, same list. Their bootloader prints nothing, so the loop is silent |
| pioarduino's `custom_sdkconfig` | Rebuilds the libraries, not the bootloader |

What fixes it: a bootloader with the A-series added to the list. Then the chip passes the very first check and the startup sequence never runs. That's [`bootloader_xmc_a.bin`](bootloader_xmc_a.bin).

## Do I need it?

Plug the board in over USB and ask the flash who it is. This needs esptool 5.x (`pip install esptool`). Close any serial monitor on that port first.

```bash
python -m esptool --port COM6 flash-id
```

| `Manufacturer` / `Device` | What it means |
|---|---|
| `20` / `7017` to `7020` | XMC A-series (XM25QH64A = `7017`, XM25QH128A = `7018`). You need this bootloader |
| `20` / `4013` to `4020`, `4117` to `4120`, `5015`, `5016` | XMC C-series, already on Espressif's list. A boot loop here is something else (power, wiring) |
| `20` / any other ID that loops with this error | XMC, but not covered by this `.bin` either (a 4 MB A-series part would be `7016`). Widen the range in the patch and [rebuild it](#rebuilding-it) |
| any manufacturer other than `20` | Not XMC. The stock bootloader is fine; this one works too |

esptool runs the same startup sequence on A-series chips, so expect it to print "XMC flash boot-up fix failed" followed by "fix successful". Ignore both.

## Arduino (PlatformIO)

[`../arduino/platformio.ini`](../arduino/platformio.ini) already points at this bootloader:

```ini
board_build.arduino.custom_bootloader = ${PROJECT_DIR}/../bootloader/bootloader_xmc_a.bin
```

Open the `arduino/` folder in PlatformIO and upload as usual. PlatformIO writes the bootloader at `0x1000` on every upload. Check the upload log once: the `0x1000` entry must name `bootloader_xmc_a.bin`. If it names `bootloader.bin`, PlatformIO didn't find the file (moved or renamed folder) and quietly used the stock one.

If you ever swap the `.bin`, run a clean build first, otherwise `firmware.factory.bin` keeps the old one.

## Arduino IDE

The IDE writes its own bootloader at `0x1000` on every upload, and there's no setting to change that. Upload as usual, then write this one over it, after every upload:

```bash
python -m esptool --chip esp32 --port COM6 write-flash 0x1000 bootloader/bootloader_xmc_a.bin
```

Opening `arduino/` in PlatformIO instead does that for you.

## Is it safe?

- **The change:** 7 lines in ESP-IDF 5.5.5's `bootloader_flash.c`, adding `20 70 17` to `20 70 20` to the XMC list ([`build/bootloader_flash_xmc_a.patch`](build/bootloader_flash_xmc_a.patch)). Everything else is the stock bootloader, and only the bootloader changes; the app is untouched.
- **On other boards:** it only adds IDs to the list, so any other flash takes exactly the same path as with the stock bootloader.
- **What it skips on A-series chips:** XMC's startup sequence. It is written for the C-series (the bootloader even calls it "XM25QHxxC startup flow"), and the people who've run A-series chips with this patch report normal operation.
- **Settings:** the same as the stock bootloader PlatformIO flashes for this project (arduino-esp32 3.3.12's `bootloader_dio_40m`) where it matters (DIO, 40 MHz, partition table at `0x8000`, app rollback on, errors-only logging). The one difference: it leaves "skip validation on deep-sleep wake" off, and the sketch never sleeps. The Arduino IDE's "ESP32 Dev Module" defaults to a QIO, 80 MHz bootloader, so there the flash mode and speed differ too; I've only run it from PlatformIO.
- **Checked:** I disassembled the built bootloader to confirm `20 70 17` is accepted, checked that the sketch's upload and factory image carry it at `0x1000`, and my board boots with it.
- **Recovery:** nothing here can brick the board. The ESP32's download mode lives in ROM, so any esptool or PlatformIO upload over USB (hold BOOT if auto-reset doesn't kick in) puts things back. A normal Arduino IDE upload restores the stock bootloader.

## Rebuilding it

The [`build/`](build/) folder is an ESP-IDF project that only exists to produce this bootloader. ESP-IDF compiles every folder in `bootloader_components/` into the bootloader, and one named `bootloader_support` replaces the stock component there.

#### 1: Let PlatformIO download ESP-IDF 5.5.5

```bash
cd bootloader/build
python -m platformio pkg install
```

#### 2: Copy and patch `bootloader_support`

```bash
python prepare_bootloader_support.py
```

It refuses to run unless the ESP-IDF it finds is 5.5.5, and refuses to patch if the XMC list isn't the one the patch expects.

#### 3: Build and copy the result

```bash
python -m platformio run
cp .pio/build/esp32dev/bootloader.bin ../bootloader_xmc_a.bin
```

The platform needs PlatformIO Core 6.2 or newer. The one inside the PlatformIO IDE (`%USERPROFILE%\.platformio\penv\Scripts\python.exe -m platformio`) is.

## Sources

- [esp-idf #9952](https://github.com/espressif/esp-idf/issues/9952) and [#10999](https://github.com/espressif/esp-idf/issues/10999) (same reporter): the same error on XM25QH128A, fixed by adding `0x70` to the list
- [esp-idf #18947](https://github.com/espressif/esp-idf/issues/18947): the same error and the same proposed change, declined by Espressif
- [esptool #1189](https://github.com/espressif/esptool/issues/1189): the same chip family, same fix
- ESP-IDF 5.5.5 `components/bootloader_support/bootloader_flash/src/bootloader_flash.c`: `is_xmc_chip_strict()` and `bootloader_flash_xmc_startup()`
