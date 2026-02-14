/**
 * @file who_multiclass.cpp
 * @brief Implementation of WhoMulticlass mod.
 *
 * Intercepts OP_WhoAllResponse (ROF2 opcode 0x578c) and reformats the output
 * to display THJ multi-class characters correctly.
 *
 * The server sends a class bitmask when multiclassing is enabled:
 *   Bit 0=Warrior, Bit 1=Cleric, ... Bit 15=Berserker
 * The standard client only understands class IDs 1-16, so multi-class values
 * show as "Unknown (Unknown)". We intercept the packet, decode the bitmask,
 * and output a formatted who list matching the Pyrelight DLL format:
 *   * GM *  Morsal - Level 100 Iksar  (Shaman/Necromancer/Magician)
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "who_multiclass.h"
#include "../core.h"

#include <cstdint>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Opcode
// ---------------------------------------------------------------------------
static constexpr uint32_t OP_WhoAllResponse = 0x578c;

// ---------------------------------------------------------------------------
// Class bitmask → name table (bit position → class name)
// ---------------------------------------------------------------------------
static const char* s_classNames[] = {
    "Warrior",       // bit 0  (1)
    "Cleric",        // bit 1  (2)
    "Paladin",       // bit 2  (4)
    "Ranger",        // bit 3  (8)
    "Shadow Knight", // bit 4  (16)
    "Druid",         // bit 5  (32)
    "Monk",          // bit 6  (64)
    "Bard",          // bit 7  (128)
    "Rogue",         // bit 8  (256)
    "Shaman",        // bit 9  (512)
    "Necromancer",   // bit 10 (1024)
    "Wizard",        // bit 11 (2048)
    "Magician",      // bit 12 (4096)
    "Enchanter",     // bit 13 (8192)
    "Beastlord",     // bit 14 (16384)
    "Berserker",     // bit 15 (32768)
};
static constexpr int s_numClasses = 16;

// Standard EQ class IDs (1-based) → name
static const char* GetSingleClassName(uint32_t classId)
{
    if (classId >= 1 && classId <= 16)
        return s_classNames[classId - 1];
    return "Unknown";
}

// Decode a class bitmask into "Class1/Class2/Class3" format
static std::string DecodeClassBitmask(uint32_t classBits)
{
    std::string result;
    for (int i = 0; i < s_numClasses; ++i)
    {
        if (classBits & (1u << i))
        {
            if (!result.empty())
                result += "/";
            result += s_classNames[i];
        }
    }
    return result.empty() ? "Unknown" : result;
}

// Check if a value is a multiclass bitmask (more than one bit set)
static bool IsMulticlass(uint32_t classValue)
{
    return (classValue & (classValue - 1)) != 0;
}

// ---------------------------------------------------------------------------
// Race ID → name table
// ---------------------------------------------------------------------------
static const char* GetRaceName(uint32_t raceId)
{
    switch (raceId)
    {
        case 1:   return "Human";
        case 2:   return "Barbarian";
        case 3:   return "Erudite";
        case 4:   return "Wood Elf";
        case 5:   return "High Elf";
        case 6:   return "Dark Elf";
        case 7:   return "Half Elf";
        case 8:   return "Dwarf";
        case 9:   return "Troll";
        case 10:  return "Ogre";
        case 11:  return "Halfling";
        case 12:  return "Gnome";
        case 128: return "Iksar";
        case 130: return "Vah Shir";
        case 330: return "Froglok";
        case 522: return "Drakkin";
        default:  return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// GM rank string IDs
// ---------------------------------------------------------------------------
static const char* GetRankTag(uint32_t rankMsgId)
{
    switch (rankMsgId)
    {
        case 12312: return " * GM * ";
        case 12315: return " TRADER ";
        case 6056:  return " BUYER ";
        default:    return "";
    }
}

// ---------------------------------------------------------------------------
// Packet parsing helpers
// ---------------------------------------------------------------------------

// Safe read of a uint32 from buffer with bounds checking
static bool ReadUint32(const uint8_t* buf, uint32_t size, uint32_t& offset, uint32_t& out)
{
    if (offset + 4 > size) return false;
    memcpy(&out, buf + offset, 4);
    offset += 4;
    return true;
}

// Safe read of a null-terminated string
static bool ReadString(const uint8_t* buf, uint32_t size, uint32_t& offset, char* out, uint32_t maxLen)
{
    uint32_t start = offset;
    while (offset < size && buf[offset] != '\0')
        offset++;

    if (offset >= size) return false;  // No null terminator found

    uint32_t len = offset - start;
    if (len >= maxLen) len = maxLen - 1;
    memcpy(out, buf + start, len);
    out[len] = '\0';
    offset++;  // Skip the null terminator
    return true;
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* WhoMulticlass::GetName() const
{
    return "WhoMulticlass";
}

bool WhoMulticlass::Initialize()
{
    LogFramework("WhoMulticlass: Initializing...");
    LogFramework("WhoMulticlass: Listening for OP_WhoAllResponse (0x%04X)", OP_WhoAllResponse);
    LogFramework("WhoMulticlass: Initialized");
    return true;
}

void WhoMulticlass::Shutdown()
{
    LogFramework("WhoMulticlass: Shutdown");
}

void WhoMulticlass::OnPulse()
{
    // No per-frame work needed
}

bool WhoMulticlass::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    if (opcode == OP_WhoAllResponse && buffer && size > 0x40)
    {
        if (HandleWhoResponse(buffer, size))
            return false;  // Suppress original — we've generated our own output
    }

    return true;  // Pass through all other messages
}

// ---------------------------------------------------------------------------
// Who response packet parsing and reformatting
// ---------------------------------------------------------------------------

bool WhoMulticlass::HandleWhoResponse(const void* buffer, uint32_t size)
{
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    uint32_t offset = 0;

    // --- Parse header (WhoAllReturnStruct) ---
    // 0x00: uint32 id
    // 0x04: uint32 playerineqstring
    // 0x08: char[27] line
    // 0x23: uint8 unknown35
    // 0x24: uint32 unknown36
    // 0x28: uint32 playersinzonestring
    // 0x2C: uint32 unknown44[2]
    // 0x34: uint32 unknown52
    // 0x38: uint32 unknown56
    // 0x3C: uint32 playercount

    if (size < 0x40)
        return false;

    uint32_t playerCount;
    memcpy(&playerCount, buf + 0x3C, 4);

    LogFramework("WhoMulticlass: Processing who response — %u player(s)", playerCount);

    offset = 0x40;  // Skip header

    // --- Parse each player entry ---
    for (uint32_t i = 0; i < playerCount; ++i)
    {
        uint32_t formatMsgId, padding1, padding2;
        char     name[64] = {};
        uint32_t rankMsgId;
        char     guild[128] = {};
        uint32_t unknown80_0, unknown80_1;
        uint32_t zoneMsgId, zone;
        uint32_t classValue, level, race;
        char     account[64] = {};
        uint32_t ending;

        if (!ReadUint32(buf, size, offset, formatMsgId)) return false;
        if (!ReadUint32(buf, size, offset, padding1))    return false;
        if (!ReadUint32(buf, size, offset, padding2))    return false;
        if (!ReadString(buf, size, offset, name, sizeof(name))) return false;
        if (!ReadUint32(buf, size, offset, rankMsgId))   return false;
        if (!ReadString(buf, size, offset, guild, sizeof(guild))) return false;
        if (!ReadUint32(buf, size, offset, unknown80_0)) return false;
        if (!ReadUint32(buf, size, offset, unknown80_1)) return false;
        if (!ReadUint32(buf, size, offset, zoneMsgId))   return false;
        if (!ReadUint32(buf, size, offset, zone))        return false;
        if (!ReadUint32(buf, size, offset, classValue))  return false;
        if (!ReadUint32(buf, size, offset, level))       return false;
        if (!ReadUint32(buf, size, offset, race))        return false;
        if (!ReadString(buf, size, offset, account, sizeof(account))) return false;
        if (!ReadUint32(buf, size, offset, ending))      return false;

        // --- Format the output line ---
        const char* rankTag = GetRankTag(rankMsgId);
        const char* raceName = GetRaceName(race);

        std::string classStr;
        if (IsMulticlass(classValue))
            classStr = DecodeClassBitmask(classValue);
        else if (classValue >= 1 && classValue <= 16)
            classStr = GetSingleClassName(classValue);
        else if (classValue != 0)
            classStr = DecodeClassBitmask(classValue);  // Single class as bitmask
        else
            classStr = "Unknown";

        // Anonymous handling
        if (formatMsgId == 5024)
        {
            // Fully anonymous: show name only
            WriteChatf("%s %s[ANONYMOUS]", rankTag, name);
        }
        else if (formatMsgId == 5023)
        {
            // Partially anonymous: show name + race
            WriteChatf("%s %s[ANONYMOUS] %s", rankTag, name, raceName);
        }
        else
        {
            // Full display: Pyrelight format
            // "* GM *  Morsal - Level 100 Iksar  (Shaman/Necromancer/Magician)"
            if (guild[0] != '\0')
            {
                WriteChatf("%s %s %s - Level %u %s  (%s)",
                    rankTag, name, guild, level, raceName, classStr.c_str());
            }
            else
            {
                WriteChatf("%s %s - Level %u %s  (%s)",
                    rankTag, name, level, raceName, classStr.c_str());
            }
        }
    }

    // --- Output footer ---
    if (playerCount == 0)
        WriteChatf("There are no players in EverQuest that match those who filters.");
    else if (playerCount == 1)
        WriteChatf("There is %u player in EverQuest.", playerCount);
    else
        WriteChatf("There are %u players in EverQuest.", playerCount);

    return true;
}
