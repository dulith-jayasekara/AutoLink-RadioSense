#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <string.h>
#include "BeaconDecoder.h"
#include "SCF_Codec.h"

namespace forwarding
{
constexpr uint32_t ORIGIN = 0x54455354U; // Synthetic TEST origin, not Node A MAC.
constexpr uint32_t DESTINATION = 3;
constexpr uint32_t TTL = 300000;
inline bool check(bool ok, const char *label)
{
    Serial.printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    return ok;
}
inline bool initialize()
{
    Serial.begin(115200);
    delay(500);
    pinMode(5, OUTPUT); digitalWrite(5, HIGH); pinMode(26, INPUT);
    SPI.begin(18, 19, 23, 5);
    LoRa.setPins(5, 14, 26);
    if (!check(LoRa.begin(433000000L), "SX1278 initialization")) return false;
    LoRa.setSpreadingFactor(7); LoRa.setSignalBandwidth(125000L);
    LoRa.setCodingRate4(5); LoRa.setSyncWord(0x12); LoRa.setPreambleLength(8);
    LoRa.enableCrc(); LoRa.setTxPower(10);
    Serial.println("433MHz SF7 BW125kHz CR4/5 sync=0x12 preamble=8 CRC=on explicit header");
    Serial.println("CRC must match actual Node A firmware before hardware testing.");
    return true;
}
inline bool receive(uint8_t *bytes, size_t capacity, size_t &length)
{
    const int size = LoRa.parsePacket();
    if (size <= 0) return false;
    Serial.printf("RF bytes=%d RSSI=%d dBm SNR=%.2f dB\n", size, LoRa.packetRssi(), LoRa.packetSnr());
    length = 0;
    bool readOk = true;
    while (LoRa.available())
    {
        const int value = LoRa.read();
        if (value < 0) { readOk = false; break; }
        if (length < capacity) bytes[length] = static_cast<uint8_t>(value);
        ++length;
    }
    return check(readOk && length == static_cast<size_t>(size) && length <= capacity,
                 "complete bounded RF frame");
}
inline void hex(const uint8_t *bytes, size_t length)
{
    Serial.print("Original Beacon HEX:");
    for (size_t i = 0; i < length; ++i) Serial.printf(" %02X", bytes[i]);
    Serial.println();
}
inline void beaconLog(const beacon::Packet &p)
{
    Serial.printf("Beacon MAC=%02X:%02X:%02X:%02X:%02X:%02X sequence=%u\n",
                  p.nodeId[0],p.nodeId[1],p.nodeId[2],p.nodeId[3],p.nodeId[4],p.nodeId[5],p.sequence);
    Serial.printf("lat=%.7f lon=%.7f speed=%u cm/s heading=%u timestamp=%lu%s\n",
                  p.latitude/10000000.0,p.longitude/10000000.0,p.speed,p.heading,
                  static_cast<unsigned long>(p.timestamp),p.timestamp ? "" : " (unsynchronized)");
}
inline void identity(const scf::Message &m)
{
    Serial.printf("Synthetic SCF ID=(%lu,%lu) hop=%u/%u created=%lu expires=%lu\n",
                  static_cast<unsigned long>(m.bundleId.sourceId),static_cast<unsigned long>(m.bundleId.sequence),
                  m.hopCount,m.hopLimit,static_cast<unsigned long>(m.createdAt),static_cast<unsigned long>(m.expiresAt));
}
}
