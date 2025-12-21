# XP-Driven Randombot Gear Upgrades Implementation Plan

Reference materials:
- Spec: `modules/xp-upgrade-gear-spec.md`
- Journal note: `JOURNAL.md`
- Repo root: `/home/bjw/azerothcore` (modules: `mod-playerbots`, `mod-individual-progression`)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add vendor-seeded baseline gear and XP-driven percentile upgrades for randombots, enforced by individual progression rules and a configurable weekly BiS ramp. Bots always meet vendor baseline, upgrade candidates respect progression (using DB-driven conditions from the individual progression module), and odds ramp is global-config-driven. Remove the legacy progression caps conf to avoid conflicting sources.

**Architecture:** Build a vendor gear cache; store per-bot XP since last upgrade; on configurable XP steps, level-ups, and endgame week changes, roll a percentile per slot across all suitable gear (up to bot level and progression) distributed by gear score, pick the nearest percentile item, and equip if it beats current; otherwise fall back to vendor baseline if better. Progression enforcement uses a public helper in mod-individual-progression backed by DB conditions (progression quests, vendor/drop gating) with a test seam; retire static caps/config entirely. Optimize upgrade selection by using cumulative weight + binary search and cache score computations per (class/spec/level/progression) to avoid repeated StatWeight calculations. A global weeks-at-endgame config lifts percentile floors and over-cap odds for max-level bots at progression cap. Existing auto-upgrade paths are gated by the new mode. Configs live in playerbots.conf.

**Tech Stack:** AzerothCore C++ modules (mod-playerbots, mod-individual-progression), CMake, (g)test harness for new unit/logic tests.

### Task 1: Progression allow-list helper
**Files:**
- Modify: `modules/mod-individual-progression/src/IndividualProgression.h`
- Modify: `modules/mod-individual-progression/src/IndividualProgression.cpp`
- Modify: `src/test/modules/ProgressionHelperTests.cpp`
**Step 1: Write the failing test**  
Create gtest that sets up a fake player/progression state and asserts `IsItemAllowedForProgression(Player*, uint32)` returns false for an out-of-phase item and true for an allowed item. Include clear fixtures for expansion/phase boundaries.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V` (expect failure: function missing or default behavior).
**Step 3: Write minimal implementation**  
Add a public helper in `IndividualProgression` that mirrors existing gating (phase, level cap, expansion) for items; declare in header. Ensure it is usable from other modules (include path/export).
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V` (expect pass).
**Step 5: Commit**  
`git add modules/mod-individual-progression/src/IndividualProgression.* modules/mod-individual-progression/tests/ProgressionHelperTests.cpp && git commit -m "feat: expose progression item allow helper"`

### Task 1b: DB-conditioned progression helper with test seam
**Files:**
- Modify: `modules/mod-individual-progression/src/IndividualProgression.h`
- Modify: `modules/mod-individual-progression/src/IndividualProgression.cpp`
- Modify: `src/test/modules/ProgressionHelperTests.cpp`
- (Optional for test seam) Add: `modules/mod-individual-progression/include/ProgressionConditionProvider.h`
**Step 1: Write the failing test**  
Extend gtests to assert the shared `IsItemAllowedForProgression(Player*, itemId)` uses DB-backed progression conditions (e.g., quest-gated drop/vendor conditions) rather than static caps. Provide a fixture/stub condition provider so tests can simulate conditions without a live DB. Cover allow/deny across multiple progression quests.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V` (expect failure due to missing provider/export).
**Step 3: Write minimal implementation**  
Add an injectable condition provider interface (default implementation pulls ConditionMgr/world DB conditions for progression quests tied to items) and wire `IsItemAllowedForProgression` to use it. Drop config-based caps; rely on DB conditions as the sole source. Export helper for external modules.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V`.
**Step 5: Commit**  
`git add modules/mod-individual-progression/src/IndividualProgression.* src/test/modules/ProgressionHelperTests.cpp modules/mod-individual-progression/include/ProgressionConditionProvider.h && git commit -m "feat: drive progression gating from db conditions"`

### Task 1c: Replace hardcoded expansion gate in playerbots
**Files:**
- Modify: `modules/mod-playerbots/src/factory/PlayerbotFactory.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp`
**Step 1: Write the failing test**  
Add/extend gtests to assert playerbots use the shared progression allowance helper (not hardcoded `IsItemBeyondExpansionLimit`) for gating items; override helper to block a WotLK item at level 60 and ensure it’s rejected even if the old hardcoded limit would allow it.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (expect failure due to hardcoded gate).
**Step 3: Write minimal implementation**  
Remove `IsItemBeyondExpansionLimit` checks; call the shared progression helper for expansion/progression gating in vendor selection and upgrade passes.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/factory/PlayerbotFactory.cpp modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "refactor: use shared progression allowance for gear gating"`

### Task 1d: Remove unused vendor helper
**Files:**
- Modify: `modules/mod-playerbots/src/RandomItemMgr.h`
- Modify: `modules/mod-playerbots/src/RandomItemMgr.cpp`
- Modify: `modules/mod-playerbots/tests/VendorCacheTests.cpp` (if needed)
**Step 1: Write the failing test**  
Add/adjust tests to ensure no code relies on `GetBestVendorItem` (or assert its removal). If not referenced, no new test needed; confirm build/tests fail if dangling references exist.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R VendorCacheTests -V` (expect missing symbol if references remain).
**Step 3: Write minimal implementation**  
Remove `GetBestVendorItem` declarations/definitions; clean up any dead code paths or references.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R VendorCacheTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomItemMgr.* modules/mod-playerbots/tests/VendorCacheTests.cpp && git commit -m "chore: remove unused vendor helper"`

### Task 1e: Remove static progression caps config
**Files:**
- Modify: `modules/mod-individual-progression/src/IndividualProgression.cpp`
- Delete: `modules/mod-individual-progression/conf/progression_item_caps.conf.dist`
- Modify: `src/test/modules/ProgressionHelperTests.cpp`
**Step 1: Write the failing test**  
Add/adjust gtests to assert the live allowance path uses DB conditions by default and does not rely on static cap tables or config. Ensure no references remain to the removed conf.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V` (expect failure while caps/conf still present).
**Step 3: Write minimal implementation**  
Delete the cap loading/config code and references; rely solely on DB-conditioned gating. Update docs/comments to point to DB-driven gating. Ensure playerbots continue to route through the shared helper.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-individual-progression-tests && ctest -R ProgressionHelperTests -V`.
**Step 5: Commit**  
`git add modules/mod-individual-progression/src/IndividualProgression.cpp src/test/modules/ProgressionHelperTests.cpp && git rm modules/mod-individual-progression/conf/progression_item_caps.conf.dist && git commit -m "refactor: remove static progression caps in favor of db conditions"`

### Task 2: Playerbot configs for XP upgrades and ramp
**Files:**
- Modify: `modules/mod-playerbots/conf/playerbots.conf.dist`
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.h`
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.cpp`
- Add: `modules/mod-playerbots/tests/PlayerbotConfigTests.cpp`
**Step 1: Write the failing test**  
Add gtest asserting defaults: `XpUpgradeEnabled=0`, `XpUpgradeChunk=7000` (configurable), `VendorSeedEnabled=1`, `ProgressionState` fixed value, `BiSWeeksAtEndgame=0`. Verify getters surface these.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R PlayerbotConfigTests -V` (expect missing config fields).
**Step 3: Write minimal implementation**  
Add config reads with defaults; expose getters in `PlayerbotAIConfig`.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R PlayerbotConfigTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/conf/playerbots.conf.dist modules/mod-playerbots/src/PlayerbotAIConfig.* modules/mod-playerbots/tests/PlayerbotConfigTests.cpp && git commit -m "feat: add playerbot xp upgrade configs"`

### Task 3: Vendor gear cache and baseline equip
**Files:**
- Modify: `modules/mod-playerbots/src/RandomItemMgr.h`
- Modify: `modules/mod-playerbots/src/RandomItemMgr.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotFactory.cpp`
- Add: `modules/mod-playerbots/tests/VendorCacheTests.cpp`
**Step 1: Write the failing test**  
Add gtest verifying vendor cache returns vendor candidates ≤ level for a slot and `InitEquipment` equips the vendor item with the best `StatsWeightCalculator` score (spec-aware) when a slot is empty.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R VendorCacheTests -V` (expect fail).
**Step 3: Write minimal implementation**  
Build cache from `npc_vendor` join `item_template` (duration=0, reqLevel>0, armor/weapon), keyed by level+invType, filtered by class/proficiency; `InitEquipment` uses the vendor candidate ≤ bot level with the highest `StatsWeightCalculator` score for the bot’s spec as the baseline fill (tie-break by item level/req level). Use the same weight calculator used for normal gear selection so vendor seeding follows the same distribution.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R VendorCacheTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomItemMgr.* modules/mod-playerbots/src/RandomPlayerbotFactory.cpp modules/mod-playerbots/tests/VendorCacheTests.cpp && git commit -m "feat: seed bots with vendor baseline gear"`

### Task 4: XP tracking per bot
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.h`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Add: `modules/mod-playerbots/tests/XpTrackingTests.cpp`
**Step 1: Write the failing test**  
Add gtest verifying XP counters persist per GUID via `GetValue/SetValue`-backed helpers and reset on bot creation; check configurable chunk value read from config.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpTrackingTests -V` (expect fail).
**Step 3: Write minimal implementation**  
Add `GetXpSinceLastUpgrade/ConsumeXpForUpgrade` helpers storing in value store; initialize to 0 on bot init.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpTrackingTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.* modules/mod-playerbots/tests/XpTrackingTests.cpp && git commit -m "feat: track bot xp for upgrades"`

### Task 5: Hook XP gain to trigger upgrades
**Files:**
- Modify: `modules/mod-playerbots/src/PlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Add: `modules/mod-playerbots/tests/XpHookTests.cpp`
**Step 1: Write the failing test**  
Add gtest simulating XP gain calling hook to ensure when accumulated XP ≥ the configured chunk, upgrade pass is invoked and counter reduced; also triggers on level-up events; ensure disabled when config off.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpHookTests -V`.
**Step 3: Write minimal implementation**  
Wire into XP event (Player::GiveXP or existing bot XP callback) to increment counter and call upgrade when threshold crossed; respect `XpUpgradeEnabled`.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpHookTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/PlayerbotMgr.cpp modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/XpHookTests.cpp && git commit -m "feat: trigger gear upgrades on xp gain"`

### Task 5b: Base XP simulation on level set
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Add: `modules/mod-playerbots/tests/XpBackfillTests.cpp`
**Step 1: Write the failing test**  
Add gtest ensuring when a bot’s level is set (without live XP), the system simulates “earned XP” up to that level by replaying upgrade passes every chunk from level 1 to current level using level-appropriate gear pools, without zeroing on login.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpBackfillTests -V`.
**Step 3: Write minimal implementation**  
Provide a helper to backfill upgrade passes for a bot when its level is assigned (e.g., called by bracket integration): iterate chunks up to current level, running the upgrade pass at each chunk boundary using the bot’s current level/progression constraints. Do not reset XP on login.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R XpBackfillTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/XpBackfillTests.cpp && git commit -m "feat: simulate xp-based upgrades on level set"`

### Task 6: Upgrade lottery with progression enforcement
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.h`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotFactory.cpp`
- Add: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp`
**Step 1: Write the failing test**  
Add gtest ensuring per-slot upgrade trigger runs on XP chunk, level-up, and endgame week change; when upgrading, roll a percentile across the score-sorted suitable gear pool (level/progression/slot-filtered) using StatsWeightCalculator distribution and pick the nearest item to that percentile; equip if it beats current; otherwise equip vendor baseline if it is better; no disallowed items selected.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 3: Write minimal implementation**  
Implement upgrade pass using StatsWeightCalculator, vendor baseline fallback, progression helper; no skip chance—roll percentile per slot each trigger; equip via existing helpers; gate AutoUpgradeEquip to avoid double upgrades when XP mode on.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.* modules/mod-playerbots/src/RandomPlayerbotFactory.cpp modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "feat: add progression-bound xp gear lottery"`

### Task 6d: Score-weighted percentile selection
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp`
**Step 1: Write the failing test**  
Add gtests to cover score-gap scenarios: many similar low-score items plus one high-score outlier; assert that percentile mapping is based on cumulative score/weight (not rank) so the outlier only appears in the top weight slice (e.g., roll 99+ picks it, roll 90 picks a mid-tier).
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (expect current rank-based selection to fail spacing expectations).
**Step 3: Write minimal implementation**  
Change selection to score-weighted cumulative percentile: compute weights (score or dampened function), build cumulative total, map roll percentile to cumulative weight, and pick the item where cumulative crosses the target; preserve vendor baseline logic and progression gates.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "refactor: use weighted percentile for gear selection"`

### Task 6f: Optimize selection lookup (cumulative + binary search)
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp`
**Step 1: Write the failing test**  
Add gtest ensuring the selection uses a cumulative weight array with binary search rather than linear scanning, and that selection matches the previous behavior for representative rolls (0, mid, 100) on a known candidate set.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (expect failure while linear scan remains).
**Step 3: Write minimal implementation**  
Compute cumulative weights once, then use `std::lower_bound` on the cumulative array to pick the candidate for the rolled weight. Keep the rank-based fallback for zero/invalid total weight.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "perf: binary search cumulative weights for gear selection"`

### Task 6g: Cache score calculations per bot state
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Add (for test seam): `modules/mod-playerbots/src/factory/StatsWeightCache.h/.cpp` or integrate into `RandomPlayerbotMgr`
- Modify: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp`
**Step 1: Write the failing test**  
Add gtest with a test double for the score provider (e.g., injectable interface) that counts `CalculateItem` calls; assert that repeated candidate evaluation for the same (class/spec/level/progression) uses cached scores (call count == 1 per item) and that a different spec/level/progression invalidates or keys separately.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (expect failure without caching/injection).
**Step 3: Write minimal implementation**  
Introduce a cache keyed by (class, spec tab, level, progression state, itemId/randomProp) storing computed scores. Allow injection of a score provider for tests; production uses `StatsWeightCalculator`. Clear or bypass cache when bot state changes. Use the cache in upgrade pass to avoid recomputation.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/src/factory/StatsWeightCache.* modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "perf: cache gear scores per bot state"`

### Task 6e: Diagnostic logging for gear rolls
**Files:**
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Modify: `modules/mod-playerbots/conf/playerbots.conf.dist` (to add a toggle)
- Add: `modules/mod-playerbots/tests/UpgradeLotteryTests.cpp` (if needed)
**Step 1: Write the failing test**  
Optionally add a lightweight test to assert that when logging is enabled, upgrade passes emit a log entry with bot name, slot, roll, chosen item, score, vendor fallback. If too heavy, skip test per logging exception.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (or skip if no test).
**Step 3: Write minimal implementation**  
Add a config flag `AiPlayerbot.UpgradeLogging` (default off). When enabled, log per-slot upgrade decisions: roll value (after BiS ramp/floor), percentile, chosen item ID/name/score, vendor candidate ID/name, current gear score, and reason (xp-gain/level-set/bracket). Keep logging concise and gated to avoid spam.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R UpgradeLotteryTests -V` (or verify manually if test skipped).
**Step 5: Commit**  
`git add modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/conf/playerbots.conf.dist modules/mod-playerbots/tests/UpgradeLotteryTests.cpp && git commit -m "feat: add optional upgrade roll logging"`

### Task 6b: Bracket level-change integration
**Files:**
- Modify: `modules/mod-player-bot-level-brackets/src/mod-player-bot-level-brackets.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp` (if a helper is needed)
- Add: `modules/mod-player-bot-level-brackets/tests/BracketUpgradeIntegrationTests.cpp`
**Step 1: Write the failing test**  
Add test(s) to ensure when the bracket module adjusts a bot’s level, it: (a) resets the bot’s XP-for-upgrade counter, (b) seeds gear using vendor/spec-aware baseline plus a single upgrade pass at the new level (not a full randomize), and (c) does not retroactively roll through all historical 7k chunks.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-player-bot-level-brackets-tests && ctest -R BracketUpgradeIntegrationTests -V` (expect missing hook/behavior).
**Step 3: Write minimal implementation**  
In the bracket level reset path (AdjustBotToRange): call `ResetXpForUpgrade(botGuid)` and replace the `Randomize(false)` full reroll with: vendor/spec-aware baseline equip and a single upgrade pass appropriate to the new level (reuse the same upgrade helper you’ll add in Task 6). Ensure no multi-chunk retroactive upgrades—only the one-time pass at the new level, then future upgrades happen via XP/level triggers.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-player-bot-level-brackets-tests && ctest -R BracketUpgradeIntegrationTests -V`.
**Step 5: Commit**  
`git add modules/mod-player-bot-level-brackets/src/mod-player-bot-level-brackets.cpp modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-player-bot-level-brackets/tests/BracketUpgradeIntegrationTests.cpp && git commit -m "feat: integrate bracket level resets with xp-based upgrades"`

### Task 7: BiS ramp via global weeks config
**Files:**
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.h`
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.cpp`
- Modify: `modules/mod-playerbots/src/RandomPlayerbotMgr.cpp`
- Add: `modules/mod-playerbots/tests/BiSRampTests.cpp`
**Step 1: Write the failing test**  
Add gtest verifying percentile floor = min(90, 50 + 10 * weeks) and over-cap chance = min(10, 2 + weeks) when bot is max level and at progression cap; ramp inactive otherwise; ramp shifts percentile selection for upgrade rolls.
**Step 2: Run it to make sure it fails**  
`cmake --build build --target mod-playerbots-tests && ctest -R BiSRampTests -V`.
**Step 3: Write minimal implementation**  
Read `BiSWeeksAtEndgame` config; apply floor/over-cap calculations only when at max level and at configured progression state; shift percentile roll floor accordingly and apply over-cap chance; integrate into lottery weighting.
**Step 4: Run the tests to confirm success**  
`cmake --build build --target mod-playerbots-tests && ctest -R BiSRampTests -V`.
**Step 5: Commit**  
`git add modules/mod-playerbots/src/PlayerbotAIConfig.* modules/mod-playerbots/src/RandomPlayerbotMgr.cpp modules/mod-playerbots/tests/BiSRampTests.cpp && git commit -m "feat: add global bis ramp config"`

### Task 8: Integration docs and verification
**Files:**
- Add: `doc/CHANGELOG-playerbots-xp-upgrade.md`
- Modify: `modules/mod-playerbots/conf/playerbots.conf.dist` (notes)
- Modify: `modules/mod-playerbots/README.md` (if present) or add section describing configs/workflow
**Step 1: Write the failing test**  
Add doc lint/check placeholder test expecting changelog entry mention new configs (lightweight text check script).
**Step 2: Run it to make sure it fails**  
`ctest -R PlayerbotDocsTests -V` (or run script) expecting missing entry.
**Step 3: Write minimal implementation**  
Document new configs, vendor baseline guarantee, progression helper dependency, BiS ramp semantics, upgrade cadence; note enabling/disabling AutoUpgradeEquip when XP mode is on.
**Step 4: Run the tests to confirm success**  
`ctest -R PlayerbotDocsTests -V` (or rerun script) expect pass.
**Step 5: Commit**  
`git add doc/CHANGELOG-playerbots-xp-upgrade.md modules/mod-playerbots/conf/playerbots.conf.dist modules/mod-playerbots/README.md && git commit -m "docs: document xp-driven bot gear upgrades"`

### Task 9: Smoke verification
**Files:** (no code changes)
- Use existing configs/tests
**Step 1: Configure and build**  
From repo root: `cmake -S . -B build` (if not already), then `cmake --build build`.
**Step 2: Run module test suites**  
`cmake --build build --target mod-individual-progression-tests mod-playerbots-tests` then `ctest -R "ProgressionHelperTests|PlayerbotConfigTests|VendorCacheTests|XpTrackingTests|XpHookTests|UpgradeLotteryTests|BiSRampTests|PlayerbotDocsTests" -V` in `build`.
**Step 3: Optional in-game spot check (manual)**  
Enable `AiPlayerbot.XpUpgradeEnabled=1`, set `XpUpgradeChunk`, `ProgressionState`, `BiSWeeksAtEndgame`, spawn a bot and grant XP/level-up to observe gear changes. Verify vendor baseline applies and percentile upgrades respect progression.
