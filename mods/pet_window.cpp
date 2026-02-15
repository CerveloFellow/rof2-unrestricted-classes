/**
 * @file pet_window.cpp
 * @brief PetWindow mod — POC for multi-pet display in the Pet Info Window.
 *
 * Phase 1: Locate the live CPetInfoWnd by scanning CXWndManager's window list.
 * Phase 2: Walk child widget tree, dump all children for analysis.
 *
 * Uses /petwindebug [phase] to run diagnostics.
 *
 * @date 2026-02-14
 */

#include "pch.h"
#include "pet_window.h"
#include "../core.h"
#include "../game_state.h"
#include "../commands.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <windows.h>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// CXWnd memory layout (from eqlib CXWnd.h)
// ---------------------------------------------------------------------------
// CXWnd inherits TListNode<CXWnd> then TList<CXWnd>:
//   +0x00: CXWnd* m_pPrev       (TListNode - previous sibling)
//   +0x04: CXWnd* m_pNext       (TListNode - next sibling)
//   +0x08: TList* m_pList       (TListNode - parent's child list)
//   +0x0C: CXWnd* m_pFirstNode  (TList - first child)
//   +0x10: CXWnd* m_pLastNode   (TList - last child)
//   +0x14: vtable pointer
//   +0x18: CXWnd data members start
//   +0x60: CXRect Location      {left, top, right, bottom}
//   +0xD8: uint32 XMLIndex
//   +0x1A8: CXStr WindowText
//   +0x1D8: end of CXWnd
//
// CSidlScreenWnd adds:
//   +0x1DC: CXStr SidlText

// MSVC places vtable FIRST when class introduces virtuals and bases have none
static constexpr uint32_t OFF_CXWND_VTABLE       = 0x00;
static constexpr uint32_t OFF_CXWND_PREV_SIBLING = 0x04;  // TListNode::m_pPrev
static constexpr uint32_t OFF_CXWND_NEXT_SIBLING = 0x08;  // TListNode::m_pNext
static constexpr uint32_t OFF_CXWND_PARENT_LIST  = 0x0C;  // TListNode::m_pList
static constexpr uint32_t OFF_CXWND_FIRST_CHILD  = 0x10;  // TList::m_pFirstNode
static constexpr uint32_t OFF_CXWND_LAST_CHILD   = 0x14;  // TList::m_pLastNode
static constexpr uint32_t OFF_CXWND_LOCATION     = 0x60;
static constexpr uint32_t OFF_CXWND_XMLINDEX     = 0xD8;
static constexpr uint32_t OFF_CXWND_RIGHTOFFSET   = 0x0FC;
static constexpr uint32_t OFF_CXWND_BOTTOMOFFSET  = 0x100;
static constexpr uint32_t OFF_CXWND_LEFTOFFSET    = 0x184;
static constexpr uint32_t OFF_CXWND_WINDOWTEXT    = 0x1A8;
static constexpr uint32_t OFF_CXWND_TOPOFFSET     = 0x1D0;
static constexpr uint32_t OFF_SIDL_TEXT            = 0x1DC;

// CXStr / CStrRep layout
static constexpr uint32_t OFF_CXSTR_REP_UTF8 = 0x14;
static constexpr uint32_t OFF_CXSTR_REP_LEN  = 0x08;

// CXWndManager layout
static constexpr uint32_t OFF_WNDMGR_WINDOWS_COUNT = 0x04;
static constexpr uint32_t OFF_WNDMGR_WINDOWS_ARRAY = 0x08;

// Known vtable addresses for widget type identification (raw, pre-ASLR)
static constexpr uint32_t VFTABLE_CGaugeWnd  = 0x9E87A8;
static constexpr uint32_t VFTABLE_CButtonWnd = 0xA1B41C;
static constexpr uint32_t VFTABLE_CXWnd      = 0xA19C74;

// CXWnd vtable offsets
static constexpr uint32_t VTOFF_UPDATEGEOMETRY = 0x11C;
static constexpr uint32_t VTOFF_SETWINDOWTEXT  = 0x124;

// ---------------------------------------------------------------------------
// CXRect helper (same layout as EQ's CXRect)
// ---------------------------------------------------------------------------
struct CXRect { int left, top, right, bottom; };

// ---------------------------------------------------------------------------
// CStrRep helper — constructs a temporary CXStr for passing to game functions.
// The CStrRep is allocated on the heap so it survives function calls that
// store the CXStr (like SetWindowText). RefCount set high to prevent free.
// ---------------------------------------------------------------------------
struct CStrRep
{
    int   refCount;   // +0x00
    int   alloc;      // +0x04
    int   length;     // +0x08
    int   encoding;   // +0x0C
    void* freeList;   // +0x10
    char  data[1];    // +0x14 (variable length)
};

// Allocate a CStrRep on the process heap. Returns the CStrRep pointer,
// which IS the CXStr value (CXStr = CStrRep*).
static void* MakeCXStr(const char* text)
{
    int len = (int)strlen(text);
    int repSize = 0x14 + len + 1;
    CStrRep* rep = (CStrRep*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, repSize);
    if (!rep) return nullptr;
    rep->refCount = 100;     // high refcount prevents game from freeing
    rep->alloc    = len + 1;
    rep->length   = len;
    rep->encoding = 0;       // UTF-8
    rep->freeList = nullptr;
    memcpy(rep->data, text, len + 1);
    return (void*)rep;
}

// ---------------------------------------------------------------------------
// Safe memory read helpers using SEH
// ---------------------------------------------------------------------------
static bool SafeReadUint32(uintptr_t addr, uint32_t& out)
{
    __try
    {
        out = *(uint32_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeReadPtr(uintptr_t addr, uintptr_t& out)
{
    __try
    {
        out = *(uintptr_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeReadInt(uintptr_t addr, int& out)
{
    __try
    {
        out = *(int*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Pointer validation helper
// ---------------------------------------------------------------------------
static inline bool IsValidPtr(uintptr_t p)
{
    return p >= 0x10000 && p < 0x7FFF0000;
}

// ---------------------------------------------------------------------------
// Read a CXStr from a given address. Returns the string or nullptr.
// The address should point to the CXStr field (which is a CStrRep*).
// ---------------------------------------------------------------------------
static const char* ReadCXStr(uintptr_t cxstrAddr)
{
    __try
    {
        uintptr_t repPtr = *(uintptr_t*)cxstrAddr;
        if (!IsValidPtr(repPtr)) return nullptr;

        uint32_t len = *(uint32_t*)(repPtr + OFF_CXSTR_REP_LEN);
        if (len == 0 || len > 256) return nullptr;

        int refCount = *(int*)(repPtr + 0x00);
        if (refCount <= 0 || refCount > 10000) return nullptr;

        const char* str = (const char*)(repPtr + OFF_CXSTR_REP_UTF8);
        if (str[0] < 0x20 || str[0] > 0x7E) return nullptr;

        return str;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Read the SIDL name from a window (CSidlScreenWnd::SidlText at +0x1DC).
// ---------------------------------------------------------------------------
static const char* ReadSidlName(void* wnd)
{
    if (!wnd || !IsValidPtr((uintptr_t)wnd)) return nullptr;
    return ReadCXStr((uintptr_t)wnd + OFF_SIDL_TEXT);
}

// ---------------------------------------------------------------------------
// Read the WindowText from a CXWnd (+0x1A8).
// ---------------------------------------------------------------------------
static const char* ReadWindowText(void* wnd)
{
    if (!wnd || !IsValidPtr((uintptr_t)wnd)) return nullptr;
    return ReadCXStr((uintptr_t)wnd + OFF_CXWND_WINDOWTEXT);
}

// ---------------------------------------------------------------------------
// Identify widget type by comparing vtable pointer against known addresses.
// Returns a human-readable type string.
// ---------------------------------------------------------------------------
static const char* IdentifyWidgetType(void* wnd, uintptr_t eqBase)
{
    if (!wnd) return "null";

    uintptr_t vtable = 0;
    if (!SafeReadPtr((uintptr_t)wnd + OFF_CXWND_VTABLE, vtable))
        return "unreadable";

    // Convert runtime vtable address back to raw offset for comparison
    // eqBase is the ASLR base; raw = vtable - eqBase + 0x400000
    uint32_t rawVtable = (uint32_t)(vtable - eqBase + 0x400000);

    if (rawVtable == VFTABLE_CButtonWnd) return "CButtonWnd";
    if (rawVtable == VFTABLE_CXWnd)      return "CXWnd";

    // Log unknown vtable for future identification
    static char buf[64];
    snprintf(buf, sizeof(buf), "vt=0x%06X", rawVtable);
    return buf;
}

// ---------------------------------------------------------------------------
// Static instance for command callbacks
// ---------------------------------------------------------------------------
static PetWindow* s_instance = nullptr;

static void CmdPetWinDebug(eqlib::PlayerClient* /*pChar*/, const char* szLine)
{
    if (!s_instance) return;

    // Parse subcommand
    if (szLine && *szLine)
    {
        // Skip leading spaces
        while (*szLine == ' ') szLine++;

        if (_strnicmp(szLine, "children", 8) == 0)
        {
            s_instance->DebugChildren();
            return;
        }
        if (_strnicmp(szLine, "create", 6) == 0)
        {
            s_instance->CreateGauge();
            return;
        }
    }

    // Default: Phase 1 find
    s_instance->DebugPetWindow();
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------
const char* PetWindow::GetName() const { return "PetWindow"; }

bool PetWindow::Initialize()
{
    s_instance = this;
    Commands::AddCommand("/petwindebug", CmdPetWinDebug);

    LogFramework("PetWindow: initialized - /petwindebug [children]");
    return true;
}

void PetWindow::Shutdown()
{
    Commands::RemoveCommand("/petwindebug");
    s_instance = nullptr;
    m_petInfoWnd = nullptr;
}

void PetWindow::OnPulse()
{
    // No per-frame work needed for Phase 3 - TopOffset/BottomOffset persist
}

bool PetWindow::OnIncomingMessage(uint32_t, const void*, uint32_t) { return true; }

void PetWindow::OnSetGameState(int)
{
    m_petInfoWnd = nullptr;
}

// ---------------------------------------------------------------------------
// Phase 1: Find CPetInfoWnd
// ---------------------------------------------------------------------------
void* PetWindow::FindPetInfoWnd()
{
    void* wndMgr = GameState::GetWndManager();
    if (!wndMgr) return nullptr;

    uint32_t count = 0;
    uintptr_t arrayPtr = 0;

    if (!SafeReadUint32((uintptr_t)wndMgr + OFF_WNDMGR_WINDOWS_COUNT, count))
        return nullptr;
    if (!SafeReadPtr((uintptr_t)wndMgr + OFF_WNDMGR_WINDOWS_ARRAY, arrayPtr))
        return nullptr;

    if (!IsValidPtr(arrayPtr) || count == 0 || count > 50000)
        return nullptr;

    void** array = (void**)arrayPtr;

    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t wndPtr = 0;
        if (!SafeReadPtr((uintptr_t)&array[i], wndPtr)) continue;
        if (!IsValidPtr(wndPtr)) continue;

        const char* name = ReadSidlName((void*)wndPtr);
        if (name && strcmp(name, "PetInfoWindow") == 0)
        {
            return (void*)wndPtr;
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// /petwindebug (Phase 1)
// ---------------------------------------------------------------------------
void PetWindow::DebugPetWindow()
{
    WriteChatf("--- PetWindow Phase 1: Find ---");

    void* wndMgr = GameState::GetWndManager();
    if (!wndMgr)
    {
        WriteChatf("\ar  Window manager not available");
        return;
    }

    m_petInfoWnd = FindPetInfoWnd();

    if (m_petInfoWnd)
    {
        WriteChatf("\ag  PetInfoWindow FOUND at 0x%08X",
                   (unsigned int)(uintptr_t)m_petInfoWnd);

        __try
        {
            int left   = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x00);
            int top    = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x04);
            int right  = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x08);
            int bottom = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x0C);
            WriteChatf("  Size: %d x %d  Pos: (%d,%d)", right - left, bottom - top, left, top);

            int btnCount = 0;
            for (int i = 0; i < 14; i++)
            {
                void* btn = *(void**)((uintptr_t)m_petInfoWnd + 0x234 + i * 4);
                if (btn) btnCount++;
            }
            WriteChatf("  Buttons: %d/14, Buffs: 0x%08X",
                       btnCount,
                       (unsigned int)*(uintptr_t*)((uintptr_t)m_petInfoWnd + 0x2B4));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteChatf("\ar  Exception reading pet window!");
        }

        WriteChatf("  Use '/petwindebug children' for Phase 2");
    }
    else
    {
        WriteChatf("\ar  PetInfoWindow NOT FOUND (is it open?)");
    }
    WriteChatf("-------------------------------");
}

// ---------------------------------------------------------------------------
// Phase 2: Walk child widget tree
// ---------------------------------------------------------------------------
void PetWindow::DebugChildren()
{
    WriteChatf("--- PetWindow Phase 2: Children ---");

    // Ensure we have the pet window
    if (!m_petInfoWnd)
        m_petInfoWnd = FindPetInfoWnd();

    if (!m_petInfoWnd)
    {
        WriteChatf("\ar  PetInfoWindow not found. Run /petwindebug first.");
        return;
    }

    // Get EQ base address for vtable comparison
    uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");
    WriteChatf("  eqgame.exe base: 0x%08X", (unsigned int)eqBase);

    // Walk immediate children via TList::m_pFirstNode at +0x0C
    uintptr_t firstChild = 0;
    if (!SafeReadPtr((uintptr_t)m_petInfoWnd + OFF_CXWND_FIRST_CHILD, firstChild))
    {
        WriteChatf("\ar  Failed to read first child pointer");
        return;
    }

    if (!IsValidPtr(firstChild))
    {
        WriteChatf("\ay  No children found (firstChild=0x%08X)", (unsigned int)firstChild);
        return;
    }

    int childIdx = 0;
    uintptr_t child = firstChild;

    while (IsValidPtr(child) && childIdx < 200) // safety limit
    {
        __try
        {
            // Read child properties
            int left = 0, top = 0, right = 0, bottom = 0;
            SafeReadInt(child + OFF_CXWND_LOCATION + 0x00, left);
            SafeReadInt(child + OFF_CXWND_LOCATION + 0x04, top);
            SafeReadInt(child + OFF_CXWND_LOCATION + 0x08, right);
            SafeReadInt(child + OFF_CXWND_LOCATION + 0x0C, bottom);

            uint32_t xmlIndex = 0;
            SafeReadUint32(child + OFF_CXWND_XMLINDEX, xmlIndex);

            const char* wndText = ReadWindowText((void*)child);
            const char* sidlName = ReadSidlName((void*)child);
            const char* widgetType = IdentifyWidgetType((void*)child, eqBase);

            // Count this child's own children
            uintptr_t grandchild = 0;
            int subChildCount = 0;
            if (SafeReadPtr(child + OFF_CXWND_FIRST_CHILD, grandchild) && IsValidPtr(grandchild))
            {
                uintptr_t gc = grandchild;
                while (IsValidPtr(gc) && subChildCount < 500)
                {
                    subChildCount++;
                    uintptr_t next = 0;
                    if (!SafeReadPtr(gc + OFF_CXWND_NEXT_SIBLING, next)) break;
                    gc = next;
                }
            }

            // Print to chat (compact format)
            char line[256];
            snprintf(line, sizeof(line),
                     "  [%d] %s %dx%d @(%d,%d) xml=%u kids=%d",
                     childIdx, widgetType,
                     right - left, bottom - top, left, top,
                     xmlIndex, subChildCount);

            if (sidlName)
            {
                char extra[128];
                snprintf(extra, sizeof(extra), " sidl='%s'", sidlName);
                strncat_s(line, sizeof(line), extra, _TRUNCATE);
            }
            if (wndText)
            {
                char extra[128];
                snprintf(extra, sizeof(extra), " text='%.30s'", wndText);
                strncat_s(line, sizeof(line), extra, _TRUNCATE);
            }

            WriteChatf("%s", line);

            // Also log full details to file
            LogFramework("PetWindow child[%d]: addr=0x%08X type=%s rect=(%d,%d,%d,%d) "
                         "xml=%u kids=%d sidl=%s text=%s",
                         childIdx, (unsigned int)child, widgetType,
                         left, top, right, bottom,
                         xmlIndex, subChildCount,
                         sidlName ? sidlName : "(none)",
                         wndText ? wndText : "(none)");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteChatf("  [%d] \ar<exception reading child at 0x%08X>",
                       childIdx, (unsigned int)child);
        }

        // Advance to next sibling
        uintptr_t next = 0;
        if (!SafeReadPtr(child + OFF_CXWND_NEXT_SIBLING, next)) break;
        child = next;
        childIdx++;
    }

    WriteChatf("  Total immediate children: %d", childIdx);
    WriteChatf("  Full details in dinput8_proxy.log");
    WriteChatf("-------------------------------");
}

// ---------------------------------------------------------------------------
// Phase 4: Find and verify the XML-created gauge widgets
// ---------------------------------------------------------------------------
// The SIDL XML (EQUI_PetInfoWindow.xml) now defines PIW_Pet2HPGauge and
// PIW_Pet3HPGauge as children of PetInfoWindow. This function finds them
// by iterating children and matching by WindowText or position.

void PetWindow::CreateGauge()
{
    WriteChatf("--- PetWindow Phase 4: Find XML Gauges ---");

    if (!m_petInfoWnd)
        m_petInfoWnd = FindPetInfoWnd();

    if (!m_petInfoWnd)
    {
        WriteChatf("\ar  PetInfoWindow not found.");
        return;
    }

    uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");

    // Walk children looking for our 2 new gauges (Pet 2 and Pet 3)
    // They're CGaugeWnd with text "Pet 2" and "Pet 3"
    uintptr_t child = 0;
    if (!SafeReadPtr((uintptr_t)m_petInfoWnd + OFF_CXWND_FIRST_CHILD, child) || !IsValidPtr(child))
    {
        WriteChatf("\ar  No children found.");
        return;
    }

    int gaugeCount = 0;
    int childIdx = 0;

    while (IsValidPtr(child) && childIdx < 200)
    {
        __try
        {
            uintptr_t vt = 0;
            SafeReadPtr(child + OFF_CXWND_VTABLE, vt);
            uint32_t rawVt = (uint32_t)(vt - eqBase + 0x400000);

            if (rawVt == VFTABLE_CGaugeWnd)
            {
                const char* text = ReadWindowText((void*)child);
                gaugeCount++;

                if (text && strcmp(text, "Pet 2") == 0)
                {
                    m_newGauge1 = (void*)child;
                    WriteChatf("\ag  Found Pet 2 gauge: child[%d] at 0x%08X",
                               childIdx, (unsigned int)child);
                }
                else if (text && strcmp(text, "Pet 3") == 0)
                {
                    m_newGauge2 = (void*)child;
                    WriteChatf("\ag  Found Pet 3 gauge: child[%d] at 0x%08X",
                               childIdx, (unsigned int)child);
                }
                else
                {
                    WriteChatf("  Gauge[%d]: text='%s'", childIdx, text ? text : "(null)");
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        uintptr_t next = 0;
        if (!SafeReadPtr(child + OFF_CXWND_NEXT_SIBLING, next)) break;
        child = next;
        childIdx++;
    }

    WriteChatf("  Total gauges found: %d", gaugeCount);

    if (m_newGauge1)
        WriteChatf("\ag  Pet 2 gauge cached for updates");
    else
        WriteChatf("\ar  Pet 2 gauge NOT found! Check EQUI_PetInfoWindow.xml has PIW_Pet2HPGauge");

    if (m_newGauge2)
        WriteChatf("\ag  Pet 3 gauge cached for updates");
    else
        WriteChatf("\ar  Pet 3 gauge NOT found! Check EQUI_PetInfoWindow.xml has PIW_Pet3HPGauge");

    WriteChatf("-------------------------------");
}

