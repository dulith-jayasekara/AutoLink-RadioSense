#include "SCF_Simulation.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iomanip>
#include <memory>
#include <sstream>

namespace scf_sim
{
namespace
{
uint64_t keyFor(const scf::BundleId &bundleId)
{
    return (static_cast<uint64_t>(bundleId.sourceId) << 32) |
           bundleId.sequence;
}

bool timeInWindow(uint32_t now, const Contact &contact)
{
    return now >= contact.start && now < contact.end;
}

MessageMetrics makeMetrics(const scf::BundleId &bundleId)
{
    MessageMetrics metrics = {};
    metrics.bundleId = bundleId;
    return metrics;
}
}

class Simulation::Network
{
public:
    Network(Simulation &simulation, ScenarioState &scenario,
            const std::vector<Contact> &contactSchedule)
        : owner(simulation), state(scenario), contacts(contactSchedule),
          now(0), nextOrder(0)
    {
    }

    void attach(Node *node)
    {
        nodes.push_back(node);
    }

    void setTime(uint32_t currentTime)
    {
        now = currentTime;
    }

    bool isConnected(uint32_t first, uint32_t second) const;
    bool schedule(uint32_t from, uint32_t target, const uint8_t *frame,
                  size_t length, bool retransmission);
    void deliverDue();

private:
    const Contact *findContact(uint32_t first, uint32_t second) const;
    void recordDataTransmission(const uint8_t *frame, size_t length,
                                uint32_t from, bool retransmission);
    void recordDrop(const uint8_t *frame, size_t length);

    Simulation &owner;
    ScenarioState &state;
    const std::vector<Contact> &contacts;
    std::vector<Node *> nodes;
    std::vector<VirtualPacket> events;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> attempts;
    uint32_t now;
    uint32_t nextOrder;
};

class Simulation::FakeRadio final : public scf::RadioPort
{
public:
    FakeRadio(Network &network, uint32_t nodeId)
        : network(network), nodeId(nodeId), targetId(0), currentTime(0),
          nextRetransmission(false), initialized(false), lastSender(0),
          lastLength(0), lastValid(false)
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
        lastValid = false;
        if (queue.empty())
        {
            return scf::PortReceiveResult::NoPacket;
        }

        const VirtualPacket packet = queue.front();
        queue.pop_front();
        if (packet.length > capacity)
        {
            return scf::PortReceiveResult::Overflow;
        }

        for (size_t index = 0; index < packet.length; ++index)
        {
            frame[index] = packet.bytes[index];
        }
        length = packet.length;
        lastSender = packet.from;
        lastLength = packet.length;
        for (size_t index = 0; index < packet.length; ++index)
        {
            lastFrame[index] = packet.bytes[index];
        }
        lastValid = true;
        return scf::PortReceiveResult::Packet;
    }

    bool transmit(const uint8_t *frame, size_t length) override
    {
        if (!initialized)
        {
            return false;
        }
        return network.schedule(nodeId, targetId, frame, length,
                                nextRetransmission);
    }

    void setTransmit(uint32_t target, uint32_t time, bool retransmission)
    {
        targetId = target;
        currentTime = time;
        nextRetransmission = retransmission;
    }

    void push(const VirtualPacket &packet)
    {
        queue.push_back(packet);
    }

    uint32_t sender() const
    {
        return lastSender;
    }

    const uint8_t *lastBytes() const
    {
        return lastFrame;
    }

    size_t lastSize() const
    {
        return lastLength;
    }

    bool hasLastPacket() const
    {
        return lastValid;
    }

private:
    Network &network;
    uint32_t nodeId;
    uint32_t targetId;
    uint32_t currentTime;
    bool nextRetransmission;
    bool initialized;
    uint32_t lastSender;
    size_t lastLength;
    bool lastValid;
    uint8_t lastFrame[scf::RADIO_FRAME_CAPACITY];
    std::deque<VirtualPacket> queue;
};

struct Simulation::Node
{
    Node(Network &network, uint32_t nodeId)
        : id(nodeId), radio(network, nodeId), adapter(radio, nodeId),
          pending()
    {
        pending.valid = false;
    }

    uint32_t id;
    FakeRadio radio;
    scf::RadioAdapter adapter;
    scf::RadioAdapter::OutgoingFrame pending;
};

const Contact *Simulation::Network::findContact(
    uint32_t first, uint32_t second) const
{
    for (std::vector<Contact>::const_iterator iterator = contacts.begin();
         iterator != contacts.end(); ++iterator)
    {
        if ((iterator->from == first && iterator->to == second) ||
            (iterator->from == second && iterator->to == first))
        {
            return &*iterator;
        }
    }

    return nullptr;
}

bool Simulation::Network::isConnected(uint32_t first, uint32_t second) const
{
    const Contact *contact = findContact(first, second);
    return contact != nullptr && timeInWindow(now, *contact);
}

void Simulation::Network::recordDataTransmission(
    const uint8_t *frame, size_t length, uint32_t from, bool retransmission)
{
    if (length < scf::FRAME_HEADER_SIZE || frame[2] != scf::DATA_FRAME_TYPE)
    {
        return;
    }

    scf::Message message = {};
    uint32_t destination = 0;
    if (!scf::decodeData(frame, length, message, destination))
    {
        return;
    }

    const uint64_t key = keyFor(message.bundleId);
    if (state.metrics.find(key) == state.metrics.end())
    {
        state.metrics[key] = makeMetrics(message.bundleId);
    }
    MessageMetrics &metrics = state.metrics[key];
    ++metrics.dataTransmissions;
    if (from != message.bundleId.sourceId)
    {
        ++metrics.forwarded;
        ++metrics.hopCount;
    }
    if (retransmission)
    {
        ++metrics.retransmissions;
    }
}

void Simulation::Network::recordDrop(const uint8_t *frame, size_t length)
{
    if (length < scf::FRAME_HEADER_SIZE || frame[2] != scf::DATA_FRAME_TYPE)
    {
        return;
    }

    scf::Message message = {};
    uint32_t destination = 0;
    if (scf::decodeData(frame, length, message, destination))
    {
        ++state.metrics[keyFor(message.bundleId)].dropped;
    }
}

bool Simulation::Network::schedule(uint32_t from, uint32_t target,
                                   const uint8_t *frame, size_t length,
                                   bool retransmission)
{
    if (frame == nullptr || length == 0 ||
        length > scf::RADIO_FRAME_CAPACITY ||
        length < scf::FRAME_HEADER_SIZE)
    {
        return false;
    }

    recordDataTransmission(frame, length, from, retransmission);
    const bool feedback = frame[2] == scf::ACK_FRAME_TYPE ||
                          frame[2] == scf::NACK_FRAME_TYPE;
    std::vector<uint32_t> recipients;
    for (std::vector<Node *>::const_iterator iterator = nodes.begin();
         iterator != nodes.end(); ++iterator)
    {
        const uint32_t candidate = (*iterator)->id;
        if (candidate == from)
        {
            continue;
        }
        if (feedback ? isConnected(from, candidate)
                     : candidate == target && isConnected(from, target))
        {
            recipients.push_back(candidate);
        }
    }

    if (recipients.empty())
    {
        recordDrop(frame, length);
        return false;
    }

    const Contact *contact = findContact(from, recipients.front());
    const std::pair<uint32_t, uint32_t> link(from, recipients.front());
    uint32_t &attempt = attempts[link];
    ++attempt;
    if (contact != nullptr &&
        ((contact->dropFirst && attempt == 1) ||
         (contact->dropEvery != 0 && attempt % contact->dropEvery == 0)))
    {
        recordDrop(frame, length);
        return true;
    }

    for (std::vector<uint32_t>::const_iterator iterator = recipients.begin();
         iterator != recipients.end(); ++iterator)
    {
        VirtualPacket packet = {};
        packet.from = from;
        packet.to = *iterator;
        packet.deliverAt = now + (contact == nullptr ? 0 : contact->latency);
        packet.order = nextOrder++;
        packet.length = length;
        for (size_t index = 0; index < length; ++index)
        {
            packet.bytes[index] = frame[index];
        }
        events.push_back(packet);
    }

    return true;
}

void Simulation::Network::deliverDue()
{
    std::stable_sort(events.begin(), events.end(),
                     [](const VirtualPacket &left, const VirtualPacket &right) {
                         if (left.deliverAt != right.deliverAt)
                         {
                             return left.deliverAt < right.deliverAt;
                         }
                         return left.order < right.order;
                     });

    size_t index = 0;
    while (index < events.size())
    {
        if (events[index].deliverAt > now)
        {
            ++index;
            continue;
        }

        const VirtualPacket packet = events[index];
        events.erase(events.begin() + static_cast<ptrdiff_t>(index));
        if (!isConnected(packet.from, packet.to))
        {
            recordDrop(packet.bytes, packet.length);
            continue;
        }

        for (std::vector<Node *>::const_iterator iterator = nodes.begin();
             iterator != nodes.end(); ++iterator)
        {
            if ((*iterator)->id == packet.to)
            {
                (*iterator)->radio.push(packet);
                break;
            }
        }
    }
}

Simulation::Simulation(uint32_t simulationEndTime, uint32_t simulationTickMs)
    : endTime(simulationEndTime), tickMs(simulationTickMs == 0 ? 1 : simulationTickMs), nodeIds(),
      contacts(), generatedMessages()
{
}

void Simulation::addNode(uint32_t nodeId)
{
    if (nodeId == 0 &&
        std::find(nodeIds.begin(), nodeIds.end(), nodeId) == nodeIds.end())
    {
        return;
    }
    if (std::find(nodeIds.begin(), nodeIds.end(), nodeId) == nodeIds.end())
    {
        nodeIds.push_back(nodeId);
    }
}

void Simulation::addContact(const Contact &contact)
{
    contacts.push_back(contact);
}

bool Simulation::generate(uint32_t nodeId, const scf::Message &message)
{
    if (std::find(nodeIds.begin(), nodeIds.end(), nodeId) == nodeIds.end() ||
        nodeId != message.bundleId.sourceId ||
        !scf::isValidMessage(message))
    {
        return false;
    }

    generatedMessages.push_back(message);
    return true;
}

SimulationResult Simulation::runDtn(uint32_t sourceId, uint32_t destinationId,
                                    const scf::BundleId &bundleId)
{
    return execute(sourceId, destinationId, bundleId, false);
}

SimulationResult Simulation::runDirectOnly(
    uint32_t sourceId, uint32_t destinationId, const scf::BundleId &bundleId)
{
    return execute(sourceId, destinationId, bundleId, true);
}

SimulationResult Simulation::execute(uint32_t sourceId, uint32_t destinationId,
                                     const scf::BundleId &bundleId,
                                     bool directOnly)
{
    ScenarioState scenario = {};
    scenario.sourceId = sourceId;
    scenario.destinationId = destinationId;
    scenario.bundleId = bundleId;
    scenario.directOnly = directOnly;

    for (std::vector<scf::Message>::const_iterator iterator =
             generatedMessages.begin();
         iterator != generatedMessages.end(); ++iterator)
    {
        if (scf::sameBundle(iterator->bundleId, bundleId))
        {
            const uint64_t key = keyFor(bundleId);
            scenario.metrics[key] = makeMetrics(bundleId);
            scenario.metrics[key].generated = 1;
            scenario.generatedAt[key] = iterator->createdAt;
        }
    }

    Network network(*this, scenario, contacts);
    std::vector<Node> nodes;
    nodes.reserve(nodeIds.size());
    for (std::vector<uint32_t>::const_iterator iterator = nodeIds.begin();
         iterator != nodeIds.end(); ++iterator)
    {
        nodes.emplace_back(network, *iterator);
        network.attach(&nodes.back());
        assert(nodes.back().adapter.initialize() ==
               scf::RadioAdapter::InitResult::Initialized);
    }

    for (std::vector<Node>::iterator iterator = nodes.begin();
         iterator != nodes.end(); ++iterator)
    {
        if (iterator->id == sourceId)
        {
            for (std::vector<scf::Message>::const_iterator message =
                     generatedMessages.begin();
                 message != generatedMessages.end(); ++message)
            {
                if (scf::sameBundle(message->bundleId, bundleId))
                {
                    assert(iterator->adapter.enqueue(*message) ==
                           scf::Scheduler::EnqueueResult::Enqueued);
                }
            }
        }
    }

    for (uint32_t now = 0; now <= endTime; now += tickMs)
    {
        network.setTime(now);
        network.deliverDue();

        for (std::vector<Node>::iterator iterator = nodes.begin();
             iterator != nodes.end(); ++iterator)
        {
            Node &node = *iterator;
            scf::RadioAdapter::OutgoingFrame response = {};
            const scf::RadioAdapter::PollResult pollResult =
                node.adapter.poll(now, response);
            if (node.radio.hasLastPacket())
            {
                scf::Message received = {};
                uint32_t destination = 0;
                if (node.radio.lastSize() >= scf::FRAME_HEADER_SIZE &&
                    node.radio.lastBytes()[2] == scf::DATA_FRAME_TYPE &&
                    scf::decodeData(node.radio.lastBytes(), node.radio.lastSize(),
                                    received, destination) &&
                    scf::sameBundle(received.bundleId, bundleId))
                {
                    MessageMetrics &metrics = scenario.metrics[keyFor(bundleId)];
                    if (pollResult == scf::RadioAdapter::PollResult::DataAccepted &&
                        node.id == destinationId)
                    {
                        ++metrics.delivered;
                        metrics.deliveryLatency =
                            now - scenario.generatedAt[keyFor(bundleId)];
                        metrics.hopCount = received.hopCount;
                    }
                    else if (pollResult ==
                             scf::RadioAdapter::PollResult::DuplicateData)
                    {
                        ++metrics.duplicateReceptions;
                    }
                }
                else if (node.radio.lastSize() == scf::FEEDBACK_FRAME_SIZE &&
                         (node.radio.lastBytes()[2] == scf::ACK_FRAME_TYPE ||
                          node.radio.lastBytes()[2] == scf::NACK_FRAME_TYPE))
                {
                    scf::FeedbackFrame feedback = {};
                    if (scf::decodeFeedback(node.radio.lastBytes(),
                                            node.radio.lastSize(), feedback) &&
                        scf::sameBundle(feedback.bundleId, bundleId) &&
                        feedback.type == scf::ACK_FRAME_TYPE)
                    {
                        scenario.metrics[keyFor(bundleId)].linkAcknowledged = true;
                    }
                }
            }

            if (pollResult == scf::RadioAdapter::PollResult::NackReady &&
                response.valid && response.length == scf::FEEDBACK_FRAME_SIZE &&
                response.bytes[16] == static_cast<uint8_t>(scf::NackReason::Expired))
            {
                ++scenario.metrics[keyFor(bundleId)].expired;
            }

            if (response.valid)
            {
                node.radio.setTransmit(response.peerId, now,
                                       response.retransmission);
                node.adapter.transmit(response, now);
            }
        }

        for (std::vector<Node>::iterator iterator = nodes.begin();
             iterator != nodes.end(); ++iterator)
        {
            Node &node = *iterator;
            if (!node.pending.valid)
            {
                const scf::RadioAdapter::TickResult tickResult =
                    node.adapter.tick(now, node.pending);
                if (tickResult == scf::RadioAdapter::TickResult::Failed)
                {
                    MessageMetrics &metrics = scenario.metrics[keyFor(bundleId)];
                    ++metrics.dropped;
                }
            }
            if (node.pending.valid && !directOnly)
            {
                node.radio.setTransmit(node.pending.peerId, now,
                                       node.pending.retransmission);
                node.adapter.transmit(node.pending, now);
            }
        }

        for (std::vector<Contact>::const_iterator contact = contacts.begin();
             contact != contacts.end(); ++contact)
        {
            if (!timeInWindow(now, *contact))
            {
                continue;
            }
            for (std::vector<Node>::iterator iterator = nodes.begin();
                 iterator != nodes.end(); ++iterator)
            {
                if (iterator->id != contact->from || iterator->pending.valid)
                {
                    continue;
                }
                if (directOnly && iterator->id != sourceId)
                {
                    continue;
                }
                if (directOnly && contact->to != destinationId)
                {
                    continue;
                }

                scf::RadioAdapter::OutgoingFrame outgoing = {};
                const scf::RadioAdapter::PrepareResult prepareResult =
                    iterator->adapter.prepareEncounter(contact->to, now, outgoing);
                if (prepareResult == scf::RadioAdapter::PrepareResult::Ready)
                {
                    iterator->radio.setTransmit(outgoing.peerId, now,
                                                 outgoing.retransmission);
                    iterator->adapter.transmit(outgoing, now);
                }
            }
        }

        for (std::vector<Node>::const_iterator iterator = nodes.begin();
             iterator != nodes.end(); ++iterator)
        {
            ++scenario.occupancySamples;
            scenario.occupancyTotal +=
                static_cast<uint32_t>(iterator->adapter.bundleCount());
        }

        if (now > UINT32_MAX - tickMs)
        {
            break;
        }
    }

    SimulationResult result = {};
    for (std::map<uint64_t, MessageMetrics>::const_iterator iterator =
             scenario.metrics.begin();
         iterator != scenario.metrics.end(); ++iterator)
    {
        result.messages.push_back(iterator->second);
    }

    AggregateMetrics &aggregate = result.aggregate;
    aggregate = {};
    for (std::vector<MessageMetrics>::const_iterator iterator =
             result.messages.begin();
         iterator != result.messages.end(); ++iterator)
    {
        aggregate.generated += iterator->generated;
        aggregate.delivered += iterator->delivered;
        aggregate.dropped += iterator->dropped;
        aggregate.expired += iterator->expired;
        aggregate.retransmissions += iterator->retransmissions;
        aggregate.duplicateReceptions += iterator->duplicateReceptions;
        aggregate.forwardingTransmissions += iterator->forwarded;
        aggregate.dataTransmissions += iterator->dataTransmissions;
        if (iterator->delivered != 0)
        {
            aggregate.deliveryLatencyTotal += iterator->deliveryLatency;
        }
    }

    aggregate.packetDeliveryRatio = aggregate.generated == 0
                                        ? 0.0
                                        : static_cast<double>(aggregate.delivered) /
                                              aggregate.generated;
    aggregate.averageDeliveryDelay = aggregate.delivered == 0
                                         ? 0.0
                                         : static_cast<double>(aggregate.deliveryLatencyTotal) /
                                               aggregate.delivered;
    aggregate.forwardingOverhead = aggregate.generated == 0
                                       ? 0.0
                                       : static_cast<double>(aggregate.forwardingTransmissions) /
                                             aggregate.generated;
    aggregate.occupancySamples = scenario.occupancySamples;
    aggregate.occupancyTotal = scenario.occupancyTotal;
    aggregate.averageBufferOccupancy = aggregate.occupancySamples == 0
                                           ? 0.0
                                           : static_cast<double>(aggregate.occupancyTotal) /
                                                 aggregate.occupancySamples;

    for (std::vector<scf::Message>::const_iterator iterator =
             generatedMessages.begin();
         iterator != generatedMessages.end(); ++iterator)
    {
        if (scf::sameBundle(iterator->bundleId, bundleId) &&
            result.messages.size() == 1 &&
            result.messages.front().delivered == 0 &&
            static_cast<int32_t>(endTime - iterator->expiresAt) >= 0)
        {
            result.messages.front().expired = 1;
            aggregate.expired = 1;
        }
    }

    std::ostringstream csv;
    exportCsv(result, csv);
    result.csv = csv.str();
    return result;
}

void Simulation::exportCsv(const SimulationResult &result, std::ostream &output)
{
    output << "source_id,sequence,generated,data_transmissions,forwarded,delivered,"
              "duplicate_receptions,dropped,expired,retransmissions,delivery_latency_ms,"
              "hop_count,link_acknowledged\n";
    for (std::vector<MessageMetrics>::const_iterator iterator =
             result.messages.begin();
         iterator != result.messages.end(); ++iterator)
    {
        output << iterator->bundleId.sourceId << ','
               << iterator->bundleId.sequence << ','
               << iterator->generated << ','
               << iterator->dataTransmissions << ','
               << iterator->forwarded << ','
               << iterator->delivered << ','
               << iterator->duplicateReceptions << ','
               << iterator->dropped << ','
               << iterator->expired << ','
               << iterator->retransmissions << ','
               << iterator->deliveryLatency << ','
               << static_cast<unsigned int>(iterator->hopCount) << ','
               << (iterator->linkAcknowledged ? 1 : 0) << '\n';
    }
}

void Simulation::exportAggregateCsv(const SimulationResult &result,
                                    std::ostream &output)
{
    output << "generated,delivered,dropped,expired,retransmissions,"
              "duplicate_receptions,forwarding_transmissions,data_transmissions,"
              "delivery_latency_total_ms,packet_delivery_ratio,"
              "average_delivery_delay_ms,forwarding_overhead,"
              "average_buffer_occupancy\n";
    const AggregateMetrics &metrics = result.aggregate;
    output << metrics.generated << ','
           << metrics.delivered << ','
           << metrics.dropped << ','
           << metrics.expired << ','
           << metrics.retransmissions << ','
           << metrics.duplicateReceptions << ','
           << metrics.forwardingTransmissions << ','
           << metrics.dataTransmissions << ','
           << metrics.deliveryLatencyTotal << ','
           << std::setprecision(10) << metrics.packetDeliveryRatio << ','
           << metrics.averageDeliveryDelay << ','
           << metrics.forwardingOverhead << ','
           << metrics.averageBufferOccupancy << '\n';
}
}
