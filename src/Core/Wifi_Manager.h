#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "../../include/Config.h"

#define WIFI_SSID "AUTOLINK-WIFI-" NODE_NO
#define WIFI_PASSWORD "AutoLink123"
#define WIFI_CHANNEL 1
#define WIFI_MAX_CLIENTS 1
#define WIFI_VISIBLE 0

void WiFi_Initialization();

#endif