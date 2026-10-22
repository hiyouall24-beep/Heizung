#pragma once
#include <Arduino.h>

// Steuerung der VEVOR / "AirHeaterBLE" Diesel-Luftheizung über Bluetooth LE.
// Protokoll: Service FFE0, Characteristic FFE1 (Schreiben + Notify),
// 8-Byte-Befehle  AA 55 <pk_hi> <pk_lo> <cmd> <arg_lo> <arg_hi> <checksum>.
class HeaterBle {
public:
  enum Mode : uint8_t { MODE_LEVEL = 1, MODE_TEMP = 2 };

  struct State {
    bool connected = false;
    bool valid = false;        // mindestens ein Statusframe empfangen
    uint8_t running = 0;       // 0 = aus, 1 = an
    uint8_t error = 0;         // Fehlercode der Heizung (0 = ok)
    uint8_t step = 0;          // 0 Standby, 1 Selbsttest, 2 Zündung, 3 Läuft, 4 Abkühlen
    uint8_t mode = 0;          // 1 = Stufe, 2 = Temperatur
    uint8_t level = 0;         // 1..10
    uint8_t targetTemp = 0;    // 8..36 °C
    uint16_t altitude = 0;
    float voltage = 0;         // Versorgungsspannung
    int16_t caseTemp = 0;      // Gehäuse-/Brennkammertemperatur
    int16_t cabTemp = 0;       // interner Raumfühler der Heizung
    uint32_t lastFrameMs = 0;
  };

  void begin();
  void loop();

  bool powerOn()                 { return queue(3, 1); }
  bool powerOff()                { return queue(3, 0); }
  bool setMode(Mode m)           { return queue(2, m); }
  bool setLevel(uint8_t level)   { return queue(4, constrain(level, 1, 10)); }
  bool setTemp(uint8_t celsius)  { return queue(4, constrain(celsius, 8, 36)); }
  // sofort senden (für Not-Aus), ohne Warteschlange
  bool sendNow(uint8_t cmd, uint16_t arg);

  // false = keine Verbindungsversuche (z. B. bei Not-Aus, Heizung stromlos)
  void setEnabled(bool en) { enabled_ = en; }

  const State& state() const { return st_; }
  bool isHeating() const {
    return st_.connected && st_.valid && (st_.running || (st_.step >= 1 && st_.step <= 3));
  }
  const String& lastRaw() const { return lastRaw_; }

  // intern (Callbacks)
  void onNotify(const uint8_t* data, size_t len);
  void onDisconnected();

private:
  struct Pending { uint8_t cmd; uint16_t arg; uint32_t queuedMs; };
  static const uint8_t QUEUE_LEN = 6;
  static const uint32_t PENDING_TTL_MS = 60000;

  bool queue(uint8_t cmd, uint16_t arg);
  bool connect();
  bool findHeater();
  void parse(const uint8_t* d, size_t n);

  State st_;
  String lastRaw_;
  String address_;
  uint8_t addrType_ = 0;
  bool enabled_ = true;
  uint8_t failCount_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastSendMs_ = 0;
  Pending q_[QUEUE_LEN];
  uint8_t qCount_ = 0;

  // vom BLE-Task befüllt, in loop() ausgewertet
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t rxBuf_[64];
  size_t rxLen_ = 0;
  volatile bool rxReady_ = false;
  volatile bool disconnected_ = false;
};
