#pragma once
#include "Hazard_Service.h"
#include "Hazard_Storage.h"

enum class SyncType : uint8_t { Summary = 1, Request = 2, Update = 3, EvidenceSummary = 4, EvidenceRequest = 5, EvidenceUpdate = 6 };
struct SyncFrame
{
    SyncType type;
    uint32_t sender, target, session, sequence;
    uint8_t payload[92];
    size_t length;
};
class HazardSyncCodec
{
public:
    static constexpr uint8_t PROTOCOL_VERSION = 4;
    static constexpr size_t MAX_FRAME = 119;
    static bool recognizes(const uint8_t *data, size_t size);
    static bool encode(const SyncFrame &frame, uint8_t *data, size_t capacity, size_t &size);
    static bool decode(const uint8_t *data, size_t size, SyncFrame &frame);
    static uint32_t fingerprint(const HazardRecord &record);
    static bool encodeRecord(const HazardRecord &record, uint8_t *data);
    static bool decodeRecord(const uint8_t *data, HazardRecord &record);
    static void put(uint8_t *data, uint64_t value, size_t bytes);
    static uint64_t get(const uint8_t *data, size_t bytes);
};
struct HazardSyncStats
{
    uint32_t sent = 0, received = 0, malformed = 0, ignored = 0;
    uint32_t imported = 0, unchanged = 0, conflicts = 0, rejected = 0;
    uint32_t evidenceImported = 0, evidenceUnchanged = 0;
    uint32_t retries = 0, exhausted = 0, queueFull = 0, transportBusy = 0;
};
class HazardSync
{
public:
    // True means the transport accepted a complete frame; it must copy data before returning.
    using Send = bool (*)(const uint8_t *, size_t, void *);
    HazardSync(HazardRepository &repository, HazardService &service, uint32_t node,
               uint32_t session, Send send, void *context);
    void tick(uint32_t milliseconds);
    void receive(const uint8_t *data, size_t size, uint32_t milliseconds);
    const HazardSyncStats &stats() const { return counters; }
    static constexpr size_t REQUEST_CAPACITY = 16;
    static constexpr size_t RESPONSE_CAPACITY = 8;
    static constexpr uint32_t SUMMARY_INTERVAL_MS = 30000;
    static constexpr uint32_t RETRY_MS = 5000;
    static constexpr uint8_t MAX_ATTEMPTS = 3;
private:
    struct Pending
    {
        bool used;
        uint32_t peer, session;
        HazardId id;
        uint32_t version, digest, token, lastSent;
        uint8_t attempts;
        bool evidence;
    };
    struct Response
    {
        bool used;
        uint32_t peer, session, token, outgoingSequence;
        HazardId id;
        bool evidence;
    };
    HazardRepository &repository;
    HazardService &service;
    uint32_t node, session, sequence, nextTransmit, nextSummary;
    size_t summaryCursor, requestCursor, responseCursor;
    bool summarizing, started;
    Send send;
    void *context;
    Pending pending[REQUEST_CAPACITY];
    Response responses[RESPONSE_CAPACITY];
    HazardSyncStats counters;
    uint32_t allocateSequence();
    bool transmit(SyncFrame &frame);
    void handleSummary(const SyncFrame &frame);
    void handleRequest(const SyncFrame &frame);
    void handleUpdate(const SyncFrame &frame);
};
