# Walkable Carrier Deck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the avatar, NPCs, and monsters walk up a parked flatbed's loading ramp onto its raised deck (z+1) and around it, standing next to the carried car.

**Architecture:** A roof part flagged `WALKABLE_ROOF` makes the open-air tile above it a walkable "deck floor." One new predicate `map::deck_floor_below(p)` is the single source of truth; each mover's movement/pathfinding gate that currently mis-reads the deck as a ledge/void is routed through it. Standing already works (roof support via `map::valid_move`→`vehicle::roof_at_part`); this plan only makes *walking onto/across/up-to* the deck work. An on-foot ramp lift (reusing `veh_ramp_dir`) is the way up.

**Tech Stack:** C++17 engine (`src/`), data-driven JSON content (`data/json/`), Catch2 tests (`tests/`). Build: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8`. Run one test: `./tests/cata_test "<name>"` from the repo root.

## Global Constraints

- Branch: `veh-drive-on-carrying` (this feature continues PR #10). Do not branch off master.
- New vpart flag used in JSON **must** have a `json_flag` entry in `data/json/vehicleparts/vp_flags.json` or JSON load debugmsgs "unknown flag".
- A C++-hot vpart flag is registered in three places: the `vpart_bitflags` enum (`src/veh_type.h`, before `NUM_VPFLAGS`), the string→flag map (`src/veh_type.cpp`), and `vp_flags.json`.
- `veh_ramp_dir(ovp, mover)` returns 0 unless the probed vehicle is **stationary** (`velocity == 0`) — matches the stationary-deck scope; do not remove that guard.
- Tests treat any `debugmsg` as a run failure (`test_main.cpp:486`); an interactive `uilist` `assert(!test_mode)`s. Assert cleanliness by capturing with `capture_debugmsg_during` and by the test simply not aborting.
- Astyle 3.1 C++ style (`if( x )`, spaces inside parens, `--align-pointer=name`). Run `make style-json` for JSON. Commit frequently, one task per commit.
- Co-author every commit: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## File Structure

- `src/veh_type.h` / `src/veh_type.cpp` — register `VPFLAG_WALKABLE_ROOF`.
- `data/json/vehicleparts/vp_flags.json` — `WALKABLE_ROOF` json_flag.
- `data/json/vehicleparts/vehicle_ramp.json` — new `veh_flatbed_deck` part (feature-local; lives with the ramp part).
- `data/json/vehicles/custom_vehicles.json` — `test_flatbed_truck` bed uses `veh_flatbed_deck`.
- `src/map.h` / `src/map.cpp` — `map::deck_floor_below` predicate (next to `has_vehicle_floor`, ~map.cpp:2734).
- `src/game.cpp` — avatar ledge gate (~10547) routes through the predicate.
- `src/avatar_action.cpp` — on-foot ramp lift (~234).
- `src/map.cpp` — `update_pathfinding_cache` (~10542) marks a vehicle ramp as a way up.
- `tests/vehicle_flatbed_test.cpp` — all new tests (already contains the flatbed setup helpers and the drive-up-with-avatar setup this plan reuses).

---

### Task 1: Register `WALKABLE_ROOF` and add the `veh_flatbed_deck` bed part

**Files:**
- Modify: `src/veh_type.h:127` (add enum before `NUM_VPFLAGS`)
- Modify: `src/veh_type.cpp:155` (add to string→flag map, alphabetical-ish near the other flags)
- Modify: `data/json/vehicleparts/vp_flags.json` (add json_flag)
- Modify: `data/json/vehicleparts/vehicle_ramp.json` (add `veh_flatbed_deck` part)
- Modify: `data/json/vehicles/custom_vehicles.json:329-330` (bed mounts x=-1,0 use `veh_flatbed_deck`)
- Test: `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Produces: `VPFLAG_WALKABLE_ROOF` (enum value in `vpart_bitflags`); JSON flag string `"WALKABLE_ROOF"`; part id `veh_flatbed_deck`.

- [ ] **Step 1: Write the failing test**

Add to `tests/vehicle_flatbed_test.cpp`:

```cpp
TEST_CASE( "test_flatbed_bed_has_walkable_roof_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    // At least one bed part carries WALKABLE_ROOF, and it is a roof (provides fall support).
    bool found_walkable_deck = false;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            found_walkable_deck = true;
            CHECK( truck->roof_at_part( vpr.part_index() ) >= 0 );
        }
    }
    CHECK( found_walkable_deck );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "test_flatbed_bed_has_walkable_roof_deck"`
Expected: compile FAIL — `VPFLAG_WALKABLE_ROOF` undeclared.

- [ ] **Step 3: Register the flag (C++)**

In `src/veh_type.h`, add before `NUM_VPFLAGS` (after `VPFLAG_VEH_RAMP_DOWN`):

```cpp
    VPFLAG_WALKABLE_ROOF,
```

In `src/veh_type.cpp`, add to the `vpart_bitflag_map` next to the other entries:

```cpp
    { "WALKABLE_ROOF", VPFLAG_WALKABLE_ROOF },
```

- [ ] **Step 4: Register the flag (JSON) and add the deck part**

In `data/json/vehicleparts/vp_flags.json`, add:

```json
  {
    "id": "WALKABLE_ROOF",
    "type": "json_flag",
    "info": "The open-air tile directly above this roof part is a walkable deck: creatures on the level above can stand and walk on it as if it were floor."
  },
```

In `data/json/vehicleparts/vehicle_ramp.json`, add a second object to the array:

```json
  {
    "id": "veh_flatbed_deck",
    "copy-from": "roof",
    "type": "vehicle_part",
    "name": { "str": "flatbed deck" },
    "description": "A sturdy steel bed roof strong enough to stand on from above; another vehicle can be carried on it while people walk the deck alongside.",
    "flags": [ "ROOF", "WALKABLE_ROOF" ]
  }
```

- [ ] **Step 5: Use the deck part on the flatbed bed**

In `data/json/vehicles/custom_vehicles.json`, change the two bed rows (currently `"roof"`):

```json
      { "x": -1, "y": 0, "parts": [ "hdframe", "veh_flatbed_deck" ] },
      { "x": -1, "y": 1, "parts": [ "hdframe", "veh_flatbed_deck" ] },
      { "x": 0, "y": 0, "parts": [ "hdframe", "veh_flatbed_deck", "wheel_mount_medium", "wheel_wide" ] },
      { "x": 0, "y": 1, "parts": [ "hdframe", "veh_flatbed_deck", "wheel_mount_medium", "wheel_wide" ] },
```

- [ ] **Step 6: Format JSON, build, run test + drive-up regression**

Run: `make style-json`
Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "[flatbed]"`
Expected: PASS — new test green, and `test_car_drives_up_ramp_onto_parked_truck_bed` still green (the deck part didn't break the car drive-up, since it's roof-type like the `roof` it replaced).

- [ ] **Step 7: Commit**

```bash
git add src/veh_type.h src/veh_type.cpp data/json/vehicleparts/vp_flags.json data/json/vehicleparts/vehicle_ramp.json data/json/vehicles/custom_vehicles.json tests/vehicle_flatbed_test.cpp
git commit -m "feat(veh): WALKABLE_ROOF flag + veh_flatbed_deck bed part

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `map::deck_floor_below` predicate

**Files:**
- Modify: `src/map.h` (declare next to `has_vehicle_floor`)
- Modify: `src/map.cpp:2734` (define next to `has_vehicle_floor`)
- Test: `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: `VPFLAG_WALKABLE_ROOF` (Task 1).
- Produces: `bool map::deck_floor_below( const tripoint_bub_ms &p ) const;`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE( "deck_floor_below_true_only_over_walkable_roof", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    // open air above z0 so deck tiles are is_open_air
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );

    // A tile above a WALKABLE_ROOF bed part -> true.
    std::optional<tripoint_bub_ms> deck_tile;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            deck_tile = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            break;
        }
    }
    REQUIRE( deck_tile.has_value() );
    CHECK( here.deck_floor_below( *deck_tile ) );

    // A z+1 tile far from the truck (open air over bare pavement) -> false.
    CHECK_FALSE( here.deck_floor_below( tripoint_bub_ms( 40, 40, 1 ) ) );
    // The deck part's own z0 tile is not open air-above-a-deck -> false.
    CHECK_FALSE( here.deck_floor_below( *deck_tile + tripoint_rel_ms( 0, 0, -1 ) ) );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "deck_floor_below_true_only_over_walkable_roof"`
Expected: compile FAIL — `deck_floor_below` not a member of `map`.

- [ ] **Step 3: Declare and define the predicate**

In `src/map.h`, next to `bool has_vehicle_floor( const tripoint_bub_ms &p ) const;`:

```cpp
        // True when p is open air but the vehicle part directly below carries
        // WALKABLE_ROOF, i.e. p is a walkable vehicle deck floor.
        bool deck_floor_below( const tripoint_bub_ms &p ) const;
```

In `src/map.cpp`, after `map::has_vehicle_floor` (~2738):

```cpp
bool map::deck_floor_below( const tripoint_bub_ms &p ) const
{
    if( !is_open_air( p ) ) {
        return false;
    }
    const tripoint_bub_ms below( p.xy(), p.z() - 1 );
    return veh_at( below ).part_with_feature( VPFLAG_WALKABLE_ROOF, false ).has_value();
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "deck_floor_below_true_only_over_walkable_roof"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/map.h src/map.cpp tests/vehicle_flatbed_test.cpp
git commit -m "feat(map): deck_floor_below predicate for walkable vehicle decks

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Avatar walks across the deck (ledge-gate fix)

**Files:**
- Modify: `src/game.cpp:10547` (the `is_open_air` ledge hazard)
- Test: `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: `map::deck_floor_below` (Task 2).

This is the change proven necessary: a step onto/across a deck tile currently raises a "ledge" hazard (`game.cpp:10546-10552`) and opens the climb-down menu (`game.cpp:10730` → `iexamine::ledge`). The test reuses the drive-up-with-avatar setup to get onto the deck, then walks across.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE( "avatar_walks_across_flatbed_deck_no_ledge_menu", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );

    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;
    vehicle *car = here.add_vehicle( vehicle_prototype_test_car,
                                     tripoint_bub_ms( 56, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( car != nullptr );

    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 56, 60, 0 ) );
    here.board_vehicle( u.pos_bub( here ), &u );
    REQUIRE( u.in_vehicle );
    car->tags.insert( "IN_CONTROL_OVERRIDE" );
    car->engine_on = true;
    car->cruise_velocity = 200;
    car->velocity = 200;
    for( int cycle = 0; cycle < 20; cycle++ ) {
        here.vehmove();
        if( car->pos_bub( here ).z() == 1 ) {
            car->cruise_velocity = 0;
            car->velocity = 0;
            break;
        }
    }
    for( int i = 0; i < 3; i++ ) {
        here.vehmove();
    }
    REQUIRE( u.pos_bub( here ).z() == 1 );

    // Step off the car onto the truck's own deck, then across it.
    creature_tracker &cr = get_creature_tracker();
    here.unboard_vehicle( u.pos_bub( here ) );
    std::optional<tripoint_bub_ms> roof_deck;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( truck->roof_at_part( vpr.part_index() ) < 0 ) {
            continue;
        }
        const tripoint_bub_ms above = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
        if( cr.creature_at( above ) == nullptr && !here.veh_at( above ) ) {
            roof_deck = above;
            break;
        }
    }
    REQUIRE( roof_deck.has_value() );
    u.setpos( here, *roof_deck );
    REQUIRE( u.pos_bub( here ) == *roof_deck );

    // Find an adjacent roof-supported deck tile and walk to it.
    std::optional<tripoint_rel_ms> across_dir;
    for( const tripoint_rel_ms &d : { tripoint_rel_ms( 1, 0, 0 ), tripoint_rel_ms( -1, 0, 0 ),
                                      tripoint_rel_ms( 0, 1, 0 ), tripoint_rel_ms( 0, -1, 0 ) } ) {
        const tripoint_bub_ms nb = *roof_deck + d;
        if( here.deck_floor_below( nb ) && !here.veh_at( nb ) && cr.creature_at( nb ) == nullptr ) {
            across_dir = d;
            break;
        }
    }
    REQUIRE( across_dir.has_value() );
    const tripoint_bub_ms across_target = *roof_deck + *across_dir;
    u.set_moves( 1000 );
    std::string dmsg = capture_debugmsg_during( [&]() {
        avatar_action::move( u, here, *across_dir );
    } );
    CAPTURE( dmsg );
    CHECK( dmsg.empty() );
    CHECK( u.pos_bub( here ) == across_target );  // actually walked across, no menu/fall
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "avatar_walks_across_flatbed_deck_no_ledge_menu"`
Expected: FAIL — SIGABRT `assert(!test_mode)` in `uilist::init` (the climb-down menu).

- [ ] **Step 3: Route the ledge gate through the predicate**

In `src/game.cpp` at the `is_open_air` block (~10546):

```cpp
    if( here.is_open_air( dest_loc ) ) {
        if( !veh_dest && !here.deck_floor_below( dest_loc ) &&
            !u.has_effect_with_flag( json_flag_LEVITATION ) ) {
            harmful_stuff.emplace_back( "ledge" );
            if( harmful_stuff.size() == max ) {
                return harmful_stuff;
            }
        }
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "avatar_walks_across_flatbed_deck_no_ledge_menu"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/game.cpp tests/vehicle_flatbed_test.cpp
git commit -m "feat(game): a roof-supported deck tile is not a ledge for walkers

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Avatar walks up the ramp on foot

**Files:**
- Modify: `src/avatar_action.cpp:234` (mirror the terrain-ramp lift for vehicle ramps)
- Possibly modify: `data/json/vehicles/custom_vehicles.json` (ramp mounts also carry `veh_flatbed_deck`)
- Test: `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: `veh_ramp_dir` (`src/vehicle.h:235`), `map::deck_floor_below` (Task 2).

**Geometry note (highest-risk task):** lifting a walker onto the ramp tile lands them at (ramp x, z+1), which above a bare ramp is open air with no roof → they'd fall. The primary approach makes the ramp tiles *also* carry `veh_flatbed_deck`, so the tile above the ramp is a supported walkable deck that connects to the bed — mirroring how a terrain ramp's upper tile is solid floor. If that reintroduces a car-vs-part collision on the ramp tile (the `test_car_drives_up...` regression fails), fall back to leaving the ramp bare and instead performing the lift on the ramp→bed transition; iterate against the test below, which defines "done".

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE( "avatar_walks_up_ramp_onto_flatbed_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );

    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );
    truck->velocity = 0;
    truck->engine_on = false;

    // Ramp tail is at world x=58 (mount -2). Start two tiles behind it on the ground.
    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 56, 60, 0 ) );
    here.build_map_cache( 0, true );

    // Walk east up the ramp onto the bed. A few steps; stop once on the deck at z+1.
    std::string dmsg = capture_debugmsg_during( [&]() {
        for( int step = 0; step < 6 && u.pos_bub( here ).z() == 0; step++ ) {
            u.set_moves( 1000 );
            avatar_action::move( u, here, tripoint_rel_ms::east );
        }
    } );
    CAPTURE( dmsg );
    CAPTURE( u.pos_bub( here ).to_string() );
    CHECK( dmsg.empty() );
    CHECK( u.pos_bub( here ).z() == 1 );                 // climbed to the deck
    CHECK( here.deck_floor_below( u.pos_bub( here ) ) );  // standing on a walkable deck tile
    u.gravity_check( &here );
    CHECK( u.pos_bub( here ).z() == 1 );                 // and does not fall
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "avatar_walks_up_ramp_onto_flatbed_deck"`
Expected: FAIL — the avatar never leaves z0 (no on-foot vehicle-ramp lift), so `z() == 1` fails.

- [ ] **Step 3: Add the vehicle-ramp lift to on-foot movement**

In `src/avatar_action.cpp` after the terrain-ramp block (~241), extend it:

```cpp
    bool via_ramp = false;
    if( m.has_flag( ter_furn_flag::TFLAG_RAMP_UP, dest_loc ) ) {
        dest_loc.z() += 1;
        via_ramp = true;
    } else if( m.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, dest_loc ) ) {
        dest_loc.z() -= 1;
        via_ramp = true;
    } else if( const int vramp = veh_ramp_dir( m.veh_at( dest_loc ), nullptr ) ) {
        // Walk up/down a parked vehicle's loading ramp onto/off its deck, mirroring
        // terrain ramps and the drive-up path (both key off veh_ramp_dir).
        dest_loc.z() += vramp;
        via_ramp = true;
    }
```

Ensure `#include "vehicle.h"` is present in `src/avatar_action.cpp` (it declares `veh_ramp_dir`).

- [ ] **Step 4: Run test; if the walker ends in open air above the ramp, extend deck coverage to the ramp mounts**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "avatar_walks_up_ramp_onto_flatbed_deck"`

If `deck_floor_below(pos)` is false or the avatar falls, add `veh_flatbed_deck` to the ramp mounts in `data/json/vehicles/custom_vehicles.json` so the tile above the ramp is a supported deck:

```json
      { "x": -2, "y": 0, "parts": [ "veh_ramp_fixed", "veh_flatbed_deck" ] },
      { "x": -2, "y": 1, "parts": [ "veh_ramp_fixed", "veh_flatbed_deck" ] }
```

Then re-run. Expected: PASS.

- [ ] **Step 5: Verify the car drive-up did not regress**

Run: `./tests/cata_test "[flatbed]"`
Expected: PASS — including `test_car_drives_up_ramp_onto_parked_truck_bed`. If adding the deck to the ramp mounts broke the car drive-up (collision), revert the JSON change from Step 4 and instead lift on the ramp→bed transition (see the geometry note); re-run both tests until both pass.

- [ ] **Step 6: Commit**

```bash
git add src/avatar_action.cpp data/json/vehicles/custom_vehicles.json tests/vehicle_flatbed_test.cpp
git commit -m "feat(veh): walk up a parked vehicle's loading ramp onto its deck

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Pathfinding recognizes the vehicle ramp as a way up

**Files:**
- Modify: `src/map.cpp:10542` (`update_pathfinding_cache` — set `GoesUp|RampUp` / `GoesDown|RampDown` for vehicle ramps too)
- Test: `tests/vehicle_flatbed_test.cpp`

**Interfaces:**
- Consumes: `VPFLAG_VEH_RAMP_UP`, `VPFLAG_VEH_RAMP_DOWN` (existing).

NPC/monster routes use the pathfinding cache. Open-air deck tiles are already cached as `Ground` and are horizontally walkable, but the cache only marks *terrain* ramps as a vertical connection, so a router can't find the way up onto the deck. Mirror the terrain-ramp flags for a vehicle ramp part.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE( "pathfinding_routes_up_vehicle_ramp_onto_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );

    // Target: a deck tile at z+1. Start: ground behind the ramp at z0.
    std::optional<tripoint_bub_ms> deck_tile;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            deck_tile = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            break;
        }
    }
    REQUIRE( deck_tile.has_value() );
    const tripoint_bub_ms start( 56, 60, 0 );

    // Requires `#include "pathfinding.h"` in the test file.
    pathfinding_settings settings;
    settings.allow_climb_stairs = true;   // ramps use the stair-climb path
    const std::vector<tripoint_bub_ms> route =
        here.route( start, pathfinding_target::point( *deck_tile ), settings );
    CHECK_FALSE( route.empty() );          // a route up onto the deck exists
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "pathfinding_routes_up_vehicle_ramp_onto_deck"`
Expected: FAIL — `route` is empty (no vertical connection cached at the vehicle ramp). If it unexpectedly passes, keep the test as a guard and skip Step 3.

- [ ] **Step 3: Mark vehicle ramps as vertical connections in the pathfinding cache**

In `src/map.cpp` `update_pathfinding_cache`, after the terrain `TFLAG_RAMP_DOWN` block (~10549), add:

```cpp
    if( veh != nullptr ) {
        const optional_vpart_position vp = veh_at( p );
        if( vp.part_with_feature( VPFLAG_VEH_RAMP_UP, true ) ) {
            cur_value |= PathfindingFlag::GoesUp | PathfindingFlag::RampUp;
        }
        if( vp.part_with_feature( VPFLAG_VEH_RAMP_DOWN, true ) ) {
            cur_value |= PathfindingFlag::GoesDown | PathfindingFlag::RampDown;
        }
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "pathfinding_routes_up_vehicle_ramp_onto_deck"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/map.cpp tests/vehicle_flatbed_test.cpp
git commit -m "feat(map): vehicle loading ramps are vertical connections for pathfinding

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: An NPC walks onto the deck

**Files:**
- Test: `tests/vehicle_flatbed_test.cpp`
- Modify (only if the test fails): the specific NPC move/pathfinding gate the failure points to, routed through `map::deck_floor_below`.

**Interfaces:**
- Consumes: everything from Tasks 1–5.

This is a discovery-guarded task: with the deck cached as `Ground` and the ramp now a vertical connection, NPC routing may already work. The test proves it; if it fails, the failure names the gate to fix.

- [ ] **Step 1: Write the test**

```cpp
TEST_CASE( "npc_routes_up_ramp_onto_flatbed_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );

    std::optional<tripoint_bub_ms> deck_tile;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            deck_tile = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            break;
        }
    }
    REQUIRE( deck_tile.has_value() );

    // Requires `#include "npc.h"` and `#include "pathfinding.h"` in the test file.
    standard_npc npc( "DeckWalker", tripoint_bub_ms( 56, 60, 0 ), {}, 0, 8, 8, 8, 8 );
    npc.setpos( here, tripoint_bub_ms( 56, 60, 0 ) );
    // The Creature overload of route() uses the npc's own get_pathfinding_settings().
    const std::vector<tripoint_bub_ms> route =
        here.route( npc, pathfinding_target::point( *deck_tile ) );
    CAPTURE( deck_tile->to_string() );
    CHECK_FALSE( route.empty() );
}
```

- [ ] **Step 2: Build and run**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "npc_routes_up_ramp_onto_flatbed_deck"`

If PASS: NPC routing already works — go to Step 4 (commit as a guard).
If FAIL: the NPC's `get_pathfinding_settings()` differs (e.g. `allow_climb_stairs` false, or an avoid flag). In Step 3, inspect `npc::get_pathfinding_settings` in `src/npcmove.cpp` and make the deck reachable — first by confirming the settings allow ramp climbing; only route a gate through `map::deck_floor_below` if a concrete rejection is found there.

- [ ] **Step 3: (Only if failing) fix the identified NPC gate**

Apply the minimal change the failure points to (settings or a specific gate), keeping `map::deck_floor_below` as the source of truth. Re-run until PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/vehicle_flatbed_test.cpp src/npcmove.cpp
git commit -m "test(veh): NPC routes up the ramp onto the flatbed deck

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
(Drop `src/npcmove.cpp` from the add if Step 3 made no change.)

---

### Task 7: A monster paths onto the deck

**Files:**
- Test: `tests/vehicle_flatbed_test.cpp`
- Modify (only if the test fails): the specific monster move/pathfinding gate the failure points to.

**Interfaces:**
- Consumes: everything from Tasks 1–5.

Same discovery-guarded shape as Task 6, for a monster. Per the spec, if monster ramp-climbing proves gnarly this can be the piece that lands last; keep the test even if it must be temporarily `[.]`-tagged (hidden) with a note.

- [ ] **Step 1: Write the test**

```cpp
TEST_CASE( "monster_routes_up_ramp_onto_flatbed_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    clear_creatures();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    vehicle *truck = here.add_vehicle( vehicle_prototype_test_flatbed_truck,
                                       tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( truck != nullptr );

    std::optional<tripoint_bub_ms> deck_tile;
    for( const vpart_reference &vpr : truck->get_all_parts() ) {
        if( vpr.info().has_flag( VPFLAG_WALKABLE_ROOF ) ) {
            deck_tile = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            break;
        }
    }
    REQUIRE( deck_tile.has_value() );

    // Requires `#include "pathfinding.h"` in the test file.
    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 56, 60, 0 ) );
    const std::vector<tripoint_bub_ms> route =
        here.route( zombie, pathfinding_target::point( *deck_tile ) );
    CAPTURE( deck_tile->to_string() );
    CHECK_FALSE( route.empty() );
}
```

- [ ] **Step 2: Build and run**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "monster_routes_up_ramp_onto_flatbed_deck"`

If PASS: commit as a guard (Step 4).
If FAIL: inspect `monster::get_pathfinding_settings` (`src/monster.cpp`) and the monster move path (`src/monmove.cpp`). Make the deck reachable with the minimal change, keeping `map::deck_floor_below` as the source of truth.

- [ ] **Step 3: (Only if failing) fix the identified monster gate**

Apply the minimal change and re-run until PASS. If it proves too involved for this pass, tag the test `[.][vehicle][flatbed]` (hidden) with a `// TODO(monster-deck)` comment and record it in the memory update (Task 9) as the remaining monster-up follow-on.

- [ ] **Step 4: Commit**

```bash
git add tests/vehicle_flatbed_test.cpp src/monmove.cpp src/monster.cpp
git commit -m "test(veh): monster routes up the ramp onto the flatbed deck

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
(Drop unchanged source files from the add.)

---

### Task 8: Negative test — an unflagged roof is not walkable

**Files:**
- Test: `tests/vehicle_flatbed_test.cpp`

Proves opt-in: a vehicle whose roof lacks `WALKABLE_ROOF` still reads as open air (no deck floor). Guards against a future change that makes all roofs walkable.

- [ ] **Step 1: Write the test**

```cpp
TEST_CASE( "generic_roof_is_not_a_walkable_deck", "[vehicle][flatbed]" )
{
    clear_map();
    clear_vehicles();
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int x = 0; x < SEEX * MAPSIZE; x++ ) {
        for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
        }
    }
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    here.build_map_cache( 1, true );
    // "car" has an ordinary roof (no WALKABLE_ROOF).
    vehicle *plain = here.add_vehicle( vproto_id( "car" ), tripoint_bub_ms( 60, 60, 0 ),
                                       0_degrees, 0, 0 );
    REQUIRE( plain != nullptr );
    bool any_roof = false;
    for( const vpart_reference &vpr : plain->get_all_parts() ) {
        if( plain->roof_at_part( vpr.part_index() ) >= 0 ) {
            any_roof = true;
            const tripoint_bub_ms above = vpr.pos_bub( here ) + tripoint_rel_ms( 0, 0, 1 );
            CHECK_FALSE( here.deck_floor_below( above ) );  // roof, but not a walkable deck
        }
    }
    REQUIRE( any_roof );
}
```

- [ ] **Step 2: Build and run**

Run: `make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8 && ./tests/cata_test "generic_roof_is_not_a_walkable_deck"`
Expected: PASS (no code change — this is a guard).

- [ ] **Step 3: Commit**

```bash
git add tests/vehicle_flatbed_test.cpp
git commit -m "test(veh): generic roofs are not walkable decks (opt-in guard)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Regression sweep, docs, and memory update

**Files:**
- Modify: `docs/superpowers/specs/2026-08-06-walkable-carrier-deck-design.md` (mark Status: Implemented; note any monster-up follow-on)
- Modify: the `vehicle-ramp-lock-feature` memory file

- [ ] **Step 1: Run the affected suites**

Run: `./tests/cata_test "[flatbed]"` then `./tests/cata_test "[vehicle]"` then `./tests/cata_test "[ramp]"` then `./tests/cata_test "[multifloor]"`
Expected: all green. Fix any regression before proceeding.

- [ ] **Step 2: Full isolated suite**

Run: `build-scripts/gha_test_only.sh` (isolated, order-safe batches).
Expected: green.

- [ ] **Step 3: Update the design doc status and the memory**

In the spec, set `Status:` to `Implemented`. In the `vehicle-ramp-lock-feature` memory file, record: walkable stationary deck done (flag `WALKABLE_ROOF`, part `veh_flatbed_deck`, predicate `deck_floor_below`, ledge-gate + avatar ramp-lift + pathfinding-ramp sites), item-1 board spam resolved as part of it, and whether monster-up (Task 7) fully landed or is a follow-on. Carried-while-driving + vertical lock remain M2.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-08-06-walkable-carrier-deck-design.md
git commit -m "docs(veh): mark walkable carrier deck implemented

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review Notes

- **Spec coverage:** flag+part (Task 1), predicate (Task 2), avatar walk-across ledge fix (Task 3), on-foot ramp lift (Task 4), NPC+monster via pathfinding (Tasks 5–7), opt-in/negative (Task 8), regressions (Tasks 1, 4, 9). Item-1 board spam is covered transitively — Tasks 3–4 exercise the on/off-deck board paths under `capture_debugmsg_during`.
- **Fall-prevention** is deliberately untouched (already works); no task modifies `valid_move`'s `roof_at_part` support.
- **Highest risk** is isolated in Task 4 (ramp geometry) and Task 7 (monster movement), each with an explicit fallback and a test that defines "done".
