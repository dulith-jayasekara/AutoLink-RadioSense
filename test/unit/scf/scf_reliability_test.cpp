#include "../../../src/Store Carry Forward/SCF_Reliability.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
scf::Message makeMessage(uint32_t sourceId = 1, uint32_t sequence = 1,
                         uint32_t expiresAt = 1000)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {sourceId, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = scf::Priority::Normal;
    message.hopLimit = 4;
    return message;
}

void testAckSuccessAndDuplicateAck()
{
    scf::Reliability reliability(2, 100);
    const scf::Message message = makeMessage();
    assert(reliability.start(message, 7, 10) == scf::Reliability::StartResult::Waiting);
    assert(reliability.acknowledge(message.bundleId, 7) ==
           scf::Reliability::FeedbackResult::Acknowledged);
    assert(reliability.state(message.bundleId, 7) == scf::Reliability::State::Acknowledged);
    assert(reliability.acknowledge(message.bundleId, 7) ==
           scf::Reliability::FeedbackResult::DuplicateAck);
}

void testWrongPeerAndWrongBundleAck()
{
    scf::Reliability reliability;
    const scf::Message message = makeMessage(2, 3);
    assert(reliability.start(message, 7, 10) == scf::Reliability::StartResult::Waiting);
    assert(reliability.acknowledge(message.bundleId, 8) ==
           scf::Reliability::FeedbackResult::WrongPeer);
    assert(reliability.state(message.bundleId, 7) == scf::Reliability::State::Waiting);
    assert(reliability.acknowledge({2, 4}, 7) ==
           scf::Reliability::FeedbackResult::WrongBundle);
}

void testNackAndCallerExecutedRetry()
{
    scf::Reliability reliability(2, 100);
    const scf::Message message = makeMessage();
    scf::Reliability::RetryRequest request = {};
    assert(reliability.start(message, 7, 10) == scf::Reliability::StartResult::Waiting);
    assert(reliability.reject(message.bundleId, 7) ==
           scf::Reliability::FeedbackResult::RetryDue);
    assert(reliability.state(message.bundleId, 7) == scf::Reliability::State::RetryDue);
    assert(request.message == nullptr);
    assert(reliability.retrySent(message.bundleId, 7, 11));
    assert(reliability.state(message.bundleId, 7) == scf::Reliability::State::Waiting);
}

void testTimeoutRetryAndRetryExhaustion()
{
    scf::Reliability reliability(2, 100);
    const scf::Message message = makeMessage();
    scf::Reliability::RetryRequest request = {};
    assert(reliability.start(message, 7, 10) == scf::Reliability::StartResult::Waiting);

    assert(reliability.update(109, request) == scf::Reliability::UpdateResult::Waiting);
    assert(reliability.update(110, request) == scf::Reliability::UpdateResult::RetryDue);
    assert(request.retransmissionCount == 1);
    assert(reliability.retrySent(message.bundleId, 7, 110));
    assert(reliability.update(210, request) == scf::Reliability::UpdateResult::RetryDue);
    assert(request.retransmissionCount == 2);
    assert(reliability.retrySent(message.bundleId, 7, 210));
    assert(reliability.update(310, request) == scf::Reliability::UpdateResult::Failed);
    assert(reliability.state(message.bundleId, 7) == scf::Reliability::State::Failed);
}

void testMaxRetriesCountsOnlyRetransmissions()
{
    scf::Reliability reliability(0, 100);
    const scf::Message message = makeMessage();
    scf::Reliability::RetryRequest request = {};
    assert(reliability.start(message, 7, 10) == scf::Reliability::StartResult::Waiting);
    assert(reliability.update(110, request) == scf::Reliability::UpdateResult::Failed);
    assert(request.retransmissionCount == 0);
}

void testCapacityAndDuplicatePendingEntries()
{
    scf::Reliability reliability;
    for (size_t index = 1; index <= scf::Reliability::CAPACITY; ++index)
    {
        assert(reliability.start(makeMessage(1, static_cast<uint32_t>(index)),
                                 static_cast<uint32_t>(index + 10), 0) ==
               scf::Reliability::StartResult::Waiting);
    }

    assert(reliability.count() == scf::Reliability::CAPACITY);
    assert(reliability.start(makeMessage(99, 1), 99, 0) ==
           scf::Reliability::StartResult::Full);
    assert(reliability.start(makeMessage(1, 1), 11, 0) ==
           scf::Reliability::StartResult::Duplicate);
    assert(reliability.remove({1, 1}, 11));
    assert(reliability.start(makeMessage(99, 1), 99, 0) ==
           scf::Reliability::StartResult::Waiting);
}

void testInvalidInputAndBoundaryTiming()
{
    scf::Reliability reliability(1, 100);
    scf::Reliability::RetryRequest request = {};
    assert(reliability.start(makeMessage(), 0, 10) == scf::Reliability::StartResult::Rejected);
    assert(reliability.start(makeMessage(1, 2, 10), 7, 10) ==
           scf::Reliability::StartResult::Rejected);

       const scf::Message wrappedMessage = makeMessage(4, 1, 0x00000100U);
    assert(reliability.start(wrappedMessage, 7, 0xFFFFFFF0U) ==
           scf::Reliability::StartResult::Waiting);
       assert(reliability.update(0x00000054U, request) ==
           scf::Reliability::UpdateResult::RetryDue);
    assert(request.retransmissionCount == 1);
       assert(reliability.retrySent(wrappedMessage.bundleId, 7, 0x00000054U));
}
}

int main()
{
    testAckSuccessAndDuplicateAck();
    testWrongPeerAndWrongBundleAck();
    testNackAndCallerExecutedRetry();
    testTimeoutRetryAndRetryExhaustion();
    testMaxRetriesCountsOnlyRetransmissions();
    testCapacityAndDuplicatePendingEntries();
    testInvalidInputAndBoundaryTiming();

    std::cout << "SCF reliability unit tests passed.\n";
    return 0;
}
