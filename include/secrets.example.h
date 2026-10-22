#pragma once
// Diese Datei nach "secrets.h" kopieren und ausfüllen.
// secrets.h wird nicht ins Git-Repository hochgeladen.

#define WIFI_SSID   "MeinWLAN"
#define WIFI_PASS   "geheim"

// MQTT-Broker, z. B. kostenloser HiveMQ Cloud Cluster (TLS, Port 8883)
#define MQTT_HOST   "xxxxxxxx.s1.eu.hivemq.cloud"
#define MQTT_PORT   8883
#define MQTT_USER   "esp32"
#define MQTT_PASS   "geheim"
// Alle Topics beginnen hiermit: <base>/state, <base>/cmd, <base>/ack, <base>/online
#define MQTT_BASE_TOPIC "heizung/werkstatt"
// true = Zertifikat nicht prüfen (nur zum Testen!)
#define MQTT_INSECURE false

// Reolink-Kamera im gleichen WLAN (leer lassen = keine Kamera)
// In der Kamera muss HTTP aktiviert sein: Reolink-App -> Einstellungen ->
// Netzwerk -> Erweitert -> Server-Einstellungen -> HTTP an (Port 80).
// Am besten einen eigenen Kamera-Benutzer mit Rolle "Gast/Benutzer" anlegen.
#define CAM_HOST    ""          // z. B. "192.168.178.50"
#define CAM_USER    "admin"
#define CAM_PASS    ""
// Optional: zweite Reolink-Kamera (z. B. am Roboter)
#define CAM2_HOST   ""
#define CAM2_USER   "admin"
#define CAM2_PASS   ""

// Login für die lokale Weboberfläche http://heizung.local
#define WEB_USER    "admin"
#define WEB_PASS    "bitte-aendern"
