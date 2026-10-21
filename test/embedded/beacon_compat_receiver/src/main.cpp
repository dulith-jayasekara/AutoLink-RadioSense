#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "BeaconDecoder.h"

#ifndef BEACON_CRC_ENABLED
#error Set BEACON_CRC_ENABLED explicitly in the diagnostic configuration
#endif

namespace
{
bool ready = false;
uint32_t received = 0;
uint32_t decoded = 0;
uint32_t rejected = 0;
bool havePrevious = false;
uint32_t previousReception = 0;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("=== BEACON COMPATIBILITY RECEIVER / RECEIVE ONLY ===");
    Serial.println("CRC provisional: confirm teammate initialization before hardware testing.");
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    pinMode(26, INPUT);
    SPI.begin(18, 19, 23, 5);
    LoRa.setPins(5, 14, 26);
    ready = LoRa.begin(433000000L);
    Serial.println(ready ? "PASS: SX1278 initialized" : "FAIL: SX1278 initialization; reset after checking wiring");
    if (!ready) return;
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125000L);
    LoRa.setCodingRate4(5);
    LoRa.setSyncWord(0x12);
    LoRa.setPreambleLength(8);
#if BEACON_CRC_ENABLED
    LoRa.enableCrc();
    Serial.println("CRC configuration: enabled");
#else
    LoRa.disableCrc();
    Serial.println("CRC configuration: disabled");
#endif
    Serial.println("433MHz SF7 BW125kHz CR4/5 sync=0x12 preamble=8 explicit header");
    Serial.println("Waiting for real RF; no SCF buffering, RF transmission, or ACKs.");
}

void loop()
{
    if (!ready) return;
    const int packetSize = LoRa.parsePacket();
    if (packetSize <= 0) return;
    ++received;
    const uint32_t now = millis();
    Serial.println("=== PASS: RECEIVED REAL RF PACKET (decode pending) ===");
    Serial.printf("Packet=%lu length=%d RSSI=%d dBm SNR=%.2f dB\n",
                  static_cast<unsigned long>(received), packetSize, LoRa.packetRssi(), LoRa.packetSnr());
    if (havePrevious)
        Serial.printf("Observed RF interval=%lu ms (nominal 5000; all senders/packets combined)\n",
                      static_cast<unsigned long>(now - previousReception));
    previousReception = now;
    havePrevious = true;
    uint8_t bytes[beacon::PACKET_SIZE] = {};
    size_t length = 0;
    bool readError = false;
    Serial.print("Raw HEX:");
    while (LoRa.available())
    {
        const int value = LoRa.read();
        if (value < 0) { readError = true; break; }
        Serial.printf(" %02X", value);
        if (length < sizeof(bytes)) bytes[length] = static_cast<uint8_t>(value);
        ++length;
    }
    Serial.println();
    beacon::Packet packet = {};
    const beacon::Result result = !readError && length == static_cast<size_t>(packetSize)
        ? beacon::decode(bytes, length, packet) : beacon::Result::Length;
    if (result != beacon::Result::Ok)
    {
        ++rejected;
        Serial.printf("FAIL: beacon rejected: %s\n", beacon::description(result));
    }
    else
    {
        ++decoded;
        Serial.printf("MAC=%02X:%02X:%02X:%02X:%02X:%02X sequence=%u\n",
                      packet.nodeId[0], packet.nodeId[1], packet.nodeId[2],
                      packet.nodeId[3], packet.nodeId[4], packet.nodeId[5], packet.sequence);
        Serial.printf("Latitude=%.7f longitude=%.7f degrees\n",
                      packet.latitude / 10000000.0, packet.longitude / 10000000.0);
        Serial.printf("Speed=%u cm/s (%.2f m/s) heading=%u degrees timestamp=%lu Unix seconds\n",
                      packet.speed, packet.speed / 100.0, packet.heading,
                      static_cast<unsigned long>(packet.timestamp));
        if (packet.timestamp == 0) Serial.println("INFO: timestamp unsynchronized (0); accepted");
        if (packet.latitude == 0 && packet.longitude == 0)
            Serial.println("INFO: (0,0) is valid numerically; GPS fix status is unknown");
        Serial.println("PASS: EXACT 25-BYTE BEACON DECODED AND FIELDS VALIDATED");
    }
    Serial.printf("Totals: RF=%lu decoded=%lu rejected=%lu\n",
                  static_cast<unsigned long>(received), static_cast<unsigned long>(decoded),
                  static_cast<unsigned long>(rejected));
}
