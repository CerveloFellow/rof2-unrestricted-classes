# Pet Window Multi-Pet POC Plan

## Goal

Prove that we can modify the existing CPetInfoWnd to display multiple pets with
clickable HP bars. This POC validates the technical foundations before we build
the full feature.

## End-State Vision

```
+-- Pet Info Window -------------------------+
| [Pet1 Name - UI Pet]     |######### 95%|  |  <- existing, shows buffs
| [Pet2 Name (Mage)]       |#######.. 72%|  |  <- new, clickable
| [Pet3 Name (Necro)]      |####..... 48%|  |  <- new, clickable
|                                            |
| [Sit][Follow][Guard][Taunt][Hold]...       |  <- existing buttons, shifted down
| [Buff1][Buff2][Buff3]...                   |  <- existing buff display
+--------------------------------------------+
```

Clicking pet 2 or pet 3 targets that pet and cycles it to become the UI pet
(showing its buffs in the window). No server-side changes required.

---

## Available Offsets (from eqgame.h)

These are confirmed in our eqlib offset file:

| What                              | Address      | Notes                        |
|-----------------------------------|-------------|------------------------------|
| pinstCXWndManager_x               | 0x15D3D00   | Window manager global        |
| pinstCSidlManager_x               | 0x15D3D08   | SIDL manager global          |
| CSidlScreenWnd__GetChildItem_x    | 0x85CFD0    | Find child widget by name    |
| CXWnd__GetChildItem_x             | 0x868330    | Find child widget by name    |
| CSidlManagerBase__CreateLabel_x   | 0x7556F0    | Factory: create label widget |
| CSidlManager__CreateXWnd_x        | 0x755F20    | Factory: create any widget   |
| CSidlManagerBase__CreateXWnd_x    | 0x870440    | Factory variant              |
| CSidlScreenWnd__WndNotification_x | 0x85EAD0    | Window message handler       |

## Missing Offsets

| What                  | Status   | Workaround                            |
|-----------------------|----------|---------------------------------------|
| pinstCPetInfoWnd_x    | MISSING  | Enumerate via pinstCXWndManager       |
| CGaugeWnd constructor | MISSING  | Use CSidlManager factory or clone     |
| CLabelWnd constructor | MISSING  | Use CSidlManagerBase__CreateLabel     |
| CButtonWnd constructor| MISSING  | Use CSidlManager factory              |

---

## POC Phases

### Phase 1: Find the Pet Window Pointer

**Objective:** Get a live CPetInfoWnd* at runtime.

**Approach:** Use pinstCXWndManager_x (0x15D3D00) to get the window manager,
enumerate its pWindows array, and identify PetInfoWindow by checking each
window's SIDL name.

```
CXWndManager layout:
  +0x004  ArrayClass<CXWnd*> pWindows    // all top-level windows
    +0x000  int count
    +0x004  CXWnd** array

CXWnd relevant fields:
  +0x060  CXRect Location               // {left, top, right, bottom}

CSidlScreenWnd relevant fields:
  +0x214  CXStr SidlText                // the SIDL template name (e.g. "PetInfoWindow")
```

**Steps:**
1. ASLR-correct pinstCXWndManager_x to get CXWndManager**
2. Dereference to get CXWndManager*
3. Read pWindows array (offset +0x004)
4. Loop through windows, read SidlText at CSidlScreenWnd offset +0x214
5. Compare against "PetInfoWindow"
6. Cache the pointer

**Validation:** `/petwindebug` prints the address of the found window, or
"not found" if pet window is closed.

**Risk:** LOW - reading memory and comparing strings. Same pattern as our
spawn scanning.

---

### Phase 2: Enumerate and Read Child Widgets

**Objective:** Find existing child elements by name and read their properties.

**Approach:** Call GetChildItem on the pet window to locate:
- "PetHPGauge" (the main HP gauge)
- "PetName" (the pet name label)
- Pet command buttons

GetChildItem is a virtual function on CXWnd. We can either:
- **(A)** Call through the vtable (safest, uses game's own dispatch)
- **(B)** Call the known address directly (0x868330 for CXWnd version)

```
GetChildItem signature (thiscall):
  CXWnd* __thiscall CXWnd::GetChildItem(const CXStr& name)

For our __fastcall wrapper:
  CXWnd* __fastcall GetChildItem(CXWnd* thisPtr, void* edx, const CXStr& name)
```

**Steps:**
1. Use pet window pointer from Phase 1
2. Call GetChildItem("PetHPGauge") to find the HP gauge
3. Call GetChildItem("PetName") to find the name label
4. For each found widget, read:
   - Location rect (offset +0x060): position and size
   - Visibility state
   - For gauge: FillTint (CGaugeWnd +0x1DC), LastFrameVal (+0x1F8)
5. Enumerate buttons from CPetInfoWnd +0x234 (pButton[14] array)
6. Read each button's Location rect

**Validation:** `/petwindebug` prints each child's name, position, and
properties. The gauge should show the current pet HP%.

**Risk:** MEDIUM - first time calling a game virtual method from our DLL.
If the CXStr parameter format is wrong, could crash. Start with direct
address call (0x868330) which avoids vtable lookup issues.

**Fallback:** If GetChildItem crashes, manually walk the child window tree
using CXWnd's FirstChildWnd (+offset TBD) and NextSiblingWnd pointers.

---

### Phase 3: Move Existing Buttons

**Objective:** Prove we can reposition UI elements that the SIDL engine created.

**Approach:** Shift all 14 pet command buttons down by N pixels (e.g., 40px)
to make room for our pet list rows.

```
CXRect at CXWnd+0x060:
  +0x00 int left
  +0x04 int top
  +0x08 int right
  +0x0C int bottom
```

**Steps:**
1. Read pButton[0..13] from CPetInfoWnd +0x234
2. For each button, read Location rect at +0x060
3. Add 40 to top and bottom values
4. Also move the buff window (pWndBuffs at CPetInfoWnd +0x2B4) down by same amount
5. Optionally increase the pet window's own height by the same amount

**Validation:** Visually confirm buttons have moved down. Gap appears below
the pet name/HP area where our new rows will go.

**Risk:** MEDIUM - the SIDL layout engine might revert positions on next
window update/redraw. If so, we'll need to re-apply positions in
OnProcessFrame every frame.

**Fallback:** If direct rect modification doesn't stick, hook
CPetInfoWnd::OnProcessFrame (or CSidlScreenWnd::OnProcessFrame) and
re-apply positions each frame after the base implementation runs.

---

### Phase 4: Create a Label Widget

**Objective:** Prove we can add a new UI element to the pet window.

**Approach A (preferred): Use CSidlManagerBase::CreateLabel factory**

```
CreateLabel signature (thiscall):
  CXWnd* __thiscall CSidlManagerBase::CreateLabel(
      CXWnd* parent,
      CControlTemplate* pTemplate
  )
Address: 0x7556F0
```

This requires a CControlTemplate*, which we'd need to find or construct.
We might be able to use FindScreenPieceTemplate to get an existing label
template from the game's SIDL data.

**Approach B (fallback): Construct CLabelWnd directly**

If we can find the CLabelWnd constructor via pattern scanning in eqgame.exe,
call it directly:

```
CLabelWnd::CLabelWnd(CXWnd* parent, uint32_t id, const CXRect& rect)
```

Allocate sizeof(CLabelWnd) = 0x1E4 bytes via game allocator (eqAlloc at
0x8DBB3B), call constructor, set text.

**Approach C (simplest fallback): Repurpose existing text**

If widget creation is too risky for the POC, instead:
- Modify the existing PetName label text to show all 3 pets
- Format: "Pet1Name 95% | Pet2Name 72% | Pet3Name 48%"
- Proves we can update text dynamically even if we can't create widgets yet

**Steps (Approach A):**
1. ASLR-correct pinstCSidlManager_x to get CSidlManager*
2. Call FindScreenPieceTemplate to find an existing label template
3. Call CreateLabel(petWindow, template) to create label as child of pet window
4. Set position via Location rect
5. Set text via SetWindowText or direct CXStr manipulation

**Validation:** A new text label appears inside the pet window showing
"Pet 2: TestName".

**Risk:** HIGH - we've never created a widget. Multiple things could fail:
wrong template, wrong allocation, wrong parent linkage. This is the phase
most likely to need iteration.

---

### Phase 5: Create a Gauge Widget

**Objective:** Prove we can add an HP bar for a secondary pet.

**Approach:** Similar to Phase 4 but for CGaugeWnd.

**Option A: Clone from existing gauge**
1. Find "PetHPGauge" via GetChildItem (from Phase 2)
2. Read its CGaugeDrawTemplate (offset +0x210) to get texture pointers
3. Use CSidlManager::CreateXWnd or direct constructor with same textures
4. Parent to pet window, position below first gauge

**Option B: Use CSidlManager factory**
```
CSidlManager::CreateXWnd (0x755F20) or
CSidlManagerBase::CreateXWnd (0x870440)
```
With a gauge template from FindScreenPieceTemplate.

**Steps:**
1. Get texture animations from existing PetHPGauge
2. Create new CGaugeWnd as child of pet window
3. Position it below the existing gauge
4. Update its value each frame based on secondary pet's HP

**Updating the gauge value:**
```cpp
// From TrackedPet.pSpawn:
int32_t hp    = *(int32_t*)((uintptr_t)pSpawn + 0x2E4);  // HPCurrent
int32_t hpMax = *(int32_t*)((uintptr_t)pSpawn + 0x2DC);  // HPMax
int pct = hpMax > 0 ? (hp * 100 / hpMax) : 0;
// Set gauge value (need to find the right method/field)
```

**Validation:** A second HP bar appears below the original, showing the
secondary pet's current HP percentage.

**Risk:** HIGH - same as Phase 4 plus gauge-specific texture setup.

---

### Phase 6: Click Detection

**Objective:** Prove we can detect clicks on our new pet labels/gauges and
trigger petcycle.

**Approach A: Hook CPetInfoWnd::WndNotification**

Hook CSidlScreenWnd::WndNotification (0x85EAD0) or the CPetInfoWnd-specific
override. When a child is clicked, the parent receives XWM_LCLICK with
the child's CXWnd* as the sender.

```
WndNotification signature (thiscall):
  int __thiscall WndNotification(CXWnd* sender, uint32_t message, void* data)

Check: if (sender == ourPet2Label && message == XWM_LCLICK) { ... }
```

**Approach B: Hook HandleLButtonDown + hit-test**

If labels don't generate WndNotification clicks (they might be passive),
hook CXWnd::HandleLButtonDown on the pet window and check if the click
coordinates fall within our label/gauge rects.

```
HandleLButtonDown signature (thiscall):
  int __thiscall HandleLButtonDown(const CXPoint& point, uint32_t flags)
```

**Approach C: Use CButtonWnd instead of CLabelWnd**

If neither A nor B works well, go back to Phase 4 and create CButtonWnd
instances (styled to look like labels) instead. Buttons are explicitly
designed for click interaction.

**On click action:**
1. Target the clicked pet's spawn (existing /petcycle logic)
2. Call petcycle to swap it into the UI pet slot
3. The pet window naturally updates to show the new UI pet's buffs

**Validation:** Click on pet 2 row -> chat shows "Cycling to Pet2Name" ->
pet window updates to show Pet2's buffs.

**Risk:** MEDIUM - the mechanism exists, question is which approach works.
Multiple fallbacks available.

---

## Implementation Plan

### File Structure

```
mods/
  pet_window/
    pet_window.h        - PetWindow mod class (IMod)
    pet_window.cpp      - Implementation
```

Register in dllmain.cpp, add to .vcxproj.

### Slash Command

`/petwindebug [phase]` - Run a specific POC phase:
- `/petwindebug find`    - Phase 1: find pet window
- `/petwindebug children`- Phase 2: enumerate children
- `/petwindebug move`    - Phase 3: move buttons down
- `/petwindebug label`   - Phase 4: create label
- `/petwindebug gauge`   - Phase 5: create gauge
- `/petwindebug click`   - Phase 6: test click detection
- `/petwindebug reset`   - Undo all changes (restore button positions, remove created widgets)

### Build Order

Implement phases sequentially. Each phase depends on the previous:
- Phase 1 is required for everything
- Phase 2 uses the pointer from Phase 1
- Phase 3 uses children found in Phase 2
- Phase 4-5 need Phase 3's positioning to place widgets correctly
- Phase 6 needs Phase 4-5's widgets to exist

**Stop points:** If any phase fails, we stop and reassess before continuing.
Each phase is independently valuable as research.

### Integration with MultiPet Mod

The POC reads pet data from the existing MultiPet mod's tracked pet list.
No changes to MultiPet are needed for the POC. The final feature will
either extend MultiPet or reference its data.

---

## Key Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| GetChildItem crashes (bad CXStr format) | Phase 2 blocked | Try direct address call, manual child walk |
| Button positions revert on redraw | Phase 3 cosmetic | Re-apply in OnProcessFrame hook |
| Widget creation crashes | Phase 4-5 blocked | Fall back to Approach C (text-only display) |
| Labels not clickable | Phase 6 degraded | Use CButtonWnd or manual hit-testing |
| SIDL template not found for factory | Phase 4-5 blocked | Direct constructor + pattern scan |
| Pet HP not updating for secondary pets | Gauges stale | Verify OP_HPUpdate covers all spawns |

## Worst-Case Fallback

If widget creation (Phases 4-5) proves too difficult, we still have a
viable feature using **text-only display**:

- Hook the pet window's name label or title
- Show a formatted string: "Pet1 95% | Pet2 72% | Pet3 48%"
- Use /petcycle command (already working) to switch active pet
- Not as pretty, but functional with zero widget creation

This ensures we ship *something* regardless of how the POC goes.
