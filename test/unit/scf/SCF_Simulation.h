#pragma once

#include "../../../src/Store Carry Forward/SCF_Radio_Adapter.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace scf_sim
{
struct Contact
{
    uint32_t from;
    uint32_t to;
    uint32_t start;
    uint32_t end;
    uint32_t latency;
    uint32_t dropEvery;
    bool dropFirst;
};

struct MessageMetrics
{
    scf::BundleId bundleId;
    uint32_t generated;
    uint32_t dataTransmissions;
    uint32_t forwarded;
    uint32_t delivered;
    uint32_t duplicateReceptions;
    uint32_t dropped;
    uint32_t expired;
    uint32_t retransmissions;
    uint32_t deliveryLatency;
    uint8_t hopCount;
    bool linkAcknowledged;
};

struct AggregateMetrics
{
    uint32_t generated;
    uint32_t delivered;
    uint32_t dropped;
    uint32_t expired;
    uint32_t retransmissions;
    uint32_t duplicateReceptions;
    uint32_t forwardingTransmissions;
    uint32_t dataTransmissions;
    uint32_t deliveryLatencyTotal;
    uint32_t occupancySamples;
    uint32_t occupancyTotal;
    double packetDeliveryRatio;
    double averageDeliveryDelay;
    double forwardingOverhead;
    double averageBufferOccupancy;
};

struct SimulationResult
{
    std::vector<MessageMetrics> messages;
    AggregateMetrics aggregate;
    std::string csv;
};

class Simulation
{
public:
    Simulation(uint32_t endTime, uint32_t tickMs = 10);

    void addNode(uint32_t nodeId);
    void addContact(const Contact &contact);
    bool generate(uint32_t nodeId, const scf::Message &message);
    SimulationResult runDtn(uint32_t sourceId, uint32_t destinationId,
                            const scf::BundleId &bundleId);
    SimulationResult runDirectOnly(uint32_t sourceId, uint32_t destinationId,
                                   const scf::BundleId &bundleId);
    static void exportCsv(const SimulationResult &result, std::ostream &output);
    static void exportAggregateCsv(const SimulationResult &result,
                                   std::ostream &output);

private:
    struct VirtualPacket
    {
        uint32_t from;
        uint32_t to;
        uint32_t deliverAt;
        uint32_t order;
        size_t length;
        uint8_t bytes[scf::RADIO_FRAME_CAPACITY];
    };

    class Network;
    class FakeRadio;
    struct Node;

    struct ScenarioState
    {
        uint32_t sourceId;
        uint32_t destinationId;
        scf::BundleId bundleId;
        bool directOnly;
        std::map<uint64_t, MessageMetrics> metrics;
        std::map<uint64_t, uint32_t> generatedAt;
        std::map<uint64_t, uint32_t> deliveredAt;
        uint32_t dataPacketsDropped;
        uint32_t feedbackPackets;
        uint32_t occupancySamples;
        uint32_t occupancyTotal;
    };

    uint32_t endTime;
    uint32_t tickMs;
    std::vector<uint32_t> nodeIds;
    std::vector<Contact> contacts;
    std::vector<scf::Message> generatedMessages;
    SimulationResult execute(uint32_t sourceId, uint32_t destinationId,
                             const scf::BundleId &bundleId,
                             bool directOnly);
};
}
