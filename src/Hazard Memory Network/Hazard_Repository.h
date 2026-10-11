#pragma once

#include <stddef.h>
#include "Hazard_Record.h"

class HazardRepository
{
public:
    static constexpr size_t CAPACITY = 16;

    HazardRepository();

    bool add(const HazardRecord &record);
    const HazardRecord *findById(uint32_t id) const;
    bool update(const HazardRecord &record);
    bool getAt(size_t index, HazardRecord &record) const;
    size_t count() const;
    bool remove(uint32_t id);

private:
    HazardRecord records[CAPACITY];
    size_t recordCount;
};
