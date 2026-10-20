#include "WiFi_Console.h"
#include "Config.h"
#if HAZARD_ENABLE_WIFI_CONSOLE
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

namespace
{
WebServer server(80);
WiFiConsole::CommandHandler executeCommand = nullptr;
uint32_t nodeId = 0;
bool ready = false;
Preferences networkSettings;
struct NetworkCredentials { uint8_t schema; char ssid[33]; char password[65]; };
NetworkCredentials network = {};
bool settingsReady = false, networkPending = false;
uint32_t applyNetworkAt = 0, lastReconnect = 0;
bool wasConnected = false;

String jsonString(const String &value)
{
    String out = "\"";
    for (size_t i = 0; i < value.length(); ++i)
    {
        const unsigned char ch = value[i];
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", ch); out += escaped; }
        else out += static_cast<char>(ch);
    }
    return out + "\"";
}

void connectRouter()
{
    if (!network.ssid[0]) return;
    WiFi.begin(network.ssid, network.password);
    lastReconnect = millis();
}
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

const char page[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RadioSense Node Monitor</title>
<style>body{font:16px system-ui;margin:24px auto;padding:0 16px;max-width:1000px;background:#101923;color:#edf5fc}button,input{font:inherit;padding:10px;margin:4px 0}button{cursor:pointer}input{width:min(95%,700px)}pre{background:#1c2b39;padding:16px;white-space:pre-wrap;overflow-wrap:anywhere;border-radius:8px}small{color:#bdcede}</style>
<h1>RadioSense Node Monitor</h1>
<p id="connection">Connecting…</p>
<p><small>Node status refreshes every 3 seconds. LoRa continues to exchange hazards between boards.</small></p>
<button id="refresh">Refresh now</button> <label><input id="auto" type="checkbox" checked style="width:auto"> Auto refresh</label>
<pre id="status">Waiting for node status…</pre>
<h2>Node commands</h2>
<p><small>Use HELP, LIST, SYNC, MEMORY, GROUPS, GET &lt;id&gt;, EVIDENCE &lt;id&gt; or CONFIDENCE &lt;id&gt;. MEMORY shows priorities/retirement capacity; RETIRED lists compact terminal markers. CLEANUP reclaims eligible terminal history. POSITION sets optional phone-supplied priority context. CONFIRM_ID and CONTRADICT_ID add observation votes. REPORT, REVISE, RESOLVE and TIME change node data. Supply GPS and UTC from your phone when reporting.</small></p>
<form id="form"><input id="command" maxlength="191" placeholder="EVIDENCE 7503418289721180161" required autocomplete="off"><button id="send">Run command</button></form>
<pre id="result">Command results appear here.</pre>
<h2>Join your broadband Wi-Fi</h2>
<p><small>Use a 2.4 GHz network. Credentials stay on this ESP32. After joining, use the router IP shown below in the Python monitor. The setup hotspot stays available; it may briefly reconnect when the router channel changes.</small></p>
<pre id="networkStatus">Loading network status…</pre>
<form id="networkForm"><input id="ssid" maxlength="32" placeholder="Router Wi-Fi name" required autocomplete="off"><br><input id="password" type="password" minlength="8" maxlength="64" placeholder="Router Wi-Fi password" required autocomplete="new-password"><br><button id="join">Save and connect</button></form>
<script>
const el=id=>document.getElementById(id);let refreshing=false;
async function refresh(){if(refreshing)return;refreshing=true;try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);el('status').textContent=await r.text();el('connection').textContent='Connected · last update '+new Date().toLocaleTimeString()}catch(e){el('connection').textContent='Disconnected: '+e.message}finally{refreshing=false}}
el('refresh').onclick=refresh;setInterval(()=>{if(el('auto').checked)refresh()},3000);refresh();
el('form').onsubmit=async e=>{e.preventDefault();el('send').disabled=true;try{const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-RadioSense':'1'},body:new URLSearchParams({command:el('command').value})});el('result').textContent=await r.text();await refresh()}catch(err){el('result').textContent='Request failed: '+err.message}finally{el('send').disabled=false}};
async function networkStatus(){try{const r=await fetch('/api/network',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const n=await r.json();el('networkStatus').textContent='Router network: '+(n.ssid||'not configured')+'\nConnection: '+(n.connected?'connected':'disconnected')+'\nRouter IP: '+n.station_ip+'\nSetup hotspot IP: '+n.ap_ip}catch(e){el('networkStatus').textContent='Network status unavailable: '+e.message}}
networkStatus();setInterval(networkStatus,5000);
el('networkForm').onsubmit=async e=>{e.preventDefault();el('join').disabled=true;try{const r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-RadioSense':'1'},body:new URLSearchParams({ssid:el('ssid').value,password:el('password').value})});el('networkStatus').textContent=await r.text();if(r.ok)el('password').value=''}catch(err){el('networkStatus').textContent='Connection interrupted; rejoin the setup hotspot and check network status.'}finally{el('join').disabled=false}};
</script></html>)HTML";

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
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(ssid);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    if (!WiFi.softAP(ssid, HAZARD_WIFI_PASSWORD, 1, false, 2))
    { Serial.println("WIFI_CONSOLE_START_FAILED; USB and LoRa remain available"); return; }
    settingsReady = networkSettings.begin("monitor-wifi", false);
    if (settingsReady && networkSettings.isKey("network") && networkSettings.getBytesLength("network") == sizeof(network))
    {
        networkSettings.getBytes("network", &network, sizeof(network));
        if (network.schema != 1 || network.ssid[32] != 0 || network.password[64] != 0)
            network = {};
    }
    connectRouter();
    const char *keys[] = {"X-RadioSense"}; server.collectHeaders(keys, 1);
    server.on("/", HTTP_GET, [] { headers(); server.send_P(200, "text/html; charset=utf-8", page); });
    server.on("/api/status", HTTP_GET, [] {
        response.clear();
        response.printf("node=%lu uptime_seconds=%lu wifi_clients=%u\n", static_cast<unsigned long>(nodeId),
            static_cast<unsigned long>(millis() / 1000), static_cast<unsigned>(WiFi.softAPgetStationNum()));
        executeCommand("MEMORY", response); executeCommand("SYNC", response);
        executeCommand("LIST", response); executeCommand("GROUPS", response); reply();
    });
    server.on("/api/command", HTTP_POST, [] {
        headers();
        if (server.header("X-RadioSense") != "1")
        { server.send(403, "text/plain", "Use the node dashboard to send commands.\n"); return; }
        const String command = server.arg("command");
        bool invalidCharacter = false;
        for (size_t i = 0; i < command.length(); ++i)
            if (command[i] == 0 || command[i] == '\n' || command[i] == '\r') invalidCharacter = true;
        if (command.length() == 0 || command.length() > 191 || command.indexOf('\n') >= 0 ||
            invalidCharacter)
        { server.send(400, "text/plain", "Enter one command of 1 to 191 characters.\n"); return; }
        response.clear(); executeCommand(command.c_str(), response); reply();
    });
    server.on("/api/network", HTTP_GET, [] {
        headers();
        const String result = String("{\"node\":\"") + String(nodeId) + "\",\"ssid\":" + jsonString(network.ssid) +
            ",\"connected\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false") +
            ",\"station_ip\":" + jsonString(WiFi.localIP().toString()) +
            ",\"ap_ip\":" + jsonString(WiFi.softAPIP().toString()) + "}";
        server.send(200, "application/json", result);
    });
    server.on("/api/network", HTTP_POST, [] {
        headers();
        if (server.header("X-RadioSense") != "1")
        { server.send(403, "text/plain", "Use the node setup page.\n"); return; }
        const String router = server.arg("ssid"), password = server.arg("password");
        bool invalid = false;
        for (size_t i = 0; i < router.length(); ++i) if (router[i] == 0) invalid = true;
        for (size_t i = 0; i < password.length(); ++i) if (password[i] == 0) invalid = true;
        if (invalid || router.length() == 0 || router.length() > 32 || password.length() < 8 || password.length() > 64)
        { server.send(400, "text/plain", "Enter a Wi-Fi name (1–32 bytes) and password (8–64 bytes).\n"); return; }
        NetworkCredentials candidate = {}; candidate.schema = 1;
        router.toCharArray(candidate.ssid, sizeof(candidate.ssid));
        password.toCharArray(candidate.password, sizeof(candidate.password));
        if (!settingsReady || networkSettings.putBytes("network", &candidate, sizeof(candidate)) != sizeof(candidate))
        { server.send(500, "text/plain", "Could not save network settings.\n"); return; }
        network = candidate; networkPending = true; applyNetworkAt = millis() + 500;
        server.send(200, "text/plain", "Wi-Fi settings saved. Connecting; check network status for the router IP.\n");
    });
    server.onNotFound([] { headers(); server.send(404, "text/plain", "Not found\n"); });
    server.begin(); ready = true;
    Serial.printf("WiFi monitor: SSID=%s password=%s URL=http://%s\n", ssid, HAZARD_WIFI_PASSWORD,
                  WiFi.softAPIP().toString().c_str());
}
void WiFiConsole::poll()
{
    if (!ready) return;
    server.handleClient();
    if (networkPending && static_cast<int32_t>(millis() - applyNetworkAt) >= 0)
    { networkPending = false; connectRouter(); }
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !wasConnected)
        Serial.printf("WiFi router connected: node=%lu URL=http://%s\n", static_cast<unsigned long>(nodeId), WiFi.localIP().toString().c_str());
    wasConnected = connected;
    if (!connected && !networkPending && network.ssid[0] && static_cast<uint32_t>(millis() - lastReconnect) >= 30000)
    { WiFi.reconnect(); lastReconnect = millis(); }
}
#else
void WiFiConsole::begin(uint32_t, CommandHandler) {}
void WiFiConsole::poll() {}
#endif
