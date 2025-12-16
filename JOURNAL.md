## Notes
- Working in /home/bjw/azerothcore; multiple repos present.
- Treat existing uncommitted changes as part of the plan; create working branches once we start edits.
- Need to follow superpowers skills; brainstorming required for xp-upgrade gear spec.
- Plan updated: XP chunk configurable; upgrade rolls happen on XP chunk, level-up, and endgame week change; per-slot percentile roll across score-ranked suitable gear (up to level/progression). Equip nearest-percentile item if better; otherwise upgrade to best vendor baseline. Weekly ramp via global weeks config adjusts percentile floor/over-cap.
- Plan doc expanded with explicit steps, references to spec/JOURNAL, and smoke verification.
- Branch `xp-upgrade-gear` active in root repo; worktree removed per request.
- Module sources present in `modules/mod-individual-progression` and `modules/mod-playerbots`; each is its own git repo (submodule). Need to work on module branches (`xp-upgrade-gear`) before changing them.
- Enabling BUILD_TESTING triggers googletest fetch from GitHub; network is restricted so configure fails until we get access or a vendored gtest source.
- Build incantation `./acore.sh compiler build` flows: `acore.sh` → `apps/installer/main.sh` → `apps/compiler/compiler.sh` → `comp_build` → `comp_configure` + `comp_compile`. Config comes from `conf/config.sh` (if present) overriding `conf/dist/config.sh`. Multi-threading comes from `MTHREADS` (default 0 in dist); `comp_compile` sets `MTHREADS=$(nproc)+2` when 0, then runs `cmake --build . --config $CTYPE -j $MTHREADS`. To force threads, set `MTHREADS=<num>` in `conf/config.sh`; otherwise auto-uses CPU count + 2. Build/install paths: `BUILDPATH=$AC_PATH_VAR/build/obj`, `BINPATH=$AC_PATH_ROOT/env/dist`.
### 2026-02-02 XP upgrade work - build/test/mocking notes
- Build/test targets: unit tests live under `src/test`. `cmake --build build --target mod-playerbots-tests -j $(nproc)` builds `unit_tests` and runs fine after configure; long build time but parallel helps. `ctest -R <name> -V` executes filtered tests from `build/src/test`. gcov emits corruption warnings if old `.gcda` files linger; clean coverage artifacts before caring about coverage output.
- Git identity: module repos are separate; set `user.name/user.email` per-module when committing.
- Playerbot config tests need to stub world interactions: `WorldMock` (from `src/test/mocks/WorldMock.h`) implements `getIntConfig` and friends. In tests, replace `sWorld` via `sWorld.release()`/`reset(worldMock)` and restore in `TearDown`. Avoid calls into `sWorld->getIntConfig` in production code where possible (use config defaults) to keep tests light.
- Playerbot initialization is heavy: bot creation hits DB pools and can SIGFPE when no connections. Added `AiPlayerbot.SkipInitialSetup` config to short-circuit heavy init in tests; set it in test configs.
- When ptrace is blocked, gdb needs escalated permissions; request escalation and rerun. Use backtrace to spot crashes in `PlayerbotAIConfig::Initialize` triggered by DB access.
- `getWorldInstance` returns a `unique_ptr<IWorld>`; replacing it with mocks is allowed via `sWorld.release()`/`reset` in tests. Ensure restoration in `TearDown` to avoid leaking mocks across tests.
- Vendor baseline tests: `EquipNewItem` fires achievement hooks and will segfault in tests unless `ScriptRegistry<AchievementScript>::InitEnabledHooksIfNeeded(ACHIEVEMENTHOOK_END)` is called; add this to test setup alongside other script registries when mocking `sWorld`.
- Vendor seeding must be spec-aware: when filling empty slots from vendor cache, pick the candidate with the highest `StatsWeightCalculator` score for the bot (tie-break required level then item level), not just the highest item level. Tests now assert this.
- Plan tweak: vendor seeding should be spec-aware—use `StatsWeightCalculator` to pick the best vendor item for the bot’s spec from candidates ≤ bot level; add tests to cover score-based vendor selection.
