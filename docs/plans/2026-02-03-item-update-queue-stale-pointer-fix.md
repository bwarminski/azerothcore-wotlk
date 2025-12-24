# Item Update Queue Stale Pointer Fix Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prevent stale `Item*` pointers from remaining in `Player::m_itemUpdateQueue`, eliminating segfaults in `_SaveInventory` during playerbot randomize.

**Architecture:** The crash is caused by duplicate queue entries for the same `Item*` when `uQueuePos` is `-1` but the pointer is still present in the queue. The fix must restore queue integrity by detecting and repairing stale entries when adding/removing items and/or when setting `ITEM_UNCHANGED` so the queue cannot retain dangling pointers. This is a core engine fix in `Item.cpp` with minimal, targeted logic.

**Tech Stack:** AzerothCore C++ (core engine), gdb, CMake Debug build.

---

## Background / Root Cause Evidence

- Crash in `_SaveInventory` dereferencing `Item*` from `m_itemUpdateQueue`.
- Logs show `AddToUpdateQueueOf duplicate` for an `Item*` with `uQueuePos == -1` while the same pointer is still present in the queue at `existingIndex`.
- After item destruction, only the most recent queue slot is cleared; the older slot remains and later causes a segfault.
- Symbolized stack for duplicate add: `Item::SetState` → `Item::CreateItem` → `Player::EquipNewItem` → `EquipItemToSlot` (playerbot gear upgrade), but the core issue is a queue invariant violation.

---

### Task 1: Add a minimal regression test (if feasible)

**Files:**
- Test: `src/test/` (create a new file if feasible; otherwise document TDD exception)

**Step 1: Search for existing item update queue tests**
Run: `rg "itemUpdateQueue|UpdateQueue" src/test`
Expected: No existing coverage.

**Step 2: Decide test feasibility**
If creating a test would require heavy DB/world setup, **skip with a documented TDD exception** in the commit message. Otherwise, implement a small unit test that:
- Creates a `Player` in a minimal test harness (if available),
- Creates an `Item`, pushes it into the update queue,
- Forces a state change that clears `uQueuePos` without removing the queue entry,
- Ensures re-adding the same `Item*` does not create a duplicate entry.

**Step 3: If test is feasible, write failing test**
Expected: FAIL due to duplicate queue entry.

**Step 4: Run test**
Run: `cmake --build build --target unit_tests && ctest -R <test-name> -V`
Expected: FAIL.

**Step 5: Commit (optional)**
If test added, commit test alone:
```
git add src/test/<new-test-file>
git commit -m "test: cover item update queue duplication"
```

---

### Task 2: Fix queue duplication and stale entries in core

**Files:**
- Modify: `src/server/game/Entities/Item/Item.cpp`

**Step 1: Add queue repair in AddToUpdateQueueOf**
Modify `Item::AddToUpdateQueueOf` so that if `this` is already present in the owner’s queue:
- Set `uQueuePos` to the existing index
- Do **not** push a duplicate entry
- (Optional) keep debug log for visibility

**Exact code change (example):**
```cpp
// In Item::AddToUpdateQueueOf
#ifdef ACORE_DEBUG
auto& updateQueue = player->GetItemUpdateQueue();
auto existing = std::find(updateQueue.begin(), updateQueue.end(), this);
if (existing != updateQueue.end())
{
    uQueuePos = static_cast<int32>(std::distance(updateQueue.begin(), existing));
    LOG_WARN("entities.player.items",
        "Item::AddToUpdateQueueOf duplicate item_ptr={} entry={} owner={} player={} state={} queuePos={} existingIndex={}",
        static_cast<void*>(this), GetEntry(), GetOwnerGUID().ToString(), player->GetGUID().ToString(),
        static_cast<int32>(GetState()), uQueuePos, uQueuePos);
    return;
}
#endif
```

**Step 2: Add queue repair in RemoveFromUpdateQueueOf**
Modify `Item::RemoveFromUpdateQueueOf` so that if `uQueuePos == -1`:
- Scan the owner’s queue for `this`
- If found, clear that slot and return
- This ensures removal still cleans stale entries when `uQueuePos` was reset earlier

**Exact code change (example):**
```cpp
if (!IsInUpdateQueue())
{
#ifdef ACORE_DEBUG
    auto& updateQueue = player->GetItemUpdateQueue();
    auto existing = std::find(updateQueue.begin(), updateQueue.end(), this);
    if (existing != updateQueue.end())
    {
        *existing = nullptr;
        LOG_WARN("entities.player.items",
            "Item::RemoveFromUpdateQueueOf repaired stale entry item_ptr={} entry={} owner={} player={} state={} existingIndex={}",
            static_cast<void*>(this), GetEntry(), GetOwnerGUID().ToString(), player->GetGUID().ToString(),
            static_cast<int32>(GetState()), std::distance(updateQueue.begin(), existing));
    }
#endif
    return;
}
```

**Step 3: Keep SetState guard**
Ensure the current `SetState(ITEM_UNCHANGED)` guard does **not** clear `uQueuePos` if the item is still in the owner queue.

**Step 4: Build**
Run: `cmake --build build-debug -j $(nproc)`
Expected: Build succeeds.

**Step 5: Commit**
```
git add src/server/game/Entities/Item/Item.cpp
git commit -m "fix: prevent stale item update queue entries"
```
If no tests were added, include a TDD exception note in the commit message body.

---

### Task 3: Verify via repro

**Files:** (no code changes)

**Step 1: Reproduce crash in Debug build**
Run: `gdb --args /home/bjw/azerothcore/env/dist/bin/worldserver -c /home/bjw/azerothcore/env/dist/etc/worldserver.conf`
Expected: No segfault during random bot creation.

**Step 2: Log verification**
Run:
```
rg "Item::AddToUpdateQueueOf duplicate" /home/bjw/azerothcore/env/dist/bin/Server.log
rg "SaveInventory queue entry" /home/bjw/azerothcore/env/dist/bin/Server.log
```
Expected: No duplicate entries and no stale pointers referencing destroyed items.

**Step 3: Commit any log-related changes**
If log instrumentation remains necessary, keep it; otherwise remove debug-only logs and commit cleanup separately.

---

### Task 4: Cleanup and documentation

**Files:**
- Update: `JOURNAL.md`
- Update: `docs/plans/2026-02-03-item-update-queue-stale-pointer-fix.md`

**Step 1: Record the fix outcome**
Add a short entry to `JOURNAL.md` noting:
- Fix applied
- Outcome of repro
- Whether duplicates still occurred

**Step 2: Commit documentation**
```
git add JOURNAL.md docs/plans/2026-02-03-item-update-queue-stale-pointer-fix.md
git commit -m "docs: record item update queue fix plan"
```

---

## Handoff Notes

- Core issue: stale `Item*` pointers persist in `Player::m_itemUpdateQueue` when `uQueuePos` is reset without clearing the queue slot.
- The stale pointer reappears as a duplicate entry for the next item allocated at the same address.
- Fix should repair queue integrity at add/remove boundaries.
- Crash evidence from latest core: `_SaveInventory` deref at `PlayerStorage.cpp:7353`, `item=0x7fff729a9100`, `index=16`.

## Status (latest run)

- TDD exception: no lightweight harness for item update queue; proceeding without automated test per Brett’s approval.
- Item::AddToUpdateQueueOf now reuses existing slots (sets `uQueuePos` and returns) and logs a short debug line instead of stack traces.
- Item::RemoveFromUpdateQueueOf repairs stale entries when `uQueuePos` is -1 and clears mismatched-owner slots; guarded against blocked queues.
- Removed debug spam from `_SaveInventory` queue listing to keep logs readable.
- Latest debug run: no `SaveInventory invalid position` logs, duplicates reuse existing slots, stale slots repaired (87 hits logged previously), no segfault observed.
