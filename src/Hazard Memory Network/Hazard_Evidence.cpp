#include "Hazard_Evidence.h"
#include <cmath>
#include <cstring>

namespace
{
void put(uint8_t *p, uint64_t value, size_t n)
{
    for (size_t i = 0; i < n; ++i) { p[i] = static_cast<uint8_t>(value); value >>= 8; }
}
uint64_t get(const uint8_t *p, size_t n)
{
    uint64_t value = 0;
    for (size_t i = 0; i < n; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
}
}
bool sameObservationPayload(const HazardObservation &a, const HazardObservation &b)
{
    return a.observerNodeId == b.observerNodeId && a.kind == b.kind && a.severity == b.severity &&
        a.type == b.type && a.clientReportId == b.clientReportId && a.hazardId == b.hazardId &&
        a.latitude == b.latitude && a.longitude == b.longitude && a.observedAt == b.observedAt &&
        a.locationFixAt == b.locationFixAt && a.locationAccuracyMeters == b.locationAccuracyMeters;
}
bool sameObservation(const HazardObservation &a, const HazardObservation &b)
{
    return a.id == b.id && sameObservationPayload(a, b);
}
bool HazardEvidenceCodec::valid(const HazardObservation &o)
{
    return o.observerNodeId && (o.id >> 32) == o.observerNodeId && static_cast<uint32_t>(o.id) &&
        (o.hazardId >> 32) && static_cast<uint32_t>(o.hazardId) &&
        o.kind >= ObservationKind::Report && o.kind <= ObservationKind::Contradiction &&
        (o.kind != ObservationKind::Report || o.id == o.hazardId) &&
        isValidHazardType(o.type) && o.severity >= 1 && o.severity <= 5 &&
        isValidCoordinates(o.latitude, o.longitude) && o.observedAt && o.locationFixAt &&
        o.locationFixAt <= o.observedAt &&
        ((o.kind != ObservationKind::Confirmation && o.kind != ObservationKind::Contradiction) ||
         (o.clientReportId && o.observedAt - o.locationFixAt <= 120 && o.locationAccuracyMeters <= 25)) &&
        std::isfinite(o.locationAccuracyMeters) &&
        o.locationAccuracyMeters > 0 && o.locationAccuracyMeters <= 100;
}
bool HazardEvidenceCodec::encode(const HazardObservation &o, uint8_t *p)
{
    if (!p || !valid(o)) return false;
    put(p, o.id, 8); put(p + 8, o.observerNodeId, 4);
    p[12] = static_cast<uint8_t>(o.kind); p[13] = 0; p[14] = o.severity; p[15] = static_cast<uint8_t>(o.type);
    put(p + 16, o.clientReportId, 8); put(p + 24, o.hazardId, 8);
    uint64_t bits;
    std::memcpy(&bits, &o.latitude, 8); put(p + 32, bits, 8);
    std::memcpy(&bits, &o.longitude, 8); put(p + 40, bits, 8);
    put(p + 48, o.observedAt, 4); put(p + 52, o.locationFixAt, 4);
    uint32_t accuracy;
    std::memcpy(&accuracy, &o.locationAccuracyMeters, 4); put(p + 56, accuracy, 4);
    return true;
}
bool HazardEvidenceCodec::decode(const uint8_t *p, HazardObservation &out)
{
    if (!p || p[13]) return false;
    HazardObservation o = {};
    o.id = get(p, 8); o.observerNodeId = static_cast<uint32_t>(get(p + 8, 4));
    o.kind = static_cast<ObservationKind>(p[12]); o.severity = p[14]; o.type = static_cast<HazardType>(p[15]);
    o.clientReportId = get(p + 16, 8); o.hazardId = get(p + 24, 8);
    uint64_t bits = get(p + 32, 8); std::memcpy(&o.latitude, &bits, 8);
    bits = get(p + 40, 8); std::memcpy(&o.longitude, &bits, 8);
    o.observedAt = static_cast<uint32_t>(get(p + 48, 4));
    o.locationFixAt = static_cast<uint32_t>(get(p + 52, 4));
    const uint32_t accuracy = static_cast<uint32_t>(get(p + 56, 4));
    std::memcpy(&o.locationAccuracyMeters, &accuracy, 4);
    if (!valid(o)) return false;
    out = o; return true;
}
uint32_t HazardEvidenceCodec::fingerprint(const HazardObservation &o)
{
    uint8_t bytes[SIZE];
    if (!encode(o, bytes)) return 0;
    uint32_t crc = UINT32_MAX;
    for (uint8_t value : bytes)
    {
        crc ^= value;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
