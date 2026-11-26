/**
 * @file Captive_Portal.cpp
 * @brief Captive portal communication infrastructure.
 * @details Owns the DNS, HTTP, and HTTPS servers and registers browser endpoints.
 * Relationships: Delegates request processing to Portal_Request_Handlers.cpp.
 */

#include "Captive_Portal.h"

namespace
{
    // Communication infrastructure owned by this module.
    DNSServer dnsServer;
    ESPWebServer httpServer(CAPTIVE_PORTAL_PORT);
    ESPWebServerSecure httpsServer(CAPTIVE_PORTAL_HTTPS_PORT);
}

// Internal access to server objects used by separated request handlers.
ESPWebServer &Portal_GetHttpServer()
{
    return httpServer;
}

ESPWebServerSecure &Portal_GetHttpsServer()
{
    return httpsServer;
}

// Initialize DNS redirection and both captive portal web servers.
void CaptivePortal_Initialization()
{
    IPAddress apIP = WiFi.softAPIP();

    dnsServer.start(DNS_PORT, "*", apIP);

    // Plain HTTP is used to install the CA certificate and guide users to HTTPS.
    httpServer.on("/", HTTP_GET, HandleHttpLanding);
    httpServer.on("/portal-ca.crt", HTTP_GET, HandleDownloadPortalCA);
    httpServer.on("/portal-ca.mobileconfig", HTTP_GET, HandleDownloadAppleProfile);
    httpServer.on("/generate_204", HTTP_GET, HandleHttpLanding);
    httpServer.on("/gen_204", HTTP_GET, HandleHttpLanding);
    httpServer.on("/hotspot-detect.html", HTTP_GET, HandleHttpLanding);
    httpServer.onNotFound(HandleHttpLanding);

    httpsServer.setServerKeyAndCert(
        const_cast<uint8_t *>(PORTAL_SERVER_KEY_DER),
        sizeof(PORTAL_SERVER_KEY_DER),
        const_cast<uint8_t *>(PORTAL_SERVER_CERT_DER),
        sizeof(PORTAL_SERVER_CERT_DER)
    );
    httpsServer.on("/", HTTP_GET, HandleRoot);
    httpsServer.on("/location", HTTP_POST, HandleLocation);
    httpsServer.on("/timestamp", HTTP_POST, HandlePhoneTimestamp);
    httpsServer.on("/generate_204", HTTP_GET, HandleRoot);
    httpsServer.on("/gen_204", HTTP_GET, HandleRoot);
    httpsServer.on("/hotspot-detect.html", HTTP_GET, HandleRoot);
    httpsServer.onNotFound(HandleNotFound);

    httpServer.begin();
    httpsServer.begin();

    Serial.println("Captive portal HTTP and HTTPS servers started.");
    Serial.print("Portal CA SHA-256 fingerprint: ");
    Serial.println(PORTAL_CA_FINGERPRINT);
    Serial.print("Certificate setup: http://");
    Serial.println(apIP);
    Serial.print("Secure location portal: https://");
    Serial.println(apIP);
}

// Process pending DNS, HTTP, and HTTPS requests.
void CaptivePortal_HandleClient()
{
    dnsServer.processNextRequest();
    httpServer.handleClient();
    httpsServer.handleClient();
}
