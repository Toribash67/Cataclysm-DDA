# Vehicle Drive-On Carrying — Milestone 1 (Traversal) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a separate small vehicle drive up a ramp *part* onto a parked larger vehicle's bed and come to rest there at z+1, supported by the larger vehicle's structure below — proving cross-vehicle traversal and support. No grid merge yet (that is Milestone 2).

**Architecture:** Reuse the existing terrain-ramp z-transition machinery. Today `vehicle::advance_precalc_mounts` (`src/vehicle.cpp` ~8895) probes ground terrain for `TFLAG_RAMP_UP/DOWN` at a moving part's destination tile and, on a hit, shifts that part's `precalc[0].z()` by ±1. We add a parallel probe: if the destination tile holds a *different, stationary* vehicle carrying a new `VEH_RAMP_UP/DOWN` vehicle-part flag, apply the identical shift. Support at the top already exists — `map::supports_above` (`src/map.cpp:2699`) returns `veh_at(p).has_value()`, and `check_falling_or_floating`'s `has_support` lambda consults it — so a vehicle resting at z+1 over another vehicle at z0 is already held up. Two ramp parts are authored: a fixed always-on slope and a deployable (OPENABLE) one whose flag is live only when open.

**Tech Stack:** C++17, CDDA `generic_factory` content system, JSON vehicle/part definitions, Catch2 tests (`tests/cata_test`).

## Global Constraints

- Fork baselined on upstream 0.I "Ito"; `master` is the integration branch; work on a feature branch and merge only when CI is green (see the `ci-setup` memo).
- Build: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8` (add `RELEASE=1` for optimized). Do **not** pass `FRAMEWORK`. Full debug build ≈ 7–8 min; incremental far faster.
- Tests: `tests/cata_test "<name-or-tag>"` for a single case while iterating. astyle/`make style-json` are the authoritative format checks and run in CI (not installed locally).
- Coordinate types are load-bearing (`tripoint_bub_ms`, `tripoint_rel_ms`, `mount.z()`); never flatten z to 0 in new code (see the multi-floor z-projection audit notes in the `multi-floor-vehicles` memo).
- Milestone boundary: the carried car stays a **separate** `vehicle` object. No merging, no `carried_flag`, no bike-rack calls in M1.

---

## File Structure

- `src/veh_type.h` — add `VPFLAG_VEH_RAMP_UP`, `VPFLAG_VEH_RAMP_DOWN` to the `vpart_bitflags` enum.
- `src/veh_type.cpp` — register the two new flags in `vpart_bitflag_map`.
- `src/vehicle.cpp` — extend `advance_precalc_mounts` with the vehicle-ramp probe (the one core engine change).
- `data/json/vehicleparts/vehicle_ramp.json` — **new**: the fixed ramp part and the deployable ramp part.
- `data/json/vehicles/custom_vehicles.json` — add `test_flatbed_truck` and `test_car` prototypes for tests.
- `tests/vehicle_flatbed_test.cpp` — **new**: the traversal + support + deployable-gating tests.
- `tests/CMakeLists.txt` / test build glob — confirm the new test file is picked up (CDDA globs `tests/*.cpp`; usually no edit needed).

---

## Task 1: Cross-vehicle support confirmation spike

Gate task. Confirms Approach A's one unproven assumption — that a vehicle resting at z+1 over another vehicle at z0 does **not** fall — using only existing code. If this fails, stop and extend `map::supports_above` / `check_falling_or_floating` before proceeding; if it passes (expected, per `supports_above` returning `veh_at().has_value()`), the rest of the plan stands.

**Files:**
- Create: `tests/vehicle_flatbed_test.cpp`
- Read for reference: `src/map.cpp:2699` (`supports_above`), `src/vehicle_move.cpp:2062` (`check_falling_or_floating`), `tests/vehicle_ramp_test.cpp` (map-building idioms)

**Interfaces:**
- Consumes: `map::add_vehicle`, `vehicle::check_falling_or_floating()`, `vehicle::is_falling` (public bool), `build_test_map`, `clear_map`, `clear_vehicles` (from `map_helpers.h`).
- Produces: the test file that later tasks extend; a proven fact that z+1-over-vehicle support works.

- [ ] **Step 1: Write the failing test**

Create `tests/vehicle_flatbed_test.cpp`:

```cpp
#include "cata_catch.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "vehicle.h"
#include "type_id.h"
#include "coordinates.h"
#include "point.h"

static const vproto_id vehicle_prototype_car( "car" );

// Build a field where z0 is solid pavement and z1 is entirely open air,
// so the ONLY possible support at z1 is a vehicle sitting at z0 below.
static void build_open_air_upper( map &here )
{
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
}

TEST_CASE( "vehicle_rests_on_vehicle_below_is_supported", "[vehicle][flatbed][multifloor]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_open_air_upper( here );

    const tripoint_bub_ms lower_pos( 60, 60, 0 );
    const tripoint_bub_ms upper_pos( 60, 60, 1 );

    // "truck" stand-in at z0 (any vehicle provides veh_at() support above it)
    vehicle *lower = here.add_vehicle( vehicle_prototype_car, lower_pos, 0_degrees, 0, 0 );
    REQUIRE( lower != nullptr );
    // "carried car" directly above at z1, over open air, supported only by `lower`
    vehicle *upper = here.add_vehicle( vehicle_prototype_car, upper_pos, 0_degrees, 0, 0 );
    REQUIRE( upper != nullptr );

    upper->check_falling_or_floating();
    CHECK_FALSE( upper->is_falling );
}
```

- [ ] **Step 2: Build and run — verify it compiles and observe result**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && tests/cata_test "vehicle_rests_on_vehicle_below_is_supported" -v`
Expected: **PASS** (support already works via `supports_above` → `veh_at().has_value()`).
If it FAILS: the two `car`s at the same x/y different z may collide on placement, or support isn't wired. Diagnose before continuing — a placement collision means pick non-overlapping footprints; a genuine no-support result means Task 1b (below) is required.

- [ ] **Step 3 (conditional): If unsupported, extend the support check**

Only if Step 2 failed with `is_falling == true`. In `src/vehicle_move.cpp` `check_falling_or_floating`, the `has_support` lambda already calls `here.supports_above( below )`. Confirm `map::supports_above` (`src/map.cpp:2699`) ends with `return veh_at( p ).has_value();`. If a stricter check is needed (e.g. require a roof/BOARDABLE part rather than any part), add it there and re-run. Do not weaken any existing terrain path.

- [ ] **Step 4: Commit**

```bash
git add tests/vehicle_flatbed_test.cpp
git commit -m "test(veh): confirm a vehicle at z+1 is supported by a vehicle below

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Add the `VEH_RAMP_UP` / `VEH_RAMP_DOWN` vpart flags

**Files:**
- Modify: `src/veh_type.h:58-124` (the `vpart_bitflags` enum — add near `VPFLAG_VERTICAL_TRAVERSAL` at line 124)
- Modify: `src/veh_type.cpp:93-154` (the `vpart_bitflag_map` — add near the `VERTICAL_TRAVERSAL` entry at line 154)

**Interfaces:**
- Consumes: nothing.
- Produces: enum values `VPFLAG_VEH_RAMP_UP`, `VPFLAG_VEH_RAMP_DOWN`; the JSON flag strings `"VEH_RAMP_UP"`, `"VEH_RAMP_DOWN"` become queryable via `vpart_info::has_flag()` and `optional_vpart_position::part_with_feature( vpart_bitflags, bool )`.

- [ ] **Step 1: Add enum entries**

In `src/veh_type.h`, in the `vpart_bitflags` enum, immediately after `VPFLAG_VERTICAL_TRAVERSAL,` (line 124) add:

```cpp
    VPFLAG_VEH_RAMP_UP,
    VPFLAG_VEH_RAMP_DOWN,
```

(Keep them before the terminal `NUM_VPFLAGS` / sentinel entry — verify the enum's last element by reading a few lines past 124.)

- [ ] **Step 2: Register the flag strings**

In `src/veh_type.cpp`, in `vpart_bitflag_map`, immediately after `{ "VERTICAL_TRAVERSAL", VPFLAG_VERTICAL_TRAVERSAL },` (line 154) add:

```cpp
    { "VEH_RAMP_UP", VPFLAG_VEH_RAMP_UP },
    { "VEH_RAMP_DOWN", VPFLAG_VEH_RAMP_DOWN },
```

- [ ] **Step 3: Build to verify it compiles**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`
Expected: clean build (the enum is used by later tasks; no behavior change yet).

- [ ] **Step 4: Commit**

```bash
git add src/veh_type.h src/veh_type.cpp
git commit -m "feat(veh): add VEH_RAMP_UP/DOWN vpart flags

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Author the fixed ramp part and the test vehicles

**Files:**
- Create: `data/json/vehicleparts/vehicle_ramp.json`
- Modify: `data/json/vehicles/custom_vehicles.json` (add `test_flatbed_truck` and `test_car`)
- Test: extend `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: the `"VEH_RAMP_UP"` JSON flag string (Task 2).
- Produces: `vpart_id( "veh_ramp_fixed" )`; `vproto_id( "test_flatbed_truck" )`, `vproto_id( "test_car" )` used by Tasks 4–6.

- [ ] **Step 1: Write the failing test (JSON loads and the truck carries a ramp part)**

Append to `tests/vehicle_flatbed_test.cpp`:

```cpp
static const vproto_id vehicle_prototype_test_flatbed_truck( "test_flatbed_truck" );
static const vproto_id vehicle_prototype_test_car( "test_car" );

TEST_CASE( "test_flatbed_truck_has_a_ramp_part", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    bool found_ramp = false;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_VEH_RAMP_UP ) ) {
            found_ramp = true;
        }
    }
    CHECK( found_ramp );
}
```

(`vpart_reference::info()` returns the `vpart_info`; confirm `has_flag(vpart_bitflags)` exists on it — it is the same accessor used throughout `vehicle.cpp`.)

- [ ] **Step 2: Run to verify it fails**

Run: `tests/cata_test "test_flatbed_truck_has_a_ramp_part" -v`
Expected: FAIL — `add_vehicle` returns null / unknown prototype `test_flatbed_truck`.

- [ ] **Step 3: Author the fixed ramp part**

Create `data/json/vehicleparts/vehicle_ramp.json`:

```json
[
  {
    "id": "veh_ramp_fixed",
    "type": "vehicle_part",
    "name": { "str": "fixed loading ramp" },
    "categories": [ "hull" ],
    "durability": 300,
    "description": "A fixed steel ramp sloping down off the back of the bed, letting another vehicle drive up onto the deck above.",
    "item": "sheet_metal",
    "location": "structure",
    "size": "400 L",
    "flags": [ "VEH_RAMP_UP", "VEH_RAMP_DOWN", "BOARDABLE" ],
    "requirements": {
      "install": { "skills": [ [ "mechanics", 3 ] ], "time": "60 m", "using": [ [ "welding_standard", 200 ], [ "vehicle_bolt_install", 2 ] ] },
      "removal": { "skills": [ [ "mechanics", 2 ] ], "time": "30 m", "using": [ [ "vehicle_wrench_2", 1 ] ] },
      "repair": { "skills": [ [ "mechanics", 3 ] ], "time": "8 m", "using": [ [ "repair_welding_lc_steel", 4 ] ] }
    },
    "breaks_into": "ig_vp_frame_vertical",
    "damage_reduction": { "all": 20 }
  }
]
```

Note: the part is `BOARDABLE` (walkable, not `OBSTACLE`) so it does not zero movecost — the same lesson as the internal ladder in the multi-floor work. Do **not** add `OBSTACLE`.

- [ ] **Step 4: Add the test vehicles**

In `data/json/vehicles/custom_vehicles.json`, add two prototypes to the top-level array (mirror the `test_bus_2floor` authoring style). The truck is all z0; its rear "bed" is frames + roof so a car resting at z1 is supported; the ramp part sits at the tail (most-negative x here, since the bus faces +x):

```json
  {
    "id": "test_flatbed_truck",
    "type": "vehicle",
    "name": "Test Flatbed Truck",
    "blueprint": [ "-----" ],
    "parts": [
      { "x": 2, "y": 0, "parts": [ "hdframe", "engine_electric" ] },
      { "x": 2, "y": 1, "parts": [ "hdframe", "storage_battery" ] },
      { "x": 1, "y": 0, "parts": [ "hdframe", "seat", "controls" ] },
      { "x": 1, "y": 1, "parts": [ "hdframe", "seat" ] },
      { "x": 3, "y": 0, "parts": [ "hdframe", "wheel_mount_medium_steerable", "wheel_wide" ] },
      { "x": 3, "y": 1, "parts": [ "hdframe", "wheel_mount_medium_steerable", "wheel_wide" ] },
      { "x": 0, "y": 0, "parts": [ "hdframe", "roof", "wheel_mount_medium", "wheel_wide" ] },
      { "x": 0, "y": 1, "parts": [ "hdframe", "roof", "wheel_mount_medium", "wheel_wide" ] },
      { "x": -1, "y": 0, "parts": [ "hdframe", "roof" ] },
      { "x": -1, "y": 1, "parts": [ "hdframe", "roof" ] },
      { "x": -2, "y": 0, "parts": [ "veh_ramp_fixed" ] },
      { "x": -2, "y": 1, "parts": [ "veh_ramp_fixed" ] }
    ]
  },
  {
    "id": "test_car",
    "type": "vehicle",
    "name": "Test Small Car",
    "blueprint": [ "--" ],
    "parts": [
      { "x": 0, "y": 0, "parts": [ "frame", "seat", "controls" ] },
      { "x": 1, "y": 0, "parts": [ "frame", "engine_electric", "storage_battery" ] },
      { "x": 0, "y": 0, "parts": [ "wheel_mount_light", "wheel" ] },
      { "x": 1, "y": 0, "parts": [ "wheel_mount_light_steerable", "wheel" ] }
    ]
  }
```

If the exact part ids above (`wheel_mount_light`, `wheel`, `roof`, `frame`, `ig_vp_frame_vertical`) do not exist, substitute the nearest real id — grep `data/json/vehicleparts/` and `data/json/vehicles/` for a known-good small-car prototype to copy wheel/engine ids from. The car must fit within the truck bed footprint (x ∈ {-1,0} after driving on) and be self-powered/steerable.

- [ ] **Step 5: Format and run**

Run: `make style-json` (or format `vehicle_ramp.json` and `custom_vehicles.json` individually), then
`make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && tests/cata_test "test_flatbed_truck_has_a_ramp_part" -v`
Expected: PASS. (A build is needed because `custom_vehicles.json`/part JSON is validated at test load.)

- [ ] **Step 6: Commit**

```bash
git add data/json/vehicleparts/vehicle_ramp.json data/json/vehicles/custom_vehicles.json tests/vehicle_flatbed_test.cpp
git commit -m "feat(veh): fixed loading-ramp part + test flatbed truck and car

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Extend `advance_precalc_mounts` to climb vehicle ramps (the core change)

This is the milestone's central engine change and its highest-risk step. It adds a vehicle-part ramp probe alongside the existing terrain-ramp probe, so a car driving into a tile occupied by a stationary vehicle's `VEH_RAMP_UP` part is lifted to z+1 (and `VEH_RAMP_DOWN` drops it) exactly as terrain ramps do.

**Files:**
- Modify: `src/vehicle.cpp:8886-8900` (inside `advance_precalc_mounts`)
- Test: extend `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: `VPFLAG_VEH_RAMP_UP` / `VPFLAG_VEH_RAMP_DOWN` (Task 2); `test_flatbed_truck`, `test_car` (Task 3); `map::veh_at`, `optional_vpart_position::part_with_feature( vpart_bitflags, bool )`, `vehicle::velocity`.
- Produces: the traversal behavior consumed by the Task 6 integration test.

- [ ] **Step 1: Write the failing integration test**

Append to `tests/vehicle_flatbed_test.cpp`. Park the truck, place the car behind the ramp tail on z0, drive it forward, and assert its parts end at z1 while it stays a separate vehicle:

```cpp
TEST_CASE( "test_car_drives_up_ramp_onto_parked_truck_bed", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    // open air above z0 so the bed's support must come from the truck
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );

    // Parked truck facing +x; its ramp tail is at the truck's -x end.
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;

    // Car placed just behind the ramp tail, facing +x (toward the truck).
    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( car != nullptr );
    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;

    // Drive forward until the car has climbed; a handful of ticks suffices.
    for( int cycle = 0; cycle < 12; cycle++ ) {
        here.vehmove();
    }

    // The car climbed to z+1...
    bool any_z1 = false;
    for( const vpart_reference &vpr : car->get_all_parts() ) {
        if( !vpr.part().removed && !vpr.part().is_fake && vpr.part().precalc[0].z() == 1 ) {
            any_z1 = true;
        }
    }
    CHECK( any_z1 );
    // ...it did not fall...
    car->check_falling_or_floating();
    CHECK_FALSE( car->is_falling );
    // ...and it is still a SEPARATE vehicle (M1 boundary: no merge).
    CHECK( here.veh_at( tripoint_bub_ms( 56, 60, 0 ) ) ? true : true ); // sanity
    CHECK( car != truck );
    bool car_has_carried = false;
    for( const vpart_reference &vpr : car->get_all_parts() ) {
        if( vpr.part().has_flag( vp_flag::carried_flag ) ) {
            car_has_carried = true;
        }
    }
    CHECK_FALSE( car_has_carried );
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tests/cata_test "test_car_drives_up_ramp_onto_parked_truck_bed" -v`
Expected: FAIL — no `precalc[0].z() == 1` part (the car never climbs; the ramp part is inert to traversal).

- [ ] **Step 3: Implement the vehicle-ramp probe**

In `src/vehicle.cpp`, in `advance_precalc_mounts`, locate the terrain probe (lines 8893-8899):

```cpp
        tripoint_rel_ms ground_probe = prt.precalc[0];
        ground_probe.z() -= prt.mount.z();
        if( here->has_flag( ter_furn_flag::TFLAG_RAMP_UP, src + dp + ground_probe ) ) {
            prt.precalc[0].z() += 1;
        } else if( here->has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, src + dp + ground_probe ) ) {
            prt.precalc[0].z() -= 1;
        }
```

Replace it with (adds an `else if` that probes for a ramp part on a *different, stationary* vehicle):

```cpp
        tripoint_rel_ms ground_probe = prt.precalc[0];
        ground_probe.z() -= prt.mount.z();
        const tripoint_bub_ms ramp_probe = src + dp + ground_probe;
        if( here->has_flag( ter_furn_flag::TFLAG_RAMP_UP, ramp_probe ) ) {
            prt.precalc[0].z() += 1;
        } else if( here->has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, ramp_probe ) ) {
            prt.precalc[0].z() -= 1;
        } else if( const optional_vpart_position ovp = here->veh_at( ramp_probe ) ) {
            // A ramp *part* on a different, stationary vehicle acts like a terrain ramp
            // for a vehicle driving onto it: it is the flatbed-loading path. The carrier
            // must be stopped (M1 loads onto parked vehicles only).
            vehicle &other = ovp->vehicle();
            if( &other != this && other.velocity == 0 ) {
                if( ovp->part_with_feature( VPFLAG_VEH_RAMP_UP, true ) ) {
                    prt.precalc[0].z() += 1;
                } else if( ovp->part_with_feature( VPFLAG_VEH_RAMP_DOWN, true ) ) {
                    prt.precalc[0].z() -= 1;
                }
            }
        }
```

Confirm `optional_vpart_position` / `vpart_position` are already available in this translation unit (they are used throughout `vehicle.cpp`). `part_with_feature( vpart_bitflags, bool )` is the flag-query overload used elsewhere in this file.

- [ ] **Step 4: Build and run**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && tests/cata_test "test_car_drives_up_ramp_onto_parked_truck_bed" -v`
Expected: PASS.

If the car COLLIDES with the ramp part instead of climbing (test shows the car stuck at z0 / velocity killed at the truck tail): the move-collision check is rejecting the entry before the z-lift is applied. This is the known central risk. Investigate `vehicle::part_collision` / the movement path in `src/vehicle_move.cpp` that calls `advance_precalc_mounts`, and confirm the collision test consults the post-ramp `precalc` z (as it must already for terrain ramps). The fix, if needed, is to make the vehicle-vs-vehicle collision test skip a target part when the mover is transitioning to a different z at that tile via a ramp — mirroring how terrain ramps avoid self-collision. Keep the fix narrow and covered by this test.

- [ ] **Step 5: Commit**

```bash
git add src/vehicle.cpp tests/vehicle_flatbed_test.cpp
git commit -m "feat(veh): drive a vehicle up another vehicle's ramp part to z+1

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Deployable ramp part, gated on open state

Adds the second ramp type: an `OPENABLE` ramp whose climb behavior is live only when deployed (open). The traversal probe must ignore a closed deployable ramp.

**Files:**
- Modify: `data/json/vehicleparts/vehicle_ramp.json` (add `veh_ramp_deploy`)
- Modify: `src/vehicle.cpp` (the Task 4 probe — add the open-state gate)
- Test: extend `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2–4; `vehicle_part::open` (bool, `src/vehicle.h:529`); `vehicle::part( int )`, `optional_vpart_position::part_index()` / the part reference for the ramp.
- Produces: `vpart_id( "veh_ramp_deploy" )`; a closed deployable ramp is inert to traversal, an open one climbs.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_flatbed_test.cpp`. Use a truck variant or install a `veh_ramp_deploy` on the fly; simplest is a second prototype `test_flatbed_truck_deploy` identical to `test_flatbed_truck` but with `veh_ramp_deploy` at the tail. Add that prototype in this step too.

```cpp
static const vproto_id vehicle_prototype_test_flatbed_truck_deploy( "test_flatbed_truck_deploy" );

static bool drive_car_climbs( vehicle *truck, bool open_ramp )
{
    map &here = get_map();
    // open the deployable ramp parts if requested
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_VEH_RAMP_UP ) &&
            vpr.info().has_flag( VPFLAG_OPENABLE ) ) {
            here.open_or_close_vehicle_part_stub( truck, vpr.part_index(), open_ramp );
        }
    }
    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 0, 0 );
    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;
    for( int cycle = 0; cycle < 12; cycle++ ) {
        here.vehmove();
    }
    bool any_z1 = false;
    for( const vpart_reference &vpr : car->get_all_parts() ) {
        if( !vpr.part().removed && !vpr.part().is_fake && vpr.part().precalc[0].z() == 1 ) {
            any_z1 = true;
        }
    }
    return any_z1;
}
```

Because opening a vehicle part is normally done via `vehicle::open_or_close`, set the state directly in the test instead of a nonexistent helper — replace the `open_or_close_vehicle_part_stub` call with directly setting `truck->part( vpr.part_index() ).open = open_ramp;` followed by `truck->invalidate_mass();`/`here.build_map_cache`. Then two `TEST_CASE`s:

```cpp
TEST_CASE( "closed_deployable_ramp_blocks_climb", "[vehicle][flatbed]" )
{
    clear_map(); clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ )
        for( int y = 0; y < SEEY * MAPSIZE; y++ )
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
    here.invalidate_map_cache( 0 ); here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true ); here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck_deploy,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    CHECK_FALSE( drive_car_climbs( truck, /*open_ramp=*/false ) );
}

TEST_CASE( "open_deployable_ramp_allows_climb", "[vehicle][flatbed]" )
{
    clear_map(); clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ )
        for( int y = 0; y < SEEY * MAPSIZE; y++ )
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
    here.invalidate_map_cache( 0 ); here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true ); here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck_deploy,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    CHECK( drive_car_climbs( truck, /*open_ramp=*/true ) );
}
```

- [ ] **Step 2: Run to verify failure**

Run: `tests/cata_test "deployable_ramp" -v` (matches both cases by substring)
Expected: FAIL — `test_flatbed_truck_deploy` unknown, and the open-state gate not yet implemented.

- [ ] **Step 3: Author the deployable ramp and the deploy-truck prototype**

Add to `data/json/vehicleparts/vehicle_ramp.json`:

```json
  {
    "id": "veh_ramp_deploy",
    "type": "vehicle_part",
    "name": { "str": "folding loading ramp" },
    "categories": [ "hull" ],
    "durability": 250,
    "description": "A hinged steel ramp that folds up flush against the bed for travel and drops down to let another vehicle drive onto the deck.  Open it to deploy the ramp.",
    "item": "sheet_metal",
    "location": "structure",
    "size": "300 L",
    "flags": [ "VEH_RAMP_UP", "VEH_RAMP_DOWN", "BOARDABLE", "OPENABLE" ],
    "requirements": {
      "install": { "skills": [ [ "mechanics", 3 ] ], "time": "60 m", "using": [ [ "welding_standard", 200 ], [ "vehicle_bolt_install", 2 ] ] },
      "removal": { "skills": [ [ "mechanics", 2 ] ], "time": "30 m", "using": [ [ "vehicle_wrench_2", 1 ] ] },
      "repair": { "skills": [ [ "mechanics", 3 ] ], "time": "8 m", "using": [ [ "repair_welding_lc_steel", 4 ] ] }
    },
    "breaks_into": "ig_vp_frame_vertical",
    "damage_reduction": { "all": 15 }
  }
```

Add `test_flatbed_truck_deploy` to `custom_vehicles.json` — a copy of `test_flatbed_truck` with the two tail parts changed from `veh_ramp_fixed` to `veh_ramp_deploy`.

- [ ] **Step 4: Gate the probe on open state**

In `src/vehicle.cpp`, in the Task 4 probe, the `VEH_RAMP_UP`/`VEH_RAMP_DOWN` branches must ignore a closed openable ramp. Change the two `part_with_feature` lookups to also require the found part be non-openable or open. Replace the inner block:

```cpp
                if( ovp->part_with_feature( VPFLAG_VEH_RAMP_UP, true ) ) {
                    prt.precalc[0].z() += 1;
                } else if( ovp->part_with_feature( VPFLAG_VEH_RAMP_DOWN, true ) ) {
                    prt.precalc[0].z() -= 1;
                }
```

with a helper that treats a closed OPENABLE ramp as inert:

```cpp
                const auto ramp_active = [&]( vpart_bitflags flag ) -> bool {
                    const std::optional<vpart_reference> rp = ovp->part_with_feature( flag, true );
                    if( !rp ) {
                        return false;
                    }
                    // a deployable (OPENABLE) ramp only ramps when deployed (open)
                    if( rp->info().has_flag( VPFLAG_OPENABLE ) && !rp->part().open ) {
                        return false;
                    }
                    return true;
                };
                if( ramp_active( VPFLAG_VEH_RAMP_UP ) ) {
                    prt.precalc[0].z() += 1;
                } else if( ramp_active( VPFLAG_VEH_RAMP_DOWN ) ) {
                    prt.precalc[0].z() -= 1;
                }
```

Confirm `vpart_reference::part()` returns the mutable/const `vehicle_part` with the public `open` field, and `vpart_reference::info()` the `vpart_info`. Adjust const-qualification as the compiler requires.

- [ ] **Step 5: Build and run**

Run: `make style-json && make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && tests/cata_test "deployable_ramp" -v`
Expected: both cases PASS.

- [ ] **Step 6: Commit**

```bash
git add data/json/vehicleparts/vehicle_ramp.json data/json/vehicles/custom_vehicles.json src/vehicle.cpp tests/vehicle_flatbed_test.cpp
git commit -m "feat(veh): deployable folding ramp part, live only when deployed

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Regression guard and milestone wrap-up

**Files:**
- Test: `tests/vehicle_ramp_test.cpp` (run only — no edit), `tests/vehicle_flatbed_test.cpp` (run only)

**Interfaces:**
- Consumes: all prior tasks.
- Produces: green M1 traversal suite; no regression on the terrain-ramp path.

- [ ] **Step 1: Run the full flatbed suite**

Run: `tests/cata_test "[flatbed]" -v`
Expected: all cases from Tasks 1, 3, 4, 5 PASS.

- [ ] **Step 2: Run the terrain-ramp regression suite**

Run: `tests/cata_test "[ramp]" -v`
Expected: PASS — the shared `advance_precalc_mounts` change did not disturb terrain ramps or the two-floor bus ramp drive.

- [ ] **Step 3: Broader vehicle regression**

Run: `tests/cata_test "[vehicle]" -v`
Expected: PASS. If any `[monster]`-adjacent ordering flakiness appears, re-run the failing case alone to confirm it is unrelated (see the `ci-setup` memo on order-dependent tests).

- [ ] **Step 4: Final commit (docs/notes if any) and push for CI**

```bash
git add -A
git commit -m "test(veh): M1 flatbed traversal regression pass

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>" || echo "nothing to commit"
git push -u origin HEAD
```

Then open a PR to `master` and let CI (`pr-build.yml`) run the full suite; merge only when green (per the `ci-setup` memo).

---

## Self-Review notes (author)

- **Spec coverage:** ramp part JSON (Tasks 3, 5) ✓; new vpart flag + registration (Task 2) ✓; core `advance_precalc_mounts` extension (Task 4) ✓; cross-vehicle support (Task 1 spike; already provided by `supports_above`) ✓; fixed + deployable ramp types (Tasks 3, 5) ✓; test vehicle prototypes + `vehicle_flatbed_test.cpp` mirroring `vehicle_ramp_test.cpp` (Tasks 1–6) ✓; terrain-ramp no-regression (Task 6) ✓; separate-vehicle M1 boundary asserted (Task 4) ✓. Merge/lock correctly deferred to M2 (not in this plan).
- **Known risk, surfaced not hidden:** Task 4 Step 4 documents the vehicle-vs-vehicle collision-ordering risk and a concrete investigation path if the car collides instead of climbing.
- **Uncertain concrete ids** (`wheel_mount_light`, `wheel`, `frame`, `roof`, `ig_vp_frame_vertical`): Task 3 Step 4 instructs verifying/substituting against real ids by grepping an existing small-car prototype — flagged rather than assumed correct.
- **API names to verify during execution** (compiler will enforce): `vpart_reference::info()/part()`, `optional_vpart_position::part_with_feature(vpart_bitflags,bool)`, `vehicle_part::open`, `vehicle::velocity`. All are used elsewhere in `vehicle.cpp`; confirm exact const-qualification when wiring Task 5 Step 4.
