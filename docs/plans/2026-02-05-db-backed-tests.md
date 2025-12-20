# DB-Backed Tests for XP Gear Work Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable optional DB-backed test runs that load real item/vendor/class/skill/stats data in-process (no external worldserver) to cut mocking and increase fidelity for XP gear upgrades.

**Architecture:** Add an opt-in test bootstrap that opens `_test` schemas via `DatabaseLoader`, loads a targeted subset of ObjectMgr data into memory, and exposes a helper for gtests to initialize once per process. Provide helper scripts to seed `_test` schemas from live DB or a minimal dump. Guard with env flags and schema suffix safety.

**Tech Stack:** AzerothCore C++ (gtest), DatabaseLoader/DatabaseWorkerPool, MySQL (existing instance), bash/CMake helper for seeding.

### Design Summary
- **Section 1 – Goals, toggle, and DB source**
  - Goal: optional DB-backed mode for gtests using real data (item/vendor/class/skill/stats) without spawning worldserver.
  - Toggle: env/config guard (e.g., `AC_TEST_DB_BOOTSTRAP=1`) to enable; off keeps existing mock-heavy tests.
  - DB source: reuse existing MySQL instance; DSNs via env (`AC_TEST_LOGIN_DB`, `AC_TEST_WORLD_DB`, `AC_TEST_CHAR_DB`, `AC_TEST_PLAYERBOTS_DB`), default to `_test` suffix schemas.
  - Snapshot handling: helper to clone from live schemas (mysqldump) or load curated minimal dump if available.
  - Isolation: set playerbots DB info too when needed; no auth flow required.

- **Section 2 – In-process bootstrap mechanics**
  - Entry: `TestDbBootstrap` helper (e.g., `src/test/support/TestDbBootstrap.h/.cpp`) invoked by DB-enabled gtests; reads env for DSNs and boolean flag.
  - Loader reuse: use `DatabaseLoader`, adding pools for login/world/characters/playerbots; allow narrower mask via env (e.g., `AC_TEST_DB_MASK=world,characters,playerbots`); call `Load()` with updates disabled; fail fast on missing schemas.
  - Table scope: after pools open, invoke necessary ObjectMgr loaders: item templates, vendor tables, skill/proficiency, class level stats, spell/item random props, playerbot vendor data. Skip unrelated systems to keep runtime down.
  - World/config: tests may still swap `sWorld` with `WorldMock` for config; bootstrap only populates DB pools and in-memory stores.
  - Lifetime: one-time per test process; caches stay live for subsequent tests; cleanup closes pools at exit.

- **Section 3 – Snapshot/seeding workflow**
  - Schemas: use `_test` suffixed schemas (`acore_auth_test`, `acore_world_test`, `acore_characters_test`, `acore_playerbots_test`).
  - Seeding options: (1) clone-from-live via helper `prepare-test-db` (mysqldump selected tables, import into `_test`, truncate first); (2) load minimal curated dump if provided.
  - Config: source selection via env (`AC_TEST_DB_SOURCE=live|minimal`), dump path via `AC_TEST_DB_DUMP`; defaults to clone-from-live if permitted.
  - Safety: enforce `_test` suffix guard; refuse to run if target schemas lack `_test` unless explicit override env set (discouraged). Optionally drop/create DBs when allowed.
  - Invocation: run seeding before ctest when `AC_TEST_DB_BOOTSTRAP=1` or when invoking `ctest -L db`.

- **Section 4 – Test integration and runtime**
  - Opt-in: DB-backed tests under `DbIntegration.*` (or similar); guard with `if (!TestDbBootstrap::Enabled()) GTEST_SKIP();` so default ctest stays fast.
  - Shared bootstrap: `EnsureInitialized()` opens pools and loads data on first DB-backed test; reuse thereafter.
  - World mocking: allowed for configs; no worldserver process needed.
  - Runtime: single bootstrap per process; expected overhead from DB load (30–90s acceptable). Subsequent tests fast.
  - Failure semantics: if `_test` unavailable or seeding skipped, tests fail with clear message unless optional skip flag set (e.g., `AC_TEST_DB_OPTIONAL=1`). Data mutations discouraged; if needed, wrap in transaction or reset helper.
  - CTest wiring: label these tests `db`; run via `ctest -L db` after preparing DB. Default `ctest` omits unless env flag set.

- **Section 5 – Config overrides and safety**
  - DSNs via env: `AC_TEST_LOGIN_DB`, `AC_TEST_WORLD_DB`, `AC_TEST_CHAR_DB`, `AC_TEST_PLAYERBOTS_DB` formatted `host;port;user;pass;schema`; default schema suffix `_test`; fail fast if missing suffix unless override env set.
  - Redaction: log host/port/schema only; never echo passwords.
  - INI override: optional `AC_TEST_DB_CONF=/path/to/conf` (worldserver-style) with env precedence.
  - Updates off by default: disable auto-updater; require explicit flag `AC_TEST_DB_ALLOW_UPDATES=1` to run updates/DDL.
  - Module configs: mirror override handling for playerbots DB info when present.
  - Guard rails: refuse to connect to live (`acore_world`, etc.) without `_test` suffix unless explicit override; docs warn not to set override in CI/dev defaults.

### Tasks

#### Task 1: Add test DB bootstrap helper
**Files:**
- Create: `src/test/support/TestDbBootstrap.h`
- Create: `src/test/support/TestDbBootstrap.cpp`
- Modify: `src/test/CMakeLists.txt` (include support dir if needed)

**Step 1: Write failing test**
- Add a small gtest `DbBootstrapSmoke` in `src/test/modules/DbBootstrapTests.cpp` that calls `TestDbBootstrap::Enabled()` (driven by env) and `EnsureInitialized()` expecting a clear failure message when env flag is set but schemas missing.

**Step 2: Run and expect fail**
- `cmake --build build --target mod-playerbots-tests && ctest -R DbBootstrapTests -V`

**Step 3: Implement helper**
- Implement `TestDbBootstrap` to:
  - Parse `AC_TEST_DB_BOOTSTRAP` flag and DSNs from env (defaults to `_test` suffix).
  - Enforce `_test` suffix unless override env.
  - Open pools via `DatabaseLoader`, with mask parsing.
  - Call targeted ObjectMgr loaders (item/vendor/skills/class stats/random props/playerbot vendor if applicable).
  - Track one-time init and expose `Enabled()`/`EnsureInitialized()` with error messages; redact passwords in logs.

**Step 4: Run and expect pass**
- `cmake --build build --target mod-playerbots-tests && ctest -R DbBootstrapTests -V`

**Step 5: Commit**
- `git add src/test/support/TestDbBootstrap.* src/test/CMakeLists.txt src/test/modules/DbBootstrapTests.cpp && git commit -m "test: add db bootstrap helper for integration tests"`

#### Task 2: Seed helper script/target
**Files:**
- Create: `tools/testing/prepare-test-db.sh`
- Modify: `CMakeLists.txt` (add custom target `prepare-test-db`)
- Modify: `docs/plans/2026-02-02-xp-upgrade-gear.md` (link) or new doc reference

**Step 1: Write failing doc/test check**
- Add a lightweight script or ctest that ensures `prepare-test-db.sh` exists and is executable when `AC_TEST_DB_BOOTSTRAP=1`.

**Step 2: Run expect fail**
- `ctest -R PrepareTestDbCheck -V`

**Step 3: Implement script/target**
- Bash script to:
  - Validate target schemas end with `_test`.
  - Clone from live via `mysqldump` selected tables or load minimal dump based on env (`AC_TEST_DB_SOURCE`, `AC_TEST_DB_DUMP`).
  - Truncate/reset target schemas safely; optional drop/create when allowed.
  - Emit instructions and errors clearly.
- Add CMake custom target `prepare-test-db` invoking the script.

**Step 4: Run expect pass**
- `cmake --build build --target prepare-test-db` or `ctest -R PrepareTestDbCheck -V`

**Step 5: Commit**
- `git add tools/testing/prepare-test-db.sh CMakeLists.txt docs/plans/2026-02-02-xp-upgrade-gear.md && git commit -m "test: add helper to seed _test databases"`

#### Task 3: Convert a target test to use DB mode (spike)
**Files:**
- Modify: `src/test/modules/VendorCacheTests.cpp` (or new `DbIntegration/VendorCacheDbTests.cpp`)
- Modify: `src/test/CMakeLists.txt` (register new test)

**Step 1: Write failing DB-backed test**
- Add `TEST(DbIntegration, VendorCacheUsesDb)` that enables bootstrap (requires env flag), runs bootstrap, then asserts `GetBestVendorItem` returns a known vendor item from DB (choose a deterministic entry from dump).

**Step 2: Run expect fail**
- `ctest -R DbIntegration.VendorCacheUsesDb -V` with `AC_TEST_DB_BOOTSTRAP=1` (after seeding) — fail until bootstrap works and data present.

**Step 3: Adjust bootstrap/test if needed**
- Ensure test skips when flag off; uses real DB data, not mocks.

**Step 4: Run expect pass**
- `ctest -R DbIntegration.VendorCacheUsesDb -V` with seeded `_test` DB.

**Step 5: Commit**
- `git add src/test/modules/VendorCacheTests.cpp src/test/CMakeLists.txt && git commit -m "test: add db-backed vendor cache integration case"`

#### Task 4: Documentation
**Files:**
- Create: `docs/testing/db-backed-tests.md`
- Modify: `docs/plans/2026-02-02-xp-upgrade-gear.md` (append overview + link)

**Step 1: Write doc**
- Document env vars, seeding steps, safety guards, ctest labels, runtime expectations.

**Step 2: Add test check (optional)**
- Simple text check ensuring doc mentions key env vars.

**Step 3: Commit**
- `git add docs/testing/db-backed-tests.md docs/plans/2026-02-02-xp-upgrade-gear.md && git commit -m "docs: add db-backed test workflow"`

#### Task 5: Verification
**Step 1: Build**
- `cmake --build build --target mod-playerbots-tests`

**Step 2: Prepare DB (optional)**
- `cmake --build build --target prepare-test-db` (or run script) with env pointing to live schemas.

**Step 3: Run tests**
- Fast suite: `ctest -R "DbBootstrapTests" -V`
- DB suite: `AC_TEST_DB_BOOTSTRAP=1 ctest -L db -V`

**Step 4: Commit if new changes**
- `git add ... && git commit -m "chore: verify db-backed test harness"`

**Execution Choice**
Plan complete and saved to `docs/plans/2026-02-05-db-backed-tests.md`. Two execution options:
1. Subagent-Driven (this session) — dispatch per task with review.
2. Parallel Session — new session using superpowers:executing-plans.
Which approach? (If continuing here, set up a WIP branch and run executing-plans.)
