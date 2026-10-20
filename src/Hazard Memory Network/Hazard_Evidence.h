#pragma once
#include "Hazard_Record.h"
#include <stddef.h>

enum class ObservationKind : uint8_t { Report = 1, Revision = 2, Clearance = 3, Confirmation = 4, Contradiction = 5 };
struct HazardObservation
{
    uint64_t id;
    uint32_t observerNodeId;
    ObservationKind kind;
    uint8_t severity;
    HazardType type;
    uint64_t clientReportId; // Stable caller retry key, scoped to observerNodeId; zero means absent.
    HazardId hazardId;       // Original record reference, never rewritten by geographical grouping.
    double latitude, longitude;
    uint32_t observedAt, locationFixAt;
    float locationAccuracyMeters;
};
class HazardEvidenceCodec
{
public:
    static constexpr size_t SIZE = 60;
    static bool valid(const HazardObservation &observation);
    static bool encode(const HazardObservation &observation, uint8_t *data);
    static bool decode(const uint8_t *data, HazardObservation &observation);
    static uint32_t fingerprint(const HazardObservation &observation);
};
bool sameObservation(const HazardObservation &a, const HazardObservation &b);
bool sameObservationPayload(const HazardObservation &a, const HazardObservation &b);
