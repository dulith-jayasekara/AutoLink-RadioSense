#include "SCF_Simulation.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>

namespace
{
scf::Message makeHazard(uint32_t expiresAt = 500)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {1, 1};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = scf::Priority::High;
    message.hopLimit = 4;
    return message;
}

scf_sim::Simulation makeAbc(uint32_t endTime = 1000)
{
    scf_sim::Simulation simulation(endTime, 10);
    simulation.addNode(1);
    simulation.addNode(2);
    simulation.addNode(3);
    simulation.addContact({1, 2, 0, 100, 10, 0, false});
    simulation.addContact({2, 3, 200, 400, 10, 0, false});
    assert(simulation.generate(1, makeHazard()));
    return simulation;
}

void testDtnDeliveryAndDirectOnlyBaseline()
{
    scf_sim::Simulation dtn = makeAbc();
    const scf_sim::SimulationResult dtnResult =
        dtn.runDtn(1, 3, {1, 1});
    assert(dtnResult.messages.size() == 1);
    assert(dtnResult.messages[0].generated == 1);
    assert(dtnResult.messages[0].delivered == 1);
    assert(dtnResult.messages[0].forwarded >= 1);
    assert(dtnResult.messages[0].hopCount == 1);
    assert(dtnResult.messages[0].deliveryLatency == 210);
    assert(dtnResult.aggregate.packetDeliveryRatio == 1.0);
    assert(dtnResult.aggregate.forwardingOverhead >= 1.0);
    assert(dtnResult.aggregate.averageBufferOccupancy > 0.0);

    scf_sim::Simulation direct = makeAbc();
    const scf_sim::SimulationResult directResult =
        direct.runDirectOnly(1, 3, {1, 1});
    assert(directResult.messages[0].generated == 1);
    assert(directResult.messages[0].delivered == 0);
    assert(directResult.aggregate.packetDeliveryRatio == 0.0);
}

void testNoContactAndTtlExpiry()
{
    scf_sim::Simulation noContact(200, 10);
    noContact.addNode(1);
    noContact.addNode(2);
    assert(noContact.generate(1, makeHazard(100)));
    const scf_sim::SimulationResult result =
        noContact.runDtn(1, 2, {1, 1});
    assert(result.messages[0].delivered == 0);
    assert(result.messages[0].expired == 1);
    assert(result.aggregate.packetDeliveryRatio == 0.0);
}

void testLossAndBoundedRetry()
{
    scf_sim::Simulation simulation(1500, 10);
    simulation.addNode(1);
    simulation.addNode(2);
    simulation.addNode(3);
    simulation.addContact({1, 2, 0, 100, 10, 0, false});
    simulation.addContact({2, 3, 200, 1400, 10, 0, true});
    assert(simulation.generate(1, makeHazard(2000)));

    const scf_sim::SimulationResult result =
        simulation.runDtn(1, 3, {1, 1});
    assert(result.messages[0].delivered == 1);
    assert(result.messages[0].retransmissions >= 1);
    assert(result.messages[0].dataTransmissions >= 2);
}

void testCsvExportAndZeroDenominators()
{
    scf_sim::Simulation simulation(50, 10);
    simulation.addNode(1);
    simulation.addNode(2);
    assert(simulation.generate(1, makeHazard(1000)));
    const scf_sim::SimulationResult result =
        simulation.runDtn(1, 2, {1, 1});
    std::ostringstream output;
    scf_sim::Simulation::exportCsv(result, output);
    assert(output.str().find("source_id,sequence,generated") == 0);
    std::ostringstream aggregateOutput;
    scf_sim::Simulation::exportAggregateCsv(result, aggregateOutput);
    assert(aggregateOutput.str().find("generated,delivered,dropped") == 0);
    assert(result.aggregate.packetDeliveryRatio == 0.0);
    assert(result.aggregate.averageDeliveryDelay == 0.0);
    assert(result.aggregate.forwardingOverhead == 0.0);
}
}

int main()
{
    testDtnDeliveryAndDirectOnlyBaseline();
    testNoContactAndTtlExpiry();
    testLossAndBoundedRetry();
    testCsvExportAndZeroDenominators();

    std::cout << "SCF simulation unit tests passed.\n";
    return 0;
}
