#include <Arduino.h>
#include <stdint.h>

#include "SCF_Buffer.h"
#include "SCF_Codec.h"
#include "SCF_DuplicateTracker.h"
#include "SCF_Message.h"
#include "SCF_Reliability.h"
#include "SCF_Scheduler.h"
#include "SCF_Radio_Adapter.h"

namespace
{
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

scf::Message makeMessage(uint32_t sourceId = 1, uint32_t sequence = 1,
                         uint32_t expiresAt = 1000)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {sourceId, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = scf::Priority::High;
    message.hopLimit = 4;
    return message;
}

void testMessage()
{
    scf::Message message = makeMessage();
    check(scf::isValidMessage(message), "message creation and validation");

    uint8_t payload[scf::MAX_PAYLOAD_SIZE] = {};
    check(scf::setPayload(message, payload, sizeof(payload)),
          "maximum payload accepted");
    check(message.payloadLength == scf::MAX_PAYLOAD_SIZE,
          "maximum payload length recorded");

    scf::Message invalid = makeMessage(0, 1);
    check(!scf::isValidMessage(invalid), "zero source rejected");
    invalid = makeMessage(1, 0);
    check(!scf::isValidMessage(invalid), "zero sequence rejected");
}

void testCodec()
{
    scf::Message original = makeMessage(0x01020304U, 0xA0B0C0D0U);
    const uint8_t payload[] = {0x00, 0x7F, 0x80, 0xFF};
    scf::setPayload(original, payload, sizeof(payload));

    uint8_t frame[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t frameLength = 0;
    check(scf::encodeData(original, 9, frame, sizeof(frame), frameLength),
          "DATA frame encoded");
    check(frame[4] == 0x01 && frame[5] == 0x02 && frame[6] == 0x03 &&
              frame[7] == 0x04,
          "DATA byte order is deterministic");

    scf::Message decoded = {};
    uint32_t destination = 0;
    check(scf::decodeData(frame, frameLength, decoded, destination),
          "DATA frame decoded");
    check(destination == 9 && scf::sameBundle(decoded.bundleId, original.bundleId) &&
              decoded.payloadLength == original.payloadLength,
          "DATA round-trip values preserved");

    check(!scf::decodeData(frame, frameLength - 1, decoded, destination),
          "truncated DATA rejected");
}

void testBufferAndDuplicates()
{
    scf::Buffer buffer;
    for (size_t index = 1; index <= scf::Buffer::CAPACITY; ++index)
    {
        check(buffer.enqueue(makeMessage(1, static_cast<uint32_t>(index))) ==
                  scf::Buffer::EnqueueResult::Enqueued,
              "buffer insertion");
    }
    check(buffer.enqueue(makeMessage(1, 99)) == scf::Buffer::EnqueueResult::Full,
          "buffer full result");
    check(buffer.expire(1000) == scf::Buffer::CAPACITY,
          "buffer TTL expiry");

    scf::DuplicateTracker tracker;
    check(tracker.remember({2, 1}) ==
              scf::DuplicateTracker::RememberResult::Added,
          "duplicate identity remembered");
    check(tracker.remember({2, 1}) ==
              scf::DuplicateTracker::RememberResult::Duplicate,
          "duplicate identity rejected");
}

void testReliabilityAndScheduler()
{
    const scf::Message message = makeMessage();
    scf::Reliability reliability(1, 100);
    scf::Reliability::RetryRequest retry = {};
    check(reliability.start(message, 7, 10) ==
              scf::Reliability::StartResult::Waiting,
          "reliability initial send tracked");
    check(reliability.acknowledge(message.bundleId, 8) ==
              scf::Reliability::FeedbackResult::WrongPeer,
          "wrong peer ACK rejected");
    check(reliability.update(110, retry) ==
              scf::Reliability::UpdateResult::RetryDue,
          "reliability timeout retry due");
    check(reliability.retrySent(message.bundleId, 7, 110),
          "reliability retry recorded");
    check(reliability.acknowledge(message.bundleId, 7) ==
              scf::Reliability::FeedbackResult::Acknowledged,
          "matching ACK accepted");

    scf::Scheduler scheduler;
    check(scheduler.enqueue(makeMessage(3, 1, 50)) ==
              scf::Scheduler::EnqueueResult::Enqueued,
          "scheduler bundle retained");
    scf::Scheduler::SendRequest request = {};
    check(scheduler.encounter(7, 50, request) ==
              scf::Scheduler::EncounterResult::NoEligibleBundle,
          "expired scheduler bundle not selected");
    check(scheduler.bundleCount() == 0, "expired scheduler bundle removed");
}

class DiagnosticPort final : public scf::RadioPort
{
public:
    bool initialize() override { return true; }
    scf::PortReceiveResult receive(uint8_t *, size_t, size_t &length) override
    {
        length = 0;
        return scf::PortReceiveResult::NoPacket;
    }
    bool transmit(const uint8_t *, size_t) override { return true; }
};

void testAdapterWithoutRadio()
{
    DiagnosticPort port;
    scf::RadioAdapter adapter(port, 9);
    check(adapter.initialize() == scf::RadioAdapter::InitResult::Initialized,
          "adapter initialized with non-radio diagnostic port");
    scf::RadioAdapter::OutgoingFrame frame = {};
    check(adapter.poll(0, frame) == scf::RadioAdapter::PollResult::NoPacket,
          "adapter receive polling is non-transmitting");
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("Component 4 SCF embedded diagnostic");
    Serial.println("RF transmission: disabled");
    Serial.println("Real SCF production sources: linked from repository");
    Serial.println();

    testMessage();
    testCodec();
    testBufferAndDuplicates();
    testReliabilityAndScheduler();
    testAdapterWithoutRadio();

    Serial.println();
    Serial.print("TOTAL PASSED: ");
    Serial.println(passed);
    Serial.print("TOTAL FAILED: ");
    Serial.println(failed);
    Serial.println(failed == 0 ? "SCF EMBEDDED DIAGNOSTIC PASS"
                               : "SCF EMBEDDED DIAGNOSTIC FAIL");
}

void loop()
{
}
