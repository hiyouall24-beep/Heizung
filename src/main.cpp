// ============================================================================
//  ESP32 Standheizungs-Steuerung
//  - VEVOR Diesel-Luftheizung über Bluetooth LE
//  - 2x DS18B20 Temperatursensoren
//  - 12-V-Licht über MOSFET
//  - Foto von einer Reolink-Kamera auf Knopfdruck
//  - Not-Aus (Relais trennt die Heizung vom Strom)
//  - Fernsteuerung + Anzeige über MQTT (sehr wenig Datenverbrauch)
//  - lokale Weboberfläche http://heizung.local
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h fehlt - es werden die Beispielwerte aus secrets.example.h benutzt"
#include "secrets.example.h"
#endif
#include "ca_cert.h"
#include "dashboard_html.h"
#include "HeaterBle.h"
#include "Sensors.h"
#include "Camera.h"

static HeaterBle heater;
static Sensors sensors;
static Preferences prefs;
static WiFiClientSecure net;
static PubSubClient mqtt(net);
static WebServer web(80);

static const String kBase = MQTT_BASE_TOPIC;
static const char* kSensorNames[Sensors::COUNT] = {SENSOR1_NAME, SENSOR2_NAME};

// ------------------------------------------------------------- Zustand ------
enum class HeatWish : uint8_t { None, Off };

static bool lightOn = false;
static bool estop = false;
static String estopReason;
static String alarmMsg;                 // letzte Sicherheitsabschaltung / Fehler
static uint32_t runStartMs = 0;      // 0 = Heizung läuft nicht (laut uns)
static uint16_t runLimitMin = MAX_RUNTIME_MIN_DEFAULT;
static float thermoTarget = NAN;     // ESP-Thermostat Sollwert, NAN = aus
static HeatWish wish = HeatWish::None;
static uint32_t wishSentMs = 0;
static uint32_t lowVoltSinceMs = 0;
static uint32_t lastThermoMs = 0;

static bool publishNow = true;       // nächsten Status sofort senden
static uint32_t lastCommandMs = 0;
static bool snapRequested = false;    // Foto per MQTT senden
static uint32_t snapTs = 0;           // Zeitpunkt des letzten Fotos
static uint32_t lastPublishMs = 0;
static uint32_t mqttRetryMs = 0;
static uint32_t mqttRetryDelay = 5000;

// ------------------------------------------------------------ Ausgänge ------
static void writeLight() {
  digitalWrite(PIN_LIGHT, lightOn == LIGHT_ACTIVE_HIGH ? HIGH : LOW);
}

static void writeEstopRelay() {
  bool energize = ESTOP_RELAY_ENERGIZE_TO_CUT ? estop : !estop;
  digitalWrite(PIN_ESTOP_RELAY, energize == ESTOP_RELAY_ACTIVE_HIGH ? HIGH : LOW);
}

static void setAlarm(const String& msg) {
  if (alarmMsg != msg) {
    alarmMsg = msg;
    Serial.printf("[ALARM] %s\n", msg.c_str());
    publishNow = true;
  }
}

// ----------------------------------------------------------- Aktionen -------
static void heaterOff(const char* why) {
  heater.powerOff();
  wish = HeatWish::Off;
  wishSentMs = millis();
  runStartMs = 0;
  thermoTarget = NAN;
  if (why) setAlarm(why);
  publishNow = true;
}

// automatische Abschaltung (nur einmal auslösen, nicht in jeder Schleife)
static void autoOff(const char* why) {
  if (wish == HeatWish::Off) return;
  heaterOff(why);
}

static void triggerEstop(const String& reason) {
  if (estop) return;
  estop = true;
  estopReason = reason;
  prefs.putBool("estop", true);
  // Heizung noch schnell per Bluetooth ausschalten, dann sofort Strom trennen
  heater.sendNow(3, 0);
  delay(150);
  writeEstopRelay();
  heater.setEnabled(false);
  runStartMs = 0;
  thermoTarget = NAN;
  wish = HeatWish::None;
  setAlarm("NOT-AUS: " + reason);
  publishNow = true;
}

static bool estopButtonPressed() {
#if ESTOP_BUTTON_ENABLED
  return digitalRead(PIN_ESTOP_BUTTON) == HIGH;  // Öffner offen oder Kabelbruch
#else
  return false;
#endif
}

static String resetEstop() {
  if (!estop) return "ok: Not-Aus war nicht aktiv";
  if (estopButtonPressed()) return "err: Not-Aus-Taster ist noch gedrückt";
  estop = false;
  estopReason = "";
  prefs.putBool("estop", false);
  writeEstopRelay();
  heater.setEnabled(true);
  alarmMsg = "";
  publishNow = true;
  return "ok: Not-Aus zurückgesetzt, Heizung hat wieder Strom (bleibt aus)";
}

// -------------------------------------------------------------- Befehle -----
// Gemeinsame Befehle für MQTT, Weboberfläche und seriellen Monitor:
//   on [minuten] | off | level 1-10 | temp 8-36 | thermo <°C>|off
//   runtime <minuten> | light on|off|toggle | snap | estop | reset | status
static String handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  int sp = line.indexOf(' ');
  String c = sp < 0 ? line : line.substring(0, sp);
  String a = sp < 0 ? "" : line.substring(sp + 1);
  a.trim();
  publishNow = true;
  lastCommandMs = millis();

  if (c == "estop" || c == "notaus") {
    triggerEstop("Fernbefehl");
    return "ok: NOT-AUS ausgelöst, Heizung stromlos";
  }
  if (c == "reset") return resetEstop();
  if (c == "status") return "ok";
  if (c == "snap" || c == "foto") {
    if (!Camera::enabled()) return "err: keine Kamera eingerichtet (CAM_HOST in secrets.h)";
    snapRequested = true;
    return "ok: Foto wird aufgenommen ...";
  }

  if (c == "light" || c == "licht") {
    if (a == "on" || a == "an" || a == "1") lightOn = true;
    else if (a == "off" || a == "aus" || a == "0") lightOn = false;
    else if (a == "toggle" || a.isEmpty()) lightOn = !lightOn;
    else return "err: light on|off|toggle";
    writeLight();
    return lightOn ? "ok: Licht an" : "ok: Licht aus";
  }

  if (c == "off" || c == "aus") {
    heaterOff(nullptr);
    return "ok: Heizung wird ausgeschaltet (Nachlauf)";
  }

  if (c == "runtime") {
    int m = a.toInt();
    if (m < 1 || m > MAX_RUNTIME_MIN_LIMIT) return "err: runtime 1-" + String(MAX_RUNTIME_MIN_LIMIT);
    runLimitMin = m;
    return "ok: Laufzeit " + String(m) + " min";
  }

  // ab hier: Befehle, die die Heizung starten/ändern -> nicht bei Not-Aus
  if (estop) return "err: Not-Aus aktiv - erst 'reset'";

  if (c == "on" || c == "an") {
    int m = a.length() ? a.toInt() : MAX_RUNTIME_MIN_DEFAULT;
    if (m < 1 || m > MAX_RUNTIME_MIN_LIMIT) return "err: on 1-" + String(MAX_RUNTIME_MIN_LIMIT);
    if (!isnan(sensors.temp(0)) && sensors.temp(0) >= OVERTEMP_CUTOFF_C)
      return "err: zu warm (" + String(sensors.temp(0), 1) + " °C)";
    if (!heater.powerOn()) return "err: Bluetooth-Warteschlange voll";
    runLimitMin = m;
    runStartMs = millis();
    wish = HeatWish::None;
    alarmMsg = "";
    return "ok: Heizung startet für " + String(m) + " min" +
           (heater.state().connected ? "" : " (Bluetooth verbindet ...)");
  }
  if (c == "level" || c == "stufe") {
    int l = a.toInt();
    if (l < 1 || l > 10) return "err: level 1-10";
    thermoTarget = NAN;
    heater.setMode(HeaterBle::MODE_LEVEL);
    heater.setLevel(l);
    return "ok: Stufe " + String(l);
  }
  if (c == "temp") {
    int t = a.toInt();
    if (t < 8 || t > 36) return "err: temp 8-36";
    thermoTarget = NAN;
    heater.setMode(HeaterBle::MODE_TEMP);
    heater.setTemp(t);
    return "ok: Heizungs-Thermostat " + String(t) + " °C";
  }
  if (c == "thermo") {
    if (a == "off" || a == "aus") {
      thermoTarget = NAN;
      return "ok: ESP-Thermostat aus";
    }
    float t = a.toFloat();
    if (t < 5 || t > 30) return "err: thermo 5-30 oder off";
    if (isnan(sensors.temp(0))) return "err: Sensor 1 liefert keinen Wert";
    thermoTarget = t;
    lastThermoMs = 0;
    heater.setMode(HeaterBle::MODE_LEVEL);
    return "ok: ESP-Thermostat " + String(t, 1) + " °C (" SENSOR1_NAME ")";
  }
  return "err: unbekannter Befehl";
}

// --------------------------------------------------------- Status-JSON ------
static float round1(float v) { return roundf(v * 10) / 10; }

static void buildState(JsonDocument& d) {
  const auto& s = heater.state();
  d["ble"] = s.connected ? 1 : 0;
  if (s.connected && s.valid) {
    d["on"] = s.running;
    d["st"] = s.step;
    d["md"] = s.mode;
    d["lv"] = s.level;
    d["tt"] = s.targetTemp;
    d["v"] = round1(s.voltage);
    d["ct"] = s.caseTemp;
    d["it"] = s.cabTemp;
    d["er"] = s.error;
  }
  JsonArray t = d["t"].to<JsonArray>();
  JsonArray n = d["n"].to<JsonArray>();
  for (uint8_t i = 0; i < Sensors::COUNT; i++) {
    float v = sensors.temp(i);
    if (isnan(v)) t.add(nullptr);
    else t.add(round1(v));
    n.add(kSensorNames[i]);
  }
  if (Camera::enabled()) d["cam"] = 1;
  if (snapTs) d["sn"] = snapTs;
  d["li"] = lightOn ? 1 : 0;
  d["es"] = estop ? 1 : 0;
  if (alarmMsg.length()) d["al"] = alarmMsg;
  d["mx"] = runLimitMin;
  if (runStartMs) {
    int32_t left = (int32_t)runLimitMin - (int32_t)((millis() - runStartMs) / 60000);
    d["rl"] = left < 0 ? 0 : left;
  }
  if (!isnan(thermoTarget)) d["th"] = round1(thermoTarget);
  time_t ts = time(nullptr);
  if (ts > 1700000000) d["ts"] = (uint32_t)ts;  // Uhrzeit (per NTP), damit man das Alter sieht
  d["up"] = millis() / 1000;
  d["rs"] = WiFi.RSSI();
}

// Änderungen, die sofort gemeldet werden sollen
static bool significantChange() {
  static String lastKey;
  static float lastT[Sensors::COUNT] = {NAN, NAN};
  static float lastV = 0;
  const auto& s = heater.state();
  String key;
  key.reserve(48);
  key += s.connected; key += s.running; key += s.step; key += s.mode;
  key += s.level; key += s.targetTemp; key += s.error; key += lightOn;
  key += estop; key += alarmMsg; key += (int)(isnan(thermoTarget) ? -1 : thermoTarget * 10);

  bool changed = key != lastKey;
  for (uint8_t i = 0; i < Sensors::COUNT; i++) {
    float v = sensors.temp(i);
    if (isnan(v) != isnan(lastT[i]) || (!isnan(v) && fabsf(v - lastT[i]) >= PUBLISH_DELTA_TEMP)) {
      changed = true;
    }
  }
  if (s.valid && fabsf(s.voltage - lastV) >= PUBLISH_DELTA_VOLT) changed = true;

  if (changed) {
    lastKey = key;
    for (uint8_t i = 0; i < Sensors::COUNT; i++) lastT[i] = sensors.temp(i);
    lastV = s.voltage;
  }
  return changed;
}

// ---------------------------------------------------------------- MQTT ------
static void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String cmd;
  cmd.reserve(len);
  for (unsigned int i = 0; i < len; i++) cmd += (char)payload[i];
  Serial.printf("[MQTT] Befehl: %s\n", cmd.c_str());
  String res = handleCommand(cmd);
  Serial.printf("[MQTT] %s\n", res.c_str());
  mqtt.publish((kBase + "/ack").c_str(), res.c_str(), false);
}

static void publishState() {
  JsonDocument d;
  buildState(d);
  char buf[512];
  size_t n = serializeJson(d, buf, sizeof(buf));
  if (mqtt.publish((kBase + "/state").c_str(), (const uint8_t*)buf, n, true)) {
    lastPublishMs = millis();
    publishNow = false;
  }
}

// Foto von der Kamera holen und direkt (stückweise) per MQTT weiterreichen.
// Retained, damit die App beim Öffnen das letzte Foto sofort anzeigt.
static void sendSnapshot() {
  snapRequested = false;
  String topic = kBase + "/snap";
  String err;
  bool started = false;
  bool ok = Camera::snapshot(
      [&](size_t total) { return started = mqtt.beginPublish(topic.c_str(), total, true); },
      [&](const uint8_t* d, size_t n) { return mqtt.write(d, n) == n; }, err);
  if (started) {
    if (ok) {
      mqtt.endPublish();
    } else {
      // halb gesendetes Paket -> Verbindung neu aufbauen
      net.stop();
    }
  }
  if (ok) {
    time_t ts = time(nullptr);
    snapTs = ts > 1700000000 ? (uint32_t)ts : 1;
    publishNow = true;
    Serial.println(F("[CAM] Foto gesendet"));
  } else {
    Serial.printf("[CAM] Fehler: %s\n", err.c_str());
    if (mqtt.connected()) mqtt.publish((kBase + "/ack").c_str(), ("err: Foto - " + err).c_str());
  }
}

static void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    uint32_t now = millis();
    if (now - mqttRetryMs < mqttRetryDelay) return;
    mqttRetryMs = now;
    String id = "heizung-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String will = kBase + "/online";
    Serial.printf("[MQTT] verbinde mit %s ...\n", MQTT_HOST);
    if (mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS, will.c_str(), 1, true, "0")) {
      Serial.println(F("[MQTT] verbunden"));
      mqtt.publish(will.c_str(), "1", true);
      mqtt.subscribe((kBase + "/cmd").c_str(), 1);
      mqttRetryDelay = 5000;
      publishNow = true;
    } else {
      Serial.printf("[MQTT] Fehler %d\n", mqtt.state());
      mqttRetryDelay = min<uint32_t>(mqttRetryDelay * 2, 300000);  // max. 5 min
      return;
    }
  }
  mqtt.loop();
  if (snapRequested) sendSnapshot();

  static bool pendingChange = false;
  uint32_t now = millis();
  uint32_t sinceLast = now - lastPublishMs;
  uint32_t heartbeat =
      (heater.isHeating() ? PUBLISH_HEARTBEAT_RUN_S : PUBLISH_HEARTBEAT_IDLE_S) * 1000UL;
  // nach einem Befehl 90 s lang schneller melden, damit man die Reaktion sieht
  uint32_t minGap = (now - lastCommandMs < 90000) ? 4000 : PUBLISH_MIN_INTERVAL_S * 1000UL;
  static uint32_t lastCheckMs = 0;
  if (now - lastCheckMs >= 500) {
    lastCheckMs = now;
    if (significantChange()) pendingChange = true;
  }
  if (publishNow || sinceLast >= heartbeat || (pendingChange && sinceLast >= minGap)) {
    publishState();
    pendingChange = false;
  }
}

// ----------------------------------------------------------------- Web ------
static bool webAuth() {
  if (web.authenticate(WEB_USER, WEB_PASS)) return true;
  web.requestAuthentication(BASIC_AUTH, "Heizung");
  return false;
}

static void setupWeb() {
  web.on("/", HTTP_GET, [] {
    if (!webAuth()) return;
    web.sendHeader("Content-Encoding", "gzip");
    web.send_P(200, "text/html; charset=utf-8", (const char*)DASHBOARD_HTML_GZ,
               sizeof(DASHBOARD_HTML_GZ));
  });
  web.on("/api/state", HTTP_GET, [] {
    if (!webAuth()) return;
    JsonDocument d;
    buildState(d);
    d["mq"] = mqtt.connected() ? 1 : 0;
    d["raw"] = heater.lastRaw();
    String out;
    serializeJson(d, out);
    web.send(200, "application/json", out);
  });
  web.on("/api/cmd", HTTP_POST, [] {
    if (!webAuth()) return;
    String res = handleCommand(web.arg("c"));
    web.send(res.startsWith("ok") ? 200 : 400, "text/plain; charset=utf-8", res);
  });
  web.on("/api/snap", HTTP_GET, [] {
    if (!webAuth()) return;
    String err;
    bool started = false;
    WiFiClient client = web.client();
    bool ok = Camera::snapshot(
        [&](size_t total) {
          web.setContentLength(total);
          web.sendHeader("Cache-Control", "no-store");
          web.send(200, "image/jpeg", "");
          return started = true;
        },
        [&](const uint8_t* d, size_t n) { return client.write(d, n) == n; }, err);
    if (!ok && !started) web.send(502, "text/plain; charset=utf-8", err);
  });
  web.onNotFound([] { web.send(404, "text/plain", "404"); });
  web.begin();
}

// ------------------------------------------------------ Sicherheit etc. -----
static void safetyLoop() {
  uint32_t now = millis();
  const auto& s = heater.state();
  bool heating = heater.isHeating();

  // Not-Aus-Taster (entprellt)
#if ESTOP_BUTTON_ENABLED
  static uint32_t pressedSince = 0;
  if (estopButtonPressed()) {
    if (!pressedSince) pressedSince = now ? now : 1;
    if (now - pressedSince > 50) triggerEstop("Taster");
  } else {
    pressedSince = 0;
  }
#endif

  // Heizung wurde z. B. mit der Original-Fernbedienung gestartet -> Laufzeit überwachen
  if (heating && !runStartMs && wish != HeatWish::Off) {
    runStartMs = now;
    runLimitMin = MAX_RUNTIME_MIN_DEFAULT;
    publishNow = true;
  }
  // Heizung ist wirklich aus -> Laufzeit zurücksetzen (Startphase abwarten)
  if (!heating && runStartMs && s.connected && s.valid && s.step == 0 &&
      now - runStartMs > 90000) {
    runStartMs = 0;
    thermoTarget = NAN;
  }

  if (runStartMs && now - runStartMs >= runLimitMin * 60000UL) {
    autoOff("Maximale Laufzeit erreicht");
  }

  float inside = sensors.temp(0);
  if (heating && !isnan(inside) && inside >= OVERTEMP_CUTOFF_C) {
    autoOff("Übertemperatur " SENSOR1_NAME);
  }

  if (LOW_VOLTAGE_CUTOFF > 0 && heating && s.valid && s.voltage > 1 &&
      s.voltage < LOW_VOLTAGE_CUTOFF) {
    if (!lowVoltSinceMs) lowVoltSinceMs = now;
    if (now - lowVoltSinceMs >= LOW_VOLTAGE_DELAY_S * 1000UL) {
      autoOff("Batterie-Unterspannung");
      lowVoltSinceMs = 0;
    }
  } else {
    lowVoltSinceMs = 0;
  }

  if (s.valid && s.error) {
    static const char* const kErr[] = {
        "", "Startfehler", "Kein Brennstoff", "Überspannung", "Ausgangsfühler defekt",
        "Eingangsfühler defekt", "Dosierpumpe defekt", "Lüfter defekt", "Glühkerze defekt",
        "Überhitzung", "Überhitzungsfühler defekt"};
    String msg = "Heizung Fehler E" + String(s.error);
    if (s.error < sizeof(kErr) / sizeof(kErr[0])) msg += String(": ") + kErr[s.error];
    setAlarm(msg);
  }

  // "Aus" so lange wiederholen, bis die Heizung es bestätigt
  if (wish == HeatWish::Off) {
    if (s.connected && s.valid && !s.running) {
      wish = HeatWish::None;
    } else if (now - wishSentMs > 30000) {
      heater.powerOff();
      wishSentMs = now;
    }
  }
}

// ESP-Thermostat: Heizstufe anhand von Sensor 1 regeln (ohne ständiges An/Aus)
static void thermoLoop() {
  if (isnan(thermoTarget) || !heater.isHeating()) return;
  uint32_t now = millis();
  if (lastThermoMs && now - lastThermoMs < THERMO_INTERVAL_S * 1000UL) return;
  lastThermoMs = now;
  float t = sensors.temp(0);
  if (isnan(t)) return;
  int level = constrain((int)lroundf(1 + (thermoTarget - t) * THERMO_GAIN), 1, 10);
  const auto& s = heater.state();
  if (s.mode != HeaterBle::MODE_LEVEL) heater.setMode(HeaterBle::MODE_LEVEL);
  if (s.level != level) {
    heater.setLevel(level);
    Serial.printf("[THERMO] %.1f °C -> Ziel %.1f °C -> Stufe %d\n", t, thermoTarget, level);
  }
}

static void serialLoop() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) Serial.println(handleCommand(line));
      line = "";
    } else if (line.length() < 64) {
      line += ch;
    }
  }
}

static void ledLoop() {
  // an = online, blinkt = kein MQTT, schnell = Not-Aus
  uint32_t now = millis();
  bool on;
  if (estop) on = (now / 150) % 2;
  else if (mqtt.connected()) on = true;
  else on = (now / 700) % 2;
  digitalWrite(PIN_STATUS_LED, on);
}

// --------------------------------------------------------------- Setup ------
void setup() {
  // Ausgänge zuerst in sicheren Zustand bringen
  prefs.begin("heizung", false);
  estop = prefs.getBool("estop", false);
  if (estop) estopReason = "vor Neustart ausgelöst";
  writeEstopRelay();
  pinMode(PIN_ESTOP_RELAY, OUTPUT);
  writeEstopRelay();
  writeLight();
  pinMode(PIN_LIGHT, OUTPUT);
  writeLight();
  pinMode(PIN_STATUS_LED, OUTPUT);
#if ESTOP_BUTTON_ENABLED
  pinMode(PIN_ESTOP_BUTTON, INPUT_PULLUP);
#endif

  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Heizungssteuerung startet ==="));
  if (estop) {
    alarmMsg = "NOT-AUS aktiv (" + estopReason + ")";
    Serial.println(F("[ESTOP] Not-Aus ist noch aktiv -> 'reset' zum Freigeben"));
  }

  sensors.begin();
  heater.begin();
  heater.setEnabled(!estop);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);  // verbindet im Hintergrund

  if (MQTT_INSECURE) net.setInsecure();
  else net.setCACert(MQTT_CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(10);
  mqtt.setBufferSize(768);

  setupWeb();
  Serial.println(F("Befehle im seriellen Monitor: on [min] | off | level N | temp N | "
                   "thermo N|off | light on|off | snap | estop | reset"));
}

void loop() {
  static bool wifiWasUp = false;
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp && !wifiWasUp) {
    Serial.printf("[WLAN] verbunden, IP %s\n", WiFi.localIP().toString().c_str());
    configTime(0, 0, "pool.ntp.org");  // nur Zeitstempel, ~100 Byte pro Stunde
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }
  wifiWasUp = wifiUp;

  sensors.loop();
  heater.loop();
  safetyLoop();
  thermoLoop();
  mqttLoop();
  web.handleClient();
  serialLoop();
  ledLoop();
  delay(2);
}
