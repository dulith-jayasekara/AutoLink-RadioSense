#include "../SCF_Simulation.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
scf::Message makeHazard()
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {1, 1};
    message.createdAt = 0;
    message.expiresAt = 500;
    message.priority = scf::Priority::High;
    message.hopLimit = 4;
    return message;
}

scf_sim::Simulation makeScenario()
{
    scf_sim::Simulation simulation(1000, 10);
    simulation.addNode(1);
    simulation.addNode(2);
    simulation.addNode(3);
    simulation.addContact({1, 2, 0, 100, 10, 0, false});
    simulation.addContact({2, 3, 200, 400, 10, 0, false});
    simulation.generate(1, makeHazard());
    return simulation;
}

std::string delayText(const scf_sim::MessageMetrics &metrics)
{
    if (metrics.delivered == 0)
    {
        return "N/A";
    }

    return std::to_string(metrics.deliveryLatency) + " ms";
}

std::string ratioText(double ratio)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << ratio;
    return output.str();
}

void writeCsv(const std::string &path, const scf_sim::SimulationResult &result)
{
    std::ofstream output(path.c_str());
    if (!output)
    {
        throw std::runtime_error("cannot open CSV output: " + path);
    }

    scf_sim::Simulation::exportCsv(result, output);
}

void writeAggregateCsv(const std::string &path,
                       const scf_sim::SimulationResult &result)
{
    std::ofstream output(path.c_str());
    if (!output)
    {
        throw std::runtime_error("cannot open CSV output: " + path);
    }

    scf_sim::Simulation::exportAggregateCsv(result, output);
}

void printScenario(const char *name, const scf_sim::SimulationResult &result)
{
    const scf_sim::MessageMetrics &message = result.messages.front();
    const scf_sim::AggregateMetrics &aggregate = result.aggregate;

    std::cout << "\n" << name << "\n";
    std::cout << "  delivery: " << aggregate.delivered << "/"
              << aggregate.generated << "\n";
    std::cout << "  packet delivery ratio: "
              << ratioText(aggregate.packetDeliveryRatio) << "\n";
    std::cout << "  delivery delay: " << delayText(message) << "\n";
    std::cout << "  forwarding hops: "
              << static_cast<unsigned int>(message.hopCount) << "\n";
    std::cout << "  forwarding transmissions: "
              << aggregate.forwardingTransmissions << "\n";
    std::cout << "  DATA transmissions: " << aggregate.dataTransmissions << "\n";
    std::cout << "  retransmissions: " << aggregate.retransmissions << "\n";
    std::cout << "  duplicate receptions: "
              << aggregate.duplicateReceptions << "\n";
    std::cout << "  expired bundles: " << aggregate.expired << "\n";
    std::cout << "  average buffer occupancy: "
              << aggregate.averageBufferOccupancy << "\n";
}

bool referenceChecks(const scf_sim::SimulationResult &dtn,
                     const scf_sim::SimulationResult &direct)
{
    return dtn.aggregate.generated == 1 &&
           dtn.aggregate.delivered == 1 &&
           dtn.messages.size() == 1 &&
           dtn.messages.front().hopCount == 1 &&
           direct.aggregate.generated == 1 &&
           direct.aggregate.delivered == 0 &&
           direct.messages.size() == 1 &&
           direct.messages.front().expired == 1;
}
}

int main(int argc, char **argv)
{
    const std::string outputDirectory = argc > 1 ? argv[1] : "output";
    const std::string dtnCsv = outputDirectory + "/dtn.csv";
    const std::string dtnAggregateCsv = outputDirectory + "/dtn_aggregate.csv";
    const std::string directCsv = outputDirectory + "/direct_only.csv";
    const std::string directAggregateCsv =
        outputDirectory + "/direct_only_aggregate.csv";

    std::cout << "AutoLink RadioSense Component 4 DTN-SCF demonstration\n";
    std::cout << "\nScenario\n";
    std::cout << "  A=1, B=2, C=3\n";
    std::cout << "  A-B contact: 0-100 ms, 10 ms latency\n";
    std::cout << "  B-C contact: 200-400 ms, 10 ms latency\n";
    std::cout << "  A and C never meet\n";
    std::cout << "  Delivery means destination DATA acceptance\n";
    std::cout << "  No physical radio or RF transmission is involved\n";

    const scf_sim::SimulationResult dtn =
        makeScenario().runDtn(1, 3, {1, 1});
    const scf_sim::SimulationResult direct =
        makeScenario().runDirectOnly(1, 3, {1, 1});

    printScenario("DTN-SCF", dtn);
    printScenario("Direct-only baseline", direct);

    std::cout << "\nComparison\n";
    std::cout << "Metric                       DTN-SCF       Direct-only\n";
    std::cout << "Delivery                     " << dtn.aggregate.delivered
              << "/" << dtn.aggregate.generated << "          "
              << direct.aggregate.delivered << "/"
              << direct.aggregate.generated << "\n";
    std::cout << "Delivery delay               "
              << delayText(dtn.messages.front()) << "       "
              << delayText(direct.messages.front()) << "\n";
    std::cout << "Forwarding hops              "
              << static_cast<unsigned int>(dtn.messages.front().hopCount)
              << "             "
              << static_cast<unsigned int>(direct.messages.front().hopCount)
              << "\n";
    std::cout << "Retransmissions              "
              << dtn.aggregate.retransmissions << "             "
              << direct.aggregate.retransmissions << "\n";
    std::cout << "Expired bundles              " << dtn.aggregate.expired
              << "             " << direct.aggregate.expired << "\n";

    try
    {
        writeCsv(dtnCsv, dtn);
        writeAggregateCsv(dtnAggregateCsv, dtn);
        writeCsv(directCsv, direct);
        writeAggregateCsv(directAggregateCsv, direct);
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 2;
    }

    const bool passed = referenceChecks(dtn, direct);
    std::cout << "\nReference scenario checks: "
              << (passed ? "PASS" : "FAIL") << "\n";
    std::cout << "CSV outputs: " << dtnCsv << ", " << dtnAggregateCsv
              << ", " << directCsv << ", " << directAggregateCsv << "\n";
    return passed ? 0 : 1;
}
