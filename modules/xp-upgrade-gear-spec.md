XP-Driven Gear Upgrade Spec (Randombots)
========================================

Goal
----
Make randombot gear feel more organic: seed with vendor-grade starter gear, then slowly upgrade over playtime using a weighted lottery instead of always picking the top-score item.

Current Context
---------------
- Gear selection: `PlayerbotFactory::InitEquipment` picks the highest `StatsWeightCalculator` score per slot. The calculator is spec-aware (role + `AiFactory::GetPlayerSpecTab`).
- Gear pool: `RandomItemMgr::GetCachedEquipments(requiredLevel, inventoryType)` holds weapon/armor (and quest rewards) keyed by level + invType. Expansion gating via `AiPlayerbot.LimitGearExpansion`.
- Upgrades today: `AutoMaintenanceOnLevelupAction::AutoUpgradeEquip` can re-run incremental gear on level-up. `EquipmentPersistence` stops full re-randomization past a level.
- Vendor data: “Sold by vendor” is only in `npc_vendor`, not `item_template`.

Design Overview
---------------
1) Seed with vendor-ish gear
   - Build a vendor cache at startup: query `npc_vendor` JOIN `item_template` for armor/weapon, duration=0, RequiredLevel > 0. Index by requiredLevel and inventoryType, keep a few top vendor items per slot. Filter by class mask, armor proficiency, weapon type.
   - In `InitEquipment` (level ≥ 5), if a slot is empty, try the best vendor candidate ≤ bot level; fall back to current cache if none.

2) XP-driven upgrade cadence
   - Track `xp_since_last_upgrade` per bot (store in `RandomPlayerbotMgr` values keyed by GUID).
   - Hook XP gain (or piggyback on level-up): when accumulated XP ≥ 7000, subtract 7000 and run an upgrade pass.

3) Upgrade pass logic (per slot)
   - 50% chance to skip (no upgrade).
   - Gather candidates:
     - From `GetCachedEquipments` for requiredLevel in [level - delta, level], invType matching slot.
     - Respect expansion gating (keep `LimitGearExpansion` logic).
     - Filter to equippable items (skills, armor/weapon type) that are at least comparable or better than current.
   - Score candidates with `StatsWeightCalculator` (spec-aware).
   - Weighted lottery:
     - Sort by score; define percentile bands.
     - Roll 1–100; if ≤ 50 → no upgrade. Else map (roll - 50) to a top-50% band (e.g., 51 ≈ lower percentile upgrade, 100 = best).
     - Pick randomly within that band (or weight by score^k).
   - Equip via existing helpers (`CanEquipUnseenItem`, `EquipNewItem`, `AutoUnequipOffhandIfNeed`).

4) Persistence / coexistence
   - Keep `EquipmentPersistence` on to stop full re-randomize; this XP path becomes the main improvement vector.
   - Disable or short-circuit `AutoUpgradeEquip` when the XP-upgrade mode is enabled to avoid double upgrades.
   - Leave enchants/gems gated by `MinEnchantingBotLevel` and expansion limits.

Config Toggles (proposed)
-------------------------
- `AiPlayerbot.XpUpgradeEnabled` (bool, default off).
- `AiPlayerbot.XpUpgradeChunk` (default 7000 XP).
- `AiPlayerbot.XpUpgradeSkipChance` (default 0.5).
- `AiPlayerbot.XpUpgradePercentileMapping` (mapping function or simple bands for the lottery).
- `AiPlayerbot.VendorSeed` (bool) to enable vendor seeding.
- Optional: `AiPlayerbot.XpUpgradeQualityFloor` to prevent multi-tier jumps.

Implementation Steps
--------------------
1) Add XP counter storage in `RandomPlayerbotMgr` (GetValue/SetValue per GUID).
2) Add vendor cache builder (new helper or inside `RandomItemMgr`): query `npc_vendor`, filter by class/slot, store by level/invType.
3) Modify `PlayerbotFactory::InitEquipment` seeding to try vendor cache first when a slot is empty (respect armor/weapon proficiencies).
4) Hook XP gain (Player::GiveXP or bot AI event) to increment the counter and trigger the upgrade pass when threshold is crossed.
5) Implement the upgrade pass using `StatsWeightCalculator` and existing equip helpers with the percentile-weighted lottery.
6) Gate the new behavior behind config flags; default off to preserve current behavior.

Notes / Assumptions
-------------------
- Vendor detection needs the `npc_vendor` query; without it, approximate by low-quality items from the existing cache.
- Expansion gating can reuse `LimitGearExpansion` plus requiredLevel; an explicit expansion filter can be added if needed.
- The lottery mapping is tunable: e.g., 51–55 → bottom 10% upgrades, 56–70 → middle, 71–90 → upper-middle, 91–100 → top.

Progression Module Integration (heavier option)
-----------------------------------------------
Goal: obey mod-individual-progression when building bot gear pools.

Option A (simple/approx): Map progression state → allowed expansion/ilvl band (e.g., Vanilla ≤ ilvl ~92, TBC ≤ ~164, Wrath ≤ ~290). Filter candidates in `InitEquipment`/upgrade pass by that band. This is easy but coarse.

Option B (better): Add a helper in the progression module, e.g., `bool IsItemAllowedForProgression(Player* p, uint32 itemId)`, that mirrors the DB conditions used for vendors/drops/phasing. Then:
- Expose it via a public API in mod-individual-progression.
- In playerbots’ gear selection, when considering a candidate item, call this helper and drop disallowed items.
- Gate this with a config like `AiPlayerbot.ProgressionEnforce = 1` so it’s opt-in.

Hooking bots to a progression state:
- Use existing `.ip setbot` or add a config `AiPlayerbot.ProgressionState` to force bots to a specific state on creation/refresh by calling `ForceUpdateProgressionState`.
- Alternatively, sync to a “server progression” number you maintain manually; store it in config and set bots accordingly at creation.

Weekly BiS Odds Ramp (simulation notes)
---------------------------------------
Objective: after reaching endgame, gradually increase the chance that upgrade rolls land in the top percentile, simulating raid loot circulation over weeks.

Ideas:
1) Time-based weight boost: track a “weeks_at_endgame” counter (increment weekly or daily tick). Adjust lottery weights: weight = score^(1 + weeks_at_endgame * k), where k is small (e.g., 0.05). Cap at a maximum to avoid instant BiS.
2) Percentile floor lift: each week, raise the minimum percentile band reachable on an upgrade roll. Example: week 0 → 50% of upgrades are skipped (per base rule), week 1+ -> still 50% skip, but when upgrading, minimum band starts at top 60% instead of 50%; week 2 -> top 70%, etc., capped at, say, 90%.
3) Allow over-cap tail: if using a gearscore/quality cap, add a weekly small probability (e.g., 2% + 1% per week, capped) to exceed the cap by one tier, simulating raid drops leaking into circulation.
4) Loot budget accrual: accumulate “raid loot tokens” weekly per bot/account; spending a token guarantees an upgrade from the top X% of the pool. Tokens earned only at max level and after a minimum progression state.

Implementation options:
- Store `weeks_at_endgame` or `loot_tokens` per bot in `RandomPlayerbotMgr` values; increment on a scheduled event (e.g., once per 7 real days) or manual GM command.
- Add config to enable/scale the ramp: `AiPlayerbot.BiSRampEnabled`, `BiSWeeklyBoost`, `BiSMaxPercentile`, `BiSOvercapChancePerWeek`.
- Integrate with progression: only apply the BiS ramp when bot progression state >= your “endgame” threshold (e.g., current progression phase cap).

Automatic Progression Sync for Bots
-----------------------------------
Goal: align bots with individual progression so they respect level caps (60/70) and phased content, and clamp their gear pool.

Configs (proposed):
- `AiPlayerbot.ProgressionSyncMode`: off | fixed | lowest_online | server_progression.
  - off: no sync (current behavior).
  - fixed: use `AiPlayerbot.ProgressionState` value.
  - lowest_online: set bots to the lowest progression state of online real players.
  - server_progression: use an admin-defined number representing the realm’s current phase.
- `AiPlayerbot.ProgressionEnforce`: bool, when true filters gear by progression.

Bot progression setting:
- On bot init/refresh, if sync is enabled, call the individual progression API to set the bot’s progression state (equivalent to `.ip setbot`). This lets the module enforce its level caps and phased vendors/drops for bots.
- Provide a GM command to bulk-update existing bots’ progression.

Gear pool filtering by progression:
- Option A (coarse): map progression state → ilvl/expansion ceiling (e.g., Vanilla ≤ ilvl ~92, TBC ≤ ~164, WotLK ≤ ~290) and reject items above the ceiling during candidate selection in `InitEquipment` and upgrade passes.
- Option B (better): add a helper in mod-individual-progression, e.g., `IsItemAllowedForProgression(Player*, itemId)`, mirroring its vendor/drop conditions. Call it when evaluating candidates; skip disallowed items. Gate with `AiPlayerbot.ProgressionEnforce=1`.
- Keep `LimitGearExpansion` on; progression filter is stricter when enabled.

Existing over-cap bots:
- After adding sync, run the GM command or a script to set their progression state. The progression module will cap levels to 60/70 until advanced.
- Optionally re-randomize or apply gear filters so their equipped items comply; or rely on the progression enforcement to block future out-of-band equips.

BiS weekly ramp integration with progression:
- Only apply the weekly BiS boost for bots at max level AND at the current progression cap (as defined by the sync mode).
- Use the weekly counters/tokens described above to gradually raise top-percentile odds or over-cap chances within the allowed progression tier.
