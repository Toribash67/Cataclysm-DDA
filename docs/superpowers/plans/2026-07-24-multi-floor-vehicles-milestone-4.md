# Multi-Floor Vehicles — Milestone 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the two-floor bus into a *stationary livable deck* — climb between decks through a vertical connector, and fall when the floor under you is destroyed — reaching the spec's committed shippable checkpoint (design §4, milestone 4).

**Architecture:** M1–M3 already made `mount` 3D, added the `VERTICAL_CONNECTOR` flag with a connector-gated `can_mount`/split/merge, and made rendering + the floor cache per-deck. So M4 adds exactly two pieces of *new* behavior on top of that foundation: (1) **deck-to-deck traversal** — a testable `vehicle` predicate for "can I climb here?" plus a `game` executor that performs the intra-bubble z-move, wired as an early branch in `game::vertical_move` (which is otherwise terrain-only and would reject the move); (2) **gravity on floor destruction** — a `map` recheck helper invoked from `vehicle::break_off` so a rider whose deck part is destroyed is unboarded and re-run through `gravity_check`. Boarding upper-deck seats already works (design §4: verified reuse) and gets a regression lock, not new code. Rendering already landed in M3 and needs no new code.

**Tech Stack:** C++17, Catch2 (`tests/`), the existing multi-floor test bus (`test_bus_2floor` in `data/json/vehicles/custom_vehicles.json`), JSON vehicle-part content. Build/test through the NAS Docker harness `.nas-build/cdda.sh` (no host toolchain — see Global Constraints).

## Global Constraints

- **Build (curses, fast dev loop):** `.nas-build/cdda.sh build` — compiles `tests/cata_test`, validates all JSON.
- **Single test while iterating:** `.nas-build/cdda.sh test "<case name>"` (Catch2 name or `[tag]`). Fine for one case.
- **Full isolated suite (the milestone gate):** `.nas-build/cdda.sh suite` — never a single `cata_test` call for the whole suite (some `[monster]` tests cross-contaminate in one process; the harness runs `gha_test_only.sh`, `--order lex`, isolated batches).
- **Tiles build (CI mirror, final gate only):** `.nas-build/cdda.sh build-tiles`. M4 adds no rendering code, but the final task must confirm tiles still links because CI builds tiles.
- **Single-floor no-op is sacred:** every shipped vehicle omits `z`, so all their parts sit at `mount.z == 0`. No change in this milestone may alter behavior for a single-deck vehicle. Where a code path is z-gated, the `z == 0` / no-connector case must behave byte-identically to today. Add a single-floor regression assertion whenever you add a two-floor one.
- **Connector-gated rule is single-sourced:** the "two stacked tiles are connected only through a `VERTICAL_CONNECTOR`" rule already lives in `vehicle::connected_neighbours()` / `vehicle::has_vertical_connector_at()` (`src/vehicle.cpp:1258`, `1271`). Reuse those. Do **not** re-derive adjacency with a fresh hand-rolled z-walk — that is exactly the duplication M3's review removed.
- **Test file & tag:** all new tests go in `tests/vehicle_multifloor_test.cpp`, tag `"[vehicle][multifloor]"`.
- **Formatting before any commit:** C++ with astyle 3.1 (`--options=.astylerc`); JSON with `make style-json`. astyle 3.1 runs locally via `docker run --rm -v <repo>:/src -w /src ubuntu:24.04 bash -c "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n <files> && chown 3000:3003 <files>"` (ubuntu-24.04 apt astyle == 3.1, exactly what CI uses).
- **Branch:** `explore/multi-floor-vehicles-m4`. Merge via a PR with a merge commit titled `Multi-floor vehicles — Milestone 4: livable deck (#N)`; delete the branch after.

## The test bus (reference — do not re-derive coordinates)

`test_bus_2floor` (`data/json/vehicles/custom_vehicles.json`), spawned at `tripoint_bub_ms( 60, 60, 0 )`, `0_degrees`:

| mount (x,y,z) | parts | role |
|---|---|---|
| `(0,0,0)` | `hdframe`, `ladder_internal` | ground tile carrying the sole `VERTICAL_CONNECTOR`; `ladder_internal` is `BOARDABLE` |
| `(1,0,0)` | `hdframe`, `controls`, `engine_electric` | ground drive tile |
| `(0,1,0)` | `hdframe`, `seat` | ground seat |
| `(0,0,1)` | `hdframe`, `deck_floor` | **upper landing directly above the connector**; `deck_floor` is `ROOF`+`BOARDABLE` |
| `(0,1,1)` | `hdframe`, `deck_floor`, `seat` | upper seat |
| `(1,0,1)` | `hdframe`, `deck_floor`, `cargo_space` | upper cargo |
| `(1,1,1)` | `hdframe`, `deck_floor`, `seat` | upper seat |

Key facts used throughout: the connector sits at `(0,0,0)`; the tile you climb **up** to is `(0,0,1)` (has a `BOARDABLE` `deck_floor`); the tile you climb **down** to is `(0,0,0)` (has a `BOARDABLE` `ladder_internal`). Frames (`hdframe`/`frame`) are **not** `BOARDABLE`; `deck_floor` and `ladder_internal` are.

---

## File Structure

- `src/vehicle.h` / `src/vehicle.cpp` — add `bool vehicle::allows_deck_traversal( const tripoint_rel_ms &from_mount, int dz ) const;` (Task 1). Add the gravity hook call inside `vehicle::break_off` (Task 3).
- `src/game.h` / `src/game.cpp` — add `bool game::try_vehicle_deck_move( int movez );` and call it as an early branch in `game::vertical_move` (Task 2).
- `src/map.h` / `src/map.cpp` — add `void map::vehicle_floor_removed_recheck( const tripoint_bub_ms &p );` (Task 3).
- `tests/vehicle_multifloor_test.cpp` — new test cases for Tasks 1–4 (append; existing cases stay).

Each task ends with an independently testable deliverable and its own commit.

---

## Task 1: `vehicle::allows_deck_traversal` — the "can I climb here?" predicate

Pure, side-effect-free geometry over the vehicle's parts. This is the unit-testable core of traversal; Task 2 consumes it.

**Files:**
- Modify: `src/vehicle.h` (declaration, near `has_vertical_connector_at` at `src/vehicle.h:831`)
- Modify: `src/vehicle.cpp` (definition, next to `connected_neighbours` at `src/vehicle.cpp:1271`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `vehicle::has_vertical_connector_at( const tripoint_rel_ms & ) const` (`src/vehicle.cpp:1258`); `vehicle::part_with_feature( const tripoint_rel_ms &, vpart_bitflags, bool unbroken, bool include_fake = false ) const` (`src/vehicle.h:1288`); `VPFLAG_BOARDABLE` (`src/veh_type.h`); `tripoint_rel_ms::below` / `::above`.
- Produces: `bool vehicle::allows_deck_traversal( const tripoint_rel_ms &from_mount, int dz ) const` — true iff `dz` is ±1, the vertical edge is connector-gated (up needs a connector on `from_mount`; down needs one on the tile below, mirroring `connected_neighbours`), **and** the destination mount `from_mount + (0,0,dz)` has a `BOARDABLE` part to stand on. Task 2 relies on this exact name/signature.

- [ ] **Step 1: Write the failing tests**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "allows_deck_traversal_up_from_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Standing on the connector at (0,0,0), climbing up to the boardable deck floor at (0,0,1).
    CHECK( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 1 ) );
    // From the upper landing (0,0,1), climbing back down to (0,0,0): connector is on the tile below.
    CHECK( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 1 ), -1 ) );
}

TEST_CASE( "allows_deck_traversal_rejects_non_connector_and_bad_dz", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // (0,1,0) is a plain seat tile with no connector: no vertical travel either way.
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 1, 0 ), 1 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 1, 1 ), -1 ) );
    // dz other than +/-1 is never a deck move (defends the early branch in vertical_move).
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 2 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 0 ) );
}

TEST_CASE( "allows_deck_traversal_requires_floor_at_destination", "[vehicle][multifloor]" )
{
    // A lone connector with nothing built above must not let you climb into empty air.
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Extend the car by a ground tile carrying a connector, but build nothing on z=1 above it.
    const tripoint_rel_ms ground( 2, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, ground, vpart_ladder_internal ) >= 0 );
    // Connector present, but (2,-2,1) has no boardable part -> no traversal.
    CHECK_FALSE( veh->allows_deck_traversal( ground, 1 ) );
}
```

(`vehicle_prototype_test_bus_2floor`, `vehicle_prototype_car`, `vpart_frame`, `vpart_ladder_internal` are already declared at the top of this test file from M2/M3.)

- [ ] **Step 2: Run tests to verify they fail (compile error — method undeclared)**

Run: `.nas-build/cdda.sh test "allows_deck_traversal_up_from_connector"`
Expected: FAIL — build error, `allows_deck_traversal` is not a member of `vehicle`.

- [ ] **Step 3: Declare the method**

In `src/vehicle.h`, right after the `has_vertical_connector_at` declaration (`src/vehicle.h:831`):

```cpp
        // True iff a character standing on `from_mount` may climb by `dz` (+/-1 only)
        // to the tile directly above/below: the vertical edge must be gated by a
        // VERTICAL_CONNECTOR (up: connector on `from_mount`; down: connector on the
        // tile below -- same rule as connected_neighbours()), and the destination
        // mount must carry a BOARDABLE part to stand on. Pure; no state change.
        bool allows_deck_traversal( const tripoint_rel_ms &from_mount, int dz ) const;
```

- [ ] **Step 4: Define the method**

In `src/vehicle.cpp`, immediately after `connected_neighbours` (ends at `src/vehicle.cpp:1289`):

```cpp
bool vehicle::allows_deck_traversal( const tripoint_rel_ms &from_mount, int dz ) const
{
    if( dz != 1 && dz != -1 ) {
        return false;
    }
    // Connector gates the vertical edge, same asymmetry as connected_neighbours():
    // climbing up needs a connector on `from_mount`; climbing down needs one on
    // the lower tile (from_mount below).
    const bool gated = dz == 1
                       ? has_vertical_connector_at( from_mount )
                       : has_vertical_connector_at( from_mount + tripoint_rel_ms::below );
    if( !gated ) {
        return false;
    }
    // The destination must be a floor you can stand on.
    const tripoint_rel_ms dest = from_mount + tripoint_rel_ms( 0, 0, dz );
    return part_with_feature( dest, VPFLAG_BOARDABLE, false ) >= 0;
}
```

- [ ] **Step 5: Run the three tests to verify they pass**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS — all `[multifloor]` cases green (the three new ones plus all M1–M3 ones).

- [ ] **Step 6: Format and commit**

```bash
# astyle the two changed C++ files (see Global Constraints for the docker one-liner), then:
git add src/vehicle.h src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): add vehicle::allows_deck_traversal connector predicate"
```

---

## Task 2: `game::try_vehicle_deck_move` + wire into `game::vertical_move`

`game::vertical_move` (`src/game.cpp:12291`) is built entirely around **terrain** (`TFLAG_GOES_UP/DOWN`, stair-finding, `find_or_make_stairs`) and would reject a climb that only a vehicle connector enables. Add a small executor that, when the avatar is standing on a vehicle tile whose connector permits the move, performs the intra-bubble z-shift itself and returns `true`; wire it as the first thing `vertical_move` tries for a `±1` non-forced move so the terrain path is bypassed. Both decks are already loaded in the reality bubble, so this is *not* a stair teleport — it is `vertical_shift` (re-centers the bubble z, keeps x/y) + re-board at the new z.

**Files:**
- Modify: `src/game.h` (declaration, near the other `vertical_*` members — search `void vertical_move(`)
- Modify: `src/game.cpp` (definition; and the early-branch call inside `game::vertical_move` at `src/game.cpp:12291`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `vehicle::allows_deck_traversal` (Task 1); `map::veh_at( const tripoint_bub_ms & )` → `optional_vpart_position`; the standing part's **3D** mount via `vp->vehicle().part( vp->part_index() ).mount` (NOTE: `mount_pos()` is 2D `point_rel_ms` and drops z — do not use it here); `game::vertical_shift( int z_after )` (`src/game.cpp:12971`); `game::update_map( int x, int y, bool z_level_changed )`; `map::board_vehicle`/`map::unboard_vehicle` (`src/map.cpp:1385`,`1442`); `avatar u`, `u.pos_bub( map & )`, `u.setpos`, `u.mod_moves`, `u.in_vehicle`.
- Produces: `bool game::try_vehicle_deck_move( int movez )` — returns `false` (did nothing; caller continues normal `vertical_move`) when no vehicle deck traversal applies; performs the deck move and returns `true` when it does. Task 2's integration test calls it directly.

- [ ] **Step 1: Write the failing integration tests**

Append to `tests/vehicle_multifloor_test.cpp`. This drives the executor directly (no UI), asserting the avatar's z and boarding state.

```cpp
#include "avatar.h"
#include "game.h"
#include "player_helpers.h"

TEST_CASE( "try_vehicle_deck_move_climbs_between_decks", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    avatar &u = get_avatar();
    // Stand the avatar on the ground connector tile (0,0,0) of the bus and board.
    const tripoint_bub_ms connector_pos = veh->bub_part_pos( here,
                                          veh->part( veh->part_with_feature(
                                                  tripoint_rel_ms( 0, 0, 0 ), "VERTICAL_CONNECTOR", false ) ) );
    u.setpos( here, connector_pos );
    here.board_vehicle( connector_pos, &u );
    REQUIRE( u.in_vehicle );
    REQUIRE( u.posz() == 0 );

    // Climb up: executor handles it and reports true.
    CHECK( g->try_vehicle_deck_move( 1 ) );
    CHECK( u.posz() == 1 );
    CHECK( u.pos_bub().xy() == connector_pos.xy() );
    CHECK( u.in_vehicle );

    // Climb back down.
    CHECK( g->try_vehicle_deck_move( -1 ) );
    CHECK( u.posz() == 0 );
    CHECK( u.in_vehicle );
}

TEST_CASE( "try_vehicle_deck_move_declines_without_connector", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    avatar &u = get_avatar();
    // Ground seat tile (0,1,0): boardable but no connector -> executor must decline (return false)
    // so the normal terrain-based vertical_move logic runs instead.
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here,
                                     veh->part( veh->part_with_feature(
                                             tripoint_rel_ms( 0, 1, 0 ), "SEAT", false ) ) );
    u.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &u );
    REQUIRE( u.posz() == 0 );

    CHECK_FALSE( g->try_vehicle_deck_move( 1 ) );
    CHECK( u.posz() == 0 );
}

TEST_CASE( "try_vehicle_deck_move_declines_off_vehicle", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    // No vehicle at the avatar's tile at all: executor declines, no crash.
    CHECK_FALSE( g->try_vehicle_deck_move( 1 ) );
    CHECK_FALSE( g->try_vehicle_deck_move( -1 ) );
}
```

- [ ] **Step 2: Run tests to verify they fail (compile error)**

Run: `.nas-build/cdda.sh test "try_vehicle_deck_move_climbs_between_decks"`
Expected: FAIL — `try_vehicle_deck_move` is not a member of `game`.

- [ ] **Step 3: Declare the method**

In `src/game.h`, next to the `vertical_move` declaration (search for `void vertical_move(`):

```cpp
        /**
         * If the avatar is standing on a vehicle tile whose VERTICAL_CONNECTOR permits
         * moving `movez` (+/-1) to the deck above/below, perform that intra-bubble deck
         * change (re-center the reality bubble at the new z, keep x/y, re-board) and
         * return true. Return false -- doing nothing -- when no deck traversal applies,
         * so vertical_move() falls through to its normal terrain-based handling.
         */
        bool try_vehicle_deck_move( int movez );
```

- [ ] **Step 4: Define the method**

Add to `src/game.cpp` (place it just before `game::vertical_move` at `src/game.cpp:12291`):

```cpp
bool game::try_vehicle_deck_move( int movez )
{
    if( movez != 1 && movez != -1 ) {
        return false;
    }
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub( here );
    const optional_vpart_position vp = here.veh_at( pos );
    if( !vp ) {
        return false;
    }
    vehicle &veh = vp->vehicle();
    // The full 3D mount of the part we're standing on. mount_pos() is 2D and would
    // drop the z, collapsing an upper-deck tile onto the ground deck -- use .mount.
    const tripoint_rel_ms from_mount = veh.part( vp->part_index() ).mount;
    if( !veh.allows_deck_traversal( from_mount, movez ) ) {
        return false;
    }

    const int z_after = here.get_abs_sub().z() + movez;
    if( z_after < -OVERMAP_DEPTH || z_after > OVERMAP_HEIGHT ) {
        return false;
    }

    // Re-board on the destination deck: drop the old passenger slot first, shift the
    // reality bubble's active z (keeps x/y; moves the avatar's abs pos to z_after),
    // realign submaps, then board the vehicle tile now under us.
    here.unboard_vehicle( pos );
    const bool z_changed = vertical_shift( z_after );
    const point_rel_sm submap_shift = update_map( u.pos_bub( here ).x(), u.pos_bub( here ).y(),
                                      z_changed );
    static_cast<void>( submap_shift );

    const tripoint_bub_ms dest = u.pos_bub( here );
    if( here.veh_at( dest ) ) {
        here.board_vehicle( dest, &u );
    }

    u.mod_moves( -100 );
    here.invalidate_map_cache( here.get_abs_sub().z() );
    here.build_map_cache( here.get_abs_sub().z() );
    u.gravity_check();
    return true;
}
```

- [ ] **Step 5: Wire it into `vertical_move`**

In `game::vertical_move` (`src/game.cpp:12291`), insert an early branch right after `map &here = get_map();` / `tripoint_bub_ms pos = u.pos_bub( here );` (the block at `src/game.cpp:12308-12309`), before the climbing/terrain logic:

```cpp
    // Multi-floor vehicles: a deck-to-deck climb through a VERTICAL_CONNECTOR is
    // recognized here because vertical_move's terrain checks (GOES_UP/DOWN, stairs)
    // do not see vehicle parts. If this handles the move, we are done.
    if( !force && !peeking && !u.is_underwater() && try_vehicle_deck_move( movez ) ) {
        return;
    }
```

- [ ] **Step 6: Run the three integration tests**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS — the three new cases and every prior `[multifloor]` case.

- [ ] **Step 7: Regression — confirm ordinary z-movement is unaffected**

Run: `.nas-build/cdda.sh test "[vehicle]"` then `.nas-build/cdda.sh test "vehicle_ramp"`
Expected: PASS — `vehicle_ramp_test` (which uses forced `g->vertical_move`) is untouched; the early branch never fires for `force`/`peeking`/underwater or a non-connector tile.

- [ ] **Step 8: Format and commit**

```bash
# astyle src/game.cpp src/game.h, then:
git add src/game.h src/game.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): deck-to-deck traversal via game::try_vehicle_deck_move"
```

---

## Task 3: Gravity check when an upper-floor part is destroyed

Today a rider on the upper deck is held up by two things: for the avatar, `Character::gravity_check` early-returns on `in_vehicle` (`src/character.cpp:11283`); for a monster, `monster::gravity_check` (`src/monster.cpp:398`) relies on `map::try_fall` → `valid_move` seeing the deck's floor-cache entry. When the **floor part itself is destroyed** (combat/`break_off`), nothing re-runs the check, so the occupant silently hovers over open air (design §4, a correctness bug). Add a `map` helper that, given the tile a floor part was just removed from, drops the passenger's boarding status if the vehicle no longer floors that tile and re-runs their gravity check; call it from `vehicle::break_off` after the part is removed.

**Files:**
- Modify: `src/map.h` (declaration, near `unboard_vehicle`)
- Modify: `src/map.cpp` (definition)
- Modify: `src/vehicle.cpp` (call from `vehicle::break_off`, `src/vehicle.cpp:7908`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `get_creature_tracker().creature_at( const tripoint_bub_ms &, bool allow_hallucination = false )`; `map::veh_at`; `vehicle::part_with_feature( const tripoint_rel_ms &, vpart_bitflags, bool ) const`; the standing part's 3D mount via `vp->vehicle().part( vp->part_index() ).mount` (again, not the 2D `mount_pos()`); `map::unboard_vehicle( const tripoint_bub_ms & )`; `Creature::as_character()` / `as_monster()`; `Character::in_vehicle`, `Character::gravity_check( map * )` (`src/character.cpp:11289`); `monster::gravity_check( map * )` (`src/monster.cpp:398`).
- Produces: `void map::vehicle_floor_removed_recheck( const tripoint_bub_ms &p )` — no-op unless a creature stands at `p` and the vehicle no longer provides a `BOARDABLE` part there; then unboards (character) and runs gravity. Called by `vehicle::break_off`.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`. Uses a monster (no `in_vehicle`-guard subtleties, and `try_fall` moves it directly):

```cpp
#include "monster.h"
#include "mtype.h"

static const mtype_id mon_zombie( "mon_zombie" );

TEST_CASE( "destroying_upper_floor_drops_the_rider", "[vehicle][multifloor]" )
{
    clear_map();
    clear_creatures();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // The upper cargo/floor tile (1,0,1) sits over open air terrain at z=1.
    const int floor_idx = veh->part_with_feature( tripoint_rel_ms( 1, 0, 1 ), "BOARDABLE", false );
    REQUIRE( floor_idx >= 0 );
    const tripoint_bub_ms floor_pos = veh->bub_part_pos( here, veh->part( floor_idx ) );
    REQUIRE( floor_pos.z() == 1 );
    REQUIRE( here.is_open_air( floor_pos ) ); // terrain beneath the deck is open air

    // Put a zombie on that upper-deck floor. With the floor intact it must NOT fall.
    monster &zed = spawn_test_monster( "mon_zombie", floor_pos );
    zed.gravity_check( &here );
    REQUIRE( zed.pos_bub( here ) == floor_pos );

    // Destroy the floor part outright, then let break_off run its recheck.
    veh->damage_direct( here, veh->part( floor_idx ), 100000, damage_bash );

    // The zombie no longer has a floor and must have fallen off z=1.
    monster *still = get_creature_tracker().creature_at<monster>( floor_pos );
    CHECK( still == nullptr );
}
```

Notes for the implementer:
- `spawn_test_monster` is in `tests/map_helpers.h`; `damage_bash` is the damage type id used across the vehicle tests (include what `vehicle_ramp_test.cpp` includes — `"damage.h"` / `damage_type_id`; use `damage_bash` or `STATIC( damage_type_id damage_bash( "bash" ) )` following the existing pattern in this file).
- `damage_direct` with a very large value forces `is_broken()` → `break_off`. If a single `damage_direct` call does not tear the frame off (the `break_off` scatter roll at `src/vehicle.cpp:7913` is random), loop the damage a few times, or destroy via the deterministic path the implementer confirms; the *assertion* (zombie fell) is what matters, not the exact API to demolish the part.

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh test "destroying_upper_floor_drops_the_rider"`
Expected: FAIL — after destruction the zombie is still at `floor_pos` (no recheck runs today).

- [ ] **Step 3: Declare the map helper**

In `src/map.h`, near the `unboard_vehicle` declarations:

```cpp
        /**
         * A vehicle floor part was just removed from tile `p`. If a creature is standing
         * there and the vehicle no longer provides a boardable floor at that tile, drop
         * the creature's boarding status (characters) and re-run its gravity check so it
         * falls instead of hovering over open air. No-op when the tile is still floored.
         */
        void vehicle_floor_removed_recheck( const tripoint_bub_ms &p );
```

- [ ] **Step 4: Define the map helper**

Add to `src/map.cpp` (near the other vehicle/boarding helpers):

```cpp
void map::vehicle_floor_removed_recheck( const tripoint_bub_ms &p )
{
    Creature *c = get_creature_tracker().creature_at( p, true );
    if( c == nullptr ) {
        return;
    }
    // Still floored by the vehicle at this tile? then supported, nothing to do.
    // Use the part's full 3D .mount (mount_pos() is 2D and would look at the wrong deck).
    const optional_vpart_position vp = veh_at( p );
    if( vp ) {
        vehicle &veh = vp->vehicle();
        if( veh.part_with_feature( veh.part( vp->part_index() ).mount,
                                   VPFLAG_BOARDABLE, false ) >= 0 ) {
            return;
        }
    }
    if( Character *ch = c->as_character() ) {
        if( ch->in_vehicle ) {
            unboard_vehicle( p );
        }
        ch->gravity_check( this );
    } else if( monster *m = c->as_monster() ) {
        m->gravity_check( this );
    }
}
```

- [ ] **Step 5: Call it from `break_off`**

In `vehicle::break_off` (`src/vehicle.cpp:7908`), `pos` is `bub_part_pos( here, vp )` — the exact tile of the destroyed part. Both destruction branches (structural and non-structural) end by removing the part. Add the recheck at the very end of `break_off`, just before its `return`, so it runs whichever branch fired:

```cpp
    // If we just tore out a floor from under someone, make them fall (M4).
    here.vehicle_floor_removed_recheck( pos );
    return dmg;
```

(Use the function's existing final `return` value / variable — do not change what `break_off` returns; only insert the recheck call immediately before it. If the two branches have separate returns, add the call before each, or hoist to a single tail return.)

- [ ] **Step 6: Run the test to verify it passes**

Run: `.nas-build/cdda.sh test "destroying_upper_floor_drops_the_rider"`
Expected: PASS — zombie is no longer at `floor_pos`.

- [ ] **Step 7: Regression — ground-floor destruction must not drop anyone**

Add and run this single-floor guard (a creature on a ground tile over solid earth stays put when a part there is destroyed, because `gravity_check`/`try_fall` see the terrain floor):

```cpp
TEST_CASE( "destroying_ground_part_does_not_drop_rider", "[vehicle][multifloor]" )
{
    clear_map();
    clear_creatures();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int seat = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ), "BOARDABLE", false );
    REQUIRE( seat >= 0 );
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here, veh->part( seat ) );
    REQUIRE_FALSE( here.is_open_air( seat_pos ) );
    monster &zed = spawn_test_monster( "mon_zombie", seat_pos );
    here.vehicle_floor_removed_recheck( seat_pos );      // direct call: must be a no-op on solid ground
    CHECK( zed.pos_bub( here ) == seat_pos );
}
```

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS — both new cases plus all prior.

- [ ] **Step 8: Format and commit**

```bash
# astyle src/map.cpp src/map.h src/vehicle.cpp, then:
git add src/map.h src/map.cpp src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): fall when an upper-deck floor part is destroyed under you"
```

---

## Task 4: Upper-deck boarding regression lock + milestone gate

Design §4 states boarding an upper-deck seat is a *verified reuse* (`board_vehicle` resolves the part at the character's exact tripoint; the vehicle cache is per-z). No new code — but lock it with a regression test so a future refactor can't silently break it, then run the full milestone gate.

**Files:**
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `map::board_vehicle`, `avatar`/`get_avatar`, `clear_avatar`, the test bus. No production changes.

- [ ] **Step 1: Write the regression test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "avatar_boards_upper_deck_seat", "[vehicle][multifloor]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Upper seat at (0,1,1). Board directly at its exact tripoint (z=1).
    const int seat = veh->part_with_feature( tripoint_rel_ms( 0, 1, 1 ), "SEAT", false );
    REQUIRE( seat >= 0 );
    const tripoint_bub_ms seat_pos = veh->bub_part_pos( here, veh->part( seat ) );
    REQUIRE( seat_pos.z() == 1 );

    avatar &u = get_avatar();
    u.setpos( here, seat_pos );
    here.board_vehicle( seat_pos, &u );
    CHECK( u.in_vehicle );
    CHECK( u.pos_bub().z() == 1 );
    // The boarded part the map resolves for us is the upper-deck seat, not a ground part.
    const optional_vpart_position vp = here.veh_at( u.pos_bub() );
    REQUIRE( vp );
    CHECK( vp->part_with_feature( "SEAT", false ).has_value() );
}
```

- [ ] **Step 2: Run it (should pass immediately — reuse, no new code)**

Run: `.nas-build/cdda.sh test "avatar_boards_upper_deck_seat"`
Expected: PASS. If it fails, that is a real finding — boarding across z is *not* the free reuse the spec claims; stop and investigate before proceeding.

- [ ] **Step 3: Commit the regression lock**

```bash
git add tests/vehicle_multifloor_test.cpp
git commit -m "test(veh): lock upper-deck seat boarding regression"
```

- [ ] **Step 4: Full isolated suite gate (curses)**

Run: `.nas-build/cdda.sh suite`
Expected: PASS — the entire suite green, both isolated batches. This is the milestone's correctness gate (loads/validates all JSON + all C++ tests).

- [ ] **Step 5: Tiles build gate (CI mirror)**

Run: `.nas-build/cdda.sh build-tiles`
Expected: links cleanly. M4 adds no rendering code, but CI builds tiles, so confirm the tiles flavor still compiles/links before opening the PR.

- [ ] **Step 6: Final formatting sweep**

Ensure every touched C++ file is astyle-3.1 clean and `make style-json` is a no-op (no JSON changed in M4). Fix any drift and amend the relevant commit.

---

## Self-Review

**Spec coverage (design §4 + milestone-4 line):**
- "rendering (upper deck solid & drawn when you're on it)" — **already delivered in M3** (per-deck `part_displayed_at`/`get_display_of_tile` + floor-cache); no new work. Locked indirectly by the existing `part_displayed_at_resolves_per_deck` test. ✓ (documented, not re-implemented)
- "deck-to-deck traversal" — Task 1 (predicate) + Task 2 (executor wired into `vertical_move`). ✓
- "board upper-deck seats" — Task 4 regression lock; verified reuse per spec. ✓
- "gravity-check on floor destruction" — Task 3. ✓
- Single-floor no-op preserved — regression assertions in Tasks 2 (`vehicle_ramp` untouched, non-connector declines), 3 (`destroying_ground_part_does_not_drop_rider`), and the standing M1–M3 single-floor cases; full suite gate in Task 4. ✓
- Connector rule single-sourced — Task 1 reuses `has_vertical_connector_at`; no new adjacency walk. ✓

**Deferred to M5 (out of scope here, consistent with the spec):** driving physics, collision across z, the `mount.z=1 × ramp` composition cross-term test, top-heavy stability, `veh_interact` install-on-floor UX. Also still carried from M3's deferral list: 3D `loot_zones`/`labels` keys, split z-base normalization, open-top-deck shelter semantics. None are required for the stationary livable-deck checkpoint.

**Placeholder scan:** every code step contains real code and a concrete run command with expected output. The one intentionally open point — the exact API to fully demolish a part in Task 3 Step 1 — is flagged with the fallback (loop the damage) because the `break_off` scatter roll is random; the *assertion* is deterministic.

**Type consistency:** `allows_deck_traversal( const tripoint_rel_ms &, int )` is declared in Task 1 and consumed verbatim in Task 2. `try_vehicle_deck_move( int )` declared/defined in Task 2. `vehicle_floor_removed_recheck( const tripoint_bub_ms & )` declared in Task 3 Step 3, defined Step 4, called Step 5. `part_with_feature(tripoint_rel_ms, vpart_bitflags/std::string, bool)` overloads all exist in `src/vehicle.h:1269/1288`. The standing part's 3D mount is read as `vp->vehicle().part( vp->part_index() ).mount` (public `tripoint_rel_ms`, `src/vehicle.h:456`); `vpart_position::mount_pos()` is 2D `point_rel_ms` (`src/vpart_position.h:112`) and is deliberately **not** used for the z-sensitive lookups. Consistent throughout.
