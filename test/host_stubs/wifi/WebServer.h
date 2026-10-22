#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
#include <vector>
enum HTTPMethod { HTTP_GET, HTTP_POST };
class WebServer;
extern WebServer *testServer;
class WebServer
{
    using Handler = std::function<void()>;
    std::map<std::pair<std::string, HTTPMethod>, Handler> handlers;
    Handler missing;
    std::map<std::string, String> arguments, requestHeaders;
public:
    std::vector<std::string> collectedHeaders;
    std::map<std::string, std::string> responseHeaders;
    int code = 0; std::string body, contentType; bool started = false; unsigned polls = 0;
    explicit WebServer(int) { testServer = this; }
    void collectHeaders(const char **keys, size_t count) { for (size_t i=0;i<count;++i) collectedHeaders.emplace_back(keys[i]); }
    void on(const char *path, HTTPMethod method, Handler handler) { handlers[{path,method}] = handler; }
    void onNotFound(Handler handler) { missing = handler; }
    void begin() { started = true; }
    void handleClient() { ++polls; }
    void sendHeader(const char *key, const char *value) { responseHeaders[key] = value; }
    void send(int status, const char *type, const char *text) { code = status; contentType = type; body = text; }
    void send_P(int status, const char *type, const char *text) { send(status,type,text); }
    String header(const char *key) { return requestHeaders[key]; }
    String arg(const char *key) { return arguments[key]; }
    void invoke(const char *path, HTTPMethod method, std::map<std::string,String> args={}, std::map<std::string,String> headers={})
    {
        arguments = args; requestHeaders = headers; body.clear(); responseHeaders.clear(); code = 0;
        const auto found = handlers.find({path,method}); if(found != handlers.end()) found->second(); else missing();
    }
};
