#pragma once

#include <ESP8266WiFi.h>

#define HOSTNAME            "myesp8266"
#define PORT                8080

#define STA_SSID            "your-ssid"
#define STA_PASSWORD        "your-password"
//#define STATIC_ADDRESS

#ifdef STATIC_ADDRESS
IPAddress staticIP(192, 168, 0, 100);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 0, 1);
#endif
