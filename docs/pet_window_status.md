# Pet Window POC - Status (2026-02-14)

## Current State: Phases 1-4 WORKING

### Phase 1: Find Pet Window - COMPLETE
- Scans CXWndManager's pWindows array matching SidlText=="PetInfoWindow"
- SEH-protected memory reads for safety
- `/petwindebug` command finds and caches the CPetInfoWnd pointer

### Phase 2: Walk Child Tree - COMPLETE
- Iterates children via TList/TListNode at CXWnd +0x10/+0x08
- Identifies widget types by vtable comparison (CGaugeWnd=0x9E87A8, CCheckBoxWnd=0xA1DB4C)
- `/petwindebug children` dumps all children with type, position, text

### Phase 3: Move Buttons Down - COMPLETE
- Uses `UpdateGeometry()` via CXWnd vtable offset 0x11C (the proper game API)
- Direct Location rect writes and TopOffset/BottomOffset writes DON'T work - SIDL layout engine overrides them
- Skips CGaugeWnd children (by vtable check), only shifts buttons + buff window
- Grows parent window by SHIFT_PIXELS (52px)
- `/petwindebug move` shifts buttons down, creating gap for new gauges

### Phase 4: Create Gauge Widgets - COMPLETE (via XML)
- **SIDL factory approach FAILED**: Template names defined inside `<Screen>` elements are scoped/inline, NOT globally registered. `FindScreenPieceTemplate("PIW_PetHPGauge")` returns null.
- **XML approach WORKS**: Added 2 new `<Gauge>` definitions to `EQUI_PetInfoWindow.xml`:
  - `PIW_Pet2HPGauge` at y=60, text="Pet 2", green fill, EQType=0
  - `PIW_Pet3HPGauge` at y=86, text="Pet 3", green fill, EQType=0
  - Added to `<Pieces>` list after PIW_PetTargetHPGauge
- Game SIDL engine creates them automatically as children [2] and [3]
- `/petwindebug create` finds them by text match and caches pointers

### Current Child Layout (after move):
```
[0]  CGaugeWnd  108x26 @(2,0)   text='No Pet'     <- pet 1 HP (existing)
[1]  CGaugeWnd  108x28 @(2,32)  text='No Target'  <- target HP (existing)
[2]  CGaugeWnd  108x26 @(2,60)  text='Pet 2'      <- NEW pet 2 HP
[3]  CGaugeWnd  108x26 @(2,86)  text='Pet 3'      <- NEW pet 3 HP
[4]  CCheckBox  54x20  @(2,121) text='attack'      <- shifted +52px
[5]  CCheckBox  54x20  @(56,121) text='follow'
...
[18] BuffWindow 4x4    @(0,52)  sidl='PIW_BuffWindow'
```

## Remaining Phases

### Phase 5: Update Gauge Values (NEXT)
- Hook OnPulse to read pet data from MultiPet mod's tracked pet list
- For each secondary pet, update the corresponding gauge:
  - Set WindowText to pet name via SetWindowText vtable (offset 0x124)
  - Set gauge fill percentage based on HPCurrent/HPMax
  - CGaugeWnd has `LastFrameVal` at +0x1F8 and `LastFrameTarget` at +0x200 for the fill level
- Need to construct CXStr for SetWindowText (HeapAlloc CStrRep with high refcount)
- OR directly modify the existing CStrRep data buffer (simpler, avoids allocation issues)

### Phase 6: Click Detection
- Hook CPetInfoWnd::WndNotification or CXWnd::HandleLButtonDown
- Detect clicks on pet 2/3 gauges
- Execute /petcycle to swap clicked pet to UI pet slot
- Approach options: WndNotification hook, manual hit-testing, or CButtonWnd instead of gauge

## Key Technical Discoveries

### UpdateGeometry is THE way to move widgets
- Direct Location rect writes: overridden by layout engine every frame
- TopOffset/BottomOffset writes: only affects anchor-based widgets (buff window), not absolute-positioned buttons
- `UpdateGeometry()` vtable call at offset 0x11C: properly updates all internal state, persists through redraws

### CXWnd Vtable Offsets (confirmed working)
| Offset | Function |
|--------|----------|
| 0x11C  | UpdateGeometry(const CXRect&, bool, bool, bool, bool) |
| 0x120  | Move(const CXPoint&) |
| 0x124  | SetWindowText(const CXStr&) |
| 0x160  | UpdateLayout(bool) |

### CXWnd Memory Layout (confirmed)
| Offset | Field |
|--------|-------|
| +0x00  | vtable pointer |
| +0x04  | TListNode::m_pPrev (previous sibling) |
| +0x08  | TListNode::m_pNext (next sibling) |
| +0x0C  | TListNode::m_pList (parent's child list) |
| +0x10  | TList::m_pFirstNode (first child) |
| +0x14  | TList::m_pLastNode (last child) |
| +0x60  | CXRect Location {left, top, right, bottom} |
| +0xD8  | uint32 XMLIndex |
| +0x1A8 | CXStr WindowText |
| +0x1DC | CXStr SidlText (CSidlScreenWnd) |

### CStrRep Layout (from game dump)
```
+0x00: int refCount (game uses 4 for shared strings)
+0x04: int alloc    (game uses 32 for "No Pet")
+0x08: int length   (string length, no null)
+0x0C: int encoding (0 = UTF-8)
+0x10: void* freeList (game has non-null: 0x010418A0)
+0x14: char data[]  (null-terminated UTF-8)
```

### SIDL Template Scoping
- Templates defined inside `<Screen item="...">` via `<Pieces>` are INLINE, not globally registered
- `FindScreenPieceTemplate()` only finds top-level templates
- To add new widgets: define them in the XML file and add to Pieces list

## Files Modified

### Source (dinput8 DLL)
- `mods/pet_window.h` - PetWindow class with Phase 1-4 methods
- `mods/pet_window.cpp` - Full implementation (~700 lines)
- `dllmain.cpp` - PetWindow mod registration
- `dinput8.vcxproj` - Project file includes

### Client XML
- `uifiles/default/EQUI_PetInfoWindow.xml` - Added PIW_Pet2HPGauge and PIW_Pet3HPGauge gauge definitions + Pieces entries

## Build & Deploy
```bash
# Build
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" dinput8.sln /p:Configuration=Release /p:Platform=Win32

# Deploy DLL
cp build/bin/release/dinput8.dll C:\resurrection\thj_rebuild\dinput8.dll

# XML is already in place at:
# C:\resurrection\thj_rebuild\uifiles\default\EQUI_PetInfoWindow.xml
```

## Test Commands
```
/petwindebug          - Phase 1: find pet window
/petwindebug children - Phase 2: list all children
/petwindebug move     - Phase 3: shift buttons down 52px
/petwindebug create   - Phase 4: find and cache new gauge pointers
```
