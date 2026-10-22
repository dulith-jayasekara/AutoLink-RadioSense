#include "WiFi_Console.h"
#include "Config.h"
#if HAZARD_ENABLE_WIFI_CONSOLE
#include <WiFi.h>
#include <WebServer.h>

namespace
{
WebServer server(80);
WiFiConsole::CommandHandler executeCommand = nullptr;
uint32_t nodeId = 0;
bool ready = false;
static_assert(HAZARD_WIFI_MAX_CLIENTS >= 1 && HAZARD_WIFI_MAX_CLIENTS <= 4, "Use one to four hotspot clients");
class Response : public Print
{
public:
    char data[16385] = {};
    size_t length = 0;
    bool truncated = false;
    void clear() { length = 0; data[0] = 0; truncated = false; }
    size_t write(uint8_t ch) override
    {
        if (length == sizeof(data) - 1) { truncated = true; return 0; }
        data[length++] = static_cast<char>(ch); data[length] = 0; return 1;
    }
} response;
void headers()
{
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
}
void reply()
{
    headers();
    if (response.truncated) server.send(507, "text/plain", "Output exceeded 16384 bytes; query individual records.\n");
    else server.send(200, "text/plain; charset=utf-8", response.data);
}
}
void WiFiConsole::begin(uint32_t node, CommandHandler handler)
{
    executeCommand = handler;
    if (!handler) return;
    nodeId = node;
    char ssid[33]; snprintf(ssid, sizeof(ssid), "RadioSense-%lu", static_cast<unsigned long>(node));
    // Each vehicle has its own device link. No remembered router connection or
    // scanning may change the hotspot channel while the application is using it.
    WiFi.persistent(false);
    WiFi.setHostname(ssid);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP);
    const IPAddress address(192, 168, 4, 1), subnet(255, 255, 255, 0);
    if (!WiFi.softAPConfig(address, address, subnet) ||
        !WiFi.softAP(ssid, HAZARD_WIFI_PASSWORD, 1, false, HAZARD_WIFI_MAX_CLIENTS))
    { Serial.println("WIFI_CONSOLE_START_FAILED; USB and LoRa remain available"); return; }
    const char *keys[] = {"X-RadioSense", "X-RadioSense-Node"}; server.collectHeaders(keys, 2);
    server.on("/", HTTP_GET, [] {
        headers();
        server.send(200, "application/json", "{\"service\":\"RadioSense node API\",\"ui\":\"Run the application locally on your laptop\",\"status\":\"/api/status\"}");
    });
    server.on("/api/status", HTTP_GET, [] {
        response.clear();
        response.printf("node=%lu uptime_seconds=%lu wifi_clients=%u connection=device_ap\n", static_cast<unsigned long>(nodeId),
            static_cast<unsigned long>(millis() / 1000), static_cast<unsigned>(WiFi.softAPgetStationNum()));
        executeCommand("MEMORY", response); executeCommand("SYNC", response);
        executeCommand("LIST", response); executeCommand("GROUPS", response); reply();
    });
    server.on("/api/command", HTTP_POST, [] {
        headers();
        if (server.header("X-RadioSense") != "1")
        { server.send(403, "text/plain", "Use this node's device app to send commands.\n"); return; }
        // Existing generic clients may omit this header. The device app always
        // binds its commands/retries to the discovered node, even after roaming.
        const String expected = server.header("X-RadioSense-Node");
        if (expected.length() && expected != String(nodeId))
        { server.send(409, "text/plain", "WRONG_NODE; reconnect to the original hotspot or reload to select this node.\n"); return; }
        const String command = server.arg("command");
        bool invalid = command.length() == 0 || command.length() > 191;
        for (size_t i = 0; i < command.length(); ++i)
            if (command[i] == 0 || command[i] == '\n' || command[i] == '\r') invalid = true;
        if (invalid)
        { server.send(400, "text/plain", "Enter one command of 1 to 191 characters.\n"); return; }
        response.clear(); executeCommand(command.c_str(), response); reply();
    });
    server.on("/api/network", HTTP_GET, [] {
        headers(); response.clear();
        response.printf("{\"node\":\"%lu\",\"mode\":\"device_ap\",\"ap_ssid\":\"RadioSense-%lu\","
                        "\"ap_ip\":\"192.168.4.1\",\"clients\":%u,\"max_clients\":%u,"
                        "\"router_required\":false,\"synchronization\":\"lora\",\"api_version\":3,"
                        "\"ssid\":\"\",\"connected\":false,\"station_ip\":\"0.0.0.0\"}",
            static_cast<unsigned long>(nodeId), static_cast<unsigned long>(nodeId),
            static_cast<unsigned>(WiFi.softAPgetStationNum()), static_cast<unsigned>(HAZARD_WIFI_MAX_CLIENTS));
        server.send(200, "application/json", response.data);
    });
    server.on("/api/network", HTTP_POST, [] {
        headers();
        server.send(409, "text/plain", "DEVICE_LINK_MODE; connect your device to this node's hotspot. Router setup is not required.\n");
    });
    server.onNotFound([] { headers(); server.send(404, "text/plain", "Not found\n"); });
    server.begin(); ready = true;
    Serial.printf("WiFi node API: SSID=%s password=%s URL=http://%s max_clients=%u; peer sync=LoRa\n",
        ssid, HAZARD_WIFI_PASSWORD, WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(HAZARD_WIFI_MAX_CLIENTS));
}
void WiFiConsole::poll()
{
    if (ready) server.handleClient();
}
#else
void WiFiConsole::begin(uint32_t, CommandHandler) {}
void WiFiConsole::poll() {}
#endif
