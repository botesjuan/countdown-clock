/*
 * I2C scanner for ESP8266.
 *
 * Only needed if the main sketch reports "SSD1306 not found".
 * Tries several common SDA/SCL pin pairs and reports any devices found.
 * The OLED is almost always at 0x3C, occasionally 0x3D.
 */

#include <Wire.h>

struct PinPair { uint8_t sda; uint8_t scl; const char* label; };

PinPair pairs[] = {
  { 4,  5,  "GPIO4/GPIO5  (D2/D1)" },
  { 5,  4,  "GPIO5/GPIO4  (D1/D2)" },
  { 0,  2,  "GPIO0/GPIO2  (D3/D4)" },
  { 2,  0,  "GPIO2/GPIO0  (D4/D3)" },
  { 12, 14, "GPIO12/GPIO14 (D6/D5)" },
};

void scanPair(PinPair p) {
  Serial.print(F("\nScanning "));
  Serial.println(p.label);

  Wire.begin(p.sda, p.scl);
  delay(50);

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  FOUND device at 0x"));
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
    delay(2);
  }

  if (found == 0) Serial.println(F("  (nothing)"));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("=== ESP8266 I2C scanner ==="));

  for (PinPair p : pairs) scanPair(p);

  Serial.println(F("\nDone. Set PIN_SDA/PIN_SCL and OLED_ADDR in the"));
  Serial.println(F("main sketch to whichever pair reported 0x3C or 0x3D."));
}

void loop() {
  delay(10000);
}
