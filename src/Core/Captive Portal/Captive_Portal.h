/**
 * @file Captive_Portal.h
 * @brief Public interface for captive portal server management.
 * @details Initializes and processes HTTP, HTTPS, and DNS server clients.
 * Relationships: Used by main.cpp and implemented in Captive_Portal.cpp.
 */

#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

// Initialize the DNS, HTTP, and HTTPS portal infrastructure.
void CaptivePortal_Initialization();

// Process pending DNS, HTTP, and HTTPS client requests.
void CaptivePortal_HandleClient();

#endif
