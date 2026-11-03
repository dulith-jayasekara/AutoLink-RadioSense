#include "Hazard_Repository.h"

#include <cassert>
#include <iostream>
#include <limits>

namespace
{
HazardRecord makeHazard(uint32_t id)
{
    return HazardRecord{
        id,
        HazardType::RoadObstacle,
        6.9271,
        79.8612,
        0,
        0,
        1,
        50,
        HazardStatus::Unverified};
}

void testRecordValidation()
{
    HazardRecord record = makeHazard(1);
    assert(isValidHazardRecord(record));

    record.id = 0;
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.latitude = 90.01;
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.longitude = -180.01;
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.latitude = std::numeric_limits<double>::quiet_NaN();
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.confidence = 101;
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.type = static_cast<HazardType>(255);
    assert(!isValidHazardRecord(record));

    record = makeHazard(1);
    record.status = static_cast<HazardStatus>(255);
    assert(!isValidHazardRecord(record));
}

void testRepositoryOperations()
{
    HazardRepository repository;
    const HazardRecord first = makeHazard(1);
    assert(repository.add(first));
    assert(repository.count() == 1);
    assert(!repository.add(first));

    const HazardRecord *stored = repository.findById(1);
    assert(stored != nullptr);
    assert(stored->latitude == first.latitude);
    assert(repository.findById(99) == nullptr);

    HazardRecord updated = first;
    updated.confidence = 85;
    updated.status = HazardStatus::Active;
    assert(repository.update(updated));
    assert(repository.findById(1)->confidence == 85);

    HazardRecord invalid = updated;
    invalid.longitude = 181.0;
    assert(!repository.update(invalid));
    assert(repository.findById(1)->longitude == updated.longitude);

    HazardRecord indexed;
    assert(repository.getAt(0, indexed));
    assert(indexed.id == 1);
    assert(!repository.getAt(1, indexed));

    assert(repository.remove(1));
    assert(repository.count() == 0);
    assert(repository.findById(1) == nullptr);
    assert(!repository.remove(1));
}

void testRepositoryCapacity()
{
    HazardRepository repository;
    for (size_t i = 1; i <= HazardRepository::CAPACITY; ++i)
    {
        assert(repository.add(makeHazard(static_cast<uint32_t>(i))));
    }

    assert(repository.count() == HazardRepository::CAPACITY);
    assert(!repository.add(makeHazard(1000)));
}
}

int main()
{
    testRecordValidation();
    testRepositoryOperations();
    testRepositoryCapacity();

    std::cout << "Hazard Memory host tests passed.\n";
    return 0;
}
