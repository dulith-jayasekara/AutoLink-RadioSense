#include "../../../src/Store Carry Forward/SCF_Buffer.h"
#include "../../../src/Store Carry Forward/SCF_DuplicateTracker.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
scf::Message makeMessage(uint32_t sourceId, uint32_t sequence,
                         scf::Priority priority = scf::Priority::Normal,
                         uint32_t expiresAt = 100)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {sourceId, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = priority;
    message.hopCount = 0;
    message.hopLimit = 4;
    return message;
}

void testMessageValidationAndPayloadBoundaries()
{
    scf::Message message = makeMessage(1, 1);
    assert(scf::isValidMessage(message));

    uint8_t payload[scf::MAX_PAYLOAD_SIZE] = {};
    assert(scf::setPayload(message, payload, scf::MAX_PAYLOAD_SIZE));
    assert(message.payloadLength == scf::MAX_PAYLOAD_SIZE);
    assert(scf::isValidMessage(message));

    uint8_t extraPayload[scf::MAX_PAYLOAD_SIZE + 1] = {};
    assert(!scf::setPayload(message, extraPayload, sizeof(extraPayload)));

    message = makeMessage(0, 1);
    assert(!scf::isValidMessage(message));
    message = makeMessage(1, 0);
    assert(!scf::isValidMessage(message));
    message = makeMessage(1, 1, scf::Priority::Normal, 0);
    assert(!scf::isValidMessage(message));

    message = makeMessage(1, 1);
    message.hopCount = 5;
    message.hopLimit = 4;
    assert(!scf::isValidMessage(message));

    message = makeMessage(1, 1);
    message.version = 2;
    assert(!scf::isValidMessage(message));
}

void testBufferInsertionLookupAndRemoval()
{
    scf::Buffer buffer;
    const scf::Message message = makeMessage(1, 1);

    assert(buffer.empty());
    assert(buffer.enqueue(message) == scf::Buffer::EnqueueResult::Enqueued);
    assert(buffer.count() == 1);
    assert(buffer.find(message.bundleId) != nullptr);
    assert(buffer.enqueue(message) == scf::Buffer::EnqueueResult::Duplicate);
    assert(buffer.remove(message.bundleId));
    assert(buffer.empty());
    assert(!buffer.remove(message.bundleId));
}

void testBufferCapacityAndFullBehavior()
{
    scf::Buffer buffer;
    for (size_t sequence = 1; sequence <= scf::Buffer::CAPACITY; ++sequence)
    {
        assert(buffer.enqueue(makeMessage(1, static_cast<uint32_t>(sequence))) ==
               scf::Buffer::EnqueueResult::Enqueued);
    }

    assert(buffer.count() == scf::Buffer::CAPACITY);
    assert(buffer.enqueue(makeMessage(1, 100)) == scf::Buffer::EnqueueResult::Full);
    assert(buffer.count() == scf::Buffer::CAPACITY);
}

void testExpiry()
{
    scf::Buffer buffer;
    buffer.enqueue(makeMessage(1, 1, scf::Priority::Normal, 10));
    buffer.enqueue(makeMessage(1, 2, scf::Priority::Normal, 20));

    assert(buffer.selectNext(10) != nullptr);
    assert(buffer.selectNext(10)->bundleId.sequence == 2);
    assert(buffer.expire(10) == 1);
    assert(buffer.count() == 1);
    assert(buffer.expire(20) == 1);
    assert(buffer.empty());
}

void testDeterministicPrioritySelection()
{
    scf::Buffer buffer;
    buffer.enqueue(makeMessage(3, 1, scf::Priority::High, 50));
    buffer.enqueue(makeMessage(2, 2, scf::Priority::Critical, 90));
    buffer.enqueue(makeMessage(1, 3, scf::Priority::Critical, 90));
    buffer.enqueue(makeMessage(4, 4, scf::Priority::Critical, 80));

    const scf::Message *selected = buffer.selectNext(0);
    assert(selected != nullptr);
    assert(selected->bundleId.sourceId == 4);

    buffer.remove(selected->bundleId);
    selected = buffer.selectNext(0);
    assert(selected != nullptr);
    assert(selected->bundleId.sourceId == 1);
}

void testDuplicateTrackerBoundaries()
{
    scf::DuplicateTracker tracker;
    assert(tracker.remember({0, 1}) == scf::DuplicateTracker::RememberResult::Invalid);

    for (size_t sequence = 1; sequence <= scf::DuplicateTracker::CAPACITY; ++sequence)
    {
        assert(tracker.remember({2, static_cast<uint32_t>(sequence)}) ==
               scf::DuplicateTracker::RememberResult::Added);
    }

    assert(tracker.remember({2, 1}) == scf::DuplicateTracker::RememberResult::Duplicate);
    assert(tracker.remember({2, 100}) == scf::DuplicateTracker::RememberResult::Full);
    assert(tracker.count() == scf::DuplicateTracker::CAPACITY);
    tracker.clear();
    assert(tracker.count() == 0);
    assert(tracker.remember({2, 100}) == scf::DuplicateTracker::RememberResult::Added);
}
}

int main()
{
    testMessageValidationAndPayloadBoundaries();
    testBufferInsertionLookupAndRemoval();
    testBufferCapacityAndFullBehavior();
    testExpiry();
    testDeterministicPrioritySelection();
    testDuplicateTrackerBoundaries();

    std::cout << "SCF unit tests passed.\n";
    return 0;
}
