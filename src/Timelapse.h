#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

// Zeitraffer: speichert in festen Abständen Kamerabilder auf einer microSD-Karte.
// Ablage: /tl/<kamera>/<JJJJMMTT>/<HHMMSS>.jpg
namespace Timelapse {

void begin();
bool sdOk();
uint32_t freeMB();

// Aufnahme-Einstellungen (werden im Flash gespeichert)
void configure(uint16_t intervalMin, bool onlyHeating);
uint16_t intervalMin();   // 0 = aus
bool onlyHeating();

// regelmäßig aufrufen; nimmt Bilder auf, wenn es Zeit ist
void loop(bool heating);

// Listen für die App (neueste zuerst / chronologisch)
void listDays(uint8_t cam, JsonArray out);                 // [{"d":"20261022","n":96}, ...]
void listFrames(uint8_t cam, const String& day, JsonArray out);  // ["063000", ...]
File openFrame(uint8_t cam, const String& day, const String& frame);

// Abspielen Bild für Bild (für die Übertragung per MQTT)
bool playStart(uint8_t cam, const String& day, uint8_t step, uint16_t& total, String& err);
bool playNext(File& f, String& frame);   // false = fertig
void playStop();
bool playing();

bool validDay(const String& d);
bool validFrame(const String& f);

}  // namespace Timelapse
