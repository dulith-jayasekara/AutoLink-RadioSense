/**
 * @file Portal_Location_Page.h
 * @brief HTTPS location and timestamp portal page for the captive portal module.
 * @details Preserves the existing HTML and JavaScript used by the phone browser.
 * Relationships: Included by Portal_Request_Handlers.cpp.
 */

#pragma once

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

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

  <button id="locationButton" onclick="toggleLocationSharing()">
    Start location updates
  </button>
  <p id="locationStatus">Waiting for location permission.</p>

  <script>
    let locationWatchId = null;
    let locationSharingActive = false;
    let locationRequestInFlight = false;
    let pendingLocationPosition = null;
    let lastQueuedFixTimestamp = 0;

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

    function toggleLocationSharing() {
      const locationStatus = document.getElementById("locationStatus");
      const locationButton = document.getElementById("locationButton");

      if (locationWatchId !== null) {
        locationSharingActive = false;
        pendingLocationPosition = null;
        navigator.geolocation.clearWatch(locationWatchId);
        locationWatchId = null;
        locationButton.textContent = "Start location updates";
        locationStatus.textContent = "Location updates stopped.";
        return;
      }

      if (!window.isSecureContext || !("geolocation" in navigator)) {
        locationStatus.textContent =
          "Secure location access is unavailable. Open the trusted HTTPS portal.";
        return;
      }

      locationStatus.textContent = "Requesting location permission...";
      locationButton.textContent = "Stop location updates";
      locationSharingActive = true;

      locationWatchId = navigator.geolocation.watchPosition(
        function(position) {
          queueLocationUpdate(position);
        },
        function(error) {
          if (!locationSharingActive) {
            return;
          }
          locationStatus.textContent =
            "Location unavailable or permission denied. Error: " +
            error.message;
        },
        {
          enableHighAccuracy: true,
          timeout: 30000,
          maximumAge: 0
        }
      );
    }

    async function queueLocationUpdate(position) {
      if (!locationSharingActive) {
        return;
      }

      const fixTimestamp = Math.floor(position.timestamp);
      if (fixTimestamp <= lastQueuedFixTimestamp) {
        return;
      }
      lastQueuedFixTimestamp = fixTimestamp;

      if (locationRequestInFlight) {
        // Keep only the newest fix while the current HTTPS request completes.
        pendingLocationPosition = position;
        return;
      }

      locationRequestInFlight = true;
      let nextPosition = position;
      try {
        while (nextPosition !== null && locationSharingActive) {
          pendingLocationPosition = null;
          await sendLocationUpdate(nextPosition);
          nextPosition = pendingLocationPosition;
        }
      } finally {
        locationRequestInFlight = false;
      }
    }

    async function sendLocationUpdate(position) {
      const locationStatus = document.getElementById("locationStatus");
      const latitude = position.coords.latitude;
      const longitude = position.coords.longitude;
      const accuracy = position.coords.accuracy;
      const speed = position.coords.speed;
      const heading = position.coords.heading;

      locationStatus.textContent = "Sending location to vehicle node...";

      const body = new URLSearchParams({
        latitude: latitude,
        longitude: longitude,
        accuracy: accuracy,
        speed: Number.isFinite(speed) ? speed : "null",
        heading: Number.isFinite(heading) ? heading : "null",
        fixTimestamp: String(Math.floor(position.timestamp))
      });

      const controller = new AbortController();
      const timeoutId = setTimeout(function() {
        controller.abort();
      }, 10000);

      try {
        const response = await fetch("/location", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded"
          },
          body: body,
          signal: controller.signal
        });
        await response.text();

        if (!locationSharingActive) {
          return;
        }

        if (response.ok) {
          const motionMessage =
            "Speed: " + (Number.isFinite(speed)
              ? (speed * 3.6).toFixed(2) + " km/h"
              : "unavailable") +
            "; heading: " + (Number.isFinite(heading)
              ? heading.toFixed(1) + " degrees"
              : "unavailable") + ".";
          const accuracyMessage =
            "Reported accuracy: " + accuracy.toFixed(1) + " m.";
          locationStatus.textContent = accuracy <= 1
            ? "Location update received. " + motionMessage + " " +
              accuracyMessage + " Reported estimate is within the 1 m target."
            : "Location update received. " + motionMessage + " " +
              accuracyMessage +
              " Best available fix; above the 1 m target.";
        } else {
          locationStatus.textContent =
            "The vehicle node could not receive the location.";
        }
      } catch (error) {
        if (locationSharingActive) {
          locationStatus.textContent =
            error.name === "AbortError"
              ? "Location request timed out; waiting for the next GPS update."
              : "Could not communicate with the vehicle node.";
        }
      } finally {
        clearTimeout(timeoutId);
      }
    }

    // The script is placed at the end of the page, so the status elements exist.
    // Synchronize once whenever this HTTPS portal page loads.
    synchronizePhoneTime();
  </script>
</body>
</html>
)rawliteral";
