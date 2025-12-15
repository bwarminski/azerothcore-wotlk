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
