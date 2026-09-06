/*
 * Retirement Countdown Clock
 * ESP8266 + integrated 0.96" SSD1306 OLED (128x64), board rev V2.1.2
 *
 * Displays today's date, days remaining to the target date, working days
 * remaining, and a progress bar.
 *
 * WiFi credentials live in secrets.h (gitignored).
 * Copy secrets.h.example to secrets.h and fill it in.
 *
 * I2C pins CONFIRMED on this board by hardware test:
 *   SDA = GPIO14 (silkscreen D6)
 *   SCL = GPIO12 (silkscreen D5)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

#include "secrets.h"

// ============ CONFIGURE THIS SECTION ============

// The finish line. These are just the defaults on first boot - once set from
// the web page (see /set-target below), the real values live in LittleFS at
// /target.txt and survive reboots/reflashes.
int targetY = 2037;
int targetM = 6;
int targetD = 6;

// When the countdown "started" - used only for the progress bar. Reset to
// today automatically whenever a new target is set from the web page.
int startY = 2026;
int startM = 8;
int startD = 1;

// Working days per month, used to derive working days remaining.
const double WORKDAYS_PER_MONTH = 21.0;

// South Africa: UTC+2, no daylight saving.
const char* TZ_INFO = "SAST-2";

// ================================================

#define SCREEN_W   128
#define SCREEN_H   64
#define OLED_ADDR  0x3C
#define PIN_SDA    14   // D6 on this board
#define PIN_SCL    12   // D5 on this board

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
ESP8266WebServer server(80);
ESP8266WiFiMulti wifiMulti;

const char* MONTHS[] = { "Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec" };

time_t targetTime;
time_t startTime;
char   targetLabel[24];
unsigned long lastDraw = 0;

// Snapshot of the current countdown state, shared by the OLED draw and the
// web server so both always report the same numbers.
struct Status {
  bool   retired;
  char   todayStr[24];
  long   daysLeft;
  long   workDays;
  int    pct;
};

Status computeStatus() {
  Status st;
  time_t now = time(nullptr);
  struct tm* lt = localtime(&now);
  strftime(st.todayStr, sizeof(st.todayStr), "%a %d %b %Y", lt);

  long secondsLeft = (long)difftime(targetTime, now);
  st.retired  = secondsLeft <= 0;
  st.daysLeft = secondsLeft / 86400L;
  st.workDays = (long)(st.daysLeft * (WORKDAYS_PER_MONTH * 12.0) / 365.25);

  double total   = difftime(targetTime, startTime);
  double elapsed = difftime(now, startTime);
  double frac    = (total > 0) ? (elapsed / total) : 0.0;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  st.pct = (int)(frac * 100);

  return st;
}

// --- Persisted target/start date, so a new date set from the web page
// survives a reboot instead of reverting to the compiled-in defaults. ---

void saveTargetToFS() {
  File f = LittleFS.open("/target.txt", "w");
  if (!f) {
    Serial.println(F("Failed to open /target.txt for writing."));
    return;
  }
  f.printf("%d,%d,%d,%d,%d,%d\n", targetY, targetM, targetD, startY, startM, startD);
  f.close();
}

void loadTargetFromFS() {
  if (!LittleFS.exists("/target.txt")) return;
  File f = LittleFS.open("/target.txt", "r");
  if (!f) return;
  String line = f.readStringUntil('\n');
  f.close();

  int ty, tm, td, sy, sm, sd;
  if (sscanf(line.c_str(), "%d,%d,%d,%d,%d,%d", &ty, &tm, &td, &sy, &sm, &sd) == 6) {
    targetY = ty; targetM = tm; targetD = td;
    startY  = sy; startM  = sm; startD  = sd;
    Serial.println(F("Loaded saved target date from LittleFS."));
  }
}

// Build a local-midnight time_t from a calendar date.
time_t makeLocalDate(int y, int m, int d) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year  = y - 1900;
  t.tm_mon   = m - 1;
  t.tm_mday  = d;
  t.tm_hour  = 0;
  t.tm_min   = 0;
  t.tm_sec   = 0;
  t.tm_isdst = 0;
  return mktime(&t);
}

// Print text centred horizontally at a given y position.
void printCentered(const char* text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_W - (int)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

// Insert thousands separators: 4015 -> "4,015"
String withCommas(long n) {
  String s = String(n);
  String out = "";
  int count = 0;
  for (int i = s.length() - 1; i >= 0; i--) {
    out = s[i] + out;
    count++;
    if (count % 3 == 0 && i > 0) out = "," + out;
  }
  return out;
}

void showStatus(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  printCentered(line1, 20, 1);
  if (line2) printCentered(line2, 36, 1);
  display.display();
}

// --- Idle-screen easter egg: a little robot that walks across the panel.
// Shown once every EASTER_EGG_EVERY one-minute refreshes, purely for
// personality - not tied to any activity/idle sensor. ---

const int EASTER_EGG_EVERY = 5;   // show the robot once every 5 minutes
unsigned long refreshCount = 0;

void drawRobot(int x, int y, bool frameA) {
  // antenna
  display.drawLine(x + 6, y - 3, x + 6, y, SSD1306_WHITE);
  display.fillCircle(x + 6, y - 4, 1, SSD1306_WHITE);

  // head, with two eye "holes" punched through
  display.fillRoundRect(x + 2, y, 9, 7, 2, SSD1306_WHITE);
  display.drawPixel(x + 4, y + 3, SSD1306_BLACK);
  display.drawPixel(x + 8, y + 3, SSD1306_BLACK);

  // body
  display.fillRoundRect(x + 1, y + 8, 11, 8, 1, SSD1306_WHITE);

  // arms
  display.drawLine(x - 1, y + 9, x - 1, y + 14, SSD1306_WHITE);
  display.drawLine(x + 13, y + 9, x + 13, y + 14, SSD1306_WHITE);

  // legs, alternating for a walk cycle
  if (frameA) {
    display.drawLine(x + 3, y + 16, x + 1, y + 20, SSD1306_WHITE);
    display.drawLine(x + 8, y + 16, x + 10, y + 20, SSD1306_WHITE);
  } else {
    display.drawLine(x + 3, y + 16, x + 5, y + 20, SSD1306_WHITE);
    display.drawLine(x + 8, y + 16, x + 6, y + 20, SSD1306_WHITE);
  }
}

void playRobotWalk() {
  const int robotY = 20;  // keeps the ~20px-tall robot within the panel
  bool frame = false;
  for (int x = -14; x <= SCREEN_W + 14; x += 4) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawRobot(x, robotY, frame);
    display.display();
    frame = !frame;
    server.handleClient();  // stay responsive during the ~2.7s animation
    delay(70);
  }
}

// --- Light local web server: mirrors what's on the OLED ---

void handleRoot() {
  Status st = computeStatus();
  char dateVal[11];
  snprintf(dateVal, sizeof(dateVal), "%04d-%02d-%02d", targetY, targetM, targetD);

  String html = F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='30'>"
    "<title>Retirement Countdown</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding-top:40px}"
    ".date{color:#ffdd00;font-size:1.3em}"
    ".big{color:#55ccff;font-size:4em;margin:10px 0}"
    ".sub{color:#ffdd00;font-size:1.1em;margin:4px 0}"
    "progress{width:80%;height:18px}"
    "form{margin-top:28px}"
    "input[type=date]{font-size:1em;padding:5px;border-radius:4px;border:none}"
    "button{font-size:1em;padding:5px 12px;margin-left:6px;border-radius:4px;border:none;background:#55ccff;cursor:pointer}"
    "</style></head><body>"
  );
  if (st.retired) {
    html += F("<h1 class='big'>RETIRED</h1><p class='sub'>Go do something fun</p>");
  } else {
    html += "<div class='date'>" + String(st.todayStr) + "</div>";
    html += "<div class='big'>" + withCommas(st.daysLeft) + "</div>";
    html += "<div class='sub'>" + String(targetLabel) + "</div>";
    html += "<div class='sub'>" + withCommas(st.workDays) + " work days</div>";
    html += "<progress value='" + String(st.pct) + "' max='100'></progress> " + String(st.pct) + "%";
  }
  html += "<form method='POST' action='/set-target'>";
  html += "<label>New target date: <input type='date' name='target' value='" + String(dateVal) + "'></label>";
  html += "<button type='submit'>Set</button>";
  html += "</form>";
  html += F("</body></html>");
  server.send(200, "text/html", html);
}

// Sets a new countdown target from the web form. The progress-bar "start"
// date is reset to today, since the old start date has no meaning against a
// brand new target. Persists to LittleFS so it survives a reboot.
void handleSetTarget() {
  if (!server.hasArg("target")) {
    server.send(400, "text/plain", "Missing 'target' field.");
    return;
  }

  int y, m, d;
  bool ok = sscanf(server.arg("target").c_str(), "%d-%d-%d", &y, &m, &d) == 3
            && y >= 1970 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
  if (!ok) {
    server.send(400, "text/plain", "Invalid date. Expected YYYY-MM-DD.");
    return;
  }

  time_t now = time(nullptr);
  struct tm* lt = localtime(&now);

  targetY = y; targetM = m; targetD = d;
  startY  = lt->tm_year + 1900;
  startM  = lt->tm_mon + 1;
  startD  = lt->tm_mday;

  targetTime = makeLocalDate(targetY, targetM, targetD);
  startTime  = makeLocalDate(startY, startM, startD);
  snprintf(targetLabel, sizeof(targetLabel), "to %02d %s %d",
           targetD, MONTHS[targetM - 1], targetY);

  saveTargetToFS();

  Serial.print(F("New target set: "));
  Serial.println(targetLabel);

  server.sendHeader("Location", "/");
  server.send(303);

  lastDraw = millis() - 60000;  // force an immediate OLED refresh
}

void handleStatusJson() {
  Status st = computeStatus();
  String json = "{";
  json += "\"today\":\"" + String(st.todayStr) + "\",";
  json += "\"days_left\":" + String(st.daysLeft) + ",";
  json += "\"work_days_left\":" + String(st.workDays) + ",";
  json += "\"target\":\"" + String(targetLabel) + "\",";
  json += "\"progress_pct\":" + String(st.pct) + ",";
  json += "\"retired\":" + String(st.retired ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Retirement Countdown Clock starting..."));

  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS mount failed - target date won't persist across reboots."));
  } else {
    loadTargetFromFS();
  }

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 not found at 0x3C on GPIO14/GPIO12."));
    Serial.println(F("Run tools/i2c_scan to find the real address/pins."));
    for (;;) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  showStatus("Connecting to", "WiFi...");

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
#ifdef WIFI_SSID2
  // Optional backup access point, tried if the primary one isn't in range.
  wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);
#endif

  int attempts = 0;
  while (wifiMulti.run() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nWiFi connection failed."));
    showStatus("WiFi failed", "check secrets.h");
    delay(5000);
    ESP.restart();
  }

  Serial.println();
  Serial.print(F("Connected to: "));
  Serial.println(WiFi.SSID());
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("RSSI: "));
  Serial.println(WiFi.RSSI());

  showStatus("Syncing time...", NULL);

  // TZ-aware NTP. The ESP8266 SNTP client re-syncs automatically.
  configTime(TZ_INFO, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  time_t now = time(nullptr);
  attempts = 0;
  while (now < 1700000000 && attempts < 60) {  // wait for a sane epoch
    delay(500);
    now = time(nullptr);
    attempts++;
  }

  if (now < 1700000000) {
    Serial.println(F("NTP sync failed."));
    showStatus("NTP failed", "restarting...");
    delay(3000);
    ESP.restart();
  }

  targetTime = makeLocalDate(targetY, targetM, targetD);
  startTime  = makeLocalDate(startY, startM, startD);

  snprintf(targetLabel, sizeof(targetLabel), "to %02d %s %d",
           targetD, MONTHS[targetM - 1], targetY);

  Serial.print(F("Time synced: "));
  Serial.println(ctime(&now));

  server.on("/", handleRoot);
  server.on("/status.json", handleStatusJson);
  server.on("/set-target", HTTP_POST, handleSetTarget);
  server.begin();
  Serial.print(F("Web server started: http://"));
  Serial.println(WiFi.localIP());

  lastDraw = millis() - 60000;  // force an immediate draw
}

// This panel is a two-tone OLED: rows 0-15 sit under a yellow filter, rows
// 16-63 under blue. That split is physical, not something the driver can
// change - so layout below is deliberately anchored to that boundary:
// the date stays inside the yellow band, and everything from the big
// number down starts exactly at row 16 so nothing straddles the two colors.
void draw() {
  Status st = computeStatus();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (st.retired) {
    printCentered("RETIRED", 16, 2);
    printCentered("Go do something fun", 44, 1);
    display.display();
    return;
  }

  // --- Line 1: today's date (yellow band, rows 0-15) ---
  printCentered(st.todayStr, 0, 1);

  // --- Line 2: big day count (blue band, starts at row 16) ---
  String days = withCommas(st.daysLeft);
  printCentered(days.c_str(), 16, 3);

  // --- Line 3: target date label ---
  printCentered(targetLabel, 40, 1);

  // --- Line 4: working days remaining (21/month basis) ---
  char sub[28];
  snprintf(sub, sizeof(sub), "%s work days", withCommas(st.workDays).c_str());
  printCentered(sub, 48, 1);

  // --- Line 5: web page IP, where the progress bar used to be ---
  String ip = WiFi.localIP().toString();
  printCentered(ip.c_str(), 56, 1);

  display.display();

  Serial.print(F("Days left: "));
  Serial.print(st.daysLeft);
  Serial.print(F("  Work days: "));
  Serial.print(st.workDays);
  Serial.print(F("  Progress: "));
  Serial.print(st.pct);
  Serial.println(F("%"));
}

void loop() {
  server.handleClient();  // serve web requests on every pass, not gated by the minute timer

  if (millis() - lastDraw >= 60000UL) {   // refresh once a minute
    lastDraw = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi dropped, reconnecting..."));
      wifiMulti.run();  // falls back across every AP added in setup()
    }

    refreshCount++;
    if (refreshCount % EASTER_EGG_EVERY == 0) {
      playRobotWalk();
    }
    draw();
  }
  delay(100);
}
