<p align="center">
  <a href="#how-the-tile-psu-works"><img src="https://img.shields.io/badge/How_it_works-a_50_Hz_square_wave-6f42c1" alt="It is literally a square wave. That's the whole secret."></a>
  <a href="#parts"><img src="https://img.shields.io/badge/Parts-an_ESP32_and_two_supplies-2da44e" alt="Not a single resistor"></a>
  <a href="#wiring"><img src="https://img.shields.io/badge/Wiring-one_ground_tie-d29922" alt="The one wire that matters"></a>
  <a href="#firmware"><img src="https://img.shields.io/badge/Firmware-one_Arduino_sketch-0e8a16" alt="loop() does nothing, and that's the point"></a>
  <a href="#bring-up"><img src="https://img.shields.io/badge/Bring--up-no_scope_needed-8250df" alt="A multimeter will do"></a>
  <a href="#troubleshooting"><img src="https://img.shields.io/badge/Troubleshooting-still_red%3F-cf222e" alt="Breathing red is the Tile's love language"></a>
</p>

# LIFX Tile power supply, rebuilt around an ESP32

My LIFX Tile power supply died, and you can't buy a new one. Tiles have been discontinued for years.

The annoying part is that the Tile power supply is not just a power supply. Besides 24 V, it sends a square wave down a dedicated wire. Without that signal the tiles refuse to work and sit there breathing red at you.

[u/Final-Hawk90 worked out](https://www.reddit.com/r/lifx/comments/1mrlftz/) that you can fake that signal with an ESP8266. I wanted to do it with an ESP32.

> [!NOTE]
> This works on my tiles: a 24 V supply on the tiles, the ESP32 on its own 5 V supply, the two grounds tied together, and the ESP32's GPIO wired straight to the tile's ID wire. The sketch was flashed from the PlatformIO project in `arduino/` (my board needed [the patched bootloader](bootloader/README.md)). It also compiles on Arduino-ESP32 2.0.11 and 3.3.9.

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

1. **The tile's own MCU is an ESP32.** You can see it in the FCC photos. ESP32 inputs are not 5 V tolerant, so a 5 V Arduino Nano is a bad idea. A 3.3 V ESP32 matches the original PSU's 3.14 V peak perfectly, which is why its pin can go straight to the ID wire.
2. **The tiles read ID when they boot, and keep watching it.** If ID is missing at boot you get breathing red until you power-cycle the whole set.
3. **The PSU has a factory-reset button.** [Holding it "for a few seconds"](https://community.lifx.com/t/button-on-lifx-tile-power-adapter/5281) factory-resets the tiles, "much like turning the tile off and on 5 times". The PSU has no data lines, so that gesture almost certainly travels over the ID wire. My takeaway: **never let the ID signal drop while the tiles are powered, and never power-cycle them five times in a row.**

That's why the ESP32 runs a tiny sketch with no Wi-Fi, no OTA and nothing else that could reboot it: once it's powered, ID never stops. Two things can still stop it: the ESP32 losing its 5 V while the tiles keep their 24 V, which is why [the power-up order](#power-up-order) matters, and re-flashing it, which is why the 24 V comes off before [an upload](#flashing-it).

---

## Parts

| Part | Notes |
|---|---|
| 24 V supply, regulated, at least 30 W | I used one I already had, with its own cable bringing the 24 V in. Run it through [the checks below](#checking-the-24-v-supply). Buying one, I'd go with a **Mean Well GST40A24-P1J** (24 V 1.67 A, needs an IEC C13 mains cord; cut its barrel plug off like the LIFX cable's and run the polarity check below on the two wires): its overload protection kicks in at 1.75–2.5 A, close to what the Tile connector's contacts can take. Five tiles are rated 30 W (34 W at full brightness), the same as the original LTPSU24A1 |
| 5 V supply for the ESP32 | Rated 1 A or more (Espressif asks for at least 0.5 A), with a short cable that isn't paper-thin. Into the board's USB port, or on its 5V pin. A supply that sags browns the ESP32 out, and that drops ID on powered tiles. Not a computer: opening a serial monitor, or the computer sleeping or rebooting, resets the ESP32 while the tiles stay powered. If your board has a USB-C port and a USB-C charger gives it nothing, use a USB-A charger with an A-to-C cable: some boards can't ask a USB-C charger for power |
| ESP32 board | ESP32 DevKitC (WROOM-32), ESP32-C3 SuperMini or an ESP32-S3 board. Any 3.3 V ESP32 works |
| A short piece of wire | The ground tie between the two supplies. See [wiring](#wiring) |
| The original 2 m LIFX Tile cable (L3TILECB20) | We cut one end off. See [the cable](#the-cable) |

That's it. No resistors, no converter, nothing to solder onto a board.

#### Checking the 24 V supply

Any supply works if it passes these checks:

| Check | Why | How |
|---|---|---|
| Regulated **constant-voltage** 24 V DC | A constant-current LED driver will happily push its rated current into the tiles at whatever voltage that takes | The label says 24 V ⎓ (DC), not "constant current" or a current like "700 mA" as the output |
| At least 1.25 A (30 W) for five tiles | That's what LIFX asks for. 1.5 A leaves headroom for full brightness (34 W) | Fewer tiles: about 0.3 A each |
| 23.5–25.5 V with nothing connected | Some cheap supplies idle high, and the tiles were built for the original 24 V supply | Measure with a multimeter, no load |
| Polarity | The tiles have no reverse protection | Red probe on the wire you think is +, black probe on the other: the meter must read +24 V, not −24 V |
| Overload protection | The original PSU was power-limited, and Tile connectors have melted before. A big LED-strip supply (24 V 5 A, 10 A…) will push all of that into a shorted Tile connector | Prefer a 30–60 W brick (they cut out on overload) over a big open-frame supply |
| Wire gauge | Up to 1.25 A, plus the volts lost along the way | The supply's own cable is fine if it came with a 1.25 A or bigger supply. Extending it: 22 AWG up to 1 m, 20 AWG beyond |

---

## The cable

The PSU end of the original cable has nothing left to plug into, so I cut it off, same as u/Jakey01010 did. The connector is a proprietary 8-pin mini-USB lookalike: Nikon UC-E6 camera cables have the right plug but only 4 thin wires, and LIFX advised against them.

#### 1: Cut the PSU end off the 2 m cable

Leave yourself 10–15 cm of tail past the cut. Inside there are 5 wires: thicker red and black, thinner green, blue and yellow.

#### 2: Check every wire with a multimeter

Probe the plug's contacts with a needle and confirm continuity from yellow to pin 7, red to pins 1 and 5, and black to pins 4 and 8. Do not trust colours you did not check yourself. Pins 4 (GND) and 5 (+24 V) sit right next to each other in that connector.

Plugging the end into an unpowered tile and probing its other socket only works for red, black, green and blue. Each tile port has its own ID line, so yellow never shows continuity through a tile.

#### 3: Insulate green and blue separately

Heat-shrink each one on its own. They are the RS-485 bus between tiles. The original PSU sent no data on them (one report says it had a termination resistor across them, see [things nobody has measured yet](#things-nobody-has-measured-yet)), and here they stay unconnected.

---

## Wiring

```text
 24 V supply + ─────────────────────────────────────────────────────────► RED     tile pins 1 + 5
 24 V supply − ───────────────────┬─────────────────────────────────────► BLACK   tile pins 4 + 8
                                  │
                                  │  ground tie: one wire, soldered or screwed down
                                  │
                ┌─────────────────┴┐
 5 V supply ═══►│ ESP32         GND│
 (USB or 5V pin)│                  │
                │          GPIO25  ├────────────────────────────────────► YELLOW  tile pin 7 (ID)
                └──────────────────┘

 GREEN, BLUE (RS-485): not connected, insulated separately.
```

Three wires to the tiles, one to tie the grounds. The 24 V supply only feeds the tiles, the 5 V supply only feeds the ESP32, and GPIO25 goes straight to the yellow wire.

Why the grounds have to meet even though the voltages differ: a voltage only means something between two points. The ESP32's 3.3 V on GPIO25 is 3.3 V above *its* GND, and the tile reads the yellow wire against *its* GND, which is the black wire. Tie the two minus sides together and both supplies measure from the same zero. The 24 V supply still sits 24 V above it, the 5 V supply 5 V above it, and nothing flows through the tie except the ID signal's tiny return current. Without the tie, the yellow wire has no reference at all and the tiles see garbage.

The rules:

1. **Tie the minus sides, never the plus sides.** 24 V and 5 V must never touch.
2. **One permanent tie.** Solder it or screw it down, close to the cut end of the tile cable. Don't count on a plug or the tile connector to make it, and don't add a second tie somewhere else.
3. **Use supplies with isolated outputs.** Wall bricks and USB chargers are isolated from the mains, so tying their minus wires together is fine. Stick to supplies that say so (a branded brick, the double-square Class II symbol, or "SELV" on the label); a bare AC-to-DC module may not be isolated. A USB charger's output also floats at a surprisingly high AC voltage to earth through its interference-suppression capacitor, tens of volts on a meter, but it can only push microamps. That's one more reason the tie stays permanently wired: the two grounds always meet before the yellow wire does.
4. **Keep the yellow wire away from the 24 V.** There's nothing between GPIO25 and the yellow wire, same as the original ESP8266 fix. That's fine as long as it only ever sees the ESP32's own 3.3 V, but if it touches 24 V, the ESP32 is gone, and the first tile's ID input may well go with it. So check it before GPIO25 is ever connected: see [bring-up](#bring-up) step 1.

#### Power-up order

The two supplies don't start and stop together on their own, and the tiles care about the order:

| | What happens |
|---|---|
| 5 V first, then 24 V | The best order: ID is already running when the tiles boot |
| 24 V first, then 5 V | Red: the tiles look for ID about half a second after they get power, and the 5 V supply plus the ESP32's boot take about that long on their own |
| Both at once (same strip or smart plug) | Usually fine: a Mean Well GST takes about a second to bring up its 24 V, and by then ID is running. A 24 V supply that starts much faster can lose that race |
| 5 V lost while the tiles run | ID stops while the tiles are powered: red, or worse (that's the factory-reset gesture). **Avoid it** |
| 24 V lost while the ESP32 runs | The tiles turn off, but ID keeps going into them. With nothing between GPIO25 and the yellow wire, only the pin's drive (up to about 20 mA by default) and the tile's own input limit that current. A second or so of it happens at every 5 V-first start; hours of it is untested |

What I'd do: both supplies on the same power strip or smart plug, so they switch together, and turn the tiles off from the LIFX app day to day rather than cutting their power. Check once that the tiles come up normally when both switch on together. If they come up red, your 24 V supply is faster than the ESP32: plug the 5 V in first and the 24 V a couple of seconds later. Leaving the 5 V on for good and switching only the 24 V works too, but then ID runs into the tiles for as long as they're off, which is the untested row above.

The thing to avoid is the 5 V dropping on its own while the tiles are on: a knocked USB cable, a 5 V supply giving up, or a mains flicker it doesn't ride through. Give the ESP32 a decent supply and a cable that can't get pulled.

If it ever comes up red, leave the 5 V alone and power-cycle only the 24 V: unplug it, wait 5 seconds, plug it back in. The ESP32 keeps running, so ID is there when the tiles boot. Don't keep cycling both supplies together: if your 24 V supply won the race once, it will most likely win it again, and five quick power cycles factory-reset the set. If a 24 V-only cycle doesn't clear it, unplug both, wait 10 seconds, plug in the 5 V, then the 24 V.

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
3. If the tiles are connected, unplug the 24 V supply first: the ESP32 resets during the upload, and ID would drop while they are powered. Then plug the ESP32 into your computer and upload (see [bring-up](#bring-up) step 2 about its 5 V supply). When it's done, unplug the computer, put the 5 V supply back, and plug the 24 V in last.

The LEDC peripheral generates the square wave in hardware, so `loop()` does nothing at all. On the S2/S3/C3/C6 it runs from the 40 MHz crystal and on the classic ESP32 from the 80 MHz APB clock. That gives exactly 50.000 Hz. If you switch to 60 Hz you get 60.001 Hz (classic ESP32) or 59.998 Hz (S2/S3/C3/C6), still far inside the 45–65 Hz window (on core 2.x the boot message may then show "59 Hz").

---

## Bring-up

No oscilloscope needed. A multimeter with a Hz range is plenty.

#### 1: Wire it up without the tiles, and check yellow first

Everything wired except the GPIO25-to-yellow joint, cable end not plugged into anything yet. Plug in the 24 V and measure yellow against black: it must read about 0 V. Anything near 24 V means yellow touches the 24 V somewhere, and that would have killed the ESP32, so fix it first. Unplug the 24 V, then make the GPIO25-to-yellow joint.

#### 2: Flash the ESP32 over USB

With the 24 V unplugged. If your 5 V supply feeds the board's 5V pin rather than its USB port, disconnect it while the computer is plugged in: two supplies on the same 5V pin fight each other. When flashing is done, unplug the computer and put the 5 V supply back.

#### 3: Measure with both supplies plugged in

Probe at the cable end, against black. That also proves the ground tie is in place: if the 5V pin and yellow both read about 0 V (or wander) while red reads 24 V, the tie is missing.

| Measure | Expected | If not |
|---|---|---|
| ESP32 5V pin | 4.5–5.2 V (fed through the USB port, many boards lose a few tenths of a volt in a diode first) | About 0 V or wandering: measure it against the ESP32's own GND pin instead. If that reads 5 V, the ground tie is missing. Otherwise check the 5 V supply and its cable |
| Red | 24 V | Check the 24 V supply and its wiring |
| Yellow, DC volts | About 1.6 V (a meter averages 0 and 3.3 V at 50 % duty) | 0 V or 3.3 V: no square wave |
| Yellow, Hz | 50 (or 60 if you changed it) | Check the sketch's pin matches your wiring (see [pins](#pins)) |
| Yellow, DC volts, worst case | **Never above 3.3 V** | Unplug everything. Something touches 24 V, and the ESP32 is most likely already damaged. Redo the step 1 check |

#### 4: Unplug both supplies, then connect the tiles

Chain all the tiles first, then plug the cable into the first one. Tiles don't like hot-plugging, so always connect everything with the power off.

#### 5: Plug the 5 V in first, then the 24 V

The tiles come up straight away.

Voila! The tiles come back in their last state, or in setup mode if they were ever factory reset. The LIFX app takes it from there.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| All tiles breathe red right after power-up | No ID at the first tile, wrong frequency, or the 24 V came up before the ESP32 | Measure Hz on yellow at the tile end, and check the ground tie. Re-check that yellow really is pin 7. Then see [power-up order](#power-up-order) |
| First tile works, the rest breathe red | Bad contact on a link cable, or not enough power downstream | Reseat every link cable starting from the last tile, then power-cycle the tiles (the 24 V only, see [power-up order](#power-up-order)). LIFX staff say any cable change needs a full power cycle |
| Tiles turn red after re-flashing the ESP32 | The upload reset the ESP32 while the tiles were powered, so ID dropped | Power-cycle the 24 V only: unplug it, wait 5 seconds, plug it back in. Next time, unplug the 24 V supply before uploading |
| The set came back factory reset | Five quick power cycles, or ID gone for a few seconds while powered | Re-onboard in the LIFX app. Don't toggle the supplies on and off rapidly |
| A sixth tile stays dark or red | The firmware stops at 1 leader + 4 followers | Run a second chain with its own ID output and 24 V feed. It shows up as a second device |
| ESP32 boot-loops with "XMC flash startup fail" (or silently, on Arduino core 2.0.x) | The board's flash is an XMC A-series chip that ESP-IDF's bootloader doesn't recognise. `esptool flash-id` says `20` / `7017` to `7020` | Use [the patched bootloader](bootloader/README.md). Turning off `CONFIG_BOOTLOADER_FLASH_XMC_SUPPORT` doesn't help |

---

## Things nobody has measured yet

If you have a working original PSU or a spare set to sacrifice, these would settle a few open questions:

1. **What the PSU's button does on the ID wire** while held (stuck low, stuck high, floating?) and for how long.
2. **How long ID can drop out on powered tiles** before they go red or factory-reset (0.2 s, 0.5 s, 1 s, 2 s, 5 s).
3. **Whether the original PSU terminates RS-485.** Measure resistance between pins 2 and 6 on a dead PSU's socket.
4. **How much current ID pushes into unpowered tiles.** Multimeter on its mA range in series between GPIO25 and yellow, tiles connected, 5 V on, 24 V unplugged.

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
