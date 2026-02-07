#include "pch.h"
#include "multiclass_data.h"
#include "../core.h"

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
std::map<eStatEntry, int64_t> MulticlassData::s_stats;
bool MulticlassData::s_hasData = false;

// ---------------------------------------------------------------------------
// IMod implementation
// ---------------------------------------------------------------------------

const char* MulticlassData::GetName() const
{
    return "MulticlassData";
}

bool MulticlassData::Initialize()
{
    LogFramework("MulticlassData: Initialized — waiting for EdgeStat packets (opcode 0x%04X)", OP_EdgeStat);
    return true;
}

void MulticlassData::Shutdown()
{
    LogFramework("MulticlassData: Shutting down — clearing %zu stat entries", s_stats.size());
    s_stats.clear();
    s_hasData = false;
}

void MulticlassData::OnPulse()
{
    // No per-frame work needed — data is updated on incoming messages
}

bool MulticlassData::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    if (opcode != OP_EdgeStat)
        return true;

    // Validate minimum size: at least the count field
    if (size < sizeof(uint32_t))
    {
        LogFramework("MulticlassData: EdgeStat packet too small (%u bytes)", size);
        return true;
    }

    auto* packet = static_cast<const EdgeStat_Struct*>(buffer);
    uint32_t count = packet->count;

    // Validate that the packet is large enough for all declared entries
    uint32_t expectedSize = sizeof(uint32_t) + count * sizeof(EdgeStatEntry_Struct);
    if (size < expectedSize)
    {
        LogFramework("MulticlassData: EdgeStat packet size mismatch — got %u bytes, expected %u for %u entries",
            size, expectedSize, count);
        return true;
    }

    // Parse entries into the stat map
    for (uint32_t i = 0; i < count; ++i)
    {
        auto key = static_cast<eStatEntry>(packet->entries[i].key);
        int64_t value = packet->entries[i].value;
        s_stats[key] = value;
    }
    s_hasData = true;

    LogFramework("MulticlassData: Received EdgeStat packet — %u entries", count);

    // Log multiclass summary
    int classCount = GetClassCount();
    if (IsClassless())
        LogFramework("MulticlassData: Character is CLASSLESS — %d classes", classCount);
    else
        LogFramework("MulticlassData: Character has %d classes", classCount);

    return true;
}

// ---------------------------------------------------------------------------
// Static query API
// ---------------------------------------------------------------------------

int64_t MulticlassData::GetStat(eStatEntry stat)
{
    auto it = s_stats.find(stat);
    if (it != s_stats.end())
        return it->second;
    return 0;
}

bool MulticlassData::IsClassless()
{
    return GetStat(eStatEntry::Classless) == CLASSLESS_VALUE;
}

int MulticlassData::GetClassCount()
{
    return static_cast<int>(GetStat(eStatEntry::ClassCount));
}

bool MulticlassData::HasData()
{
    return s_hasData;
}
