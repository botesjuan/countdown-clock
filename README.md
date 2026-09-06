# Retirement Countdown Clock

An ESP8266 (ESP-12F, board rev V2.1.2) with an integrated 128x64 SSD1306 OLED
that displays the number of days remaining until a target date. It joins WiFi,
syncs the time over NTP, and redraws the display once a minute — fully
standalone after that, no host connection required.

## What it shows

```
      Sun 06 Sep 2026          <- today's date (from NTP, SAST timezone)
         3,925                 <- days remaining (large)
      to 06 Jun 2037           <- target date
      2,708 work days          <- days remaining x (21 workdays/month basis)
 [####------------]  17%       <- progress bar (from START date to TARGET date)
```

Once the target date passes, the display switches to a "RETIRED" screen.

The device also serves the same status over a small local web server — see
[Web interface](#web-interface) below — and can fail over to a backup WiFi
access point if the primary one isn't in range — see [Backup WiFi
network](#backup-wifi-network).

## Hardware

| Item | Detail |
|---|---|
| Board | ESP8266 (ESP-12F) with onboard 0.96" SSD1306 OLED, rev V2.1.2 |
| Display | 128x64 monochrome, I2C, address `0x3C` |
| I2C pins | SDA = GPIO14 (silkscreen D6), SCL = GPIO12 (silkscreen D5) |
| USB bridge | CH340, shows up as `/dev/ttyUSB0` on Linux |
| Reset | `RSET` button only — no FLASH button; auto-reset via USB DTR/RTS |

### Display colors

This is a two-tone OLED, not RGB: the panel has a yellow filter fixed over
the top 16 pixel rows and a blue filter over the remaining 48 rows. That
split is physical — firmware can only decide which row content lands on, it
cannot recolor a pixel. The layout in `draw()` is deliberately anchored to
that boundary: the date sits inside the yellow band (rows 0-15), and
everything from the big day count downward starts at row 16 so nothing
straddles the two colors. There is only one color boundary on this panel, so
content below the number is always blue — it cannot go back to yellow further
down the screen.

## Repo layout

```
countdown-clock/
├── CLAUDE_CODE_BRIEF.md          <- original build brief
├── README.md                     <- this file
├── .gitignore                    <- excludes secrets.h
├── countdown_clock/
│   ├── countdown_clock.ino       <- main sketch
│   ├── secrets.h.example         <- template for WiFi credentials
│   └── secrets.h                 <- your real credentials (gitignored, not committed)
└── tools/
    └── i2c_scan/
        └── i2c_scan.ino          <- fallback scanner if the OLED ever goes blank
```

## First-time setup

1. Install `arduino-cli` and the ESP8266 core:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
     | BINDIR=~/.local/bin sh
   arduino-cli config init
   arduino-cli config add board_manager.additional_urls \
     https://arduino.esp8266.com/stable/package_esp8266com_index.json
   arduino-cli core update-index
   arduino-cli core install esp8266:esp8266
   arduino-cli lib install "Adafruit GFX Library" "Adafruit SSD1306"
   ```
2. Make sure your user can access the serial port:
   ```bash
   sudo usermod -aG dialout $USER
   # log out/in (or `newgrp dialout`) for group membership to take effect
   ```
   On Ubuntu, `brltty` sometimes grabs CH340 adapters and makes `/dev/ttyUSB0`
   disappear right after it appears — if that happens: `sudo apt remove brltty`.
3. Set your WiFi credentials:
   ```bash
   cp countdown_clock/secrets.h.example countdown_clock/secrets.h
   # edit secrets.h with your real SSID/password
   ```
   `secrets.h` is gitignored — never commit it.

## Reflashing

```bash
export PATH=$HOME/.local/bin:$PATH
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 countdown_clock
arduino-cli upload  -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 countdown_clock
```

No button presses needed — the CH340 bridge drives auto-reset via DTR/RTS. If
upload fails with a sync/timeout error, unplug/replug the board and retry, or
hold `RSET`, start the upload, and release `RSET` a second later.

To watch serial output afterwards:
```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```
Note: on this board/host combo, `arduino-cli monitor`'s own stdout can end up
fully buffered when not attached to an interactive terminal (e.g. run in the
background or piped) and may show nothing until the process exits. If that
happens, run it directly in an interactive shell, or read the port with a
short Python/pyserial script instead.

## Changing the target date

Edit the top of `countdown_clock/countdown_clock.ino`:

```cpp
const int TARGET_Y = 2037;   // target year
const int TARGET_M = 6;      // target month
const int TARGET_D = 6;      // target day

const int START_Y = 2026;    // countdown "start" date - only affects the progress bar
const int START_M = 8;
const int START_D = 1;

const double WORKDAYS_PER_MONTH = 21.0;  // basis for the work-days-remaining line
```

Then recompile and reflash (see above).

## If the OLED ever goes blank

Only if serial shows `SSD1306 not found`:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 tools/i2c_scan
arduino-cli upload  -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 tools/i2c_scan
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

It scans several common SDA/SCL pin pairs and reports any I2C devices found.
Take whichever pair reports `0x3C` (or `0x3D`), update `PIN_SDA`, `PIN_SCL`
and `OLED_ADDR` in the main sketch, then recompile/reflash. On this specific
board this was already confirmed unnecessary — SDA=GPIO14/SCL=GPIO12 is correct.

## Web interface

The device runs a small local web server (`ESP8266WebServer`, bundled with
the ESP8266 core — no extra library needed) that mirrors what's on the OLED:

| Endpoint | Returns |
|---|---|
| `http://<device-ip>/` | Small auto-refreshing (30s) HTML page with the same date, day count, target, work days and progress bar |
| `http://<device-ip>/status.json` | The same data as JSON: `today`, `days_left`, `work_days_left`, `target`, `progress_pct`, `retired` |

The device's IP is printed on serial at boot (`Connected to: ... IP: ...`) and
also right after the web server starts. It's the same IP shown implicitly by
watching your router's DHCP client list.

This server is **local-network only, unauthenticated** — anyone on the same
WiFi/LAN can view it. No cloud service, telemetry, or outbound connection is
involved beyond the existing NTP sync; the server only answers requests, it
doesn't call out anywhere. If you don't want it reachable by anyone on your
network, ask for basic auth to be added, or don't route/port-forward it
beyond your LAN.

## Backup WiFi network

If you want the unit to fail over to a second access point (e.g. a phone
hotspot) when it's away from the primary network, add it to `secrets.h`:

```cpp
#define WIFI_SSID "MyHome"
#define WIFI_PASS "***REMOVED-ROTATE-THIS-PASSWORD***"

#define WIFI_SSID2 "YourBackupSSID"
#define WIFI_PASS2 "YourBackupPassword"
```

The sketch uses `ESP8266WiFiMulti` (also bundled with the core) to try both
networks at boot and whenever WiFi drops, connecting to whichever is in
range. `WIFI_SSID2`/`WIFI_PASS2` are optional — if you don't define them, the
board only tries the primary SSID, exactly as before. `secrets.h.example` has
the same two lines commented out as a template. After editing, recompile and
reflash (see above).

## Operation

The device is fully standalone once flashed and powered — it does not need
this host or any specific host. Power it from any 5V USB-C source. It
reconnects WiFi automatically if the connection drops (falling back across
every access point configured in `secrets.h`), and restarts itself if initial
WiFi or NTP sync fails.

## Security note

`secrets.h` contains your real WiFi password in plaintext. It is excluded via
`.gitignore` — verify with `git check-ignore -v countdown_clock/secrets.h`
before ever committing, especially if this becomes a public repo.
