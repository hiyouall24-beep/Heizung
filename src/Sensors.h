#pragma once
#include <Arduino.h>

// Zwei DS18B20-Temperatursensoren an einem OneWire-Bus, nicht blockierend gelesen.
class Sensors {
public:
  static const uint8_t COUNT = 2;
  void begin();
  void loop();
  // NAN, wenn Sensor fehlt oder defekt
  float temp(uint8_t i) const { return i < COUNT ? t_[i] : NAN; }

private:
  float t_[COUNT] = {NAN, NAN};
  uint8_t addr_[COUNT][8] = {};
  bool present_[COUNT] = {};
  uint32_t lastRequestMs_ = 0;
  bool waiting_ = false;
  uint8_t failCount_[COUNT] = {};
};
