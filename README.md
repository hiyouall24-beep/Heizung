# ESP32 Standheizungs-Steuerung (VEVOR Diesel-Luftheizung)

Firmware für einen ESP32, der eine VEVOR-Diesel-Luftheizung mit Bluetooth-App
(„AirHeaterBLE“) in der Werkstatt fernsteuert.

**Funktionen**

- **Heizung per Bluetooth (BLE):** an/aus, Stufe 1–10, Solltemperatur 8–36 °C, Status auslesen
  (Spannung, Brennkammertemperatur, Fehlercodes)
- **2 Temperatursensoren** (DS18B20), z. B. Werkstatt und außen
- **ESP-Thermostat:** regelt die Heizstufe anhand deines eigenen Sensors in der Werkstatt
- **12-V-Licht** schalten
- **Reolink-Kamera:** Foto auf Knopfdruck in der App (nur ca. 30–60 KB pro Foto)
- **Not-Aus:** ein Relais trennt die Heizung sofort vom Strom (per App, Weboberfläche oder Taster).
  Der Not-Aus bleibt auch nach einem Neustart aktiv, bis er zurückgesetzt wird.
- **Von zu Hause steuern** über MQTT, mit sehr wenig Datenverbrauch (ca. 3–5 MB im Monat)
- **Online-Dashboard** (eine einzelne HTML-Seite, z. B. kostenlos über GitHub Pages) und eine
  **lokale Weboberfläche** unter `http://heizung.local`
- **Sicherheitsabschaltungen:** maximale Laufzeit, Übertemperatur, Batterie-Unterspannung,
  Fehlermeldungen der Heizung

```
 Handy (überall)                   Werkstatt
┌──────────────┐   Internet   ┌───────────────┐  WLAN  ┌───────────┐  Bluetooth  ┌──────────┐
│ Dashboard    │◄────────────►│ MQTT-Broker   │◄──────►│  ESP32    │◄───────────►│ Heizung  │
│ (docs/)      │  WebSocket   │ (HiveMQ Cloud)│  TLS   │           │             └──────────┘
└──────────────┘              └───────────────┘        │ DS18B20 x2, Licht, Not-Aus-Relais
                                                        └─────┬─────┘
                                                     WLAN/LAN │ Foto (HTTP)
                                                        ┌─────┴─────┐
                                                        │  Reolink  │
                                                        └───────────┘
```

---

## 1. Hardware

| Teil | Empfehlung |
|---|---|
| ESP32 | ESP32-DevKit (WROOM-32), **kein** ESP32-S2 (hat kein Bluetooth) |
| 12-V-Versorgung | Netzteil 12 V mit **mindestens 15 A** (die Glühkerze zieht beim Start ca. 8–10 A) oder Batterie mit Ladegerät |
| Spannungsversorgung ESP32 | Step-Down-Wandler 12 V → 5 V (min. 1 A) an VIN/5V |
| Temperatursensoren | 2× DS18B20 (wasserdichte Kabelversion) + 1× Widerstand 4,7 kΩ |
| Licht | MOSFET-Modul, das mit 3,3 V schaltet (z. B. **AOD4184/D4184-Modul** oder IRLZ44N). **Nicht** das IRF520-Modul. |
| Not-Aus | KFZ-Relais 12 V/30 A **Wechsler** (Klemmen 30/87/87a) + Relaismodul oder Transistor zum Ansteuern; optional Not-Aus-Pilztaster (Öffner) |
| WLAN in der Werkstatt | WLAN mit Internet (Router, Repeater oder LTE-Router) |
| Kamera (optional) | Reolink-Kamera mit WLAN/LAN im **gleichen Netz** wie der ESP32 (keine reinen Akku-/4G-Modelle, siehe unten) |
| Sicherungen | Flachsicherungen: Heizung 15 A, ESP32 2 A, Licht passend |

### Verdrahtung

```
12V (+) ──[20A]──┬──[Not-Aus-Pilz, Öffner (optional, rein mechanisch)]── 30 ┐ KFZ-Relais
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
| Thermostat | der ESP32 passt die Stufe an | dein Sensor 1 (z. B. „Werkstatt“) |

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

## 4b. Reolink-Kamera

Die App kann ein **aktuelles Foto** aus der Werkstatt anzeigen, z. B. um nach dem Start zu
prüfen, ob alles in Ordnung ist. Der ESP32 holt das Bild im lokalen Netz von der Kamera
und schickt es nur auf Knopfdruck über MQTT. Am Router muss **nichts freigegeben** werden.

**Einrichten:**

1. In der Reolink-App: Kamera → Einstellungen → **Netzwerk → Erweitert → Server-Einstellungen**
   (bzw. „Netzwerkdienste“) → **HTTP** einschalten (Port 80).
2. Im Router der Kamera eine **feste IP-Adresse** geben (z. B. `192.168.178.50`).
3. Empfohlen: in der Kamera einen zusätzlichen Benutzer mit der Rolle **Gast** anlegen und
   diesen für den ESP32 verwenden.
4. In `include/secrets.h` eintragen:
   ```cpp
   #define CAM_HOST "192.168.178.50"
   #define CAM_USER "esp32"
   #define CAM_PASS "kamerapasswort"
   ```
5. Neu flashen. In der App erscheint die Karte **Kamera** mit dem Knopf **Foto aufnehmen**.

Das Foto wird in 640 × 360 angefordert (`CAM_WIDTH`/`CAM_HEIGHT` in `config.h`), das sind
etwa 30–60 KB. Das letzte Foto bleibt auf dem Broker gespeichert und wird beim Öffnen der App
sofort angezeigt. Hängt die Kamera an einem Reolink-NVR/Home Hub, trägst du dessen IP ein
und wählst die Kamera mit `CAM_CHANNEL`.

**Live-Ansicht:** Mit **▶ Live** schickt der ESP32 laufend kleine Bilder (480 × 270, etwa
1 Bild pro Sekunde), solange die Kamera in der App offen ist. Das ist kein echtes Video
(kein Ton, kein flüssiges Bild), reicht aber, um zu sehen, was in der Werkstatt los ist.

- Datenverbrauch: ca. **1–1,5 MB pro Minute** (einmal hoch zum Broker, einmal runter aufs Handy)
- Live stoppt automatisch, wenn du die App verlässt, auf **■ Stopp** tippst oder nach 5 Minuten
- Meldet sich die App nicht mehr (z. B. Funkloch), hört der ESP32 nach spätestens 60 s auf
- Bildgröße und Bildrate: `CAM_LIVE_WIDTH`, `CAM_LIVE_HEIGHT`, `CAM_LIVE_INTERVAL_MS` in `config.h`
- Der kostenlose HiveMQ-Tarif hat ein monatliches Datenlimit. Für gelegentliches Reinschauen
  reicht es, für stundenlanges Live-Schauen nicht.

**Flüssiges Video mit Ton** gibt es weiterhin in der normalen Reolink-App. Das geht über
Reolinks eigene Cloud und lässt sich nicht in eine selbst gebaute Web-App einbetten.

**Funktioniert nicht mit:** reinen Akku-Kameras (Argus-Serie) und 4G-Kameras (Go-Serie).
Diese haben kein HTTP-Interface im lokalen Netz.

**Fehlersuche:** Die Meldung erscheint unten in der App. „kein JPEG“ bedeutet falscher
Benutzer oder falsches Passwort. „Kamera nicht erreichbar“ bedeutet falsche IP oder HTTP ist
ausgeschaltet. Test im Browser im selben WLAN:
`http://<kamera-ip>/cgi-bin/api.cgi?cmd=Snap&channel=0&rs=x&user=<benutzer>&password=<passwort>`
muss ein Bild zeigen. Liefert die Kamera trotz `CAM_WIDTH` ein sehr großes Bild (ältere
Firmware ignoriert die Größe), meldet die App „Bild zu groß“ – dann Kamera-Firmware
aktualisieren.

---

## 5. Befehle & MQTT-Topics

Topics (Basis einstellbar mit `MQTT_BASE_TOPIC`, Standard `heizung/werkstatt`):

| Topic | Richtung | Inhalt |
|---|---|---|
| `heizung/werkstatt/state` | ESP → | Status als JSON (retained) |
| `heizung/werkstatt/online` | ESP → | `1` / `0` (Last Will, retained) |
| `heizung/werkstatt/cmd` | → ESP | Befehl als Text |
| `heizung/werkstatt/ack` | ESP → | Antwort auf den Befehl (`ok: …` / `err: …`) |
| `heizung/werkstatt/snap` | ESP → | letztes Kamerafoto als JPEG (retained) |
| `heizung/werkstatt/live` | ESP → | Live-Einzelbilder als JPEG (nicht gespeichert) |

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
| `snap` | Foto von der Kamera aufnehmen und senden |
| `live [sek]` / `live off` | Live-Bilder für X Sekunden (max. 120, die App verlängert automatisch) |
| `status` | Status sofort senden |

Status-JSON (kurze Schlüssel, um Daten zu sparen):

```json
{"ble":1,"on":1,"st":3,"md":1,"lv":6,"tt":20,"v":12.6,"ct":148,"it":19,"er":0,
 "t":[18.4,-3.1],"n":["Werkstatt","Aussen"],"li":0,"es":0,"mx":120,"rl":87,
 "ts":1792650000,"up":5000,"rs":-61}
```

`ble` Bluetooth verbunden · `on` läuft · `st` Phase (0 aus, 1 Selbsttest, 2 Zündung, 3 läuft,
4 abkühlen) · `md` Modus (1 Stufe, 2 Temperatur) · `lv` Stufe · `tt` Solltemperatur ·
`v` Spannung · `ct` Brennkammer °C · `it` Heizungsfühler °C · `er` Fehlercode ·
`t` Sensoren · `li` Licht · `es` Not-Aus · `al` Alarmtext · `mx` max. Laufzeit ·
`rl` Restlaufzeit (min) · `th` Thermostat-Soll · `ts` Zeitstempel · `rs` WLAN-Signal ·
`cam` Kamera eingerichtet · `sn` Zeitpunkt des letzten Fotos.

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
Jedes Kamerafoto kostet zusätzlich etwa 30–60 KB (einmal hoch zum Broker, einmal runter aufs Handy).
Hinweis: Manche Mobilfunktarife rechnen in 100-KB-Blöcken ab – bei einer dauerhaften
Verbindung fällt das kaum ins Gewicht. Alle Intervalle findest du in `include/config.h`.

---

## 7. Sicherheitsfunktionen

| Funktion | Einstellung (`config.h`) | Verhalten |
|---|---|---|
| Max. Laufzeit | `MAX_RUNTIME_MIN_DEFAULT` | Heizung geht nach Ablauf aus – auch wenn sie mit der Original-Fernbedienung gestartet wurde |
| Übertemperatur | `OVERTEMP_CUTOFF_C` | Sensor 1 zu warm → Heizung aus, Start wird verweigert |
| Unterspannung | `LOW_VOLTAGE_CUTOFF` | Spannung zu niedrig (60 s lang) → Heizung aus. Schützt eine Batterie und erkennt ein zu schwaches Netzteil |
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
src/Camera.*            Foto von der Reolink-Kamera holen
docs/index.html         Dashboard / iPhone-App (online + lokal)
docs/manifest.webmanifest, docs/sw.js, docs/icons/   App-Icon und Offline-Start
tools/embed_web.py      packt das Dashboard in die Firmware
```
