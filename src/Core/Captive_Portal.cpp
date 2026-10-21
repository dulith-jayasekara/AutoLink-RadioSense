#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"
#include <ESPWebServer.hpp>
#include <ESPWebServerSecure.hpp>
#include "PortalTlsCertificate.h"
#include "Time_Manager.h"

static DNSServer dnsServer;
static ESPWebServer httpServer(CAPTIVE_PORTAL_PORT);
static ESPWebServerSecure httpsServer(CAPTIVE_PORTAL_HTTPS_PORT);

static String vehicleLatitude = "";
static String vehicleLongitude = "";
static String locationAccuracy = "";
static double latestLatitude = 0.0;
static double latestLongitude = 0.0;
static bool hasValidLocation = false;

static const char LOCATION_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport"
        content="width=device-width, initial-scale=1">
  <title>AutoLink RadioSense</title>
</head>
<body>
  <h2>AutoLink RadioSense</h2>
  <p>Share your vehicle's location with the vehicle node.</p>

  <p id="timeStatus">Synchronizing the vehicle clock...</p>
  <button id="retryTimeSync" onclick="synchronizePhoneTime()" hidden>
    Retry clock synchronization
  </button>

  <button onclick="shareLocation()">Share Location</button>
  <p id="locationStatus">Waiting for location permission.</p>

  <script>
    async function synchronizePhoneTime() {
      const timeStatus = document.getElementById("timeStatus");
      const retryButton = document.getElementById("retryTimeSync");
      retryButton.hidden = true;

      // Date.now() returns Unix milliseconds and does not request a permission.
      const timestampMilliseconds = Date.now();
      const body = new URLSearchParams({
        timestamp: String(timestampMilliseconds)
      });

      try {
        const response = await fetch("/timestamp", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded"
          },
          body: body
        });

        const message = await response.text();
        if (response.ok) {
          timeStatus.textContent = "Vehicle clock synchronized.";
          return;
        }

        timeStatus.textContent = message || "Clock synchronization failed.";
      } catch (error) {
        timeStatus.textContent = "Could not synchronize the vehicle clock.";
      }

      // A retry is offered only after a failed request; no timer repeatedly
      // sends phone time in the background.
      retryButton.hidden = false;
    }

    async function shareLocation() {
      const locationStatus = document.getElementById("locationStatus");

      if (!window.isSecureContext || !("geolocation" in navigator)) {
        locationStatus.textContent =
          "Secure location access is unavailable. Open the trusted HTTPS portal.";
        return;
      }

      locationStatus.textContent = "Requesting location permission...";

      navigator.geolocation.getCurrentPosition(
        async function(position) {
          const latitude = position.coords.latitude;
          const longitude = position.coords.longitude;
          const accuracy = position.coords.accuracy;

          locationStatus.textContent = "Sending location to vehicle node...";

          const body = new URLSearchParams({
            latitude: latitude,
            longitude: longitude,
            accuracy: accuracy
          });

          try {
            const response = await fetch("/location", {
              method: "POST",
              headers: {
                "Content-Type":
                  "application/x-www-form-urlencoded"
              },
              body: body
            });

            if (response.ok) {
              locationStatus.textContent =
                "Location received by the vehicle node.";
            } else {
              locationStatus.textContent =
                "The vehicle node could not receive the location.";
            }
          } catch (error) {
            locationStatus.textContent =
              "Could not communicate with the vehicle node.";
          }
        },
        function(error) {
          locationStatus.textContent =
            "Location unavailable or permission denied. Error: " +
            error.message;
        },
        {
          enableHighAccuracy: true,
          timeout: 15000,
          maximumAge: 5000
        }
      );
    }

    // The script is placed at the end of the page, so the status elements exist.
    // Synchronize once whenever this HTTPS portal page loads.
    synchronizePhoneTime();
  </script>
</body>
</html>
)rawliteral";

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


static void HandleRoot()
{
    httpsServer.send_P(
        200,
        PSTR("text/html"),
        LOCATION_PAGE
    );
}

static void HandleHttpLanding()
{
    httpServer.send_P(
        200,
        PSTR("text/html"),
        HTTP_LANDING_PAGE
    );
}

static void HandleDownloadPortalCA()
{
    httpServer.sendHeader(
        "Content-Disposition",
        "attachment; filename=AutoLink-Portal-CA.crt"
    );
    httpServer.send_P(
        200,
        PSTR("application/x-x509-ca-cert"),
        PORTAL_CA_CERT_PEM,
        sizeof(PORTAL_CA_CERT_PEM) - 1
    );
}

static void HandleDownloadAppleProfile()
{
    httpServer.sendHeader(
        "Content-Disposition",
        "attachment; filename=AutoLink-Portal-CA.mobileconfig"
    );
    httpServer.send_P(
        200,
        PSTR("application/x-apple-aspen-config"),
        PORTAL_CA_MOBILECONFIG,
        sizeof(PORTAL_CA_MOBILECONFIG) - 1
    );
}


static void HandleLocation()
{
    if (!httpsServer.hasArg("latitude") ||
        !httpsServer.hasArg("longitude") ||
        !httpsServer.hasArg("accuracy")) {

        httpsServer.send(
            400,
            "text/plain",
            "Missing location fields"
        );
        return;
    }

    vehicleLatitude = httpsServer.arg("latitude");
    vehicleLongitude = httpsServer.arg("longitude");
    locationAccuracy = httpsServer.arg("accuracy");

    const double latitude = vehicleLatitude.toDouble();
    const double longitude = vehicleLongitude.toDouble();
    if (!isfinite(latitude) || !isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0) {
        httpsServer.send(400, "text/plain", "Invalid latitude or longitude");
        return;
    }
    latestLatitude = latitude;
    latestLongitude = longitude;
    hasValidLocation = true;

    Serial.println();
    Serial.println("===== GPS LOCATION RECEIVED =====");

    Serial.print("Latitude: ");
    Serial.println(vehicleLatitude);

    Serial.print("Longitude: ");
    Serial.println(vehicleLongitude);

    Serial.print("Accuracy (metres): ");
    Serial.println(locationAccuracy);

    Serial.println("=================================");

    httpsServer.send(
        200,
        "text/plain",
        "Location received"
    );
}

bool CaptivePortal_GetLocation(double &latitude, double &longitude)
{
    if (!hasValidLocation)
    {
        return false;
    }

    latitude = latestLatitude;
    longitude = latestLongitude;
    return true;
}

// Parse an unsigned decimal timestamp without accepting signs, whitespace,
// other characters, or values that overflow uint64_t.
static bool ParsePhoneTimestamp(const String &text, uint64_t &timestampMilliseconds)
{
    if (text.length() == 0)
    {
        return false;
    }

    const uint64_t maximumValue = ~static_cast<uint64_t>(0);
    uint64_t parsedValue = 0;

    for (size_t index = 0; index < text.length(); ++index)
    {
        const char character = text.charAt(index);
        if (character < '0' || character > '9')
        {
            return false;
        }

        const uint64_t digit = static_cast<uint64_t>(character - '0');
        if (parsedValue > (maximumValue - digit) / 10ULL)
        {
            return false;
        }

        parsedValue = parsedValue * 10ULL + digit;
    }

    timestampMilliseconds = parsedValue;
    return true;
}

static void HandlePhoneTimestamp()
{
    if (!httpsServer.hasArg("timestamp"))
    {
        httpsServer.send(400, "text/plain", "Missing timestamp field");
        return;
    }

    uint64_t timestampMilliseconds = 0;
    if (!ParsePhoneTimestamp(httpsServer.arg("timestamp"), timestampMilliseconds))
    {
        httpsServer.send(400, "text/plain", "Timestamp must be unsigned Unix milliseconds");
        return;
    }

    if (!Time_IsTimestampReasonable(timestampMilliseconds))
    {
        httpsServer.send(400, "text/plain", "Timestamp is outside the supported 2020-2100 range");
        return;
    }

    if (!Time_Synchronization(timestampMilliseconds))
    {
        httpsServer.send(500, "text/plain", "ESP32 clock synchronization failed");
        return;
    }

    httpsServer.send(200, "text/plain", "Clock synchronized");
}


static void HandleNotFound()
{
    httpsServer.sendHeader(
        "Location",
        String("https://") + WiFi.softAPIP().toString() + "/",
        true
    );

    httpsServer.send(302, "text/plain", "");
}


void CaptivePortal_Initialization()
{
    IPAddress apIP = WiFi.softAPIP();

    // Redirect DNS lookups to the ESP32.
    dnsServer.start(
        DNS_PORT,
        "*",
        apIP
    );

    // Plain HTTP is used only to download/install the CA certificate and
    // guide users to the secure portal. Geolocation is served on HTTPS only.
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

    httpsServer.on(
        "/location",
        HTTP_POST,
        HandleLocation
    );

    httpsServer.on(
        "/timestamp",
        HTTP_POST,
        HandlePhoneTimestamp
    );

    // Captive checks may be retried over HTTPS by the phone.
    httpsServer.on(
        "/generate_204",
        HTTP_GET,
        HandleRoot
    );

    httpsServer.on(
        "/gen_204",
        HTTP_GET,
        HandleRoot
    );

    httpsServer.on(
        "/hotspot-detect.html",
        HTTP_GET,
        HandleRoot
    );

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


void CaptivePortal_HandleClient()
{
    dnsServer.processNextRequest();
    httpServer.handleClient();
    httpsServer.handleClient();
}
