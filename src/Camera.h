#pragma once
#include <Arduino.h>
#include <functional>

// Holt Standbilder (JPEG) von bis zu zwei Reolink-Kameras im lokalen Netz
// über deren HTTP-API (cmd=Snap) und gibt sie stückweise weiter.
namespace Camera {

static const uint8_t MAX_CAMS = 2;

// Anzahl eingerichteter Kameras (Kamera 2 zählt nur, wenn Kamera 1 eingerichtet ist)
uint8_t count();
inline bool enabled(uint8_t cam = 0) { return cam < count(); }
const char* name(uint8_t cam);

// onStart(gesamtlänge) wird einmal aufgerufen, danach onChunk() für jedes Datenstück.
// width/height = gewünschte Bildgröße (0 = volle Auflösung der Kamera).
// Liefert false und eine Fehlermeldung in err, wenn etwas schiefgeht.
bool snapshot(uint8_t cam, uint16_t width, uint16_t height,
              const std::function<bool(size_t total)>& onStart,
              const std::function<bool(const uint8_t* data, size_t len)>& onChunk,
              String& err);

}  // namespace Camera
