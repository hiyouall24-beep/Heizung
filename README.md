# ESP32 Standheizungs-Steuerung (VEVOR Diesel-Luftheizung)

Firmware für einen ESP32, der eine VEVOR-Diesel-Luftheizung mit Bluetooth-App
(„AirHeaterBLE“) fernsteuert.

**Funktionen**

- **Heizung per Bluetooth (BLE):** an/aus, Stufe 1–10, Solltemperatur 8–36 °C, Status auslesen
  (Spannung, Brennkammertemperatur, Fehlercodes)
- **2 Temperatursensoren** (DS18B20), z. B. innen und außen
- **ESP-Thermostat:** regelt die Heizstufe anhand deines eigenen Innensensors
- **12-V-Licht** schalten
- **Not-Aus:** ein Relais trennt die Heizung sofort vom Strom (per App, Weboberfläche oder Taster).
  Der Not-Aus bleibt auch nach einem Neustart aktiv, bis er zurückgesetzt wird.
- **Von zu Hause steuern** über MQTT, mit sehr wenig Datenverbrauch (ca. 3–5 MB im Monat)
- **Online-Dashboard** (eine einzelne HTML-Seite, z. B. kostenlos über GitHub Pages) und eine
  **lokale Weboberfläche** unter `http://heizung.local`
- **Sicherheitsabschaltungen:** maximale Laufzeit, Übertemperatur, Batterie-Unterspannung,
  Fehlermeldungen der Heizung

```
 Handy (überall)                   Bus / Garage
┌──────────────┐   Internet   ┌───────────────┐  WLAN  ┌───────────┐  Bluetooth  ┌──────────┐
│ Dashboard    │◄────────────►│ MQTT-Broker   │◄──────►│  ESP32    │◄───────────►│ Heizung  │
│ (docs/)      │  WebSocket   │ (HiveMQ Cloud)│  TLS   │           │             └──────────┘
└──────────────┘              └───────────────┘        │ DS18B20 x2, Licht, Not-Aus-Relais
                                                        └───────────┘
```

---

## 1. Hardware

| Teil | Empfehlung |
|---|---|
| ESP32 | ESP32-DevKit (WROOM-32), **kein** ESP32-S2 (hat kein Bluetooth) |
| Spannungsversorgung | Step-Down-Wandler 12 V → 5 V (min. 1 A) an VIN/5V |
| Temperatursensoren | 2× DS18B20 (wasserdichte Kabelversion) + 1× Widerstand 4,7 kΩ |
| Licht | MOSFET-Modul, das mit 3,3 V schaltet (z. B. **AOD4184/D4184-Modul** oder IRLZ44N). **Nicht** das IRF520-Modul. |
| Not-Aus | KFZ-Relais 12 V/30 A **Wechsler** (Klemmen 30/87/87a) + Relaismodul oder Transistor zum Ansteuern; optional Not-Aus-Pilztaster (Öffner) |
| WLAN am Fahrzeug | Heim-WLAN in Reichweite oder ein LTE-Router/Hotspot mit SIM-Karte |
| Sicherungen | Flachsicherungen: Heizung 15 A, ESP32 2 A, Licht passend |

### Verdrahtung

```
12V (+) ──[15A]──┬──[Not-Aus-Pilz, Öffner (optional, rein mechanisch)]── 30 ┐ KFZ-Relais
                 │                                                     87a ├──► Heizung (+)
                 │                                                        ┘   (87a = Öffner)
                 ├──[2A]──► Step-Down 12→5V ──► ESP32 VIN / GND
                 └──[5A]──► Licht (+)      Licht (−) ──► MOSFET-Modul ──► GND

ESP32 GPIO27 ──► Relaismodul IN   (das Relaismodul schaltet die Spule des KFZ-Relais)
ESP32 GPIO26 ──► MOSFET-Modul SIG
ESP32 GPIO4  ──► DQ beider DS18B20  (+ 4,7 kΩ von GPIO4 nach 3,3 V)
             DS18B20: VDD → 3,3 V, GND → GND
ESP32 GPIO33 ──► Not-Aus-Taster (Öffner) ──► GND   (nur wenn ESTOP_BUTTON_ENABLED true)
Alle GND (Batterie, Step-Down, ESP32, MOSFET, Relais) verbinden.
```

**Warum am Öffner (87a)?** Im Normalbetrieb ist das Relais *nicht* angezogen und die Heizung
hat Strom. Wenn der ESP32 neu startet oder ausfällt, läuft die Heizung normal weiter und kann
regulär nachlaufen/abkühlen. Nur bei Not-Aus zieht das Relais an und trennt den Strom.
Die Logik lässt sich in `include/config.h` umdrehen (`ESTOP_RELAY_ENERGIZE_TO_CUT`,
`ESTOP_RELAY_ACTIVE_HIGH`).

> ⚠️ Ein Not-Aus unterbricht den Nachlauf der Heizung. Das ist nur für Notfälle gedacht,
> nicht als normales Ausschalten. Ein mechanischer Not-Aus-Pilz in der Zuleitung funktioniert
> auch dann, wenn die Elektronik versagt, und ist deshalb sehr empfehlenswert.

---

## 2. MQTT-Broker einrichten (kostenlos, HiveMQ Cloud)

1. Auf <https://console.hivemq.cloud> registrieren und einen **Serverless**-Cluster (Free) anlegen.
2. Unter *Access Management* zwei Zugänge anlegen, z. B. `esp32` und `handy`
   (beide mit Berechtigung *Publish and Subscribe*).
3. Die Cluster-URL notieren (z. B. `abc123.s1.eu.hivemq.cloud`).
   - Port **8883** = MQTT über TLS (für den ESP32)
   - Port **8884** = WebSocket über TLS (für das Dashboard)

Jeder andere MQTT-Broker mit TLS und WebSockets funktioniert auch (z. B. ein eigener Mosquitto).

---

## 3. Firmware flashen

1. [VS Code](https://code.visualstudio.com/) + Erweiterung **PlatformIO** installieren.
2. Diesen Ordner in VS Code öffnen.
3. `include/secrets.example.h` nach **`include/secrets.h`** kopieren und ausfüllen
   (WLAN, MQTT-Zugang `esp32`, Passwort für die lokale Weboberfläche).
4. In `include/config.h` bei Bedarf Pins, Sensornamen und Grenzwerte anpassen.
5. ESP32 per USB anschließen → PlatformIO: **Upload**, danach **Monitor** (115200 Baud).

Beim ersten Start listet der serielle Monitor alle gefundenen Bluetooth-Geräte und die
Adressen der Temperatursensoren:

```
[BLE]   a4:c1:38:xx:xx:xx  rssi=-58  name='AirHeaterBLE'
[BLE] Heizung gefunden: a4:c1:38:xx:xx:xx
[TEMP]   #0 {0x28,0xFF,0x64,...}
```

Trag danach die MAC-Adresse der Heizung in `HEATER_BLE_MAC` ein (schneller und sicherer
als die automatische Suche) und, falls die Sensoren vertauscht sind, `SENSOR1_ADDR`/`SENSOR2_ADDR`.

Im seriellen Monitor kannst du alle Befehle (siehe unten) direkt eintippen, z. B. `on 30`.

---

## 4. Dashboard

Die Datei `docs/index.html` ist das komplette Dashboard. Es funktioniert auf zwei Arten:

- **Unterwegs / von zu Hause:** über MQTT. Am einfachsten über **GitHub Pages**:
  Repository → *Settings* → *Pages* → Branch `main`, Ordner **`/docs`** → Speichern.
  Danach erreichbar unter `https://<dein-name>.github.io/<repo>/`.
  Beim ersten Öffnen auf ⚙ tippen und Broker-Adresse, Port `8884`, Benutzer `handy` und
  Passwort eintragen. Die Zugangsdaten bleiben nur im Browser des Handys gespeichert.
  Tipp: Im Browser „Zum Home-Bildschirm hinzufügen“ – dann wirkt es wie eine App.
- **Vor Ort im gleichen WLAN:** `http://heizung.local` (oder die IP-Adresse) öffnen.
  Der ESP32 liefert dieselbe Seite aus und sie spricht direkt mit ihm – ganz ohne Internet.

Das Dashboard zeigt: Heizungsstatus, Restlaufzeit, Stufe/Solltemperatur, Spannung,
Brennkammer- und Heizungsfühler, beide Temperatursensoren, Licht, Not-Aus, Alarme
und wann der letzte Status gesendet wurde.

**Drei Regelarten:**

| Modus | Wer regelt | Fühler |
|---|---|---|
| Stufe | du (feste Stufe 1–10) | – |
| Temperatur | die Heizung selbst | Fühler in der Heizung |
| Thermostat | der ESP32 passt die Stufe an | dein Sensor 1 (z. B. „Innen“) |

---

## 4a. Als App aufs iPhone

Das Dashboard ist eine installierbare Web-App (PWA): eigenes Icon, Vollbild ohne Safari-Leiste,
startet sofort.

1. GitHub Pages einmalig aktivieren: Repository → **Settings → Pages** →
   *Source: Deploy from a branch* → Branch **`main`**, Ordner **`/docs`** → **Save**.
   Nach 1–2 Minuten ist die App erreichbar unter
   **<https://hiyouall24-beep.github.io/Heizung/>**
2. Diese Adresse auf dem iPhone in **Safari** öffnen (nicht Chrome).
3. Unten auf **Teilen** (Quadrat mit Pfeil) → **„Zum Home-Bildschirm“** → **Hinzufügen**.
4. Die App „Heizung“ vom Home-Bildschirm starten, auf ⚙ tippen und die MQTT-Zugangsdaten
   eintragen (Broker-Adresse, Port `8884`, Benutzer, Passwort).
   Wichtig: Die App auf dem Home-Bildschirm hat einen eigenen Speicher – die Zugangsdaten
   also **in der App** eintragen, nicht nur in Safari.

Danach: App antippen → **Starten**. Eine Benachrichtigung (`ack`) bestätigt jeden Befehl.

---

## 5. Befehle & MQTT-Topics

Topics (Basis einstellbar mit `MQTT_BASE_TOPIC`, Standard `heizung/bus`):

| Topic | Richtung | Inhalt |
|---|---|---|
| `heizung/bus/state` | ESP → | Status als JSON (retained) |
| `heizung/bus/online` | ESP → | `1` / `0` (Last Will, retained) |
| `heizung/bus/cmd` | → ESP | Befehl als Text |
| `heizung/bus/ack` | ESP → | Antwort auf den Befehl (`ok: …` / `err: …`) |

Befehle (gleich für MQTT, Weboberfläche und seriellen Monitor):

| Befehl | Wirkung |
|---|---|
| `on [min]` | Heizung starten, optional mit Laufzeit in Minuten (Standard 120) |
| `off` | Heizung ausschalten (mit normalem Nachlauf) |
| `level 1-10` | Stufen-Modus mit fester Stufe |
| `temp 8-36` | Temperatur-Modus der Heizung |
| `thermo 5-30` / `thermo off` | ESP-Thermostat mit Sensor 1 |
| `runtime <min>` | maximale Laufzeit ändern |
| `light on` / `light off` / `light toggle` | Licht |
| `estop` | **Not-Aus**: Heizung sofort stromlos |
| `reset` | Not-Aus zurücksetzen (Heizung bekommt wieder Strom, bleibt aber aus) |
| `status` | Status sofort senden |

Status-JSON (kurze Schlüssel, um Daten zu sparen):

```json
{"ble":1,"on":1,"st":3,"md":1,"lv":6,"tt":20,"v":12.6,"ct":148,"it":19,"er":0,
 "t":[18.4,-3.1],"n":["Innen","Aussen"],"li":0,"es":0,"mx":120,"rl":87,
 "ts":1792650000,"up":5000,"rs":-61}
```

`ble` Bluetooth verbunden · `on` läuft · `st` Phase (0 aus, 1 Selbsttest, 2 Zündung, 3 läuft,
4 abkühlen) · `md` Modus (1 Stufe, 2 Temperatur) · `lv` Stufe · `tt` Solltemperatur ·
`v` Spannung · `ct` Brennkammer °C · `it` Heizungsfühler °C · `er` Fehlercode ·
`t` Sensoren · `li` Licht · `es` Not-Aus · `al` Alarmtext · `mx` max. Laufzeit ·
`rl` Restlaufzeit (min) · `th` Thermostat-Soll · `ts` Zeitstempel · `rs` WLAN-Signal.

---

## 6. Datenverbrauch

Die Firmware ist auf möglichst wenig Datenverkehr ausgelegt:

- **eine** dauerhafte, verschlüsselte MQTT-Verbindung (kein ständiges Neu-Verbinden)
- Ping nur alle 4 Minuten (`MQTT_KEEPALIVE_S`)
- Status nur bei echten Änderungen (Temperatur ± 0,5 °C, Zustandswechsel), höchstens alle 30 s;
  sonst alle 30 min (Heizung aus) bzw. alle 5 min (Heizung an)
- direkt nach einem Befehl 90 s lang schneller, damit man die Reaktion sieht
- kompaktes JSON mit kurzen Schlüsseln (~250 Byte)
- Bluetooth-Abfragen der Heizung laufen lokal und kosten kein Internet

Grob gerechnet sind das **ca. 100–150 KB pro Tag, also etwa 3–5 MB im Monat**.
Hinweis: Manche Mobilfunktarife rechnen in 100-KB-Blöcken ab – bei einer dauerhaften
Verbindung fällt das kaum ins Gewicht. Alle Intervalle findest du in `include/config.h`.

---

## 7. Sicherheitsfunktionen

| Funktion | Einstellung (`config.h`) | Verhalten |
|---|---|---|
| Max. Laufzeit | `MAX_RUNTIME_MIN_DEFAULT` | Heizung geht nach Ablauf aus – auch wenn sie mit der Original-Fernbedienung gestartet wurde |
| Übertemperatur | `OVERTEMP_CUTOFF_C` | Sensor 1 zu warm → Heizung aus, Start wird verweigert |
| Unterspannung | `LOW_VOLTAGE_CUTOFF` | Batterie zu leer (60 s lang) → Heizung aus |
| Fehler der Heizung | – | Fehlercode wird als Alarm angezeigt (E1 Startfehler, E2 kein Brennstoff, …) |
| „Aus“ wird wiederholt | – | bis die Heizung das Ausschalten bestätigt |
| Not-Aus | Relais + optional Taster | Strom weg, bleibt über Neustarts gespeichert |

Allgemein: Abgas immer nach draußen führen, CO-Melder im Innenraum verwenden, Heizung nicht
unbeaufsichtigt in geschlossenen Räumen betreiben.

---

## 8. Bluetooth-Hinweise & Fehlersuche

- Die Heizung erlaubt meist **nur eine** Bluetooth-Verbindung. Solange der ESP32 verbunden ist,
  kann sich die Handy-App nicht verbinden.
- Das Bluetooth-Protokoll stammt aus Community-Analysen der „AirHeaterBLE“-Heizungen
  (Service `FFE0`, Characteristic `FFE1`, Passwort Standard `1234`). Es gibt mehrere
  Varianten dieser Heizungen. Die Firmware versteht das verbreitete `AA 55`-Format und
  versuchsweise das neuere, verschlüsselte `AA 66`-Format.
- Wenn die Heizung reagiert, aber keine Werte angezeigt werden: Unter
  `http://heizung.local/api/state` steht im Feld `raw` das zuletzt empfangene Datenpaket;
  unbekannte Pakete erscheinen auch im seriellen Monitor.
- Wurde das Passwort in der App geändert, `HEATER_PASSKEY` anpassen.
- Wird die Heizung nicht gefunden: in der Handy-App trennen, App schließen, ESP32 näher
  an die Heizung bringen und `HEATER_BLE_MAC` eintragen.

**LED am ESP32:** leuchtet = online · blinkt langsam = keine MQTT-Verbindung ·
blinkt schnell = Not-Aus aktiv.

---

## Projektstruktur

```
platformio.ini          Build-Konfiguration (PlatformIO)
include/config.h        Pins, Grenzwerte, Intervalle
include/secrets.h       WLAN-/MQTT-Zugangsdaten (selbst anlegen, nicht im Git)
include/ca_cert.h       Root-Zertifikate für TLS (Let's Encrypt)
include/dashboard_html.h  automatisch erzeugt aus docs/index.html
src/main.cpp            Logik: Befehle, Sicherheit, MQTT, Webserver
src/HeaterBle.*         Bluetooth-Protokoll der Heizung
src/Sensors.*           DS18B20-Auslesung
docs/index.html         Dashboard / iPhone-App (online + lokal)
docs/manifest.webmanifest, docs/sw.js, docs/icons/   App-Icon und Offline-Start
tools/embed_web.py      packt das Dashboard in die Firmware
```
