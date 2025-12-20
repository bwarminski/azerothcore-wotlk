# Playerbots XP Gear Upgrades

## Summary
- XP-driven upgrade passes trigger on XP chunks, level-ups, and endgame week changes.
- Vendor baselines seed empty slots using spec-aware scoring.
- Progression-aware filters block items outside the configured progression state.
- Endgame ramp applies at max level: percentile floor `min(90, 50 + 10 * weeks)` and over-cap chance `min(10, 2 + weeks)`.
- `AutoUpgradeEquip` is bypassed when `AiPlayerbot.XpUpgradeEnabled` is enabled.

## Configuration
- `AiPlayerbot.XpUpgradeEnabled` (0/1): enable XP-driven upgrade passes.
- `AiPlayerbot.XpUpgradeChunk` (XP): XP required per upgrade pass.
- `AiPlayerbot.VendorSeedEnabled` (0/1): seed vendor baseline gear.
- `AiPlayerbot.ProgressionState` (0+): progression state for gear filters.
- `AiPlayerbot.BiSWeeksAtEndgame` (weeks): ramp weeks applied at max level.
