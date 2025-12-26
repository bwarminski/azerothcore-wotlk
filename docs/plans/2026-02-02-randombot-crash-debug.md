# Randombot Crash Debug Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Diagnose and fix the segfault in `PlayerbotFactory::Randomize`/`Player::_SaveInventory` (bad item pointer during bot save).

**Architecture:** Reproduce in Debug build, add targeted instrumentation around inventory save, inspect gear state, and prevent invalid items before save. Keep changes minimal and guarded.

**Tech Stack:** AzerothCore C++, gdb, CMake Debug build, gtest.

### Task 1: Build debug binary and reproduce crash

**Files:** (no code changes)
- Command: `./acore.sh compiler build -DCMAKE_BUILD_TYPE=Debug`

**Step 1: Build Debug**  
Run: `cd ~/azerothcore && ./acore.sh compiler build -DCMAKE_BUILD_TYPE=Debug`

**Step 2: Reproduce with gdb**  
Run: `cd ~/azerothcore/build/bin && ulimit -c unlimited && gdb --args ./worldserver -c ../etc/worldserver.conf`  
Reproduce the crash; record backtrace.

**Step 3: Commit (none)**  
No commit; proceed to next task.

### Task 2: Add test guard for inventory save on randomize

**Files:**
- Modify: `src/test/modules/UpgradeLotteryTests.cpp` or new test file for bot randomize (if feasible)

**Step 1: Write the failing test**  
Add a gtest that creates a bot (mock/minimal) and invokes `PlayerbotFactory::Randomize` to ensure inventory slots are valid before `SaveToDB` (e.g., assert no null/invalid items in bag slots, or guard the path if direct creation is too heavy).

**Step 2: Run to see it fail**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (or the specific new test).

**Step 3: Commit (optional)**  
Commit only if the test is feasible; otherwise note skipped and move to instrumentation.

### Task 3: Instrument inventory save path for diagnostics

**Files:**
- Modify: `modules/mod-playerbots/src/factory/PlayerbotFactory.cpp` (Randomize before SaveToDB)
- Modify: `src/server/game/Entities/Player/PlayerStorage.cpp` (optional guarded logs/asserts)

**Step 1: Add logging/asserts**  
Before `bot->SaveToDB` in `PlayerbotFactory::Randomize`, iterate inventory slots and log slot index, item pointer, entry, and GUID; skip logging if not in Debug or behind a config flag. Optionally add a defensive check in `_SaveInventory` to skip null/invalid items with a log (in Debug builds).

**Step 2: Build and rerun reproduction**  
`cmake --build build && cd build/bin && gdb --args ./worldserver -c ../etc/worldserver.conf`

**Step 3: Commit**  
`git add modules/mod-playerbots/src/factory/PlayerbotFactory.cpp src/server/game/Entities/Player/PlayerStorage.cpp && git commit -m "chore: add debug logging for bot inventory save"`

### Task 4: Identify and fix invalid item source

**Files:**
- Modify: `modules/mod-playerbots/src/factory/PlayerbotFactory.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp` (if bracket/epoch paths touch gear)

**Step 1: Analyze logs**  
Use logs from Task 3 to locate slot with bad item (likely during Randomize first pass). Check any gear removal code paths (bracket reset/backfill) for destroying items without clearing slots.

**Step 2: Write a failing test (if feasible)**  
Add gtest that simulates the suspected path (e.g., gear wipe + SaveToDB) to ensure no invalid items remain.

**Step 3: Implement minimal fix**  
Ensure gear reset destroys items and clears slots before SaveToDB, or guard `Randomize` to avoid saving bots with invalid inventories. Keep fix minimal.

**Step 4: Run tests**  
`cmake --build build --target mod-playerbots-tests && ctest -R "UpgradeLotteryTests|XpBackfillTests" -V`

**Step 5: Commit**  
`git add modules/mod-playerbots/src/factory/PlayerbotFactory.cpp modules/mod-playerbots/src/RandomPlayerbotMgr.cpp src/test/modules/UpgradeLotteryTests.cpp && git commit -m "fix: prevent invalid items during bot randomize save"`

### Task 5: Verify and clean up

**Files:** (no code changes)

**Step 1: Re-run worldserver in Debug**  
`cd ~/azerothcore/build/bin && gdb --args ./worldserver -c ../etc/worldserver.conf`  
Verify no segfault; tail logs for inventory diagnostics.

**Step 2: Smoke tests**  
`cmake --build build --target mod-playerbots-tests mod-individual-progression-tests && ctest -R "UpgradeLotteryTests|ProgressionHelperTests" -V`

**Step 3: Commit (if any remaining)**  
`git add docs/plans/2026-02-02-randombot-crash-debug.md && git commit -m "docs: add randombot crash debug plan"`
