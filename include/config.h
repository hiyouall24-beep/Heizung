#pragma once
// ============================================================================
//  Hardware- und Verhaltenseinstellungen
//  (Zugangsdaten stehen in secrets.h, siehe secrets.example.h)
// ============================================================================

// ---------------------------------------------------------------- Pins ------
#define PIN_ONEWIRE        4    // beide DS18B20 an diesem Pin (4,7 kOhm Pull-up auf 3,3 V)
#define PIN_LIGHT          26   // MOSFET-Modul für 12-V-Licht
#define PIN_ESTOP_RELAY    27   // Relaismodul in der 12-V-Zuleitung der Heizung
#define PIN_ESTOP_BUTTON   33   // optionaler Not-Aus-Taster (Öffner gegen GND)
#define PIN_STATUS_LED     2    // Onboard-LED

// Pegel, mit dem das Licht-MOSFET-Modul einschaltet
#define LIGHT_ACTIVE_HIGH  true
// Pegel am Relais-Eingang, bei dem die Relaisspule anzieht
// (viele billige Relaismodule ziehen bei LOW an -> dann false)
#define ESTOP_RELAY_ACTIVE_HIGH true
// true  = Heizung hängt am Öffner (NC): Relais zieht an -> Strom AUS.
//         Vorteil: Reset/Absturz des ESP32 unterbricht keine laufende Heizung
//         (Heizung kann normal nachlaufen/abkühlen).
// false = Heizung hängt am Schließer (NO): Relais muss angezogen sein, damit
//         die Heizung Strom hat.
#define ESTOP_RELAY_ENERGIZE_TO_CUT true
// Physischen Not-Aus-Taster auswerten? (Öffner zwischen PIN_ESTOP_BUTTON und GND;
// Kabelbruch löst ebenfalls Not-Aus aus)
#define ESTOP_BUTTON_ENABLED false

// --------------------------------------------------------- Heizung BLE ------
// MAC-Adresse der Heizung, z. B. "a4:c1:38:12:34:56". Leer = automatisch suchen.
// Tipp: Beim ersten Start werden alle gefundenen Geräte im seriellen Monitor
// gelistet – danach die MAC hier eintragen, dann verbindet er schneller/sicherer.
#define HEATER_BLE_MAC      ""
// Teil des Bluetooth-Namens, an dem die Heizung beim Suchen erkannt wird
#define HEATER_NAME_HINT    "heater"
// Passwort der Heizung (Standard der App: 1234)
#define HEATER_PASSKEY      1234
#define HEATER_SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define HEATER_CHAR_UUID    "0000ffe1-0000-1000-8000-00805f9b34fb"
#define HEATER_POLL_MS      5000    // Status-Abfrage über Bluetooth (kostet kein Internet)
#define HEATER_RETRY_MS     15000   // Pause zwischen Verbindungsversuchen

// ---------------------------------------------------------- Sicherheit ------
#define MAX_RUNTIME_MIN_DEFAULT 120   // Heizung geht nach dieser Zeit automatisch aus
#define MAX_RUNTIME_MIN_LIMIT   600   // größte erlaubte Laufzeit pro Start
#define OVERTEMP_CUTOFF_C       40.0f // Sensor 1 darüber -> Heizung aus
#define LOW_VOLTAGE_CUTOFF      11.3f // Batteriespannung darunter -> Heizung aus (0 = aus)
#define LOW_VOLTAGE_DELAY_S     60    // so lange muss die Unterspannung anliegen

// ------------------------------------------------------------ Sensoren ------
#define SENSOR1_NAME        "Innen"
#define SENSOR2_NAME        "Aussen"
// Optional feste Adressen (im seriellen Monitor beim Start angezeigt), sonst
// Reihenfolge am Bus. Beispiel: {0x28,0xFF,0x64,0x1E,0x0F,0x16,0x03,0x5C}
// #define SENSOR1_ADDR {0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
// #define SENSOR2_ADDR {0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
#define SENSOR_READ_MS      10000

// ---------------------------------------------- Thermostat (ESP-geregelt) ---
// Bei "thermo <°C>" regelt der ESP32 die Heizstufe anhand von Sensor 1.
#define THERMO_INTERVAL_S   60
#define THERMO_GAIN         2.0f   // Stufen pro °C Abweichung

// ------------------------------------------------- MQTT / Datensparen -------
#define MQTT_KEEPALIVE_S         240  // Ping-Intervall (nicht zu hoch: Mobilfunk-NAT)
#define PUBLISH_MIN_INTERVAL_S   30   // höchstens alle X s ein Status (außer nach Befehlen)
#define PUBLISH_HEARTBEAT_IDLE_S 1800 // Heizung aus: spätestens alle 30 min ein Status
#define PUBLISH_HEARTBEAT_RUN_S  300  // Heizung an: spätestens alle 5 min ein Status
#define PUBLISH_DELTA_TEMP       0.5f // Temperaturänderung, die sofort gemeldet wird
#define PUBLISH_DELTA_VOLT       0.3f

#define HOSTNAME "heizung"            // lokal erreichbar unter http://heizung.local
