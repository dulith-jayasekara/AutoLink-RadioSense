#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"

// --------------------------------------------------
// Raw LoRa Reception System Test
// AutoLink RadioSense
// --------------------------------------------------

// Set this value to the number of packets to collect before analysis.
constexpr size_t PREDEFINED_PACKET_COUNT = 10;
static_assert(PREDEFINED_PACKET_COUNT > 0, "At least one packet must be collected.");

struct PacketRecord
{
    String payload;
    int packetSize;
    unsigned long receptionTime;
    int rssi;
    float snr;
};

PacketRecord packetRecords[PREDEFINED_PACKET_COUNT];
size_t packetCount = 0;
unsigned long testStartTime = 0;
bool testComplete = false;

void printPacket(const PacketRecord &packet, size_t packetNumber);
void printStatistics();

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" AutoLink RadioSense");
    Serial.println(" System Test 02 - Raw Reception");
    Serial.println("========================================");
    Serial.println();

    LoRa_Initialization();

    testStartTime = millis();
    LoRa.receive();

    Serial.println("LoRa receiver started.");
    Serial.print("Collecting ");
    Serial.print(PREDEFINED_PACKET_COUNT);
    Serial.println(" packets before analysis.");
    Serial.println("Waiting for raw packets...");
    Serial.println();
}

void loop()
{
    if (testComplete)
    {
        delay(1000);
        return;
    }

    int packetSize = LoRa.parsePacket();

    if (packetSize)
    {
        const unsigned long receptionTime = millis();
        String payload;
        payload.reserve(packetSize);

        while (LoRa.available())
        {
            payload += static_cast<char>(LoRa.read());
        }

        PacketRecord &packet = packetRecords[packetCount];
        packet.payload = payload;
        packet.packetSize = packetSize;
        packet.receptionTime = receptionTime;
        packet.rssi = LoRa.packetRssi();
        packet.snr = LoRa.packetSnr();
        ++packetCount;

        printPacket(packet, packetCount);

        if (packetCount == PREDEFINED_PACKET_COUNT)
        {
            LoRa.sleep();
            printStatistics();
            testComplete = true;
        }
    }
}

void printPacket(const PacketRecord &packet, size_t packetNumber)
{
    Serial.println("----------------------------------------");
    Serial.print("PACKET ");
    Serial.print(packetNumber);
    Serial.print(" OF ");
    Serial.println(PREDEFINED_PACKET_COUNT);

    Serial.print("Payload        : ");
    Serial.println(packet.payload);

    Serial.print("Packet size    : ");
    Serial.print(packet.packetSize);
    Serial.println(" bytes");

    Serial.print("Reception time : ");
    Serial.print(packet.receptionTime);
    Serial.println(" ms");

    Serial.print("RSSI           : ");
    Serial.print(packet.rssi);
    Serial.println(" dBm");

    Serial.print("SNR            : ");
    Serial.print(packet.snr, 2);
    Serial.println(" dB");
    Serial.println("----------------------------------------");
    Serial.println();
}

void printStatistics()
{
    double totalRSSI = 0;
    double totalRSSISquared = 0;
    double totalSNR = 0;
    double totalSNRSquared = 0;
    uint64_t totalBytesReceived = 0;
    uint64_t totalInterPacketInterval = 0;

    for (size_t i = 0; i < packetCount; ++i)
    {
        const PacketRecord &packet = packetRecords[i];
        totalRSSI += packet.rssi;
        totalRSSISquared += static_cast<double>(packet.rssi) * packet.rssi;
        totalSNR += packet.snr;
        totalSNRSquared += static_cast<double>(packet.snr) * packet.snr;
        totalBytesReceived += packet.packetSize;

        if (i > 0)
        {
            totalInterPacketInterval +=
                packet.receptionTime - packetRecords[i - 1].receptionTime;
        }
    }

    const double averageRSSI = totalRSSI / packetCount;
    const double averageSNR = totalSNR / packetCount;
    double rssiVariance = (totalRSSISquared / packetCount) - (averageRSSI * averageRSSI);
    double snrVariance = (totalSNRSquared / packetCount) - (averageSNR * averageSNR);

    if (rssiVariance < 0)
    {
        rssiVariance = 0;
    }

    if (snrVariance < 0)
    {
        snrVariance = 0;
    }

    const double rssiVariation = sqrt(rssiVariance);
    const double snrVariation = sqrt(snrVariance);
    const double averageInterPacketInterval =
        packetCount > 1
            ? static_cast<double>(totalInterPacketInterval) / (packetCount - 1)
            : 0;
    const unsigned long elapsedTime =
        packetRecords[packetCount - 1].receptionTime - testStartTime;
    const double throughput =
        elapsedTime > 0
            ? (static_cast<double>(totalBytesReceived) * 8.0 * 1000.0) / elapsedTime
            : 0;

    Serial.println("============== STATISTICS ==============");

    Serial.print("Total packets received       : ");
    Serial.println(packetCount);

    Serial.print("Average RSSI                 : ");
    Serial.print(averageRSSI, 2);
    Serial.println(" dBm");

    Serial.print("RSSI variation               : ");
    Serial.print(rssiVariation, 2);
    Serial.println(" dB");

    Serial.print("Average SNR                  : ");
    Serial.print(averageSNR, 2);
    Serial.println(" dB");

    Serial.print("SNR variation                : ");
    Serial.print(snrVariation, 2);
    Serial.println(" dB");

    Serial.print("Average inter-packet interval: ");
    if (packetCount > 1)
    {
        Serial.print(averageInterPacketInterval, 2);
        Serial.println(" ms");
    }
    else
    {
        Serial.println("N/A");
    }

    Serial.print("Throughput                   : ");
    Serial.print(throughput, 2);
    Serial.println(" bit/s");

    Serial.println("========================================");
    Serial.println("Packet collection complete; LoRa radio is asleep.");
}
