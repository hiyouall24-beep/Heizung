#pragma once
#include <Arduino.h>
#include <functional>

// Holt ein Standbild (JPEG) von einer Reolink-Kamera im lokalen Netz
// über deren HTTP-API (cmd=Snap) und gibt es stückweise weiter.
namespace Camera {

// true, wenn eine Kamera in secrets.h eingetragen ist
bool enabled();

// onStart(gesamtlänge) wird einmal aufgerufen, danach onChunk() für jedes Datenstück.
// Liefert false und eine Fehlermeldung in err, wenn etwas schiefgeht.
bool snapshot(const std::function<bool(size_t total)>& onStart,
              const std::function<bool(const uint8_t* data, size_t len)>& onChunk,
              String& err);

}  // namespace Camera
