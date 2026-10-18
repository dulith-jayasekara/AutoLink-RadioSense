#include "../../../src/Store Carry Forward/SCF_Scheduler.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
scf::Message makeMessage(uint32_t sourceId, uint32_t sequence,
                         scf::Priority priority = scf::Priority::Normal,
                         uint32_t expiresAt = 1000,
                         uint8_t hopCount = 0,
                         uint8_t hopLimit = 4)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {sourceId, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = priority;
    message.hopCount = hopCount;
    message.hopLimit = hopLimit;
    return message;
}

void testDisconnectionRetentionAndEncounterSelection()
{
    scf::Scheduler scheduler;
    assert(scheduler.enqueue(makeMessage(1, 1, scf::Priority::Low)) ==
           scf::Scheduler::EnqueueResult::Enqueued);
    assert(scheduler.enqueue(makeMessage(1, 2, scf::Priority::Critical)) ==
           scf::Scheduler::EnqueueResult::Enqueued);

    scf::Scheduler::SendRequest request = {};
    assert(scheduler.bundleCount() == 2);
    assert(scheduler.encounter(7, 10, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(request.bundleId.sequence == 2);
    assert(!request.retransmission);
    assert(request.peerId == 7);
    assert(scheduler.bundleCount() == 2);
}

void testDuplicatePeerAttemptAndDifferentPeer()
{
    scf::Scheduler scheduler;
    const scf::Message message = makeMessage(2, 1);
    assert(scheduler.enqueue(message) == scf::Scheduler::EnqueueResult::Enqueued);

    scf::Scheduler::SendRequest request = {};
    assert(scheduler.encounter(7, 10, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(scheduler.encounter(7, 11, request) ==
           scf::Scheduler::EncounterResult::NoEligibleBundle);
    assert(scheduler.encounter(8, 11, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(request.peerId == 8);
}

void testAckAndNackLifecycle()
{
    scf::Scheduler scheduler(1, 100);
    const scf::Message message = makeMessage(3, 1);
    assert(scheduler.enqueue(message) == scf::Scheduler::EnqueueResult::Enqueued);

    scf::Scheduler::SendRequest request = {};
    assert(scheduler.encounter(7, 10, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(scheduler.acknowledge(message.bundleId, 8) ==
           scf::Reliability::FeedbackResult::WrongPeer);
    assert(scheduler.reject(message.bundleId, 7) ==
           scf::Reliability::FeedbackResult::RetryDue);
    assert(scheduler.encounter(7, 11, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(request.retransmission);
    assert(request.retransmissionCount == 1);
    assert(scheduler.retrySent(request, 11));
    assert(scheduler.acknowledge(message.bundleId, 7) ==
           scf::Reliability::FeedbackResult::Acknowledged);
    assert(scheduler.encounter(7, 12, request) ==
           scf::Scheduler::EncounterResult::NoEligibleBundle);
}

void testTimeoutRetryAndFailureRetention()
{
    scf::Scheduler scheduler(1, 100);
    const scf::Message message = makeMessage(4, 1);
    assert(scheduler.enqueue(message) == scf::Scheduler::EnqueueResult::Enqueued);
    scf::Scheduler::SendRequest request = {};
    assert(scheduler.encounter(7, 10, request) ==
           scf::Scheduler::EncounterResult::SendReady);

    assert(scheduler.update(110, request) == scf::Scheduler::TickResult::RetryDue);
    assert(request.retransmission);
    assert(scheduler.retrySent(request, 110));
    assert(scheduler.update(210, request) == scf::Scheduler::TickResult::Failed);
       assert(request.message == nullptr);
    assert(scheduler.bundleCount() == 1);
    assert(scheduler.releasePeer(message.bundleId, 7));
    assert(scheduler.encounter(7, 211, request) ==
           scf::Scheduler::EncounterResult::SendReady);
}

void testExpiryAndHopLimitValidation()
{
    scf::Scheduler scheduler;
    assert(scheduler.enqueue(makeMessage(5, 1, scf::Priority::Normal, 20)) ==
           scf::Scheduler::EnqueueResult::Enqueued);
    assert(scheduler.enqueue(makeMessage(5, 2, scf::Priority::Normal, 100, 5, 4)) ==
           scf::Scheduler::EnqueueResult::Invalid);

    scf::Scheduler::SendRequest request = {};
    assert(scheduler.encounter(7, 20, request) ==
           scf::Scheduler::EncounterResult::NoEligibleBundle);
    assert(scheduler.bundleCount() == 0);
}

void testBufferSaturation()
{
    scf::Scheduler scheduler;
    for (size_t index = 1; index <= scf::Buffer::CAPACITY; ++index)
    {
        assert(scheduler.enqueue(makeMessage(6, static_cast<uint32_t>(index))) ==
               scf::Scheduler::EnqueueResult::Enqueued);
    }

    assert(scheduler.enqueue(makeMessage(6, 100)) ==
           scf::Scheduler::EnqueueResult::Full);
    assert(scheduler.enqueue(makeMessage(6, 1)) ==
           scf::Scheduler::EnqueueResult::Duplicate);
}

void testTimestampWrapBoundary()
{
    scf::Scheduler scheduler;
    const scf::Message message = makeMessage(7, 1, scf::Priority::Normal, 0x00000100U);
    assert(scheduler.enqueue(message) == scf::Scheduler::EnqueueResult::Enqueued);

    scf::Scheduler::SendRequest request = {};
    assert(scheduler.encounter(9, 0xFFFFFFF0U, request) ==
           scf::Scheduler::EncounterResult::SendReady);
    assert(scheduler.expire(0x00000054U) == 0);
    assert(scheduler.expire(0x00000100U) == 1);
}
}

int main()
{
    testDisconnectionRetentionAndEncounterSelection();
    testDuplicatePeerAttemptAndDifferentPeer();
    testAckAndNackLifecycle();
    testTimeoutRetryAndFailureRetention();
    testExpiryAndHopLimitValidation();
    testBufferSaturation();
    testTimestampWrapBoundary();

    std::cout << "SCF scheduler unit tests passed.\n";
    return 0;
}
