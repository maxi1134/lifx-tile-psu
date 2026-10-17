<p align="center">
  <a href="#how-the-tile-psu-works"><img src="https://img.shields.io/badge/How_it_works-a_50_Hz_square_wave-6f42c1" alt="It is literally a square wave. That's the whole secret."></a>
  <a href="#parts"><img src="https://img.shields.io/badge/Parts-mostly_resistors-2da44e" alt="About a dozen, I promise"></a>
  <a href="#wiring"><img src="https://img.shields.io/badge/Wiring-24_V_and_3.3_V-d29922" alt="Do not mix these two up"></a>
  <a href="#firmware"><img src="https://img.shields.io/badge/Firmware-one_Arduino_sketch-0e8a16" alt="loop() does nothing, and that's the point"></a>
  <a href="#bring-up"><img src="https://img.shields.io/badge/Bring--up-no_scope_needed-8250df" alt="A multimeter will do"></a>
  <a href="#troubleshooting"><img src="https://img.shields.io/badge/Troubleshooting-still_red%3F-cf222e" alt="Breathing red is the Tile's love language"></a>
</p>

# LIFX Tile power supply, rebuilt around an ESP32

My LIFX Tile power supply died, and you can't buy a new one. Tiles have been discontinued for years.

The annoying part is that the Tile power supply is not just a power supply. Besides 24 V, it sends a square wave down a dedicated wire. Without that signal the tiles refuse to work and sit there breathing red at you.

[u/Final-Hawk90 worked out](https://www.reddit.com/r/lifx/comments/1mrlftz/) that you can fake that signal with an ESP8266. I wanted to do it with an ESP32.

> [!NOTE]
> This works on my tiles: the Arduino sketch, flashed from the PlatformIO project in `arduino/` (my board needed [the patched bootloader](bootloader/README.md)). The sketch also compiles on Arduino-ESP32 2.0.11 and 3.3.9.

---

## How the Tile PSU works

The PSU feeds the first tile through an 8-pin "mini-USB" style cable. Pinout from [a LIFX employee](https://www.reddit.com/r/lifx/comments/oepf9e/comment/h4am5vi/):

| Pin | Wire | Signal | What I do with it |
|---|---|---|---|
| 1 | Red | +24 V | +24 V |
| 2 | Green | RS-485 A | Nothing. The PSU sent no data on it |
| 3 | — | Not connected | — |
| 4 | Black | GND | GND |
| 5 | Red | +24 V | +24 V (same red wire in the cable) |
| 6 | Blue | RS-485 B | Nothing |
| 7 | Yellow | **ID** | **The ESP32's square wave** |
| 8 | Black | GND | GND (same black wire in the cable) |

The ID signal is a 0–3.3 V square wave at 50 % duty. The original PSU simply derived it from the mains frequency, so it was 50 Hz or 60 Hz depending on your country. [Someone who reverse-engineered the Tile firmware](https://www.reddit.com/r/lifx/comments/1mrlftz/comment/ots5kqt/) found that the first tile accepts anything from **45 to 65 Hz**. I use 50 Hz: it's the frequency every confirmed replacement on real tiles has used, and it divides exactly from the ESP32's clocks. 60 Hz works too (that's what my original PSU fed my tiles here in Quebec), and switching is one setting.

That frequency is also how the tiles work out their order. The first tile sees 45–65 Hz and becomes the leader (the one on Wi-Fi), then passes a different frequency to the next tile:

| Tile position | Frequency it receives (±5 Hz) |
|---|---|
| 1 (leader) | 45–65 Hz, from the PSU (or the ESP32) |
| 2 | 150 Hz |
| 3 | 190 Hz |
| 4 | 230 Hz |
| 5 | 280 Hz |

The original post says the second tile gets 40 Hz. The firmware says 150 Hz, so trust the firmware. The tiles generate those frequencies themselves; the ESP32 only provides the first one.

Three things about this signal shaped my design:

1. **The tile's own MCU is an ESP32.** You can see it in the FCC photos. ESP32 inputs are not 5 V tolerant, so a 5 V Arduino Nano is a bad idea. A 3.3 V ESP32 matches the original PSU's 3.14 V peak perfectly.
2. **The tiles read ID when they boot, and keep watching it.** If ID is missing at boot you get breathing red until you power-cycle the whole set.
3. **The PSU has a factory-reset button.** [Holding it "for a few seconds"](https://community.lifx.com/t/button-on-lifx-tile-power-adapter/5281) factory-resets the tiles, "much like turning the tile off and on 5 times". The PSU has no data lines, so that gesture almost certainly travels over the ID wire. My takeaway: **never let the ID signal drop while the tiles are powered, and never power-cycle them five times in a row.**

That's why the ESP32 runs a tiny sketch with no Wi-Fi, no OTA and nothing else that could reboot it. Powered from the same brick as the tiles, its only way to restart is the brick being unplugged, and then the tiles restart with it. Re-flashing is the one exception, so unplug the brick before you do.

---

## Parts

| Ref | Part | Notes |
|---|---|---|
| — | 24 V regulated supply, at least 30 W | Got one already? See [the checks below](#already-have-a-24-v-supply). Buying one, I'd go with a **Mean Well GST40A24-P1J** (24 V 1.67 A, 5.5 × 2.1 mm plug, centre positive, needs an IEC C13 mains cord). Its overload protection kicks in at 1.75–2.5 A, close to what the Tile connector's contacts can take. The GST60A24-P1J (60 W) is what u/Jakey01010 used successfully, but it will push 2.6–3.75 A into a fault before it gives up. Five tiles are rated 30 W (34 W at full brightness), the same as the original LTPSU24A1 |
| J1 | 5.5 × 2.1 mm DC jack, 3 A or more | Panel mount, or a "female barrel to screw terminal" adapter |
| F1 | 2 A slow-blow fuse + holder | Protects the wiring if you use a supply without its own current limit. The original PSU was a power-limited Class 2 supply, and Tile connectors have melted before. With a GST brick, the brick's overload protection acts first, so the fuse is cheap insurance |
| D1 | SMBJ26A TVS diode, cathode to +24 V | Optional. Clamps spikes, and crowbars the supply if the input is ever reversed (a GST brick hiccups; a supply without current limiting blows F1) |
| U1 | **Pololu D36V6F5** (5 V, 600 mA, up to 50 V in) | Avoid the Mini-360 (23 V max, so 24 V is out of spec) and MP1584 modules (28 V, too tight). An XL7015 module or a genuine LM2596 set to 5.0 V also works. Skip it, along with D2 and C1, if you [power the ESP32 from a USB charger](#two-supplies-24-v-for-the-tiles-5-v-for-the-esp32) |
| D2 | SS14 or 1N5819 Schottky | Reverse-polarity protection for the buck |
| C1 | 100 µF 50 V electrolytic, at U1's input | Pololu recommends at least 33 µF at the input with long or inductive leads. In their test only inputs above 28 V spiked past the 50 V rating, so at 24 V this is cheap insurance |
| — | ESP32 board | ESP32 DevKitC (WROOM-32), ESP32-C3 SuperMini or an ESP32-S3 board. Any 3.3 V ESP32 works |
| R1 | 100 Ω | GPIO to clamp node |
| D3 | BAT54S (SOT-23), or two small Schottkies | Clamps the ID line to 3.3 V and GND |
| R2 | 1 kΩ, **1 W recommended** (0.5 W minimum) | The ID series resistor. Rated so it survives the yellow wire touching 24 V |
| R3 | 100 kΩ | Holds ID low while the ESP32 boots |
| — | The original 2 m LIFX Tile cable (L3TILECB20) | We cut one end off. See [the cable](#the-cable) |

#### Already have a 24 V supply?

I'm using a 24 V supply I already had, with its own cable bringing the 24 V into the box. Any supply works if it passes these checks:

| Check | Why | How |
|---|---|---|
| Regulated **constant-voltage** 24 V DC | A constant-current LED driver will happily push its rated current into the tiles at whatever voltage that takes | The label says 24 V ⎓ (DC), not "constant current" or a current like "700 mA" as the output |
| At least 1.25 A (30 W) for five tiles | That's what LIFX asks for. 1.5 A leaves headroom for full brightness (34 W) plus the ESP32 | Fewer tiles: about 0.3 A each, plus 0.1 A for the ESP32 |
| 23.5–25.5 V with nothing connected | Some cheap supplies idle high. Above about 26 V the SMBJ26A starts conducting | Measure with a multimeter, no load |
| Polarity | Only the buck has reverse protection (D2). The tiles have none | Red probe on the wire you think is +, black probe on the other: the meter must read +24 V, not −24 V |
| Current limit | A big LED-strip supply (24 V 5 A, 10 A…) will push all of that into a shorted Tile connector | Keep F1 and use a 2 A slow-blow. With a supply that big, F1 is not optional |
| Wire gauge | Up to 1.25 A, plus the volts lost along the way | The supply's own cable is fine if it came with a 1.25 A or bigger supply. Extending it: 22 AWG up to 1 m, 20 AWG beyond |

If its cable ends in a barrel plug, keep J1 and match the plug (5.5 × 2.1 and 5.5 × 2.5 mm look alike but don't mate properly). If you cut the plug off, skip J1 and land the two wires on a screw terminal, straight into F1.

---

## The cable

The PSU end of the original cable has nothing left to plug into, so I cut it off, same as u/Jakey01010 did. The connector is a proprietary 8-pin mini-USB lookalike: Nikon UC-E6 camera cables have the right plug but only 4 thin wires, and LIFX advised against them.

#### 1: Cut the PSU end off the 2 m cable

Leave yourself 10–15 cm of tail past the cut. Inside there are 5 wires: thicker red and black, thinner green, blue and yellow.

#### 2: Check every wire with a multimeter

Probe the plug's contacts with a needle and confirm continuity from yellow to pin 7, red to pins 1 and 5, and black to pins 4 and 8. Do not trust colours you did not check yourself. Pins 4 (GND) and 5 (+24 V) sit right next to each other in that connector.

Plugging the end into an unpowered tile and probing its other socket only works for red, black, green and blue. Each tile port has its own ID line, so yellow never shows continuity through a tile.

#### 3: Insulate green and blue separately

Heat-shrink each one on its own. They are the RS-485 bus between tiles. The original PSU sent no data on them (one report says it had a termination resistor across them, see [troubleshooting](#troubleshooting)), and here they stay unconnected.

---

## Wiring

```text
                   F1 2A T
 24 V brick ──J1───[====]──┬───────────────────────────────────────────► RED     tile pins 1 + 5
 (5.5×2.1, centre +)       │
                           │   D2 SS14       U1 Pololu D36V6F5
                           ├─────►|────┬────[VIN          VOUT]────────► ESP32 5V pin
                           │           │    [      GND        ]
                      D1 SMBJ26A    C1 100µF         │
                      (optional,     50 V            │
                     cathode +24V)     │             │
 GND ──────────────────────┴───────────┴─────────────┴──┬──────────────► ESP32 GND
                                                        └──────────────► BLACK   tile pins 4 + 8

                     R1 100Ω           R2 1k 1W
 ESP32 GPIO25 ─────[========]────┬────[========]────┬──────────────────► YELLOW  tile pin 7 (ID)
                                 │                  │
                           D3 BAT54S             R3 100k
                       pin 3 = this node            │
                       pin 1 = GND                 GND
                       pin 2 = ESP32 3V3

 GREEN, BLUE (RS-485): not connected, insulated separately.
```

With R1 (100 Ω), R2 (1 kΩ) and D3, the ESP32 survives the yellow wire being shorted to 24 V. R2 takes about 0.45 W (hence the 1 W recommendation), and D3 steers about 21 mA into the 3V3 rail instead of the pin. The board's regulator can't sink current, so this relies on the board drawing more than about 25 mA itself. A classic ESP32 at 240 MHz always does. An ESP32-C3 idling on this sketch may not, and then the 3V3 rail can creep up. Without D3, the same 20 mA would go straight into the ESP32 pin's own protection diode, and I would not bet on the ESP32 surviving.

Tie the ESP32's GND to the 24 V return right where the cable leaves the box. The ID signal shares its ground with up to 1.25 A of LED current, so keep that junction short.

#### Two supplies: 24 V for the tiles, 5 V for the ESP32

Already have a USB charger lying around? You can skip the buck converter (U1, D2 and C1) and power the ESP32 board through its own USB port. The 24 V supply then only feeds the tiles.

Any USB charger rated 1 A or more will do (Espressif asks for at least 0.5 A), with a short cable that isn't paper-thin. A charger that sags browns the ESP32 out, which drops ID on powered tiles. If your board has a USB-C port and a USB-C charger gives it nothing, use a USB-A charger with an A-to-C cable: some boards lack the resistors USB-C chargers look for.

The one wire that makes this work is the **ground tie**: the 24 V supply's minus and the ESP32's GND have to be connected.

```text
                   F1 2A T
 24 V supply + ────[====]──────────────────────────────────────────────► RED     tile pins 1 + 5
 24 V supply − ───────────────────┬─────────────────────────────────────► BLACK   tile pins 4 + 8
                                  │
                                  │  ground tie: one wire, soldered or screwed down
                                  │
                ┌─────────────────┴┐
 5 V USB ══USB═►│ ESP32         GND│
 charger        │                  │     R1 100Ω           R2 1k 1W
                │          GPIO25  ├────[========]────┬────[========]────┬──► YELLOW  tile pin 7 (ID)
                └──────────────────┘                  │                  │
                                                 D3 BAT54S            R3 100k
                                                 pin 3 = this node       │
                                                 pin 1 = GND            GND
                                                 pin 2 = ESP32 3V3
```

Why the grounds have to meet even though the voltages differ: a voltage only means something between two points. The ESP32's 3.3 V on GPIO25 is 3.3 V above *its* GND, and the tile reads the yellow wire against *its* GND, which is the black wire. Tie the two minus sides together and both supplies measure from the same zero. The 24 V supply still sits 24 V above it, the 5 V charger 5 V above it, and nothing flows through the tie except the ID signal's tiny return current. Without the tie, the yellow wire has no reference at all and the tiles see garbage.

The rules:

1. **Tie the minus sides, never the plus sides.** 24 V and 5 V must never touch.
2. **One permanent tie.** Hard-wire it inside the box. Don't count on a plug or the tile connector to make it, and don't add a second tie somewhere else.
3. **Use supplies with isolated outputs.** Wall bricks and USB chargers are isolated from the mains, so tying their minus wires together is fine. Stick to supplies that say so (a branded brick, the double-square Class II symbol, or "SELV" on the label); a bare AC-to-DC module may not be isolated. A USB charger's output also floats at a surprisingly high AC voltage to earth through its interference-suppression capacitor, tens of volts on a meter, but it can only push microamps. That's one more reason the tie stays permanently wired: the two grounds always meet before the yellow wire does.
4. **Flashing gets simpler, but deal with the tiles first.** There's no buck on the 5V pin any more, so the back-feed warning in [bring-up](#bring-up) doesn't apply: unplug the charger and plug in your computer. The ESP32 still stops while you swap cables, and again when the upload resets it, so unplug the 24 V supply before you touch the USB cable, and when you're done, plug the charger back in before the 24 V. Don't run it off a computer either: a serial monitor opening, or the computer sleeping or rebooting, resets the ESP32 while the tiles stay powered.

Two supplies cost this build its main guarantee: the ESP32 no longer restarts only together with the tiles. Anything that drops the 5 V on its own stops ID on powered tiles: a knocked USB cable, a charger giving up, or a mains flicker the charger doesn't ride through. If you can't rule that out, stick with one supply and the buck.

The power-up order matters too:

| | What happens |
|---|---|
| 5 V first, then 24 V | The best order: ID is already running when the tiles boot |
| 24 V first, then 5 V | Red: the tiles look for ID about half a second after they get power, and the charger plus the ESP32's boot take about that long on their own |
| Both at once (same strip or smart plug) | Usually fine: a Mean Well GST takes about a second to bring up its 24 V, and by then ID is running. A 24 V supply that starts much faster can lose that race |
| 5 V lost while the tiles run | ID stops while the tiles are powered: red, or worse (that's the factory-reset gesture). **Avoid it** |
| 24 V lost while the ESP32 runs | The tiles turn off, but ID keeps going into them: at most about 2.5 mA (1.2 mA on average) through 1.1 kΩ. Too little to wake a tile and probably harmless, but untested |

What I'd do: both supplies on the same power strip or smart plug, so they switch together. Check once that the tiles come up normally that way. If they come up red, your 24 V supply is faster than the ESP32: leave the charger on for good and switch only the 24 V.

If it ever comes up red, leave the 5 V alone and power-cycle only the 24 V: unplug it, wait 5 seconds, plug it back in. The ESP32 keeps running, so ID is there when the tiles boot. Don't keep cycling both supplies together: if your 24 V supply won the race once, it will most likely win it again, and five quick power cycles factory-reset the set.

In [bring-up](#bring-up), "the brick" means both supplies: unplug both in step 4, and in step 5 plug in the charger first and the 24 V last.

---

## Firmware

#### Pins

| Board | ID out | Stay away from |
|---|---|---|
| ESP32 DevKitC / WROOM-32 | GPIO25 | 0, 2, 5, 12, 15 (strapping), 6–11 (flash), 34–39 (input only) |
| ESP32-C3 SuperMini | GPIO4 | 2, 8, 9 (strapping), 18/19 (USB), 20/21 (UART) |
| ESP32-S3 (Zero, DevKitC-1) | GPIO4 | 0, 3, 45, 46 (strapping), 19/20 (USB), 26–37 (flash/PSRAM) |
| ESP32-C6 (DevKitC-1, SuperMini) | GPIO18 | 4, 5, 8, 9, 15 (strapping), 12/13 (USB), 24–30 (flash) |

The sketch picks the pin in this table for your chip (an ESP32-S2 gets GPIO4 too). To use another one, add `-DID_PIN=<n>` to `build_flags` in `platformio.ini`, or edit the `kIdPin` line for your chip near the top of the sketch. The ESP8266 version used GPIO2. On an ESP32 that is a strapping pin with the on-board LED on it, so don't copy that.

#### Flashing it

1. Open the `arduino/` folder in PlatformIO. Its [`platformio.ini`](arduino/platformio.ini) is set up for a classic ESP32 on arduino-esp32 3.3.12, including [the bootloader for XMC A-series flash](bootloader/README.md). On an ESP32-C3, S3 or C6, change `board` and delete the `custom_bootloader` line first: the patched bootloader is for the classic ESP32 only. The Arduino IDE should work too, though I've only flashed mine from PlatformIO: open [`arduino/lifx_tile_id/lifx_tile_id.ino`](arduino/lifx_tile_id/lifx_tile_id.ino) with the ESP32 boards package installed (core 2.x and 3.x both compile; the ESP32-C6 needs 3.x). If your board needs the patched bootloader, the IDE needs [one extra step](bootloader/README.md#arduino-ide).
2. Leave `ID_FREQUENCY_HZ` at 50, or set it to 60.
3. If the tiles are connected, unplug the brick first: the ESP32 resets during the upload, and ID would drop while they are powered. Then upload over USB with the buck's 5 V wire disconnected (see [bring-up](#bring-up)).

The LEDC peripheral generates the square wave in hardware, so `loop()` does nothing at all. On the S2/S3/C3/C6 it runs from the 40 MHz crystal and on the classic ESP32 from the 80 MHz APB clock. That gives exactly 50.000 Hz. If you switch to 60 Hz you get 60.001 Hz (classic ESP32) or 59.998 Hz (S2/S3/C3/C6), still far inside the 45–65 Hz window (on core 2.x the boot message may then show "59 Hz").

---

## Bring-up

No oscilloscope needed. A multimeter with a Hz range is plenty.

#### 1: Build it without the tiles

Everything wired, cable end not plugged into anything yet.

#### 2: Flash the ESP32 over USB

Disconnect the buck's 5 V wire while USB is plugged in (keep GND connected). Otherwise the buck and your computer's USB port fight over the board's 5 V pin, unless your board has a diode there. When flashing is done, unplug USB and reconnect the buck's 5 V wire before step 3.

#### 3: Measure with the brick plugged in

Probe at the cable end, against black:

| Measure | Expected | If not |
|---|---|---|
| ESP32 5V pin | 4.8–5.2 V | Check the buck before going further |
| Red | 24 V | Check F1 and the jack |
| Yellow, DC volts | About 1.6 V (a meter averages 0 and 3.3 V at 50 % duty) | 0 V or 3.3 V: no square wave |
| Yellow, Hz | 50 (or 60 if you changed it) | Check the sketch's pin matches your wiring (see [pins](#pins)) |
| Yellow, DC volts, worst case | **Never above 3.3 V** | Stop. Something is wired to 24 V |

#### 4: Unplug the brick from the wall, then connect the tiles

Chain all the tiles first, then plug the cable into the first one. Tiles don't like hot-plugging, so always connect everything with the power off.

#### 5: Plug the brick back in

Plug the brick into the wall last (if you pulled the barrel instead, unplug the brick from the wall before plugging the barrel back in). The tiles come up straight away.

Voila! The tiles come back in their last state, or in setup mode if they were ever factory reset. The LIFX app takes it from there.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| All tiles breathe red right after power-up | No ID at the first tile, or wrong frequency | Measure Hz on yellow at the tile end. Re-check that yellow really is pin 7 |
| First tile works, the rest breathe red | Bad contact on a link cable, or not enough power downstream | Reseat every link cable starting from the last tile, then power-cycle the whole set. LIFX staff say any cable change needs a full power cycle |
| Still red downstream after that | RS-485 bus without termination (unconfirmed) | One person says the original PSU had a termination resistor. Try 120 Ω between green and blue at the PSU end |
| Tiles turn red after re-flashing the ESP32 | The upload reset the ESP32 while the tiles were powered, so ID dropped | Power-cycle the whole set. Next time, unplug the brick before uploading |
| The set came back factory reset | Five quick power cycles, or ID gone for a few seconds while powered | Re-onboard in the LIFX app. Don't toggle the brick on and off rapidly |
| A sixth tile stays dark or red | The firmware stops at 1 leader + 4 followers | Run a second chain with its own ID output and 24 V feed. It shows up as a second device |
| ESP32 boot-loops with "XMC flash startup fail" (or silently, on Arduino core 2.0.x) | The board's flash is an XMC A-series chip that ESP-IDF's bootloader doesn't recognise. `esptool flash-id` says `20` / `7017` to `7020` | Use [the patched bootloader](bootloader/README.md). Turning off `CONFIG_BOOTLOADER_FLASH_XMC_SUPPORT` doesn't help |

---

## Things nobody has measured yet

If you have a working original PSU or a spare set to sacrifice, these would settle a few open questions:

1. **What the PSU's button does on the ID wire** while held (stuck low, stuck high, floating?) and for how long.
2. **How long ID can drop out on powered tiles** before they go red or factory-reset (0.2 s, 0.5 s, 1 s, 2 s, 5 s).
3. **The tile's ID input impedance.** With 1.1 kΩ in series, the high level at the tile should be close to 3.3 V. With a multimeter: measure DC volts on yellow at the first tile, with the tiles connected and powered. At 50 % duty the high level is twice that reading. If it reads below about 1.4 V (a high level below about 2.8 V), R2 can drop to 470 Ω (2 W). Be aware that the 24 V short protection above then no longer holds: about 45 mA would go into the 3V3 rail.
4. **Whether the original PSU terminates RS-485.** Measure resistance between pins 2 and 6 on a dead PSU's socket.

---

## Credits and sources

This only exists because other people did the hard part:

| Who | What |
|---|---|
| [u/Final-Hawk90](https://www.reddit.com/r/lifx/comments/1mrlftz/) | The original ESP8266 fix and [the gist](https://gist.github.com/joshua-pe/f0962231e0d79862c8f17919f2ba7601) |
| u/Redd1ng (LIFX) | [The pinout](https://www.reddit.com/r/lifx/comments/oepf9e/comment/h4am5vi/), and [the 50 Hz / 50 % duty details with a scope capture of the original PSU](https://www.reddit.com/r/lifx/comments/1h4omu5/comment/m0cp0ua/) |
| u/delfick (LIFX) | [How the leader/follower chain works](https://www.reddit.com/r/lifx/comments/kw8wj3/comment/gj305o1/), and [what the PSU button does](https://community.lifx.com/t/button-on-lifx-tile-power-adapter/5281) |
| u/Icy-Ninja-622 | [Reverse-engineering the Tile firmware](https://www.reddit.com/r/lifx/comments/1mrlftz/comment/ots5kqt/): the 45–65 Hz window, the chain frequencies, the 5-tile limit |
| u/Jakey01010 | [Proving a Mean Well brick plus a cut LIFX cable works](https://www.reddit.com/r/lifx/comments/1mrlftz/comment/or2ddjq/) |
| u/rstmtt | Five tiles running from a bench signal generator at 50 Hz / 3.3 V |
| [FCC ID 2AA53-TILE](https://fccid.io/2AA53-TILE) | Internal photos showing the ESP32 inside each tile |
