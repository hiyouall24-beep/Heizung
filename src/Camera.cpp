#include "Camera.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef CAM_HOST
#define CAM_HOST ""
#define CAM_USER ""
#define CAM_PASS ""
#endif

namespace Camera {

static String urlEncode(const char* s) {
  String out;
  for (; *s; s++) {
    char c = *s;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (uint8_t)c);
      out += b;
    }
  }
  return out;
}

bool enabled() { return strlen(CAM_HOST) > 0; }

bool snapshot(const std::function<bool(size_t)>& onStart,
              const std::function<bool(const uint8_t*, size_t)>& onChunk, String& err) {
  if (!enabled()) {
    err = "keine Kamera eingerichtet";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    err = "kein WLAN";
    return false;
  }

  String url = String("http://") + CAM_HOST + "/cgi-bin/api.cgi?cmd=Snap&channel=" +
               CAM_CHANNEL + "&rs=" + String(millis()) + "&user=" + urlEncode(CAM_USER) +
               "&password=" + urlEncode(CAM_PASS);
  if (CAM_WIDTH > 0) url += "&width=" + String(CAM_WIDTH) + "&height=" + String(CAM_HEIGHT);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(4000);
  if (!http.begin(client, url)) {
    err = "Kamera-URL ungültig";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = code < 0 ? "Kamera nicht erreichbar" : "Kamera antwortet mit HTTP " + String(code);
    http.end();
    return false;
  }
  String type = http.header("Content-Type");
  int total = http.getSize();
  if (total <= 0) {
    err = "Kamera liefert keine Bildgröße";
    http.end();
    return false;
  }
  if (total > CAM_MAX_BYTES) {
    err = "Bild zu groß (" + String(total / 1024) + " KB) - CAM_WIDTH/CAM_HEIGHT verkleinern";
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t done = 0;
  bool started = false;
  uint32_t lastData = millis();
  while (done < (size_t)total && millis() - lastData < 5000) {
    size_t avail = stream->available();
    if (!avail) {
      delay(2);
      continue;
    }
    int n = stream->readBytes(buf, min(avail, sizeof(buf)));
    if (n <= 0) continue;
    lastData = millis();
    if (!started) {
      // Reolink schickt bei falschem Passwort JSON statt JPEG
      if (buf[0] != 0xFF || buf[1] != 0xD8) {
        err = "kein JPEG - Benutzer/Passwort der Kamera prüfen";
        http.end();
        return false;
      }
      if (!onStart(total)) {
        err = "Weiterleiten nicht möglich";
        http.end();
        return false;
      }
      started = true;
    }
    if (!onChunk(buf, n)) {
      err = "Übertragung abgebrochen";
      http.end();
      return false;
    }
    done += n;
  }
  http.end();
  if (done < (size_t)total) {
    err = "Bild unvollständig";
    return false;
  }
  return true;
}

}  // namespace Camera
