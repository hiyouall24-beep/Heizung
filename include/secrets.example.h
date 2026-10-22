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
#define MQTT_BASE_TOPIC "heizung/bus"
// true = Zertifikat nicht prüfen (nur zum Testen!)
#define MQTT_INSECURE false

// Login für die lokale Weboberfläche http://heizung.local
#define WEB_USER    "admin"
#define WEB_PASS    "bitte-aendern"
