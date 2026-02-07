#pragma once

#include "mod_interface.h"
#include <cstdint>
#include <map>

// ---------------------------------------------------------------------------
// EdgeStat opcode and constants
// ---------------------------------------------------------------------------
constexpr uint32_t OP_EdgeStat = 0x1338;
constexpr int64_t  CLASSLESS_VALUE = 17;

// ---------------------------------------------------------------------------
// Stat entry enum — matches server-side EdgeStat key values
// ---------------------------------------------------------------------------
enum class eStatEntry : uint32_t
{
    Classless     = 1,
    CurHP         = 2,
    CurMana       = 3,
    CurEndur      = 4,
    MaxHP         = 5,
    MaxMana       = 6,
    MaxEndur      = 7,
    ATK           = 8,
    AC            = 9,
    STR           = 10,
    STA           = 11,
    DEX           = 12,
    AGI           = 13,
    INT           = 14,
    WIS           = 15,
    CHA           = 16,
    MR            = 17,
    FR            = 18,
    CR            = 19,
    PR            = 20,
    DR            = 21,
    Walkspeed     = 22,
    Runspeed      = 23,
    Weight        = 24,
    MaxWeight     = 25,
    MeleePower    = 26,
    SpellPower    = 27,
    HealingPower  = 28,
    MeleeHaste    = 29,
    SpellHaste    = 30,
    HealingHaste  = 31,
    MeleeCrit     = 32,
    SpellCrit     = 33,
    HealingCrit   = 34,
    TotalPower    = 35,
    SynergyLevel  = 36,
    Mitigation    = 37,
    AAPoints      = 38,
    SynergyLevel1  = 39,
    SynergyLevel2  = 40,
    SynergyLevel3  = 41,
    SynergyLevel4  = 42,
    SynergyLevel5  = 43,
    SynergyLevel6  = 44,
    SynergyLevel7  = 45,
    SynergyLevel8  = 46,
    SynergyLevel9  = 47,
    SynergyLevel10 = 48,
    SynergyLevel11 = 49,
    SynergyLevel12 = 50,
    Class1        = 51,
    Class2        = 52,
    Class3        = 53,
    ClassCount    = 54,
    Class1Level   = 55,
    Class2Level   = 56,
    Class3Level   = 57,
    Max           = 58,
};

// ---------------------------------------------------------------------------
// Wire-format structs — must match server packing
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

struct EdgeStatEntry_Struct
{
    uint32_t key;    // eStatEntry value
    int64_t  value;
};

struct EdgeStat_Struct
{
    uint32_t count;
    EdgeStatEntry_Struct entries[1]; // variable-length array
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// MulticlassData mod — receives EdgeStat packets, caches stats
// ---------------------------------------------------------------------------
class MulticlassData : public IMod
{
public:
    // IMod interface
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;

    // Static query API — callable from any mod without an instance pointer
    static int64_t GetStat(eStatEntry stat);
    static bool    IsClassless();
    static int     GetClassCount();
    static bool    HasData();

private:
    static std::map<eStatEntry, int64_t> s_stats;
    static bool s_hasData;
};
