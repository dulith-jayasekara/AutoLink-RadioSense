/**
 * @file Portal_Landing_Page.h
 * @brief HTTP certificate installation landing page for the captive portal module.
 * @details Preserves the existing certificate instructions and secure portal link.
 * Relationships: Included by Portal_Request_Handlers.cpp.
 */

#pragma once

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

static const char HTTP_LANDING_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AutoLink RadioSense secure portal</title>
</head>
<body>
  <h2>AutoLink RadioSense</h2>
  <p>Phone browsers require a trusted HTTPS page to share location.</p>
  <p>Install the portal certificate once on this phone, then open the secure portal.</p>
  <p><a href="/portal-ca.crt">Download portal CA certificate</a></p>
  <p>Verify CA SHA-256 fingerprint against the value printed in the ESP32 serial log before installing it.</p>
  <p>Android: install the downloaded CA certificate from Security settings as a CA certificate.</p>
  <p>iPhone: <a href="/portal-ca.mobileconfig">download the iPhone certificate profile</a>, install it in Settings, then enable full trust under Certificate Trust Settings.</p>
  <p>Certificate installation may not work in the captive-portal pop-up. Use Safari or Chrome after joining Wi-Fi.</p>
  <p>After installation, open <a href="https://192.168.4.1/">https://192.168.4.1/</a> in Safari or Chrome.</p>
</body>
</html>
)rawliteral";
