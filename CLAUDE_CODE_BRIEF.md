# Brief for Claude Code: Retirement Countdown Clock

## Objective

Flash an ESP8266 board with an integrated 0.96" OLED so it displays the number
of days remaining until a target retirement date. The board joins the home WiFi
network, syncs time over NTP, and refreshes the display once a minute.

Work in `/home/skippypeanut/Documents/github/countdown-clock`.

## Hardware

| Item | Detail |
|---|---|
| Board | ESP8266 (ESP-12F) with onboard 0.96" SSD1306 OLED, rev V2.1.2 |
| Display | 128x64 monochrome, I2C, expected address `0x3C` |
| I2C pins | **CONFIRMED by hardware test:** SDA = GPIO14 (D6), SCL = GPIO12 (D5) |
| Connection | USB-C to the Ubuntu host |
| USB bridge | CH340 or CP2102 -> expect `/dev/ttyUSB0` |
| Buttons | `RSET` only. No FLASH button; the board relies on auto-reset via DTR/RTS. |

## Files already in this repo

```
countdown-clock/
├── CLAUDE_CODE_BRIEF.md          <- this file
├── .gitignore
├── countdown_clock/
│   ├── countdown_clock.ino       <- main sketch
│   └── secrets.h.example         <- template for credentials
└── tools/
    └── i2c_scan/
        └── i2c_scan.ino          <- fallback, only if the OLED stays blank
```

The sketch is written and believed correct. Do not rewrite it unless a build
error or a hardware finding requires it.

---

## Task 1 — Ask the human for one value

Before doing anything else, ask for **the WiFi password** for SSID `MyHome`.

Do not guess it. Do not proceed without it.

The target date is already set in the sketch: **6 June 2037**. The I2C pins are
already confirmed correct. Nothing else needs asking.

## Task 2 — Environment setup

Install `arduino-cli` if it is not already present:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
  | BINDIR=~/.local/bin sh
```

Ensure `~/.local/bin` is on PATH.

Initialise the config and add the ESP8266 board index:

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

Install the display libraries:

```bash
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit SSD1306"
```

`Adafruit BusIO` is pulled in as a dependency automatically.

## Task 3 — Serial port permissions

The user must be in the `dialout` group to write to the serial port:

```bash
groups | grep -q dialout || sudo usermod -a -G dialout $USER
```

**If the group was just added, the human must log out and back in** (or open a
new shell with `newgrp dialout`) before uploads will work. Tell them this
explicitly rather than letting the upload fail.

Ubuntu ships `brltty`, which frequently grabs CH340 adapters and makes the port
disappear a second after plugging in. If `/dev/ttyUSB0` appears and then
vanishes, this is the cause:

```bash
sudo apt remove brltty
```

## Task 4 — Configure the sketch

```bash
cp countdown_clock/secrets.h.example countdown_clock/secrets.h
```

Edit `countdown_clock/secrets.h` with the real WiFi password.

Verify `secrets.h` is ignored by git before any commit:

```bash
git check-ignore -v countdown_clock/secrets.h
```

If this repo is or will become a public GitHub repo, that check must pass. Do
not commit until it does.

## Task 5 — Detect the board

```bash
arduino-cli board list
```

Expect something like `/dev/ttyUSB0` with an unknown or generic board type
(ESP8266 boards are frequently not auto-identified — that is fine, the FQBN is
supplied manually).

If no port appears, check `dmesg | tail -20` immediately after replugging.
A common cause is a charge-only USB-C cable — ask the human to try a different
cable before debugging further.

## Task 6 — Compile

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 countdown_clock
```

Fix any compile errors. Report the flash and RAM usage figures.

## Task 7 — Upload

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 countdown_clock
```

No button presses should be needed — the USB bridge drives auto-reset.

If upload fails with a sync or timeout error, ask the human to:
1. Hold `RSET`, start the upload, release `RSET` a second later; or
2. Unplug and replug the board, then retry immediately.

## Task 8 — Verify

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Expected serial output, in order:

```
Retirement Countdown Clock starting...
....
Connected. IP: 192.168.x.x
RSSI: -NN
Time synced: <date>
Days left: NNNN
```

On the OLED, expect four lines plus a bar:

```
      Sun 06 Sep 2026          <- today's date, from NTP
         3,925                 <- days remaining (large)
      to 06 Jun 2037           <- target date
      2,708 work days          <- at 21 work days/month
 [####------------]  17%       <- progress bar
```

**Acceptance criteria:**
- Today's date on line 1 matches the actual date in SAST
- Day count is close to 3,925 (it decrements daily)
- Work days is roughly 0.69 x the day count
- Serial shows a valid local IP and a synced date
- Nothing overlaps or is clipped on the 128x64 panel
- Display still correct after leaving it running for 5 minutes

## Task 9 — If the OLED stays blank (unlikely)

The pins were confirmed on the actual hardware, so this should not happen.
Only if `SSD1306 not found` appears on serial, flash the scanner:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 tools/i2c_scan
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 tools/i2c_scan
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

It tries five common SDA/SCL pin pairs and reports any I2C devices found.
Take whichever pair reports `0x3C` (or `0x3D`), update `PIN_SDA`, `PIN_SCL` and
`OLED_ADDR` in the main sketch, then repeat Tasks 6-8.

## Task 10 — Finish

- Write a short `README.md` covering what the device does, how to reflash it,
  and how to change the target date.
- Confirm once more that `secrets.h` is untracked.
- Report back: the day count shown, the board's IP address, and the flash usage.

---

## Constraints

- Do not commit `secrets.h` or the WiFi password in any form, including in
  commit messages or the README.
- Do not add cloud services, telemetry, or any outbound connection other than
  NTP.
- Prefer fixing the existing sketch over rewriting it.
- If a step needs a physical action (pressing a button, changing a cable),
  stop and ask rather than looping on retries.
