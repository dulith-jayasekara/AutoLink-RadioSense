#include "Hazard_Repository.h"

HazardRepository::HazardRepository()
    : recordCount(0)
{
}

bool HazardRepository::add(const HazardRecord &record)
{
    if (recordCount >= CAPACITY || findById(record.id) != nullptr)
    {
        return false;
    }

    records[recordCount++] = record;
    return true;
}

const HazardRecord *HazardRepository::findById(uint32_t id) const
{
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == id)
        {
            return &records[i];
        }
    }

    return nullptr;
}

bool HazardRepository::update(const HazardRecord &record)
{
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == record.id)
        {
            records[i] = record;
            return true;
        }
    }

    return false;
}

bool HazardRepository::getAt(size_t index, HazardRecord &record) const
{
    if (index >= recordCount)
    {
        return false;
    }

    record = records[index];
    return true;
}

size_t HazardRepository::count() const
{
    return recordCount;
}

bool HazardRepository::remove(uint32_t id)
{
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == id)
        {
            for (size_t j = i + 1; j < recordCount; ++j)
            {
                records[j - 1] = records[j];
            }

            --recordCount;
            return true;
        }
    }

    return false;
}
