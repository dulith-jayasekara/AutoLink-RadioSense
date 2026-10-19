#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "SCF_Codec.h"
#include "SCF_Message.h"
#include "SCF_Radio_Adapter.h"

namespace
{
constexpr uint32_t LOCAL_NODE_ID = 1;
constexpr uint32_t SIMULATED_PEER_ID = 2;

uint32_t passed = 0;
uint32_t failed = 0;

void check(bool condition, const char *name)
{
    if (condition)
    {
        ++passed;
        Serial.print("PASS: ");
    }
    else
    {
        ++failed;
        Serial.print("FAIL: ");
    }
    Serial.println(name);
}

scf::Message makeHazard(uint32_t sequence, uint32_t expiresAt)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {LOCAL_NODE_ID, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = scf::Priority::High;
    message.hopLimit = 4;
    return message;
}

class SimulatedPeerPort final : public scf::RadioPort
{
public:
    SimulatedPeerPort()
        : queued(false), queuedLength(0), transmitted(false),
          transmittedLength(0), initialized(false)
    {
    }

    bool initialize() override
    {
        initialized = true;
        return true;
    }

    scf::PortReceiveResult receive(uint8_t *frame, size_t capacity,
                                   size_t &length) override
    {
        length = 0;
        if (!queued)
        {
            return scf::PortReceiveResult::NoPacket;
        }
        if (queuedLength > capacity)
        {
            queued = false;
            return scf::PortReceiveResult::Overflow;
        }

        for (size_t index = 0; index < queuedLength; ++index)
        {
            frame[index] = queuedFrame[index];
        }
        length = queuedLength;
        queued = false;
        return scf::PortReceiveResult::Packet;
    }

    bool transmit(const uint8_t *frame, size_t length) override
    {
        if (!initialized || frame == nullptr || length == 0 ||
            length > sizeof(transmittedFrame))
        {
            return false;
        }

        transmittedLength = length;
        for (size_t index = 0; index < length; ++index)
        {
            transmittedFrame[index] = frame[index];
        }
        transmitted = true;
        return true;
    }

    void queueFrame(const uint8_t *frame, size_t length)
    {
        if (length > sizeof(queuedFrame))
        {
            return;
        }
        for (size_t index = 0; index < length; ++index)
        {
            queuedFrame[index] = frame[index];
        }
        queuedLength = length;
        queued = true;
    }

    bool wasTransmitted() const
    {
        return transmitted;
    }

    size_t sentLength() const
    {
        return transmittedLength;
    }

private:
    bool queued;
    size_t queuedLength;
    uint8_t queuedFrame[scf::RADIO_FRAME_CAPACITY];
    bool transmitted;
    size_t transmittedLength;
    uint8_t transmittedFrame[scf::RADIO_FRAME_CAPACITY];
    bool initialized;
};

void testMessageAndDisconnectedRetention()
{
    SimulatedPeerPort port;
    scf::RadioAdapter adapter(port, LOCAL_NODE_ID);
    check(adapter.initialize() == scf::RadioAdapter::InitResult::Initialized,
          "adapter initialized without LoRa hardware calls");

    scf::Message hazard = makeHazard(1, 1000);
    const uint8_t payload[] = {0x48, 0x41, 0x5A, 0x41, 0x52, 0x44};
    check(scf::setPayload(hazard, payload, sizeof(payload)),
          "hazard payload created");
    check(scf::isValidMessage(hazard), "hazard DATA message validated");
    check(adapter.enqueue(hazard) == scf::Scheduler::EnqueueResult::Enqueued,
          "hazard enqueued in real SCF buffer");

    scf::RadioAdapter::OutgoingFrame frame = {};
    check(adapter.poll(50, frame) == scf::RadioAdapter::PollResult::NoPacket,
          "disconnected period has no peer packet");
    check(adapter.bundleCount() == 1,
          "buffer retains hazard while disconnected");
    check(adapter.pendingCount() == 0,
          "no reliability attempt before peer contact");

    check(adapter.prepareEncounter(SIMULATED_PEER_ID, 100, frame) ==
              scf::RadioAdapter::PrepareResult::Ready,
          "simulated peer contact prepares SCF forwarding");
    check(frame.valid && frame.peerId == SIMULATED_PEER_ID,
          "forwarding frame targets simulated peer");
    check(adapter.transmit(frame, 100) == scf::RadioAdapter::TransmitResult::Sent,
          "caller-driven simulated send succeeds without RF");
    check(port.wasTransmitted() && port.sentLength() > 0,
          "fake port observed a non-RF frame submission");
    check(adapter.pendingCount() == 1,
          "reliability tracks the simulated peer attempt");

    uint8_t ack[scf::FEEDBACK_FRAME_SIZE] = {};
    size_t ackLength = 0;
    check(scf::encodeAck(hazard.bundleId, SIMULATED_PEER_ID, ack,
                         sizeof(ack), ackLength),
          "matching ACK encoded for simulated peer");
    port.queueFrame(ack, ackLength);
    check(adapter.poll(110, frame) ==
              scf::RadioAdapter::PollResult::FeedbackHandled,
          "matching ACK processed by real reliability path");
    check(adapter.releasePeer(hazard.bundleId, SIMULATED_PEER_ID),
          "acknowledged peer attempt released explicitly");
    check(adapter.pendingCount() == 0,
          "reliability pending count cleared after release");
}

void testDuplicateSuppression()
{
    SimulatedPeerPort port;
    scf::RadioAdapter receiver(port, SIMULATED_PEER_ID);
    check(receiver.initialize() == scf::RadioAdapter::InitResult::Initialized,
          "receiver adapter initialized");

    const scf::Message hazard = makeHazard(2, 1000);
    uint8_t data[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t dataLength = 0;
    check(scf::encodeData(hazard, SIMULATED_PEER_ID, data, sizeof(data),
                          dataLength),
          "hazard DATA encoded for simulated reception");

    port.queueFrame(data, dataLength);
    scf::RadioAdapter::OutgoingFrame response = {};
    check(receiver.poll(20, response) ==
              scf::RadioAdapter::PollResult::DataAccepted,
          "first DATA accepted by real adapter");
    check(receiver.bundleCount() == 1 && response.valid &&
              response.bytes[2] == scf::ACK_FRAME_TYPE,
          "first DATA retained and ACK prepared");

    port.queueFrame(data, dataLength);
    check(receiver.poll(30, response) ==
              scf::RadioAdapter::PollResult::DuplicateData,
          "duplicate DATA suppressed by real duplicate tracker");
    check(receiver.bundleCount() == 1 && response.valid &&
              response.bytes[2] == scf::ACK_FRAME_TYPE,
          "duplicate DATA does not create another buffer entry");
}

void testSeparateTtlExpiry()
{
    SimulatedPeerPort port;
    scf::RadioAdapter adapter(port, LOCAL_NODE_ID);
    adapter.initialize();
    const scf::Message expiringHazard = makeHazard(3, 200);
    scf::RadioAdapter::OutgoingFrame frame = {};

    check(adapter.enqueue(expiringHazard) ==
              scf::Scheduler::EnqueueResult::Enqueued,
          "separate expiring hazard enqueued");
    check(adapter.bundleCount() == 1, "expiring hazard initially buffered");
    check(adapter.tick(200, frame) == scf::RadioAdapter::TickResult::Waiting,
          "TTL tick completes without blocking or RF");
    check(adapter.bundleCount() == 0,
          "separate expiring hazard removed by real TTL path");
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("===============================================");
    Serial.println(" AutoLink RadioSense Component 4 SCF Lifecycle");
    Serial.println(" REAL ESP32 EXECUTION");
    Serial.println(" SIMULATED PEER CONTACT / ACK INPUT");
    Serial.println(" NO RF TRANSMISSION");
    Serial.println("===============================================");

    testMessageAndDisconnectedRetention();
    testDuplicateSuppression();
    testSeparateTtlExpiry();

    Serial.println();
    Serial.print("TOTAL PASSED: ");
    Serial.println(passed);
    Serial.print("TOTAL FAILED: ");
    Serial.println(failed);
    Serial.println(failed == 0 ? "SCF LIFECYCLE DEMO PASS"
                               : "SCF LIFECYCLE DEMO FAIL");
}

void loop()
{
}
