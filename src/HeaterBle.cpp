#include "HeaterBle.h"
#include <NimBLEDevice.h>
#include "config.h"

static HeaterBle* s_self = nullptr;
static NimBLEClient* s_client = nullptr;
static NimBLERemoteCharacteristic* s_chr = nullptr;

class HeaterClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*) override {
    if (s_self) s_self->onDisconnected();
  }
};
static HeaterClientCallbacks s_clientCb;

static void notifyCallback(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (s_self) s_self->onNotify(data, len);
}

static String toHex(const uint8_t* d, size_t n) {
  String s;
  s.reserve(n * 3);
  for (size_t i = 0; i < n; i++) {
    char b[4];
    snprintf(b, sizeof(b), i ? " %02X" : "%02X", d[i]);
    s += b;
  }
  return s;
}

void HeaterBle::begin() {
  s_self = this;
  address_ = HEATER_BLE_MAC;
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  s_client = NimBLEDevice::createClient();
  s_client->setClientCallbacks(&s_clientCb, false);
  s_client->setConnectTimeout(8);
  lastAttemptMs_ = millis() - HEATER_RETRY_MS + 3000;  // erster Versuch nach 3 s
}

void HeaterBle::onNotify(const uint8_t* data, size_t len) {
  portENTER_CRITICAL(&mux_);
  rxLen_ = min(len, sizeof(rxBuf_));
  memcpy(rxBuf_, data, rxLen_);
  rxReady_ = true;
  portEXIT_CRITICAL(&mux_);
}

void HeaterBle::onDisconnected() { disconnected_ = true; }

bool HeaterBle::findHeater() {
  Serial.println(F("[BLE] Suche Heizung (8 s) ..."));
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  NimBLEScanResults res = scan->start(8, false);
  NimBLEUUID svc(HEATER_SERVICE_UUID);
  String hint = String(HEATER_NAME_HINT);
  hint.toLowerCase();

  bool found = false;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice dev = res.getDevice(i);
    String name = dev.getName().c_str();
    Serial.printf("[BLE]   %s  rssi=%d  name='%s'\n",
                  dev.getAddress().toString().c_str(), dev.getRSSI(), name.c_str());
    String lname = name;
    lname.toLowerCase();
    bool match = dev.isAdvertisingService(svc) ||
                 (hint.length() && lname.indexOf(hint) >= 0);
    if (match && !found) {
      address_ = dev.getAddress().toString().c_str();
      addrType_ = dev.getAddress().getType();
      found = true;
    }
  }
  scan->clearResults();
  if (found) Serial.printf("[BLE] Heizung gefunden: %s\n", address_.c_str());
  else Serial.println(F("[BLE] keine Heizung gefunden"));
  return found;
}

bool HeaterBle::connect() {
  if (address_.isEmpty() && !findHeater()) return false;

  Serial.printf("[BLE] Verbinde mit %s ...\n", address_.c_str());
  NimBLEAddress addr(std::string(address_.c_str()), addrType_);
  if (!s_client->connect(addr)) {
    // Bei automatischer Suche nächstes Mal neu suchen (Adresse könnte falsch sein)
    if (strlen(HEATER_BLE_MAC) == 0) address_ = "";
    Serial.println(F("[BLE] Verbindung fehlgeschlagen"));
    return false;
  }
  NimBLERemoteService* svc = s_client->getService(HEATER_SERVICE_UUID);
  s_chr = svc ? svc->getCharacteristic(HEATER_CHAR_UUID) : nullptr;
  if (!s_chr) {
    Serial.println(F("[BLE] Characteristic FFE1 nicht gefunden"));
    s_client->disconnect();
    return false;
  }
  if (s_chr->canNotify() || s_chr->canIndicate()) {
    s_chr->subscribe(s_chr->canNotify(), notifyCallback);
  }
  disconnected_ = false;
  st_.connected = true;
  Serial.println(F("[BLE] verbunden"));
  sendNow(1, 0);  // Status anfordern
  lastPollMs_ = millis();
  return true;
}

bool HeaterBle::sendNow(uint8_t cmd, uint16_t arg) {
  if (!st_.connected || !s_chr || !s_client->isConnected()) return false;
  uint8_t b[8] = {0xAA, 0x55,
                  (uint8_t)(HEATER_PASSKEY / 100), (uint8_t)(HEATER_PASSKEY % 100),
                  cmd, (uint8_t)(arg & 0xFF), (uint8_t)(arg >> 8), 0};
  uint16_t sum = 0;
  for (int i = 2; i < 7; i++) sum += b[i];
  b[7] = sum & 0xFF;
  bool withResponse = !s_chr->canWriteNoResponse();
  bool ok = s_chr->writeValue(b, sizeof(b), withResponse);
  lastSendMs_ = millis();
  if (cmd != 1) Serial.printf("[BLE] -> %s %s\n", toHex(b, 8).c_str(), ok ? "" : "(FEHLER)");
  return ok;
}

bool HeaterBle::queue(uint8_t cmd, uint16_t arg) {
  // gleicher Befehlstyp ersetzt älteren Eintrag (z. B. mehrfach Stufe ändern)
  for (uint8_t i = 0; i < qCount_; i++) {
    if (q_[i].cmd == cmd) {
      q_[i].arg = arg;
      q_[i].queuedMs = millis();
      return true;
    }
  }
  if (qCount_ >= QUEUE_LEN) return false;
  q_[qCount_++] = {cmd, arg, millis()};
  return true;
}

void HeaterBle::parse(const uint8_t* d, size_t n) {
  lastRaw_ = toHex(d, n);

  // Klassisches, unverschlüsseltes Format (AA 55, 20 Byte)
  if (n >= 17 && d[0] == 0xAA && d[1] == 0x55) {
    st_.running = d[3];
    st_.error = d[4];
    st_.step = d[5];
    st_.altitude = d[6] | (d[7] << 8);
    st_.mode = d[8];
    if (st_.mode == MODE_TEMP) {
      st_.targetTemp = d[9];
      st_.level = d[10] + 1;
    } else {
      st_.level = d[9];
    }
    st_.voltage = (d[11] | (d[12] << 8)) / 10.0f;
    st_.caseTemp = (int16_t)(d[13] | (d[14] << 8));
    st_.cabTemp = (int16_t)(d[15] | (d[16] << 8));
    st_.valid = true;
    st_.lastFrameMs = millis();
    return;
  }

  // Neueres Format (48 Byte, mit "password" XOR verschleiert, Kopf AA 66)
  if (n == 48) {
    uint8_t x[48];
    static const char key[] = "password";
    for (size_t i = 0; i < 48; i++) x[i] = d[i] ^ (uint8_t)key[i % 8];
    if (x[0] == 0xAA && x[1] == 0x66) {
      float v = ((x[11] << 8) | x[12]) / 10.0f;
      if (v > 6 && v < 36) {  // Plausibilitätsprüfung
        st_.running = x[3];
        st_.step = x[5];
        st_.altitude = (x[6] << 8) | x[7];
        st_.mode = x[8];
        st_.targetTemp = x[9];
        st_.level = x[10];
        st_.voltage = v;
        st_.caseTemp = (int16_t)((x[13] << 8) | x[14]);
        st_.cabTemp = (int16_t)((x[32] << 8) | x[33]) / 10;
        st_.error = x[35];
        st_.valid = true;
        st_.lastFrameMs = millis();
      }
      lastRaw_ = "dec: " + toHex(x, 48);
      return;
    }
  }
  Serial.printf("[BLE] unbekannter Frame: %s\n", lastRaw_.c_str());
}

void HeaterBle::loop() {
  uint32_t now = millis();

  if (disconnected_) {
    disconnected_ = false;
    if (st_.connected) Serial.println(F("[BLE] Verbindung getrennt"));
    st_.connected = false;
    st_.valid = false;
    s_chr = nullptr;
    lastAttemptMs_ = now;
  }

  if (rxReady_) {
    uint8_t buf[64];
    size_t len;
    portENTER_CRITICAL(&mux_);
    len = rxLen_;
    memcpy(buf, rxBuf_, len);
    rxReady_ = false;
    portEXIT_CRITICAL(&mux_);
    parse(buf, len);
  }

  // abgelaufene Befehle verwerfen
  for (uint8_t i = 0; i < qCount_;) {
    if (now - q_[i].queuedMs > PENDING_TTL_MS) {
      Serial.printf("[BLE] Befehl %u verworfen (keine Verbindung)\n", q_[i].cmd);
      memmove(&q_[i], &q_[i + 1], (qCount_ - i - 1) * sizeof(Pending));
      qCount_--;
    } else {
      i++;
    }
  }

  if (!st_.connected) {
    if (!enabled_) return;
    // Verbindungsaufbau blockiert einige Sekunden -> bei Misserfolg seltener
    // versuchen (max. 1x pro Minute), wenn Befehle warten aber schneller.
    uint32_t retry = min<uint32_t>(HEATER_RETRY_MS * (1 + failCount_), 60000);
    if (qCount_) retry = 4000;
    if (now - lastAttemptMs_ >= retry) {
      if (connect()) failCount_ = 0;
      else if (failCount_ < 10) failCount_++;
      lastAttemptMs_ = millis();
    }
    return;
  }

  // Warteschlange abarbeiten: ein Befehl pro 400 ms
  if (qCount_ && now - lastSendMs_ >= 400) {
    if (sendNow(q_[0].cmd, q_[0].arg)) {
      memmove(&q_[0], &q_[1], (qCount_ - 1) * sizeof(Pending));
      qCount_--;
      lastPollMs_ = now - HEATER_POLL_MS + 1000;  // kurz danach Status holen
    }
    return;
  }

  if (now - lastPollMs_ >= HEATER_POLL_MS) {
    lastPollMs_ = now;
    sendNow(1, 0);
  }

  // Keine Antworten mehr -> Verbindung neu aufbauen
  if (st_.lastFrameMs && now - st_.lastFrameMs > 60000) {
    Serial.println(F("[BLE] keine Antwort seit 60 s, trenne"));
    st_.lastFrameMs = 0;
    s_client->disconnect();
  }
}
