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

// clang-format off
#include "pch.h"
// clang-format on
#include "pet_window.h"
#include "../commands.h"
#include "../core.h"
#include "../game_state.h"
#include "multi_pet.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>
#include <cstring>
#include <windows.h>

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
static constexpr uint32_t OFF_CXWND_VTABLE = 0x00;
static constexpr uint32_t OFF_CXWND_PREV_SIBLING = 0x04; // TListNode::m_pPrev
static constexpr uint32_t OFF_CXWND_NEXT_SIBLING = 0x08; // TListNode::m_pNext
static constexpr uint32_t OFF_CXWND_PARENT_LIST = 0x0C;  // TListNode::m_pList
static constexpr uint32_t OFF_CXWND_FIRST_CHILD = 0x10;  // TList::m_pFirstNode
static constexpr uint32_t OFF_CXWND_LAST_CHILD = 0x14;   // TList::m_pLastNode
static constexpr uint32_t OFF_CXWND_LOCATION = 0x60;
static constexpr uint32_t OFF_CXWND_XMLINDEX = 0xD8;
static constexpr uint32_t OFF_CXWND_RIGHTOFFSET = 0x0FC;
static constexpr uint32_t OFF_CXWND_BOTTOMOFFSET = 0x100;
static constexpr uint32_t OFF_CXWND_LEFTOFFSET = 0x184;
static constexpr uint32_t OFF_CXWND_WINDOWTEXT = 0x1A8;
static constexpr uint32_t OFF_CXWND_TOPOFFSET = 0x1D0;
static constexpr uint32_t OFF_SIDL_TEXT = 0x1DC;

// CXStr / CStrRep layout
static constexpr uint32_t OFF_CXSTR_REP_UTF8 = 0x14;
static constexpr uint32_t OFF_CXSTR_REP_LEN = 0x08;

// CXWndManager layout
static constexpr uint32_t OFF_WNDMGR_WINDOWS_COUNT = 0x04;
static constexpr uint32_t OFF_WNDMGR_WINDOWS_ARRAY = 0x08;

// Known vtable addresses for widget type identification (raw, pre-ASLR)
static constexpr uint32_t VFTABLE_CGaugeWnd = 0x9E87A8;
static constexpr uint32_t VFTABLE_CButtonWnd = 0xA1B41C;
static constexpr uint32_t VFTABLE_CXWnd = 0xA19C74;

// CXWnd vtable offsets
static constexpr uint32_t VTOFF_UPDATEGEOMETRY = 0x11C;
static constexpr uint32_t VTOFF_SETWINDOWTEXT = 0x124;

// Spawn HP offsets (from eqlib PlayerClient.h)
static constexpr uint32_t OFF_SPAWN_HPMAX = 0x02DC;
static constexpr uint32_t OFF_SPAWN_HPCURRENT = 0x02E4;

// CGaugeWnd offsets (from eqlib UI.h — relative to CXWnd base)
static constexpr uint32_t OFF_GAUGE_LASTFRAMEVAL = 0x1F8;    // float (0..100)
static constexpr uint32_t OFF_GAUGE_LASTFRAMETARGET = 0x204; // int
static constexpr uint32_t OFF_GAUGE_TARGETVAL = 0x238;       // int
static constexpr uint32_t OFF_GAUGE_USETARGETVAL = 0x23C;    // bool

// LabelCache — game-maintained struct with reliable XTarget HP percentages
// __LabelCache_x = 0xF74AB0 (raw offset, needs ASLR correction)
// ExtendedTargetHPPct at +0x8F4: int[20] indexed by XTarget slot
static constexpr uint32_t RAW_LABELCACHE = 0xF74AB0;
static constexpr uint32_t OFF_LC_XTARGET_HP_PCT = 0x8F4;
static constexpr int MAX_XTARGET_SLOTS = 20;

// ---------------------------------------------------------------------------
// Strip trailing digits from EQ pet names (e.g. "Kasarn000" -> "Kasarn")
// ---------------------------------------------------------------------------
static void CleanPetName(char *out, size_t outSize, const char *rawName) {
  if (!rawName || !rawName[0]) {
    strncpy_s(out, outSize, "", _TRUNCATE);
    return;
  }

  strncpy_s(out, outSize, rawName, _TRUNCATE);
  int len = (int)strlen(out);

  // Strip trailing digits
  while (len > 0 && out[len - 1] >= '0' && out[len - 1] <= '9')
    out[--len] = '\0';
}

// ---------------------------------------------------------------------------
// CXRect helper (same layout as EQ's CXRect)
// ---------------------------------------------------------------------------
struct CXRect {
  int left, top, right, bottom;
};

// ---------------------------------------------------------------------------
// CStrRep helper — constructs a temporary CXStr for passing to game functions.
// The CStrRep is allocated on the heap so it survives function calls that
// store the CXStr (like SetWindowText). RefCount set high to prevent free.
// ---------------------------------------------------------------------------
struct CStrRep {
  int refCount;   // +0x00
  int alloc;      // +0x04
  int length;     // +0x08
  int encoding;   // +0x0C
  void *freeList; // +0x10
  char data[1];   // +0x14 (variable length)
};

// Allocate a CStrRep on the process heap. Returns the CStrRep pointer,
// which IS the CXStr value (CXStr = CStrRep*).
static void *MakeCXStr(const char *text) {
  int len = (int)strlen(text);
  int repSize = 0x14 + len + 1;
  CStrRep *rep =
      (CStrRep *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, repSize);
  if (!rep)
    return nullptr;
  rep->refCount = 100; // high refcount prevents game from freeing
  rep->alloc = len + 1;
  rep->length = len;
  rep->encoding = 0; // UTF-8
  rep->freeList = nullptr;
  memcpy(rep->data, text, len + 1);
  return (void *)rep;
}

// ---------------------------------------------------------------------------
// Safe memory read helpers using SEH
// ---------------------------------------------------------------------------
static bool SafeReadUint32(uintptr_t addr, uint32_t &out) {
  __try {
    out = *(uint32_t *)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeReadPtr(uintptr_t addr, uintptr_t &out) {
  __try {
    out = *(uintptr_t *)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeReadInt(uintptr_t addr, int &out) {
  __try {
    out = *(int *)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Pointer validation helper
// ---------------------------------------------------------------------------
static inline bool IsValidPtr(uintptr_t p) {
  return p >= 0x10000 && p < 0x7FFF0000;
}

// ---------------------------------------------------------------------------
// Read a CXStr from a given address. Returns the string or nullptr.
// The address should point to the CXStr field (which is a CStrRep*).
// ---------------------------------------------------------------------------
static const char *ReadCXStr(uintptr_t cxstrAddr) {
  __try {
    uintptr_t repPtr = *(uintptr_t *)cxstrAddr;
    if (!IsValidPtr(repPtr))
      return nullptr;

    uint32_t len = *(uint32_t *)(repPtr + OFF_CXSTR_REP_LEN);
    if (len == 0 || len > 256)
      return nullptr;

    int refCount = *(int *)(repPtr + 0x00);
    if (refCount <= 0 || refCount > 10000)
      return nullptr;

    const char *str = (const char *)(repPtr + OFF_CXSTR_REP_UTF8);
    if (str[0] < 0x20 || str[0] > 0x7E)
      return nullptr;

    return str;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Read the SIDL name from a window (CSidlScreenWnd::SidlText at +0x1DC).
// ---------------------------------------------------------------------------
static const char *ReadSidlName(void *wnd) {
  if (!wnd || !IsValidPtr((uintptr_t)wnd))
    return nullptr;
  return ReadCXStr((uintptr_t)wnd + OFF_SIDL_TEXT);
}

// ---------------------------------------------------------------------------
// Read the WindowText from a CXWnd (+0x1A8).
// ---------------------------------------------------------------------------
static const char *ReadWindowText(void *wnd) {
  if (!wnd || !IsValidPtr((uintptr_t)wnd))
    return nullptr;
  return ReadCXStr((uintptr_t)wnd + OFF_CXWND_WINDOWTEXT);
}

// ---------------------------------------------------------------------------
// Identify widget type by comparing vtable pointer against known addresses.
// Returns a human-readable type string.
// ---------------------------------------------------------------------------
static const char *IdentifyWidgetType(void *wnd, uintptr_t eqBase) {
  if (!wnd)
    return "null";

  uintptr_t vtable = 0;
  if (!SafeReadPtr((uintptr_t)wnd + OFF_CXWND_VTABLE, vtable))
    return "unreadable";

  // Convert runtime vtable address back to raw offset for comparison
  // eqBase is the ASLR base; raw = vtable - eqBase + 0x400000
  uint32_t rawVtable = (uint32_t)(vtable - eqBase + 0x400000);

  if (rawVtable == VFTABLE_CButtonWnd)
    return "CButtonWnd";
  if (rawVtable == VFTABLE_CXWnd)
    return "CXWnd";

  // Log unknown vtable for future identification
  static char buf[64];
  snprintf(buf, sizeof(buf), "vt=0x%06X", rawVtable);
  return buf;
}

// ---------------------------------------------------------------------------
// Static instance for command callbacks
// ---------------------------------------------------------------------------
static PetWindow *s_instance = nullptr;

// ---------------------------------------------------------------------------
// Phase 6: PetWindowOverride to hook UI clicks
// ---------------------------------------------------------------------------
#include "../hooks.h"

// ---------------------------------------------------------------------------
// Phase 6: Hook UI Clicks via Detours
// ---------------------------------------------------------------------------
extern "C" uintptr_t EQGameBaseAddress;

using WndNotification_t = int(__fastcall *)(void *thisPtr, void *edx,
                                            void *sender, uint32_t message,
                                            void *data);
static WndNotification_t WndNotification_Original = nullptr;

static int __fastcall WndNotification_Detour(void *thisPtr, void *edx,
                                             void *sender, uint32_t message,
                                             void *data) {
  if (message == 1 /* XWM_LCLICK */) {
    if (s_instance && s_instance->HandleClick(sender)) {
      return 1; // Handled
    }
  }
  return WndNotification_Original(thisPtr, edx, sender, message, data);
}

static void CmdPetWinDebug(eqlib::PlayerClient * /*pChar*/,
                           const char *szLine) {
  if (!s_instance)
    return;

  // Parse subcommand
  if (szLine && *szLine) {
    // Skip leading spaces
    while (*szLine == ' ')
      szLine++;

    if (_strnicmp(szLine, "children", 8) == 0) {
      s_instance->DebugChildren();
      return;
    }
    if (_strnicmp(szLine, "create", 6) == 0) {
      s_instance->CreateGauge();
      return;
    }
    if (_strnicmp(szLine, "hp", 2) == 0) {
      s_instance->DebugHP();
      return;
    }
  }

  // Default: Phase 1 find
  s_instance->DebugPetWindow();
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------
const char *PetWindow::GetName() const { return "PetWindow"; }

bool PetWindow::Initialize() {
  s_instance = this;
  Commands::AddCommand("/petwindebug", CmdPetWinDebug);

  uintptr_t wndNotifAddr =
      static_cast<uintptr_t>(CSidlScreenWnd__WndNotification_x) -
      eqlib::EQGamePreferredAddress + EQGameBaseAddress;
  WndNotification_Original = reinterpret_cast<WndNotification_t>(wndNotifAddr);

  Hooks::Install("CSidlScreenWnd::WndNotification",
                 reinterpret_cast<void **>(&WndNotification_Original),
                 reinterpret_cast<void *>(&WndNotification_Detour));

  LogFramework("PetWindow: initialized - /petwindebug [children]");
  return true;
}

void PetWindow::Shutdown() {
  Commands::RemoveCommand("/petwindebug");
  s_instance = nullptr;
  m_petInfoWnd = nullptr;
}

void PetWindow::OnPulse() {
  if (GameState::GetGameState() != 5)
    return;

  // Auto-initialize: find pet window and cache gauge pointers
  // Delay a bit after zone-in to let UI load
  if (!m_initialized) {
    if (++m_initCounter < 60)
      return; // wait ~1 second

    m_petInfoWnd = FindPetInfoWnd();
    if (!m_petInfoWnd)
      return;

    // Find gauges by walking children and matching text
    CreateGauge();

    if (m_newGauge1 && m_newGauge2) {
      m_initialized = true;
      LogFramework("PetWindow: Auto-initialized — gauges cached");
    }
    return;
  }

  // Phase 5: Update gauge values from MultiPet data
  auto *multiPet = MultiPet::GetInstance();
  if (!multiPet)
    return;

  const auto &pets = multiPet->GetTrackedPets();

  // Resolve LabelCache address (ASLR-corrected) for reliable XTarget HP
  uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");
  uintptr_t labelCache = (RAW_LABELCACHE - 0x400000) + eqBase;

  // Only update gauges when we have actual pet data — don't write defaults
  // so the gauges keep their XML text until MultiPet re-detects pets

  // Pet 2 gauge (first secondary pet)
  if (pets.size() >= 1 && pets[0].pSpawn) {
    int pct = 0;
    // Use XTarget HP percentage from LabelCache (game-maintained, always
    // accurate)
    if (pets[0].xtSlot >= 0 && pets[0].xtSlot < MAX_XTARGET_SLOTS) {
      __try {
        pct = *(int *)(labelCache + OFF_LC_XTARGET_HP_PCT + pets[0].xtSlot * 4);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    } else {
      // Fallback to spawn HP if no XTarget slot assigned
      __try {
        int hpCur =
            *(int32_t *)((uintptr_t)pets[0].pSpawn + OFF_SPAWN_HPCURRENT);
        int hpMax = *(int32_t *)((uintptr_t)pets[0].pSpawn + OFF_SPAWN_HPMAX);
        pct = (hpMax > 0) ? (hpCur * 100 / hpMax) : 0;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    char cleanName[64];
    CleanPetName(cleanName, sizeof(cleanName), pets[0].name);
    UpdateGauge(m_newGauge1, cleanName, pct);
  }

  // Pet 3 gauge (second secondary pet)
  if (pets.size() >= 2 && pets[1].pSpawn) {
    int pct = 0;
    if (pets[1].xtSlot >= 0 && pets[1].xtSlot < MAX_XTARGET_SLOTS) {
      __try {
        pct = *(int *)(labelCache + OFF_LC_XTARGET_HP_PCT + pets[1].xtSlot * 4);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    } else {
      __try {
        int hpCur =
            *(int32_t *)((uintptr_t)pets[1].pSpawn + OFF_SPAWN_HPCURRENT);
        int hpMax = *(int32_t *)((uintptr_t)pets[1].pSpawn + OFF_SPAWN_HPMAX);
        pct = (hpMax > 0) ? (hpCur * 100 / hpMax) : 0;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    char cleanName[64];
    CleanPetName(cleanName, sizeof(cleanName), pets[1].name);
    UpdateGauge(m_newGauge2, cleanName, pct);
  }
}

bool PetWindow::OnIncomingMessage(uint32_t, const void *, uint32_t) {
  return true;
}

void PetWindow::OnSetGameState(int) {
  m_petInfoWnd = nullptr;
  m_newGauge1 = nullptr;
  m_newGauge2 = nullptr;
  m_initialized = false;
  m_initCounter = 0;
}

// ---------------------------------------------------------------------------
// Phase 1: Find CPetInfoWnd
// ---------------------------------------------------------------------------
void *PetWindow::FindPetInfoWnd() {
  void *wndMgr = GameState::GetWndManager();
  if (!wndMgr)
    return nullptr;

  uint32_t count = 0;
  uintptr_t arrayPtr = 0;

  if (!SafeReadUint32((uintptr_t)wndMgr + OFF_WNDMGR_WINDOWS_COUNT, count))
    return nullptr;
  if (!SafeReadPtr((uintptr_t)wndMgr + OFF_WNDMGR_WINDOWS_ARRAY, arrayPtr))
    return nullptr;

  if (!IsValidPtr(arrayPtr) || count == 0 || count > 50000)
    return nullptr;

  void **array = (void **)arrayPtr;

  for (uint32_t i = 0; i < count; i++) {
    uintptr_t wndPtr = 0;
    if (!SafeReadPtr((uintptr_t)&array[i], wndPtr))
      continue;
    if (!IsValidPtr(wndPtr))
      continue;

    const char *name = ReadSidlName((void *)wndPtr);
    if (name && strcmp(name, "PetInfoWindow") == 0) {
      return (void *)wndPtr;
    }
  }

  return nullptr;
}

// ---------------------------------------------------------------------------
// /petwindebug (Phase 1)
// ---------------------------------------------------------------------------
void PetWindow::DebugPetWindow() {
    void* wndMgr = GameState::GetWndManager();
    if (!wndMgr) {
        return;
    }

    m_petInfoWnd = FindPetInfoWnd();

    if (m_petInfoWnd) {
        __try {
            int left = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x00);
            int top = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x04);
            int right = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x08);
            int bottom = *(int*)((uintptr_t)m_petInfoWnd + OFF_CXWND_LOCATION + 0x0C);

            (void)left;
            (void)top;
            (void)right;
            (void)bottom;

            int btnCount = 0;
            for (int i = 0; i < 14; i++) {
                void* btn = *(void**)((uintptr_t)m_petInfoWnd + 0x234 + i * 4);
                if (btn)
                    btnCount++;
            }

            (void)btnCount;
            (void)*(uintptr_t*)((uintptr_t)m_petInfoWnd + 0x2B4);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // intentionally silent
        }
    }
}


// ---------------------------------------------------------------------------
// Phase 2: Walk child widget tree (silent)
// ---------------------------------------------------------------------------
void PetWindow::DebugChildren() {
    // Ensure we have the pet window
    if (!m_petInfoWnd)
        m_petInfoWnd = FindPetInfoWnd();

    if (!m_petInfoWnd) {
        return;
    }

    // Get EQ base address for vtable comparison
    uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");
    (void)eqBase;

    // Walk immediate children via TList::m_pFirstNode
    uintptr_t firstChild = 0;
    if (!SafeReadPtr((uintptr_t)m_petInfoWnd + OFF_CXWND_FIRST_CHILD,
        firstChild)) {
        return;
    }

    if (!IsValidPtr(firstChild)) {
        return;
    }

    int childIdx = 0;
    uintptr_t child = firstChild;

    while (IsValidPtr(child) && childIdx < 200) {
        __try {
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
            if (SafeReadPtr(child + OFF_CXWND_FIRST_CHILD, grandchild) &&
                IsValidPtr(grandchild)) {
                uintptr_t gc = grandchild;
                while (IsValidPtr(gc) && subChildCount < 500) {
                    subChildCount++;
                    uintptr_t next = 0;
                    if (!SafeReadPtr(gc + OFF_CXWND_NEXT_SIBLING, next))
                        break;
                    gc = next;
                }
            }

            // Touch values to avoid unused warnings
            (void)left;
            (void)top;
            (void)right;
            (void)bottom;
            (void)xmlIndex;
            (void)wndText;
            (void)sidlName;
            (void)widgetType;
            (void)subChildCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // intentionally silent
        }

        // Advance to next sibling
        uintptr_t next = 0;
        if (!SafeReadPtr(child + OFF_CXWND_NEXT_SIBLING, next))
            break;
        child = next;
        childIdx++;
    }

    (void)childIdx;
}


// ---------------------------------------------------------------------------
// Phase 4: Find and verify the XML-created gauge widgets (silent)
// ---------------------------------------------------------------------------
void PetWindow::CreateGauge() {
    if (!m_petInfoWnd)
        m_petInfoWnd = FindPetInfoWnd();

    if (!m_petInfoWnd) {
        return;
    }

    uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");
    (void)eqBase;

    // Walk children looking for our 2 new gauges (Pet 2 and Pet 3)
    uintptr_t child = 0;
    if (!SafeReadPtr((uintptr_t)m_petInfoWnd + OFF_CXWND_FIRST_CHILD, child) ||
        !IsValidPtr(child)) {
        return;
    }

    int gaugeCount = 0;
    int childIdx = 0;

    while (IsValidPtr(child) && childIdx < 200) {
        __try {
            uintptr_t vt = 0;
            SafeReadPtr(child + OFF_CXWND_VTABLE, vt);
            uint32_t rawVt = (uint32_t)(vt - eqBase + 0x400000);

            if (rawVt == VFTABLE_CGaugeWnd) {
                const char* text = ReadWindowText((void*)child);
                gaugeCount++;

                if (text && strcmp(text, "Pet 2") == 0) {
                    m_newGauge1 = (void*)child;
                }
                else if (text && strcmp(text, "Pet 3") == 0) {
                    m_newGauge2 = (void*)child;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // intentionally silent
        }

        uintptr_t next = 0;
        if (!SafeReadPtr(child + OFF_CXWND_NEXT_SIBLING, next))
            break;
        child = next;
        childIdx++;
    }

    (void)gaugeCount;
}

// ---------------------------------------------------------------------------
// Phase 5 debug: dump raw HP values from tracked pets (silent)
// ---------------------------------------------------------------------------
void PetWindow::DebugHP() {
    auto* multiPet = MultiPet::GetInstance();
    if (!multiPet) {
        return;
    }

    // Resolve LabelCache for XTarget HP comparison
    uintptr_t eqBase = (uintptr_t)GetModuleHandleA("eqgame.exe");
    uintptr_t labelCache = (RAW_LABELCACHE - 0x400000) + eqBase;

    const auto& pets = multiPet->GetTrackedPets();

    for (int i = 0; i < (int)pets.size(); ++i) {
        const auto& pet = pets[i];
        char cleanName[64];
        CleanPetName(cleanName, sizeof(cleanName), pet.name);

        if (pet.pSpawn) {
            __try {
                int hpCur = *(int32_t*)((uintptr_t)pet.pSpawn + OFF_SPAWN_HPCURRENT);
                int hpMax = *(int32_t*)((uintptr_t)pet.pSpawn + OFF_SPAWN_HPMAX);
                int spawnPct = (hpMax > 0) ? (hpCur * 100 / hpMax) : 0;

                int xtPct = -1;
                if (pet.xtSlot >= 0 && pet.xtSlot < MAX_XTARGET_SLOTS)
                    xtPct = *(int*)(labelCache + OFF_LC_XTARGET_HP_PCT + pet.xtSlot * 4);

                (void)hpCur;
                (void)hpMax;
                (void)spawnPct;
                (void)xtPct;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                // intentionally silent
            }
        }
    }

    // Gauge state inspection (silent)
    if (m_newGauge1) {
        __try {
            float fillVal =
                *(float*)((uintptr_t)m_newGauge1 + OFF_GAUGE_LASTFRAMEVAL);
            int target =
                *(int*)((uintptr_t)m_newGauge1 + OFF_GAUGE_LASTFRAMETARGET);

            (void)fillVal;
            (void)target;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // intentionally silent
        }
    }

    if (m_newGauge2) {
        __try {
            float fillVal =
                *(float*)((uintptr_t)m_newGauge2 + OFF_GAUGE_LASTFRAMEVAL);
            int target =
                *(int*)((uintptr_t)m_newGauge2 + OFF_GAUGE_LASTFRAMETARGET);

            (void)fillVal;
            (void)target;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // intentionally silent
        }
    }
}


// ---------------------------------------------------------------------------
// Phase 5: Update a gauge with pet name and HP fill percentage
// ---------------------------------------------------------------------------

void PetWindow::UpdateGauge(void *gauge, const char *petName, int hpPercent) {
  if (!gauge)
    return;

  __try {
    // Clamp percentage
    if (hpPercent < 0)
      hpPercent = 0;
    if (hpPercent > 100)
      hpPercent = 100;

    // Update gauge fill: scale is 0-1000 (CalcFillRect uses value * 0.001f)
    int fillVal = hpPercent * 10; // 0-100% -> 0-1000
    *(float *)((uintptr_t)gauge + OFF_GAUGE_LASTFRAMEVAL) =
        static_cast<float>(fillVal);
    *(int *)((uintptr_t)gauge + OFF_GAUGE_LASTFRAMETARGET) = fillVal;
    *(int *)((uintptr_t)gauge + OFF_GAUGE_TARGETVAL) = fillVal;
    *(bool *)((uintptr_t)gauge + OFF_GAUGE_USETARGETVAL) = true;

    // Update WindowText (pet name) — modify existing CStrRep data in place
    uintptr_t repPtr = *(uintptr_t *)((uintptr_t)gauge + OFF_CXWND_WINDOWTEXT);
    if (IsValidPtr(repPtr)) {
      int alloc = *(int *)(repPtr + 0x04); // CStrRep::alloc
      int newLen = (int)strlen(petName);

      if (newLen < alloc) // fits in existing buffer
      {
        char *data = (char *)(repPtr + OFF_CXSTR_REP_UTF8);
        memcpy(data, petName, newLen);
        data[newLen] = '\0';
        *(int *)(repPtr + OFF_CXSTR_REP_LEN) = newLen;
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

// ---------------------------------------------------------------------------
// Phase 6: Handle UI Clicks
// ---------------------------------------------------------------------------
bool PetWindow::HandleClick(void *sender) {
  if (!sender)
    return false;

  // Check if they clicked Pet 2 gauge
  if (sender == m_newGauge1) {
    auto *multiPet = MultiPet::GetInstance();
    if (multiPet) {
      const auto &pets = multiPet->GetTrackedPets();
      if (pets.size() >= 1 && pets[0].pSpawn) {
        void *targetSpawn = pets[0].pSpawn;
        GameState::SetTarget((eqlib::PlayerClient *)targetSpawn);
        return true;
      }
    }
  }
  // Check if they clicked Pet 3 gauge
  else if (sender == m_newGauge2) {
    auto *multiPet = MultiPet::GetInstance();
    if (multiPet) {
      const auto &pets = multiPet->GetTrackedPets();
      if (pets.size() >= 2 && pets[1].pSpawn) {
        void *targetSpawn = pets[1].pSpawn;
        GameState::SetTarget((eqlib::PlayerClient *)targetSpawn);
        return true;
      }
    }
  }

  return false;
}
