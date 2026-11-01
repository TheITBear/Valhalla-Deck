// IMU QMI8658 (gesti) + RTC PCF85063 (ora senza rete). Stesso bus I2C: SDA 11, SCL 10.
// Il touch usa Wire1 (GPIO 1/3): qui si usa Wire, nessun conflitto.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

#define PIN_SENS_SDA 11
#define PIN_SENS_SCL 10
#define RTC_ADDR 0x51

static bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t *d, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  for (size_t i = 0; i < n; i++) Wire.write(d[i]);
  return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *d, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

// ---------------- RTC PCF85063: conserva l'ora UTC ----------------
static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
// giorni dal 1970-01-01 (algoritmo di H. Hinnant), evita timegm che newlib non ha
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// Ritorna true e imposta l'orologio di sistema se l'RTC ha un'ora valida
static bool rtcLoad() {
  uint8_t r[7];
  if (!i2cRead(RTC_ADDR, 0x04, r, 7)) return false;
  if (r[0] & 0x80) return false;                          // OS: oscillatore fermato, ora non affidabile
  int y = 2000 + bcd2(r[6]), mo = bcd2(r[5] & 0x1F), d = bcd2(r[3] & 0x3F);
  if (y < 2024 || mo < 1 || mo > 12 || d < 1) return false;
  time_t t = (time_t)(daysFromCivil(y, mo, d) * 86400 + bcd2(r[2] & 0x3F) * 3600 + bcd2(r[1] & 0x7F) * 60 + bcd2(r[0] & 0x7F));
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  return true;
}

static void rtcSave() {
  time_t now = time(nullptr);
  struct tm u;
  gmtime_r(&now, &u);
  uint8_t r[7] = {tobcd(u.tm_sec), tobcd(u.tm_min), tobcd(u.tm_hour), tobcd(u.tm_mday), (uint8_t)u.tm_wday,
                  tobcd(u.tm_mon + 1), tobcd(u.tm_year % 100)};
  i2cWrite(RTC_ADDR, 0x04, r, 7);                         // scrivere i secondi azzera anche il bit OS
}

// ---------------- IMU QMI8658: solo accelerometro, gesti ----------------
struct Imu {
  bool ok = false;
  uint8_t addr = 0;
  float x = 0, y = 0, z = 0;       // g
  float zSign = 1;                 // +1 se "schermo verso l'alto" = z positivo (imparato dai tocchi)
  bool pocket = false;             // a faccia in giu' o in movimento in verticale
  bool raised = false, shook = false;   // eventi: li consuma il loop
};
static Imu imu;

static bool imuBegin() {
  Wire.begin(PIN_SENS_SDA, PIN_SENS_SCL, 400000);
  const uint8_t addrs[2] = {0x6B, 0x6A};
  for (uint8_t a : addrs) {
    uint8_t who = 0;
    if (i2cRead(a, 0x00, &who, 1) && who == 0x05) { imu.addr = a; break; }
  }
  if (!imu.addr) return false;
  uint8_t v = 0xB0; i2cWrite(imu.addr, 0x60, &v, 1);     // soft reset
  delay(20);
  v = 0x40; i2cWrite(imu.addr, 0x02, &v, 1);              // CTRL1: auto-incremento indirizzi
  v = 0x16; i2cWrite(imu.addr, 0x03, &v, 1);              // CTRL2: +-4 g, ~120 Hz
  v = 0x01; i2cWrite(imu.addr, 0x08, &v, 1);              // CTRL7: abilita accelerometro
  imu.ok = true;
  return true;
}

// Da chiamare a ogni giro del loop (~50 Hz). Riconosce: alzata verso il viso, scossa, tasca.
static void imuUpdate(uint32_t now) {
  if (!imu.ok) return;
  static uint32_t last = 0;
  if (now - last < 20) return;
  last = now;
  uint8_t r[6];
  if (!i2cRead(imu.addr, 0x35, r, 6)) return;
  const float S = 1.0f / 8192;                            // +-4 g
  imu.x = (int16_t)(r[0] | r[1] << 8) * S;
  imu.y = (int16_t)(r[2] | r[3] << 8) * S;
  imu.z = (int16_t)(r[4] | r[5] << 8) * S;
  float mag = sqrtf(imu.x * imu.x + imu.y * imu.y + imu.z * imu.z);
  float dev = fabsf(mag - 1.0f), zf = imu.z * imu.zSign;
  static float motion = 0;
  motion = motion * 0.9f + dev * 0.1f;

  // scossa: 3 picchi oltre 1 g di scarto entro 1 s
  static uint32_t peaks[3] = {0}, lastPeak = 0;
  if (dev > 1.0f && now - lastPeak > 120) {
    lastPeak = now;
    peaks[0] = peaks[1]; peaks[1] = peaks[2]; peaks[2] = now;
    if (peaks[0] && now - peaks[0] < 1000) { imu.shook = true; peaks[0] = peaks[1] = peaks[2] = 0; }
  }

  // tasca: faccia in giu', oppure in verticale e in movimento (camminata); con isteresi di 600 ms
  bool pocketNow = zf < -0.6f || (fabsf(zf) < 0.35f && motion > 0.08f);
  static uint32_t pocketSince = 0;
  if (pocketNow != imu.pocket) {
    if (!pocketSince) pocketSince = now;
    else if (now - pocketSince > 600) { imu.pocket = pocketNow; pocketSince = 0; }
  } else pocketSince = 0;

  // alzata: da "non guardabile" (giu'/verticale) per >0,8 s a schermo inclinato verso il viso e fermo per 250 ms
  static uint32_t awaySince = 0, faceSince = 0;
  static bool wasFacing = true;
  bool facing = zf > 0.45f;
  if (!facing) { if (wasFacing || !awaySince) awaySince = now; faceSince = 0; }
  else {
    if (!faceSince) faceSince = now;
    if (awaySince && now - faceSince > 250 && faceSince - awaySince > 800 && dev < 0.15f) { imu.raised = true; awaySince = 0; }
    if (now - faceSince > 3000) awaySince = 0;            // fermo sul tavolo: niente risvegli a ripetizione
  }
  wasFacing = facing;
}

// Ogni tocco = l'utente sta guardando lo schermo: si impara il verso dell'asse z.
static bool imuLearnFromTouch() {
  if (!imu.ok || fabsf(imu.z) < 0.4f) return false;
  static int vote = 0;
  vote += imu.z > 0 ? 1 : -1;
  if (vote >= 5 || vote <= -5) {
    float s = vote > 0 ? 1.0f : -1.0f;
    vote = 0;
    if (s != imu.zSign) { imu.zSign = s; return true; }   // true = da salvare
  }
  return false;
}
