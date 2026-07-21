# Multi-Floor Vehicles — Milestone 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Widen the vehicle part mount coordinate from 2D (`point_rel_ms`) to 3D (`tripoint_rel_ms`) with z pinned to 0, as a provable behavioral no-op, laying the data-model foundation for multi-floor vehicles.

**Architecture:** Pure mechanical refactor of a single struct field plus its serialization and the caches keyed on it. `mount.z` is 0 everywhere; the compiler drives the call-site changes; correctness is proven by the full existing `cata_test` suite staying green plus new save/load round-trip characterization tests. No gameplay, rendering, physics, or ramp behavior changes.

**Tech Stack:** C++17, Catch2 (`tests/cata_test`), the project's typed coordinate wrappers (`src/coordinates.h`, `src/point.h`), `generic_factory`, astyle 3.1.

**Spec:** `docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md` (§0 serialization, §1 data model). This plan implements **milestone 1 only**.

## Global Constraints

- **Zero behavior change.** `mount.z` MUST be 0 for every part after this milestone. The pass bar for every task is "the full existing suite is still green." Any test that changes result is a bug in the refactor, not a test to update.
- **Build (debug, for development):** `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`. Do NOT pass `FRAMEWORK` at all. Full from-scratch build ≈ 7–8 min; incremental is much faster.
- **Run one test while iterating:** `tests/cata_test "<name>"` (a single process is fine for one named test).
- **Run the whole suite (the real gate):** `build-scripts/gha_test_only.sh` (needs GNU `parallel`) — NOT a single `tests/cata_test` call, because some `[monster]` tests cross-contaminate in one process.
- **C++ formatting before every commit:** astyle 3.1 exactly. Run `astyle --options=.astylerc <changed files>` (see `doc/c++/CODE_STYLE.md`). Project style: spaces inside parens `if( x )`, `foo( a, b )`, `--align-pointer=name`, 4-space indent, 100-col, 1TBS.
- **Three distinct z meanings — never conflate:**
  - `vehicle_part::mount.z` — NEW: the permanent floor (0 = ground). This milestone pins it to 0.
  - `vehicle_part::precalc[i].z()` — EXISTING: transient ramp displacement. **Do not touch in this milestone.**
  - `vehicle_part::carried_part_data::mount.z()` — EXISTING: racking/carry subsystem (`mount_z` in JSON). Unrelated; leave alone.
- **Serialization back-compat:** new `mount_dz` field is omitted when zero on write and defaults to 0 on read, so pre-change saves load byte-for-byte equivalently.

## Explicitly OUT of milestone 1 (do not do here)

- The real `precalc.z = mount.z + ramp_displacement` composition / seeding. `precalc.z` today holds ramp displacement, maintained incrementally in `vehicle.cpp:8700-8715` and preserved because `coord_translate` never writes z. Reseeding it naively would wipe ramp offsets (a behavior change). The composition-safe design (separating ramp displacement into its own field) is deferred to the milestone where `mount.z` first goes non-zero and the composition is actually testable (milestone 3, rendering). In this milestone `coord_translate` continues to leave `precalc.z` untouched, which is correct because `mount.z == 0`.
- JSON `"z"` authoring, the vertical-connector `VPFLAG`, `can_mount` ladder-gate — milestone 2.
- 3D adjacency/connectivity/split logic (`find_and_split_vehicles`, `four_adjacent_offsets`) — milestone 3. This milestone keeps adjacency 2D (operating on `mount.xy()`); with z==0 that is a no-op.
- `part_displayed_at` / `get_display_of_tile` z-selection — milestone 3 (no-op while z==0).

---

## File Structure

- `src/vehicle.h` — the `vehicle_part::mount` field decl; the `relative_parts`, `edges`, `mount_min`/`mount_max` members; signatures of `parts_at_relative`, `can_mount`, `is_external_part`, `get_edge_info`, etc.
- `src/vehicle.cpp` — the bulk of call sites: `refresh()`, `parts_at_relative()`, `precalc_mounts()`, cache builders, part lookups.
- `src/vehicle_move.cpp`, `src/vehicle_group.cpp`, `src/veh_interact.cpp`, `src/veh_type.cpp`, `src/vehicle_part.cpp`, and other consumers — mechanical `.mount` / `parts_at_relative` call-site fixes surfaced by the compiler.
- `src/savegame_json.cpp:3244-3378` — `vehicle_part::serialize`/`deserialize`; add `mount_dz`.
- `tests/vehicle_savegame_mount_test.cpp` — NEW: save/load round-trip characterization + legacy back-compat + z round-trip.
- `tests/vehicle_export_test.cpp` — EXISTING: already asserts the exported prototype JSON has `{"x":0,"y":0,...}` with no `z`; it is part of the regression gate (must stay green unchanged).

---

## Task 1: Baseline save/load round-trip characterization test

Pin current 2D round-trip behavior with a test that passes against the **unmodified** code, establishing the harness before any refactor. This is characterization testing: the test encodes "a spawned vehicle survives serialize→deserialize with identical part mounts."

**Files:**
- Create: `tests/vehicle_savegame_mount_test.cpp`
- Reference (do not modify): `src/savegame_json.cpp:3561` (`vehicle::serialize`), `3435` (`vehicle::deserialize`), `tests/vehicle_test.cpp:70` (how tests spawn a vehicle), `tests/map_helpers.h` (`clear_map`).

**Interfaces:**
- Consumes: `map::add_vehicle`, `vehicle::serialize(JsonOut&)`, `vehicle::deserialize(const JsonObject&)`, `vehicle::part_count()`, `vehicle::part(int)`, `vehicle_part::mount`.
- Produces: the test file `tests/vehicle_savegame_mount_test.cpp` with helper `serialize_then_deserialize(const vehicle&)`, reused/extended in Task 2.

- [ ] **Step 1: Add the test file to the build**

Tests are globbed by the build; confirm by grepping the makefile for how test sources are collected (`grep -n "tests/.*\.cpp\|TEST_SOURCES\|wildcard.*tests" Makefile`). If tests are auto-globbed (`$(wildcard tests/*.cpp)`), no makefile edit is needed. If they are listed explicitly, add `tests/vehicle_savegame_mount_test.cpp` to that list.

- [ ] **Step 2: Write the characterization test**

```cpp
#include <sstream>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "coordinates.h"
#include "json.h"
#include "json_loader.h"
#include "flexbuffer_json.h"
#include "map.h"
#include "map_helpers.h"
#include "point.h"
#include "type_id.h"
#include "vehicle.h"
#include "vehicle_part.h"

static const vproto_id vehicle_prototype_car( "car" );

// Serialize a live vehicle to a JSON string, then load it back into `dst`.
// `vehicle` has no default ctor and a deleted copy ctor, so `dst` is
// constructed by the caller with a null vproto_id (an empty vehicle, the same
// trick vehicles::finalize_prototypes uses) and filled by deserialize.
static void serialize_then_deserialize( const vehicle &src, vehicle &dst )
{
    std::ostringstream os;
    JsonOut jsout( os );
    src.serialize( jsout );

    JsonValue jv = json_loader::from_string( os.str() );
    dst.deserialize( jv.get_object() );
}

TEST_CASE( "vehicle_part_mount_round_trips_through_save", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms::zero,
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();

    std::vector<point_rel_ms> before;
    for( int i = 0; i < veh_ptr->part_count(); i++ ) {
        before.push_back( veh_ptr->part( i ).mount );
    }
    REQUIRE_FALSE( before.empty() );

    vehicle after( vproto_id() );
    serialize_then_deserialize( *veh_ptr, after );
    REQUIRE( after.part_count() == veh_ptr->part_count() );
    for( int i = 0; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount == before[i] );
    }
}
```

Note: `before` is typed `point_rel_ms` here because at baseline `mount` is 2D. Task 2 changes this type. If `car` is not a valid `vproto_id` in this build, substitute a known one — verify with `grep -rl '"id": "car"' data/json/vehicles/` or reuse `vehicle_prototype_bicycle` as in `tests/vehicle_test.cpp:70`.

- [ ] **Step 3: Build**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: compiles; `tests/cata_test` binary produced.

- [ ] **Step 4: Run the test, expect PASS (characterization of current behavior)**

Run: `tests/cata_test "vehicle_part_mount_round_trips_through_save"`
Expected: PASS. (If it fails, the harness/prototype id is wrong — fix before proceeding; do not touch production code yet.)

- [ ] **Step 5: Format and commit**

```bash
astyle --options=.astylerc tests/vehicle_savegame_mount_test.cpp
git add tests/vehicle_savegame_mount_test.cpp Makefile
git commit -m "test: characterize vehicle part mount save round-trip (baseline)"
```

---

## Task 2: Widen `vehicle_part::mount` to `tripoint_rel_ms` + serialization

The core change. Widen the field, add `mount_dz` serialization, and mechanically fix every compile error the widening surfaces, keeping z pinned to 0. This is a single atomic compile-unit change (C++ type widening cannot be partial), so it lands in one commit; the steps below are the methodical procedure, not five independent commits.

**Files:**
- Modify: `src/vehicle.h:456` (field decl)
- Modify: `src/savegame_json.cpp:3279-3280`, `3337-3338` (add `mount_dz`)
- Modify: many call sites across `src/vehicle*.cpp`, `src/veh_*.cpp` (compiler-surfaced)
- Modify: `tests/vehicle_savegame_mount_test.cpp` (extend for z)

**Interfaces:**
- Produces: `vehicle_part::mount` is now `tripoint_rel_ms`. Consumers obtain the 2D part via `mount.xy()` (→ `point_rel_ms`) and construct from a 2D point via `tripoint_rel_ms( pt.x(), pt.y(), 0 )`.

- [ ] **Step 1: Write the failing z round-trip + legacy back-compat tests**

Add to `tests/vehicle_savegame_mount_test.cpp`:

```cpp
TEST_CASE( "vehicle_part_mount_z_round_trips_through_save", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms::zero,
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();
    REQUIRE( veh_ptr->part_count() > 0 );

    // Directly set an upper-deck z on one part (JSON authoring arrives in
    // milestone 2; this exercises the serialization path only).
    veh_ptr->part( 0 ).mount.z() = 1;

    vehicle after( vproto_id() );
    serialize_then_deserialize( *veh_ptr, after );
    REQUIRE( after.part_count() == veh_ptr->part_count() );
    CHECK( after.part( 0 ).mount.z() == 1 );
    for( int i = 1; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount.z() == 0 );
    }
}

TEST_CASE( "legacy_vehicle_part_without_mount_dz_defaults_to_z0", "[vehicle][savegame]" )
{
    clear_map();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms::zero,
                                         0_degrees, -1, 0 );
    REQUIRE( veh_ptr != nullptr );
    veh_ptr->refresh();

    // A normal (all-z0) vehicle must serialize WITHOUT any mount_dz key, so old
    // saves and new saves are byte-compatible for ground-only vehicles.
    std::ostringstream os;
    JsonOut jsout( os );
    veh_ptr->serialize( jsout );
    CHECK( os.str().find( "mount_dz" ) == std::string::npos );

    // And loading such (legacy-format) JSON yields z == 0 everywhere.
    vehicle after( vproto_id() );
    serialize_then_deserialize( *veh_ptr, after );
    for( int i = 0; i < after.part_count(); i++ ) {
        CHECK( after.part( i ).mount.z() == 0 );
    }
}
```

Also update the Task 1 test's `before` vector type from `point_rel_ms` to `tripoint_rel_ms` (so full-3D equality is checked):

```cpp
    std::vector<tripoint_rel_ms> before;
```

- [ ] **Step 2: Verify the new tests fail to COMPILE (mount has no `.z()` yet)**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 tests/cata_test`
Expected: compile error on `.mount.z()` — proves the tests exercise the new field.

- [ ] **Step 3: Widen the field**

In `src/vehicle.h:456`, change:

```cpp
        /** mount point: x is on the forward/backward axis, y is on the left/right axis */
        point_rel_ms mount;
```
to:
```cpp
        /** mount point: x fwd/back, y left/right, z is the floor (0 = ground deck) */
        tripoint_rel_ms mount;
```

- [ ] **Step 4: Update serialization (`src/savegame_json.cpp`)**

In `vehicle_part::deserialize` after line 3280 (`data.read( "mount_dy", mount.y() );`) add:

```cpp
    mount.z() = 0;
    data.read( "mount_dz", mount.z() );
```

(`data.read` leaves `mount.z()` untouched when the key is absent; the explicit `= 0` guards against any uninitialized path, guaranteeing legacy saves → z 0.)

In `vehicle_part::serialize` after line 3338 (`json.member( "mount_dy", mount.y() );`) add:

```cpp
    if( mount.z() != 0 ) {
        json.member( "mount_dz", mount.z() );
    }
```

- [ ] **Step 5: Mechanically fix compile errors, z pinned to 0**

Rebuild repeatedly (`make ... -j8`) and fix each error by category. The mechanical rules:

- A function expecting `point_rel_ms` fed `part.mount` → pass `part.mount.xy()`.
- Constructing/assigning `mount` from a 2D `point_rel_ms pt` → `tripoint_rel_ms( pt.x(), pt.y(), 0 )`.
- `mount + offset` where `offset` is `point_rel_ms` → `mount.xy() + offset` if a 2D result is wanted, else `mount + tripoint_rel_ms( offset.x(), offset.y(), 0 )`.
- Map/set lookups keyed by `point_rel_ms` fed `mount` → use `mount.xy()` for now (the maps themselves are widened in Tasks 3–4).
- Equality/printing of `mount` → unchanged (`tripoint_rel_ms` supports both).
- `coord_translate( ..., p.mount, ... )` in `precalc_mounts` (`vehicle.cpp:3657`) → pass `p.mount.xy()`. **Do not** add any `precalc.z` write here (see OUT-of-scope).

Representative example — `precalc_mounts` (`src/vehicle.cpp:3650-3661`) becomes:

```cpp
    std::unordered_map<point_rel_ms, tripoint_rel_ms> mount_to_precalc;
    for( vehicle_part &p : parts ) {
        if( p.removed ) {
            continue;
        }
        auto q = mount_to_precalc.find( p.mount.xy() );
        if( q == mount_to_precalc.end() ) {
            coord_translate( tdir, pivot, p.mount.xy(), p.precalc[idir] );
            mount_to_precalc.insert( { p.mount.xy(), p.precalc[idir] } );
        } else {
            p.precalc[idir] = q->second;
        }
    }
```

(The memo stays keyed by the 2D projection in this task; Task 5 widens it. Because z==0, identical behavior.)

Work file-by-file until the whole build is clean. Expect changes concentrated in `src/vehicle.cpp`, `src/vehicle_move.cpp`, `src/veh_interact.cpp`, `src/vehicle_group.cpp`, `src/veh_type.cpp`, `src/vehicle_part.cpp`.

- [ ] **Step 6: Build clean**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: full build succeeds, no errors or new warnings.

- [ ] **Step 7: Run the mount tests, expect PASS**

Run: `tests/cata_test "[savegame]"`
Expected: all three mount tests PASS (baseline round-trip, z round-trip, legacy default-z0).

- [ ] **Step 8: Run the existing export test, expect UNCHANGED PASS**

Run: `tests/cata_test "export_vehicle_test"`
Expected: PASS with no change to the expected prototype string (proves no stray `"z":0` is emitted in prototype export).

- [ ] **Step 9: Run the FULL suite (the real no-op gate)**

Run: `build-scripts/gha_test_only.sh`
Expected: entire suite green. Pay special attention to `[vehicle]`, `vehicle_ramp_test`, `vehicle_split_test`, `vehicle_drag_test`, `vehicle_efficiency_test` — any change there means the refactor leaked behavior (most likely a `.xy()` you missed or a stray z write). Fix until green.

- [ ] **Step 10: Format and commit**

```bash
astyle --options=.astylerc src/vehicle.h src/savegame_json.cpp src/vehicle.cpp src/vehicle_move.cpp src/veh_interact.cpp src/vehicle_group.cpp src/veh_type.cpp src/vehicle_part.cpp tests/vehicle_savegame_mount_test.cpp
git add -A
git commit -m "refactor: widen vehicle_part::mount to tripoint_rel_ms (z pinned 0)

Adds mount_dz serialization (omit-when-zero, default-0 read for legacy
saves). No behavior change: mount.z is 0 for all parts. Foundation for
multi-floor vehicles (milestone 1)."
```

---

## Task 3: 3D-key `relative_parts` and `parts_at_relative`

Widen the primary part-lookup cache from 2D to 3D keys. With z==0, a no-op.

**Files:**
- Modify: `src/vehicle.h:2332` (`relative_parts` type), `1231` (`parts_at_relative` signature) and related part-lookup declarations
- Modify: `src/vehicle.cpp` (`refresh()` cache build, `parts_at_relative()` body, all callers)

**Interfaces:**
- Produces: `relative_parts` is `std::map<tripoint_rel_ms, std::vector<int>>`; `parts_at_relative( const tripoint_rel_ms &dp, bool use_cache, ... )`. Callers pass a full 3D mount; 2D callers pass `tripoint_rel_ms( pt.x(), pt.y(), 0 )`.

- [ ] **Step 1: Change the cache key type**

In `src/vehicle.h:2332`:
```cpp
        std::map<tripoint_rel_ms, std::vector<int>> relative_parts; // NOLINT(cata-serialize)
```

- [ ] **Step 2: Change `parts_at_relative` to take a 3D mount**

In `src/vehicle.h` (declaration ~`1231`) and `src/vehicle.cpp` (definition), change the parameter from `const point_rel_ms &dp` to `const tripoint_rel_ms &dp`. Update the body — it indexes `relative_parts` and compares against `parts[i].mount`, both now 3D, so the internal logic is unchanged.

- [ ] **Step 3: Fix the `refresh()` cache builder**

Where `refresh()` populates `relative_parts[ parts[i].mount ]` (grep `relative_parts\[`), the key is now the 3D `mount` directly — remove any `.xy()` added in Task 2 at these specific insertion/lookup sites so the cache is genuinely 3D-keyed.

- [ ] **Step 4: Fix all `parts_at_relative` callers (compiler-driven)**

Rebuild; for each caller passing a `point_rel_ms`, wrap as `tripoint_rel_ms( pt.x(), pt.y(), 0 )`, EXCEPT callers that already have a part's `mount` — pass `part.mount` (full 3D). Callers doing 2D-grid math (adjacency via `four_adjacent_offsets`) keep operating at z==0: pass `tripoint_rel_ms( neighbor.x(), neighbor.y(), 0 )`.

- [ ] **Step 5: Build clean**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: builds with no errors.

- [ ] **Step 6: Run the full suite**

Run: `build-scripts/gha_test_only.sh`
Expected: entire suite green (no-op). Investigate any `[vehicle]` diff before proceeding.

- [ ] **Step 7: Format and commit**

```bash
astyle --options=.astylerc src/vehicle.h src/vehicle.cpp
git add -A
git commit -m "refactor: 3D-key relative_parts and parts_at_relative (z 0, no-op)"
```

---

## Task 4: 3D-key `edges`, `get_edge_info`, and `mount_min`/`mount_max`

Widen the remaining mount-keyed caches and the bounding box.

**Files:**
- Modify: `src/vehicle.h:2247` (`edges`), `2249` (`get_edge_info`), `2363-2364` (`mount_min`/`mount_max`)
- Modify: `src/vehicle.cpp` — `refresh()` edge/bounding-box loops (grep `edges\[`, `mount_min`, `mount_max`, `get_edge_info`)

**Interfaces:**
- Produces: `edges` is `std::map<tripoint_rel_ms, vpart_edge_info>`; `get_edge_info( const tripoint_rel_ms & )`; `mount_min`/`mount_max` are `tripoint_rel_ms`.

- [ ] **Step 1: Widen the members**

`src/vehicle.h`:
```cpp
        std::map<tripoint_rel_ms, vpart_edge_info> edges; // NOLINT(cata-serialize)
```
and `mount_min` / `mount_max` (`2363-2364`) from `point_rel_ms` to `tripoint_rel_ms`, and `get_edge_info` (`2249`) parameter to `const tripoint_rel_ms &`.

- [ ] **Step 2: Fix the `refresh()` edge and bounding-box loops**

At the `edges[...]` insertion and the `mount_min`/`mount_max` min/max updates, use the part's full 3D `mount`. The edge computation currently walks `four_adjacent_offsets` (2D); keep that walk 2D by projecting to z of the current part: `tripoint_rel_ms( mount.x() + off.x(), mount.y() + off.y(), mount.z() )`. With all z==0 this is unchanged behavior.

- [ ] **Step 3: Fix `get_edge_info` callers (compiler-driven)**

Wrap 2D args as `tripoint_rel_ms( pt.x(), pt.y(), 0 )`; pass `part.mount` where a mount is already in hand.

- [ ] **Step 4: Build clean**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: builds clean.

- [ ] **Step 5: Run the full suite**

Run: `build-scripts/gha_test_only.sh`
Expected: entire suite green.

- [ ] **Step 6: Format and commit**

```bash
astyle --options=.astylerc src/vehicle.h src/vehicle.cpp
git add -A
git commit -m "refactor: 3D-key edges/get_edge_info and mount bbox (z 0, no-op)"
```

---

## Task 5: 3D-key the `precalc_mounts` memo

Final cache: make the `mount_to_precalc` memoization key 3D so parts at the same (x,y) but different z will (in later milestones) get independently projected. Still a no-op here because z==0. **Still no `precalc.z` seeding** — that is milestone 3.

**Files:**
- Modify: `src/vehicle.cpp:3650-3661` (`precalc_mounts`)

**Interfaces:**
- Produces: `mount_to_precalc` keyed by `tripoint_rel_ms`; `precalc_mounts` calls `coord_translate` with `p.mount.xy()` and leaves `precalc.z` untouched.

- [ ] **Step 1: 3D-key the memo**

Revert the `.xy()` on the memo key introduced in Task 2, keying by the full 3D mount, while still passing the 2D projection to `coord_translate` (which only computes x/y):

```cpp
    std::unordered_map<tripoint_rel_ms, tripoint_rel_ms> mount_to_precalc;
    for( vehicle_part &p : parts ) {
        if( p.removed ) {
            continue;
        }
        auto q = mount_to_precalc.find( p.mount );
        if( q == mount_to_precalc.end() ) {
            coord_translate( tdir, pivot, p.mount.xy(), p.precalc[idir] );
            mount_to_precalc.insert( { p.mount, p.precalc[idir] } );
        } else {
            p.precalc[idir] = q->second;
        }
    }
```

(Requires `std::hash<tripoint_rel_ms>` — the typed tripoints already provide a hash, as tripoints are used as unordered_map keys elsewhere; if the build complains, confirm with `grep -rn "unordered_map<tripoint" src/ | head`.)

- [ ] **Step 2: Build clean**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: builds clean.

- [ ] **Step 3: Run the full suite**

Run: `build-scripts/gha_test_only.sh`
Expected: entire suite green — especially `vehicle_ramp_test` (proves `precalc.z` ramp behavior is untouched).

- [ ] **Step 4: Format and commit**

```bash
astyle --options=.astylerc src/vehicle.cpp
git add -A
git commit -m "refactor: 3D-key precalc_mounts memo (z 0, no-op)"
```

---

## Milestone 1 Definition of Done

- [ ] `vehicle_part::mount` is `tripoint_rel_ms`; `relative_parts`, `edges`, `mount_min`/`mount_max`, and the `precalc_mounts` memo are 3D-keyed.
- [ ] `mount_dz` serializes omit-when-zero and defaults to 0 on read; the three `[savegame]` mount tests pass; `export_vehicle_test` passes unchanged.
- [ ] The **full** suite via `build-scripts/gha_test_only.sh` is green — the no-op gate.
- [ ] `precalc.z` ramp behavior is untouched (`vehicle_ramp_test` green); no `mount.z != 0` exists anywhere except transiently inside the Task 2 z-round-trip test.

---

## Self-Review Notes

- **Spec coverage (§0, §1):** serialization migration → Task 2 (steps 1, 4, 7); 3D field → Task 2; 3D-keyed caches → Tasks 3–5; save round-trip regression test → Tasks 1–2. The spec's "precalc.z seeding" is deliberately deferred (documented under "Explicitly OUT of milestone 1") with rationale — it is not a gap but a re-sequencing to keep milestone 1 a provable no-op; it lands in milestone 3 where `mount.z != 0` makes it testable.
- **Split/merge, connectivity, rendering z-select, JSON authoring:** correctly belong to milestones 2–3 per the spec, not here.
- **Type consistency:** `mount` → `tripoint_rel_ms` everywhere after Task 2; 2D access is always `mount.xy()`; 2D→3D construction is always `tripoint_rel_ms( pt.x(), pt.y(), 0 )`. `parts_at_relative`/`get_edge_info` take `tripoint_rel_ms` after Tasks 3–4.
