#include "Timelapse.h"
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "Camera.h"
#include "config.h"

namespace Timelapse {

static bool s_sd = false;
static uint16_t s_interval = 0;
static bool s_onlyHeating = false;
static long s_lastSlot = -1;
static uint32_t s_lastCheckMs = 0;

static File s_playDir;
static uint8_t s_playStep = 1;
static uint16_t s_playIndex = 0;

static String camDir(uint8_t cam) { return "/tl/" + String(cam); }
static String dayDir(uint8_t cam, const String& day) { return camDir(cam) + "/" + day; }

static bool allDigits(const String& s, size_t len) {
  if (s.length() != len) return false;
  for (size_t i = 0; i < len; i++)
    if (!isdigit((unsigned char)s[i])) return false;
  return true;
}
bool validDay(const String& d) { return allDigits(d, 8); }
bool validFrame(const String& f) { return allDigits(f, 6); }

// Dateiname ohne Pfad und ohne ".jpg"
static String baseName(File& f) {
  String n = f.name();
  int slash = n.lastIndexOf('/');
  if (slash >= 0) n = n.substring(slash + 1);
  if (n.endsWith(".jpg")) n.remove(n.length() - 4);
  return n;
}

void begin() {
  Preferences p;
  p.begin("timelapse", true);
  s_interval = p.getUShort("int", 0);
  s_onlyHeating = p.getBool("heat", false);
  p.end();

  s_sd = SD.begin(PIN_SD_CS);
  if (!s_sd) {
    Serial.println(F("[TL] keine microSD-Karte gefunden - Timelapse aus"));
    return;
  }
  SD.mkdir("/tl");
  for (uint8_t c = 0; c < Camera::MAX_CAMS; c++) SD.mkdir(camDir(c));
  Serial.printf("[TL] microSD ok, %u MB frei, Intervall %u min\n", freeMB(), s_interval);
}

bool sdOk() { return s_sd; }

uint32_t freeMB() {
  if (!s_sd) return 0;
  return (uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL));
}

void configure(uint16_t intervalMin, bool heating) {
  s_interval = intervalMin;
  s_onlyHeating = heating;
  s_lastSlot = -1;
  Preferences p;
  p.begin("timelapse", false);
  p.putUShort("int", s_interval);
  p.putBool("heat", s_onlyHeating);
  p.end();
}

uint16_t intervalMin() { return s_interval; }
bool onlyHeating() { return s_onlyHeating; }

static std::vector<String> sortedEntries(const String& path, bool dirs) {
  std::vector<String> v;
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return v;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory() == dirs) v.push_back(baseName(f));
    f.close();
  }
  d.close();
  std::sort(v.begin(), v.end());
  return v;
}

static void removeDay(uint8_t cam, const String& day) {
  String path = dayDir(cam, day);
  for (const String& f : sortedEntries(path, false)) SD.remove(path + "/" + f + ".jpg");
  SD.rmdir(path);
  Serial.printf("[TL] Speicher knapp - %s gelöscht\n", path.c_str());
}

// älteste Tage löschen, bis wieder genug Platz frei ist
static void cleanup() {
  for (int guard = 0; guard < 20 && freeMB() < TL_MIN_FREE_MB; guard++) {
    String oldest;
    uint8_t oldestCam = 0;
    for (uint8_t c = 0; c < Camera::MAX_CAMS; c++) {
      auto days = sortedEntries(camDir(c), true);
      if (!days.empty() && (oldest.isEmpty() || days.front() < oldest)) {
        oldest = days.front();
        oldestCam = c;
      }
    }
    if (oldest.isEmpty()) return;
    removeDay(oldestCam, oldest);
  }
}

static void capture(const struct tm& t) {
  char day[9], frame[7];
  strftime(day, sizeof(day), "%Y%m%d", &t);
  strftime(frame, sizeof(frame), "%H%M%S", &t);
  cleanup();

  for (uint8_t c = 0; c < Camera::count(); c++) {
    String dir = dayDir(c, day);
    SD.mkdir(dir);
    String path = dir + "/" + frame + ".jpg";
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
      Serial.printf("[TL] kann %s nicht anlegen\n", path.c_str());
      continue;
    }
    String err;
    bool ok = Camera::snapshot(
        c, TL_WIDTH, TL_HEIGHT, [](size_t) { return true; },
        [&](const uint8_t* d, size_t n) { return f.write(d, n) == n; }, err);
    f.close();
    if (!ok) {
      SD.remove(path);
      Serial.printf("[TL] Kamera %u: %s\n", c + 1, err.c_str());
    }
  }
}

void loop(bool heating) {
  if (!s_sd || !s_interval || !Camera::count()) return;
  uint32_t now = millis();
  if (now - s_lastCheckMs < 1000) return;
  s_lastCheckMs = now;

  time_t ts = time(nullptr);
  if (ts < 1700000000) return;  // Uhrzeit noch nicht per NTP gestellt
  // an glatten Zeiten aufnehmen (z. B. 10:00, 10:05, 10:10 ...)
  long slot = ts / (s_interval * 60L);
  if (slot == s_lastSlot) return;
  s_lastSlot = slot;
  if (s_onlyHeating && !heating) return;

  struct tm t;
  localtime_r(&ts, &t);
  capture(t);
}

void listDays(uint8_t cam, JsonArray out) {
  if (!s_sd) return;
  auto days = sortedEntries(camDir(cam), true);
  int shown = 0;
  for (auto it = days.rbegin(); it != days.rend() && shown < 31; ++it, ++shown) {
    JsonObject o = out.add<JsonObject>();
    o["d"] = *it;
    o["n"] = sortedEntries(dayDir(cam, *it), false).size();
  }
}

void listFrames(uint8_t cam, const String& day, JsonArray out) {
  if (!s_sd || !validDay(day)) return;
  for (const String& f : sortedEntries(dayDir(cam, day), false)) out.add(f);
}

File openFrame(uint8_t cam, const String& day, const String& frame) {
  if (!s_sd || !validDay(day) || !validFrame(frame)) return File();
  return SD.open(dayDir(cam, day) + "/" + frame + ".jpg");
}

bool playStart(uint8_t cam, const String& day, uint8_t step, uint16_t& total, String& err) {
  playStop();
  if (!s_sd) {
    err = "keine microSD-Karte";
    return false;
  }
  if (!validDay(day)) {
    err = "ungültiger Tag";
    return false;
  }
  size_t n = sortedEntries(dayDir(cam, day), false).size();
  s_playDir = SD.open(dayDir(cam, day));
  if (!s_playDir || !n) {
    err = "keine Bilder für diesen Tag";
    playStop();
    return false;
  }
  s_playStep = max<uint8_t>(step, 1);
  s_playIndex = 0;
  total = (n + s_playStep - 1) / s_playStep;
  return true;
}

// Bilder liegen in Aufnahme-Reihenfolge im Verzeichnis (es wird nur angehängt)
bool playNext(File& f, String& frame) {
  while (s_playDir) {
    File e = s_playDir.openNextFile();
    if (!e) {
      playStop();
      return false;
    }
    if (e.isDirectory()) continue;
    if (s_playIndex++ % s_playStep) {
      e.close();
      continue;
    }
    frame = baseName(e);
    f = e;
    return true;
  }
  return false;
}

void playStop() {
  if (s_playDir) s_playDir.close();
}

bool playing() { return (bool)s_playDir; }

}  // namespace Timelapse
