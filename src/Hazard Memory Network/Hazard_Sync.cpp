#include "Hazard_Sync.h"
#include <cstring>

namespace
{
constexpr size_t RECORD_BYTES = HazardSnapshot::HEADER_SIZE + HazardSnapshot::RECORD_SIZE;
static_assert(RECORD_BYTES == 84, "Update protocol version requires singleton schema 2");
bool due(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
bool isEvidence(SyncType type)
{
    return type == SyncType::EvidenceSummary || type == SyncType::EvidenceRequest || type == SyncType::EvidenceUpdate;
}
bool validFrame(const SyncFrame &f)
{
    if (!f.sender || !f.session || !f.sequence) return false;
    if (f.type == SyncType::Summary || f.type == SyncType::EvidenceSummary)
    {
        if (!f.length || f.payload[0] > 4 || f.length != 1u + 16u * f.payload[0]) return false;
        for (size_t i = 0; i < f.payload[0]; ++i)
        {
            const uint8_t *entry = f.payload + 1 + i * 16;
            if (!HazardSyncCodec::get(entry, 8) || !HazardSyncCodec::get(entry + 8, 4)) return false;
            for (size_t j = 0; j < i; ++j)
                if (HazardSyncCodec::get(entry, 8) == HazardSyncCodec::get(f.payload + 1 + j * 16, 8)) return false;
        }
        return true;
    }
    if (!f.target) return false;
    if (f.type == SyncType::Request || f.type == SyncType::EvidenceRequest)
        return f.length == 16 && HazardSyncCodec::get(f.payload, 8) && HazardSyncCodec::get(f.payload + 8, 4);
    if (f.type == SyncType::EvidenceUpdate)
        return f.length == 68 && HazardSyncCodec::get(f.payload, 4) && HazardSyncCodec::get(f.payload + 4, 4);
    if (f.type == SyncType::Update)
        return f.length == 92 && HazardSyncCodec::get(f.payload, 4) && HazardSyncCodec::get(f.payload + 4, 4);
    return false;
}
}
void HazardSyncCodec::put(uint8_t *data, uint64_t value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) { data[i] = static_cast<uint8_t>(value); value >>= 8; }
}
uint64_t HazardSyncCodec::get(const uint8_t *data, size_t bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= static_cast<uint64_t>(data[i]) << (8 * i);
    return value;
}
bool HazardSyncCodec::recognizes(const uint8_t *data, size_t size)
{
    return data && size >= 4 && get(data, 4) == 0x314e4841u; // "AHN1", experimental app discriminator.
}
bool HazardSyncCodec::encode(const SyncFrame &f, uint8_t *data, size_t capacity, size_t &size)
{
    size = 0;
    if (!data || !validFrame(f) || capacity < 27 + f.length) return false;
    put(data, 0x314e4841u, 4); data[4] = 2; data[5] = static_cast<uint8_t>(f.type);
    put(data + 6, f.sender, 4); put(data + 10, f.target, 4);
    put(data + 14, f.session, 4); put(data + 18, f.sequence, 4);
    data[22] = static_cast<uint8_t>(f.length);
    std::memcpy(data + 23, f.payload, f.length);
    put(data + 23 + f.length, crc32(data, 23 + f.length), 4);
    size = 27 + f.length;
    return true;
}
bool HazardSyncCodec::decode(const uint8_t *data, size_t size, SyncFrame &f)
{
    if (!recognizes(data, size) || size < 27 || size > MAX_FRAME || data[4] != 2 ||
        size != 27u + data[22] || get(data + size - 4, 4) != crc32(data, size - 4)) return false;
    SyncFrame parsed = {};
    parsed.type = static_cast<SyncType>(data[5]);
    parsed.sender = static_cast<uint32_t>(get(data + 6, 4));
    parsed.target = static_cast<uint32_t>(get(data + 10, 4));
    parsed.session = static_cast<uint32_t>(get(data + 14, 4));
    parsed.sequence = static_cast<uint32_t>(get(data + 18, 4));
    parsed.length = data[22];
    std::memcpy(parsed.payload, data + 23, parsed.length);
    if (!validFrame(parsed)) return false;
    f = parsed;
    return true;
}
bool HazardSyncCodec::encodeRecord(const HazardRecord &record, uint8_t *data)
{
    HazardRepository singleton;
    if (!singleton.add(record)) return false;
    size_t size;
    return HazardSnapshot::encode(singleton, 1, data, RECORD_BYTES, size) && size == RECORD_BYTES;
}
bool HazardSyncCodec::decodeRecord(const uint8_t *data, HazardRecord &record)
{
    HazardRepository singleton;
    uint64_t generation = 0;
    return HazardSnapshot::decode(data, RECORD_BYTES, singleton, generation) && generation == 1 &&
        singleton.count() == 1 && singleton.getAt(0, record);
}
uint32_t HazardSyncCodec::fingerprint(const HazardRecord &record)
{
    uint8_t bytes[RECORD_BYTES];
    return encodeRecord(record, bytes) ? HazardSnapshot::checksum(bytes, sizeof(bytes)) : 0;
}

HazardSync::HazardSync(HazardRepository &repo, HazardService &svc, uint32_t ownNode,
                       uint32_t bootSession, Send callback, void *data)
    : repository(repo), service(svc), node(ownNode), session(bootSession), sequence(0),
      nextTransmit(0), nextSummary(0), summaryCursor(0), requestCursor(0), responseCursor(0),
      summarizing(false), started(false), send(callback), context(data), pending{}, responses{}, counters{} {}
uint32_t HazardSync::allocateSequence()
{
    if (sequence == UINT32_MAX) return 0;
    return ++sequence;
}
bool HazardSync::transmit(SyncFrame &frame)
{
    uint8_t bytes[HazardSyncCodec::MAX_FRAME];
    size_t size;
    frame.sender = node; frame.session = session;
    if (!frame.sequence || !HazardSyncCodec::encode(frame, bytes, sizeof(bytes), size)) return false;
    if (!send || !send(bytes, size, context)) { ++counters.transportBusy; return false; }
    ++counters.sent;
    return true;
}
void HazardSync::handleSummary(const SyncFrame &f)
{
    const bool evidence = isEvidence(f.type);
    for (size_t i = 0; i < f.payload[0]; ++i)
    {
        const uint8_t *entry = f.payload + 1 + i * 16;
        const HazardId id = HazardSyncCodec::get(entry, 8);
        const uint32_t version = static_cast<uint32_t>(HazardSyncCodec::get(entry + 8, 4));
        const uint32_t digest = static_cast<uint32_t>(HazardSyncCodec::get(entry + 12, 4));
        if (evidence && version != 1) { ++counters.rejected; continue; }
        const HazardObservation *observation = evidence ? repository.findObservation(id) : nullptr;
        if (observation)
        {
            if (HazardEvidenceCodec::fingerprint(*observation) != digest) ++counters.conflicts;
            continue;
        }
        const HazardRecord *local = evidence ? nullptr : repository.findById(id);
        if (local && local->version >= version)
        {
            if (local->version == version && HazardSyncCodec::fingerprint(*local) != digest) ++counters.conflicts;
            continue;
        }
        Pending *available = nullptr;
        bool found = false;
        for (auto &p : pending)
        {
            if (!p.used) { if (!available) available = &p; continue; }
            if (p.peer == f.sender && p.id == id && p.evidence == evidence)
            {
                found = true;
                if (p.session != f.session || version > p.version)
                    p = Pending{true, f.sender, f.session, id, version, digest, 0, 0, 0, evidence};
                break;
            }
        }
        if (found) continue;
        if (!available) { ++counters.queueFull; continue; }
        *available = Pending{true, f.sender, f.session, id, version, digest, 0, 0, 0, evidence};
    }
}
void HazardSync::handleRequest(const SyncFrame &f)
{
    const HazardId id = HazardSyncCodec::get(f.payload, 8);
    const uint32_t wanted = static_cast<uint32_t>(HazardSyncCodec::get(f.payload + 8, 4));
    const bool evidence = isEvidence(f.type);
    const HazardRecord *record = evidence ? nullptr : repository.findById(id);
    if (evidence ? (wanted != 1 || !repository.findObservation(id)) : (!record || record->version < wanted))
    { ++counters.rejected; return; }
    Response *available = nullptr;
    for (auto &r : responses)
    {
        if (!r.used) { if (!available) available = &r; continue; }
        if (r.peer == f.sender && r.session == f.session && r.token == f.sequence && r.evidence == evidence) return;
    }
    if (!available) { ++counters.queueFull; return; }
    *available = Response{true, f.sender, f.session, f.sequence, 0, id, evidence};
}
void HazardSync::handleUpdate(const SyncFrame &f)
{
    if (HazardSyncCodec::get(f.payload, 4) != session) { ++counters.ignored; return; }
    const uint32_t token = static_cast<uint32_t>(HazardSyncCodec::get(f.payload + 4, 4));
    Pending *match = nullptr;
    for (auto &p : pending)
        if (p.used && p.peer == f.sender && p.session == f.session && p.token && p.token == token && p.evidence == isEvidence(f.type))
        { match = &p; break; }
    if (!match) { ++counters.ignored; return; }
    HazardResult result;
    if (match->evidence)
    {
        HazardObservation o;
        if (!HazardEvidenceCodec::decode(f.payload + 8, o) || o.id != match->id ||
            HazardEvidenceCodec::fingerprint(o) != match->digest)
        { ++counters.rejected; return; }
        result = service.importObservation(o);
    }
    else
    {
        HazardRecord record;
        if (!HazardSyncCodec::decodeRecord(f.payload + 8, record) || record.id != match->id ||
            record.version < match->version ||
            (record.version == match->version && HazardSyncCodec::fingerprint(record) != match->digest))
        { ++counters.rejected; return; }
        result = service.importRemote(record);
    }
    if (result == HazardResult::Ok)
    {
        if (match->evidence) ++counters.evidenceImported; else ++counters.imported;
    }
    else if (result == HazardResult::Unchanged)
    {
        if (match->evidence) ++counters.evidenceUnchanged; else ++counters.unchanged;
    }
    else if (result == HazardResult::Conflict || result == HazardResult::TerminalRecord) ++counters.conflicts;
    else ++counters.rejected;
    match->used = false; // Later summaries retry transient failures; no unbounded immediate work.
}
void HazardSync::receive(const uint8_t *data, size_t size, uint32_t milliseconds)
{
    (void)milliseconds;
    SyncFrame frame;
    if (!HazardSyncCodec::decode(data, size, frame)) { ++counters.malformed; return; }
    if (!node || !session || frame.sender == node || (frame.target && frame.target != node))
    { ++counters.ignored; return; }
    ++counters.received;
    switch (frame.type)
    {
    case SyncType::EvidenceSummary:
    case SyncType::Summary: handleSummary(frame); break;
    case SyncType::EvidenceRequest:
    case SyncType::Request: handleRequest(frame); break;
    case SyncType::EvidenceUpdate:
    case SyncType::Update: handleUpdate(frame); break;
    }
}
void HazardSync::tick(uint32_t now)
{
    if (!node || !session) return;
    if (!started)
    {
        started = true;
        nextTransmit = now + ((node ^ session) % 751);
        nextSummary = now;
    }
    // Retire satisfied or exhausted requests independently of radio availability.
    for (auto &p : pending)
    {
        if (!p.used) continue;
        const HazardObservation *o = p.evidence ? repository.findObservation(p.id) : nullptr;
        if (o) { p.used = false; continue; }
        const HazardRecord *local = p.evidence ? nullptr : repository.findById(p.id);
        if (local && local->version >= p.version) { p.used = false; continue; }
        if (p.attempts >= MAX_ATTEMPTS && static_cast<uint32_t>(now - p.lastSent) >= RETRY_MS)
        { p.used = false; ++counters.exhausted; }
    }
    if (!due(now, nextTransmit)) return;
    // Distinct boot/node offsets reduce synchronized half-duplex collisions.
    nextTransmit = now + 1200 + ((node ^ session) % 501);
    for (size_t n = 0; n < RESPONSE_CAPACITY; ++n)
    {
        const size_t i = (responseCursor + n) % RESPONSE_CAPACITY;
        Response &r = responses[i];
        if (!r.used) continue;
        const HazardRecord *record = r.evidence ? nullptr : repository.findById(r.id);
        const HazardObservation *o = r.evidence ? repository.findObservation(r.id) : nullptr;
        if (r.evidence ? !o : !record) { r.used = false; continue; }
        SyncFrame frame = {};
        frame.type = r.evidence ? SyncType::EvidenceUpdate : SyncType::Update;
        frame.target = r.peer; frame.length = r.evidence ? 68 : 92;
        if (!r.outgoingSequence) r.outgoingSequence = allocateSequence();
        frame.sequence = r.outgoingSequence;
        HazardSyncCodec::put(frame.payload, r.session, 4);
        HazardSyncCodec::put(frame.payload + 4, r.token, 4);
        if (!(r.evidence ? HazardEvidenceCodec::encode(*o, frame.payload + 8) :
              HazardSyncCodec::encodeRecord(*record, frame.payload + 8))) { r.used = false; continue; }
        if (transmit(frame)) { r.used = false; responseCursor = (i + 1) % RESPONSE_CAPACITY; }
        return;
    }
    for (size_t n = 0; n < REQUEST_CAPACITY; ++n)
    {
        const size_t i = (requestCursor + n) % REQUEST_CAPACITY;
        Pending &p = pending[i];
        if (!p.used || (p.attempts && static_cast<uint32_t>(now - p.lastSent) < RETRY_MS)) continue;
        SyncFrame frame = {};
        frame.type = p.evidence ? SyncType::EvidenceRequest : SyncType::Request;
        frame.target = p.peer; frame.length = 16;
        if (!p.token) p.token = allocateSequence();
        frame.sequence = p.token;
        HazardSyncCodec::put(frame.payload, p.id, 8);
        HazardSyncCodec::put(frame.payload + 8, p.version, 4);
        HazardSyncCodec::put(frame.payload + 12, p.digest, 4);
        if (transmit(frame))
        {
            if (p.attempts) ++counters.retries;
            ++p.attempts; p.lastSent = now; requestCursor = (i + 1) % REQUEST_CAPACITY;
        }
        return;
    }
    if (!summarizing && due(now, nextSummary)) { summaryCursor = 0; summarizing = true; }
    if (!summarizing) return;
    SyncFrame frame = {};
    const bool evidence = summaryCursor >= repository.count();
    const size_t cursor = evidence ? summaryCursor - repository.count() : summaryCursor;
    const size_t available = evidence ? repository.observationCount() : repository.count();
    frame.type = evidence ? SyncType::EvidenceSummary : SyncType::Summary;
    frame.sequence = allocateSequence();
    size_t count = 0;
    while (count < 4 && cursor + count < available)
    {
        uint8_t *entry = frame.payload + 1 + count * 16;
        if (evidence)
        {
            HazardObservation o; repository.observationAt(cursor + count, o);
            HazardSyncCodec::put(entry, o.id, 8);
            HazardSyncCodec::put(entry + 8, 1, 4);
            HazardSyncCodec::put(entry + 12, HazardEvidenceCodec::fingerprint(o), 4);
        }
        else
        {
            HazardRecord record; repository.getAt(cursor + count, record);
            HazardSyncCodec::put(entry, record.id, 8);
            HazardSyncCodec::put(entry + 8, record.version, 4);
            HazardSyncCodec::put(entry + 12, HazardSyncCodec::fingerprint(record), 4);
        }
        ++count;
    }
    frame.payload[0] = static_cast<uint8_t>(count); frame.length = 1 + count * 16;
    if (transmit(frame))
    {
        summaryCursor += count;
        if (summaryCursor >= repository.count() + repository.observationCount())
        { summarizing = false; nextSummary = now + SUMMARY_INTERVAL_MS + ((node ^ session) % 2001); }
    }
}
