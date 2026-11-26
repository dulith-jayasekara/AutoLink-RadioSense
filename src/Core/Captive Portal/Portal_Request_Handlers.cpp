/**
 * @file Portal_Request_Handlers.cpp
 * @brief Browser request handling for the captive portal.
 * @details Extracts request fields and forwards GPS and timestamp data to their
 * respective managers while preserving existing endpoint responses.
 */

#include "Portal_Request_Handlers.h"

// Serve the HTTPS location page.
void HandleRoot()
{
    Portal_GetHttpsServer().send_P(200, PSTR("text/html"), LOCATION_PAGE);
}

// Serve the plain HTTP certificate landing page.
void HandleHttpLanding()
{
    Portal_GetHttpServer().send_P(200, PSTR("text/html"), HTTP_LANDING_PAGE);
}

// Download the portal CA certificate.
void HandleDownloadPortalCA()
{
    ESPWebServer &server = Portal_GetHttpServer();
    server.sendHeader(
        "Content-Disposition",
        "attachment; filename=AutoLink-Portal-CA.crt"
    );
    server.send_P(
        200,
        PSTR("application/x-x509-ca-cert"),
        PORTAL_CA_CERT_PEM,
        sizeof(PORTAL_CA_CERT_PEM) - 1
    );
}

// Download the Apple mobileconfig certificate profile.
void HandleDownloadAppleProfile()
{
    ESPWebServer &server = Portal_GetHttpServer();
    server.sendHeader(
        "Content-Disposition",
        "attachment; filename=AutoLink-Portal-CA.mobileconfig"
    );
    server.send_P(
        200,
        PSTR("application/x-apple-aspen-config"),
        PORTAL_CA_MOBILECONFIG,
        sizeof(PORTAL_CA_MOBILECONFIG) - 1
    );
}

// Receive and validate location and motion data from the phone.
void HandleLocation()
{
    ESPWebServerSecure &server = Portal_GetHttpsServer();

    if (!server.hasArg("latitude") ||
        !server.hasArg("longitude") ||
        !server.hasArg("accuracy") ||
        !server.hasArg("speed") ||
        !server.hasArg("heading") ||
        !server.hasArg("fixTimestamp"))
    {
        server.send(
            400,
            "text/plain",
            "Missing location, motion, accuracy, or fix timestamp field"
        );
        return;
    }

    GPS_ProcessResult result = GPS_ProcessLocation(
        server.arg("latitude"),
        server.arg("longitude"),
        server.arg("accuracy"),
        server.arg("speed"),
        server.arg("heading"),
        server.arg("fixTimestamp")
    );

    switch (result)
    {
    case GPS_PROCESS_INVALID:
        server.send(400, "text/plain", "Invalid location or fix data");
        return;
    case GPS_PROCESS_OUT_OF_RANGE:
        server.send(400, "text/plain", "Location or motion value out of range");
        return;
    case GPS_PROCESS_STALE:
        server.send(409, "text/plain", "Stale location fix");
        return;
    case GPS_PROCESS_ACCEPTED:
        server.send(200, "text/plain", "Location received");
        return;
    }
}

// Receive and forward an unsigned phone timestamp.
void HandlePhoneTimestamp()
{
    ESPWebServerSecure &server = Portal_GetHttpsServer();

    if (!server.hasArg("timestamp"))
    {
        server.send(400, "text/plain", "Missing timestamp field");
        return;
    }

    uint64_t timestampMilliseconds = 0;
    if (!Time_ParsePhoneTimestamp(
            server.arg("timestamp"),
            timestampMilliseconds))
    {
        server.send(400, "text/plain", "Timestamp must be unsigned Unix milliseconds");
        return;
    }

    if (!Time_IsTimestampReasonable(timestampMilliseconds))
    {
        server.send(400, "text/plain", "Timestamp is outside the supported 2020-2100 range");
        return;
    }

    if (!Time_Synchronization(timestampMilliseconds))
    {
        server.send(500, "text/plain", "ESP32 clock synchronization failed");
        return;
    }

    server.send(200, "text/plain", "Clock synchronized");
}

// Redirect unknown HTTPS paths back to the secure portal root.
void HandleNotFound()
{
    ESPWebServerSecure &server = Portal_GetHttpsServer();
    server.sendHeader(
        "Location",
        String("https://") + WiFi.softAPIP().toString() + "/",
        true
    );
    server.send(302, "text/plain", "");
}
