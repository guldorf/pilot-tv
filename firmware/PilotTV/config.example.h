// Skopiuj ten plik jako config.h i uzupelnij. config.h nie trafia do gita.
#pragma once

// --- WiFi domowe ---
#define WIFI_SSID     "nazwa_sieci"
#define WIFI_PASS     "haslo_wifi"

// --- Broker MQTT (np. darmowy HiveMQ Cloud) ---
// Host z panelu HiveMQ: "Cluster URL", np. "abc123.s1.eu.hivemq.cloud"
#define MQTT_HOST     "abc123.s1.eu.hivemq.cloud"
#define MQTT_PORT     8883          // TLS (ESP32). Strona www uzywa 8884 (WebSocket)
#define MQTT_USER     "pilot"
#define MQTT_PASS     "haslo_mqtt"

// Nazwa urzadzenia - czesc tematow MQTT: pilottv/<DEVICE_ID>/...
// Ta sama nazwa musi byc wpisana w ustawieniach strony.
#define DEVICE_ID     "salon"

// --- Piny ---
// ESP32-C3 SuperMini i klasyczny ESP32 DevKit: 4 i 5 dzialaja na obu.
#define PIN_IR_SEND   4             // baza tranzystora (przez 1k) sterujacego dioda IR
#define PIN_IR_RECV   5             // wyjscie odbiornika VS1838B / TSOP38238 (nauka kodow dekodera)
