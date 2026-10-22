#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <string>
#define PROGMEM
class String
{
    std::string value;
public:
    String() {}
    String(const char *text) : value(text) {}
    String(const std::string &text) : value(text) {}
    String(uint32_t number) : value(std::to_string(number)) {}
    size_t length() const { return value.size(); }
    char operator[](size_t index) const { return value[index]; }
    const char *c_str() const { return value.c_str(); }
    bool operator!=(const String &other) const { return value != other.value; }
    bool operator!=(const char *other) const { return value != other; }
};
class Print
{
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t value) = 0;
    size_t print(const char *text) { size_t size = 0; while (*text) size += write(static_cast<uint8_t>(*text++)); return size; }
    size_t println(const char *text) { return print(text) + write('\n'); }
    size_t printf(const char *format, ...)
    {
        char buffer[8192]; va_list arguments; va_start(arguments, format);
        const int result = vsnprintf(buffer, sizeof(buffer), format, arguments); va_end(arguments);
        if (result < 0) return 0;
        return print(buffer);
    }
};
class SerialStub : public Print
{
public:
    std::string output;
    size_t write(uint8_t value) override { output += static_cast<char>(value); return 1; }
};
extern SerialStub Serial;
uint32_t millis();
class IPAddress
{
    std::string value;
public:
    IPAddress(int a, int b, int c, int d) : value(std::to_string(a)+"."+std::to_string(b)+"."+std::to_string(c)+"."+std::to_string(d)) {}
    String toString() const { return String(value); }
};
