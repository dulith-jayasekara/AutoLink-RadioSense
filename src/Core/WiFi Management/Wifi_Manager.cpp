#include "Library_Headers.h"
#include "Project_Headers.h"

// Single storage for the project-wide identifier declared in Config.h.
String NODE_ID;

void WiFi_Initialization()
{
    // Configure ESP32 as a Wi-Fi Access Point
    WiFi.mode(WIFI_AP);

    // Retrieve the MAC address of the SoftAP interface
    NODE_ID = WiFi.softAPmacAddress();
    Serial.println("Node ID: " + NODE_ID);

    // Start broadcasting the WLAN
    bool wifiStatus = WiFi.softAP(
        WIFI_SSID,
        WIFI_PASSWORD,
        WIFI_CHANNEL,      // Wi-Fi channel
        WIFI_VISIBLE,      // WLAN visible
        WIFI_MAX_CLIENTS       // Maximum connected clients
    );

    if (wifiStatus){
        Serial.println();
        Serial.println("================================");
        Serial.println(" AutoLink RadioSense Wi-Fi");
        Serial.println("================================");

        Serial.print("Node ID: ");
        Serial.println(NODE_ID);

        Serial.print("Wi-Fi SSID: ");
        Serial.println(WIFI_SSID);

        Serial.print("IP Address: ");
        Serial.println(WiFi.softAPIP());

        Serial.print("Maximum clients: ");
        Serial.println(WIFI_MAX_CLIENTS);

        Serial.println("Wi-Fi Access Point: ACTIVE");
        Serial.println("================================");
    }

    else
    {
        Serial.println("ERROR: Wi-Fi Access Point initialization failed!");
    }
}
