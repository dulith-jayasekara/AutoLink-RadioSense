#pragma once
#include "Arduino.h"
constexpr int WIFI_AP = 2;
class WiFiStub
{
public:
    bool modeCalled = false, hostnameFirst = false, persistentValue = true, reconnect = true;
    bool configured = false, accessPoint = false;
    int selectedMode = 0, clientsLimit = 0;
    std::string ssid;
    void persistent(bool value) { persistentValue = value; }
    bool setHostname(const char *) { hostnameFirst = !modeCalled; return true; }
    bool setAutoReconnect(bool value) { reconnect = value; return true; }
    bool mode(int value) { selectedMode = value; modeCalled = true; return true; }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { configured = true; return true; }
    bool softAP(const char *name, const char *, int, bool, int maximum)
    { ssid = name; clientsLimit = maximum; accessPoint = true; return true; }
    unsigned softAPgetStationNum() const { return 1; }
    IPAddress softAPIP() const { return IPAddress(192,168,4,1); }
};
extern WiFiStub WiFi;
