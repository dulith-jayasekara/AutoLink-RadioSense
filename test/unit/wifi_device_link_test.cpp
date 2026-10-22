#include "WiFi_Console.h"
#include "WiFi.h"
#include "WebServer.h"
#include <cassert>
#include <cstring>
#include <iostream>
SerialStub Serial;
WiFiStub WiFi;
WebServer *testServer = nullptr;
uint32_t millis() { return 1000; }
namespace
{
unsigned calls = 0, mutations = 0;
void execute(const char *command, Print &output)
{
    ++calls;
    if (!std::strncmp(command,"REPORT_ID",9)) { ++mutations; output.println("OK"); }
    else if (!std::strcmp(command,"LONG")) for (unsigned i=0;i<17000;++i) output.write('x');
    else output.println(command);
}
}
int main()
{
    WiFiConsole::begin(669563705, execute);
    assert(WiFi.selectedMode == WIFI_AP && !WiFi.persistentValue && !WiFi.reconnect);
    assert(WiFi.hostnameFirst && WiFi.configured && WiFi.clientsLimit == 1);
    assert(WiFi.ssid == "RadioSense-669563705" && testServer->started);
    assert(testServer->collectedHeaders.size() == 2);
    testServer->invoke("/",HTTP_GET);
    assert(testServer->code == 200 && testServer->body.find("RadioSense node API") != std::string::npos);
    assert(testServer->contentType == "application/json" && testServer->body.find("<html") == std::string::npos);
    assert(testServer->responseHeaders["Cache-Control"] == "no-store");
    testServer->invoke("/api/network",HTTP_GET);
    assert(testServer->body.find("\"mode\":\"device_ap\"") != std::string::npos);
    assert(testServer->body.find("\"router_required\":false") != std::string::npos);
    assert(testServer->body.find("192.168.4.1") != std::string::npos);
    const unsigned before = calls;
    testServer->invoke("/api/network",HTTP_POST,{{"ssid","ignored"},{"password","not-a-real-password"}});
    assert(testServer->code == 409 && calls == before);
    testServer->invoke("/api/command",HTTP_POST,{{"command","REPORT_ID 1"}});
    assert(testServer->code == 403 && mutations == 0);
    testServer->invoke("/api/command",HTTP_POST,{{"command","REPORT_ID 1"}},{{"X-RadioSense","1"},{"X-RadioSense-Node","1747025710"}});
    assert(testServer->code == 409 && mutations == 0 && calls == before);
    const std::map<std::string,String> bound{{"X-RadioSense","1"},{"X-RadioSense-Node","669563705"}};
    for (const std::string &bad : {std::string(),std::string("LIST\nSYNC"),std::string("LIST\rSYNC"),std::string("LIST\0SYNC",9),std::string(192,'x')})
    { testServer->invoke("/api/command",HTTP_POST,{{"command",String(bad)}},bound); assert(testServer->code == 400 && calls == before); }
    testServer->invoke("/api/command",HTTP_POST,{{"command","REPORT_ID 1"}},bound);
    assert(testServer->code == 200 && mutations == 1 && testServer->body == "OK\n");
    testServer->invoke("/api/command",HTTP_POST,{{"command","LIST"}},{{"X-RadioSense","1"}});
    assert(testServer->code == 200); // Existing command clients remain compatible.
    testServer->invoke("/api/command",HTTP_POST,{{"command","LONG"}},bound);
    assert(testServer->code == 507);
    testServer->invoke("/api/status",HTTP_GET);
    assert(testServer->body.find("connection=device_ap") != std::string::npos);
    assert(testServer->body.find("MEMORY") != std::string::npos && testServer->body.find("SYNC") != std::string::npos);
    testServer->invoke("/missing",HTTP_GET); assert(testServer->code == 404);
    WiFiConsole::poll(); assert(testServer->polls == 1);
    std::cout << "PASS: independent one-client AP, stable address, disabled router setup, node-bound writes, request bounds and legacy query compatibility.\n";
}
