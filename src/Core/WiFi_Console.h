#pragma once
#include <Arduino.h>

namespace WiFiConsole
{
using CommandHandler = void (*)(const char *command, Print &output);
void begin(uint32_t node, CommandHandler handler);
void poll();
}
