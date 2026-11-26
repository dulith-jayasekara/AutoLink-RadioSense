/**
 * @file Portal_Request_Handlers.h
 * @brief Public internal interface for captive portal browser request handlers.
 * @details Declarations are consumed by Captive_Portal.cpp for endpoint registration.
 */

#ifndef PORTAL_REQUEST_HANDLERS_H
#define PORTAL_REQUEST_HANDLERS_H

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

// Internal access to server objects owned by the captive portal module.
ESPWebServer &Portal_GetHttpServer();
ESPWebServerSecure &Portal_GetHttpsServer();

// HTTP and HTTPS request handler declarations.
void HandleRoot();
void HandleHttpLanding();
void HandleDownloadPortalCA();
void HandleDownloadAppleProfile();
void HandleLocation();
void HandlePhoneTimestamp();
void HandleNotFound();

#endif
