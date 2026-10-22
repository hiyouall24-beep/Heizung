#include "Sensors.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

static OneWire s_oneWire(PIN_ONEWIRE);
static DallasTemperature s_dallas(&s_oneWire);

void Sensors::begin() {
  s_dallas.begin();
  s_dallas.setWaitForConversion(false);
  s_dallas.setResolution(11);  // 0,125 °C, ~375 ms Messzeit

  uint8_t n = s_dallas.getDeviceCount();
  Serial.printf("[TEMP] %u Sensor(en) gefunden\n", n);
  for (uint8_t i = 0; i < n; i++) {
    DeviceAddress a;
    if (s_dallas.getAddress(a, i)) {
      Serial.printf("[TEMP]   #%u {0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X}\n",
                    i, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
  }

#if defined(SENSOR1_ADDR) && defined(SENSOR2_ADDR)
  const uint8_t fixed[COUNT][8] = {SENSOR1_ADDR, SENSOR2_ADDR};
  for (uint8_t i = 0; i < COUNT; i++) {
    memcpy(addr_[i], fixed[i], 8);
    present_[i] = true;
  }
#else
  for (uint8_t i = 0; i < COUNT; i++) present_[i] = s_dallas.getAddress(addr_[i], i);
#endif
}

void Sensors::loop() {
  uint32_t now = millis();
  if (!waiting_ && now - lastRequestMs_ >= SENSOR_READ_MS) {
    s_dallas.requestTemperatures();
    lastRequestMs_ = now;
    waiting_ = true;
    return;
  }
  if (waiting_ && now - lastRequestMs_ >= 800) {
    waiting_ = false;
    for (uint8_t i = 0; i < COUNT; i++) {
      if (!present_[i]) {
        t_[i] = NAN;
        continue;
      }
      float c = s_dallas.getTempC(addr_[i]);
      // -127 = nicht erreichbar, 85 = Einschaltwert ohne gültige Messung
      if (c == DEVICE_DISCONNECTED_C || c == 85.0f) {
        if (++failCount_[i] >= 3) t_[i] = NAN;  // erst nach 3 Fehlern als defekt melden
      } else {
        failCount_[i] = 0;
        t_[i] = c;
      }
    }
  }
}
