# Multi-Floor Vehicles — Milestone 5 (Driving) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a two-floor vehicle *drive* correctly — flat and over a ramp — with collisions resolved on the right z-level for every deck, closing the stated end-goal of the vertical slice.

**Architecture:** M1–M4 already made `mount` 3D, seeded `precalc.z = mount.z + precalc_z_delta`, and delivered a stationary livable deck. M5 turns on motion. The one real production defect is in collision classification: `vehicle::part_collision` decides "is this a vertical collision?" from a part's *absolute* z (`p.z() != sm_pos.z()`), which is only equivalent to "is this a vertical *move*?" for single-floor vehicles. We thread the movement direction down from `vehicle::collision` instead. Everything else in the driving phase (mass center stays planar by design; ground contact is already `wheelcache` membership; the ramp block already records `precalc_z_delta`) is validated — not changed — by new tests. The headline is a movement/collision test of the 2-floor bus, including the roughest path: a 2-floor bus climbing a ramp (it transiently spans **three** z-levels).

**Tech Stack:** C++17, Catch2 (`tests/cata_test`), CDDA vehicle subsystem (`src/vehicle.cpp`, `src/vehicle_move.cpp`), JSON content (`data/json/vehicles/custom_vehicles.json`).

## Global Constraints

- **Build/test only via the NAS Docker harness** `.nas-build/cdda.sh` (curses + tiles) — no host toolchain. See the `nas-build-status` memory for RAM/swap, `C.UTF-8` locale, and shared test-object/PCH traps. **Build before test.**
- **Single-floor no-op:** every production change MUST be byte-for-byte behavior-identical for any vehicle whose parts are all at `mount.z == 0`. The full existing `[vehicle]` and `[ramp]` suites must stay green. This is the primary gate.
- **Read a 3D mount as** `vp->vehicle().part( vp->part_index() ).mount` — `vpart_position::mount_pos()` is 2D and silently drops z.
- **Connector-gated rules** established in M2/M3 stay authoritative; do not re-derive z-connectivity here.
- **astyle 3.1** on every changed `.cpp/.h` before commit. Locally: `docker run --rm -v <repo>:/src -w /src ubuntu:24.04 bash -c "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n <files> && chown 3000:3003 <files>"`. **`make style-json`** for any changed JSON.
- **Branch:** `explore/multi-floor-vehicles-m5`. Merge via PR with a merge commit titled `... (#6)`; delete branch after.
- **Test tag:** new tests carry `[vehicle][multifloor]` (movement ones also `[ramp]` where they drive a ramp) so they run in the existing isolated batches.
- The test vehicle is `test_bus_2floor` (`data/json/vehicles/custom_vehicles.json`): a 4×2 footprint with `controls`+`engine_electric`+`storage_battery` on the lower deck (z=0), `wheel_wide` on four corners (all z=0), and a z=1 upper deck of `deck_floor` + `seat` + `cargo_space` seeded from the `ladder_internal` connector at mount `(0,0,0)`. It can drive under its own power.

---

## File Structure

- `src/vehicle.h` — change the `part_collision` declaration (add a `vertical` parameter).
- `src/vehicle_move.cpp` — `vehicle::collision` passes its already-computed `vertical` down; `vehicle::part_collision` consumes it instead of recomputing from `p.z()`.
- `src/teleport.cpp` — update the one external `part_collision` call site to supply `vertical`.
- `src/veh_type.cpp` — add the load-time ground-contact guard (`WHEEL` ⇒ `mount.z == 0`) in `vpart_info::check()`.
- `tests/vehicle_multifloor_test.cpp` — new collision-classification, ground-contact, cross-term, and mass-center tests.
- `tests/vehicle_ramp_test.cpp` — new "drive the 2-floor bus" flat + ramp movement tests (reuses the existing `clear_game_and_set_ramp` harness in this file).

No new files. All changes extend existing units following their established patterns.

---

### Task 1: Collision classification follows the *move*, not a part's absolute z

**Why:** `vehicle::part_collision` computes `const bool vert_coll = bash_floor || p.z() != sm_pos.z();` (`src/vehicle_move.cpp:842`). `vert_coll` selects `vertical_velocity` vs `velocity` (`:878-880`) and, when that velocity is 0, bails early (`:881-883`). For a single-floor vehicle every real part sits at `sm_pos.z()`, so `vert_coll` is true iff the move was vertical — correct. For a **two-floor** vehicle an upper-deck part's target `p.z()` is `sm_pos.z()+1` even during a purely **horizontal** drive, so `vert_coll` is wrongly `true`, `vertical_velocity` is 0, and the upper deck **phases through** any obstacle at its own level. `vehicle::collision` already knows the truth — `const bool vertical = bash_floor || dp.z() != 0;` (`:734`) — because it splits every z-diagonal move into pure-horizontal + pure-vertical passes first (`:720-724`). Thread that flag down.

**Files:**
- Modify: `src/vehicle.h:1903-1904` (declaration)
- Modify: `src/vehicle_move.cpp:836-842` (definition + `vert_coll`), `:760` and `:764` (internal call sites)
- Modify: `src/teleport.cpp:61` (external call site)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Produces: `veh_collision vehicle::part_collision( map &here, int part, const tripoint_abs_ms &p, bool just_detect, bool bash_floor, bool vertical )` — new trailing `bool vertical` (true = vertical pass). Callers pass the move-direction verticality; `part_collision` no longer derives it from `p.z()`.

- [ ] **Step 1: Write the failing test**

Add to `tests/vehicle_multifloor_test.cpp` (near the other `test_bus_2floor` cases). Drive the bus horizontally toward a wall placed **only on the upper deck's z-level** and assert the bus collides (velocity is knocked down) rather than phasing through:

```cpp
TEST_CASE( "two_floor_bus_upper_deck_collides_horizontally", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map(); // z-levels 0 and 1 present in the bubble

    const tripoint_bub_ms origin( 60, 60, 0 );
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor, origin, 90_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    veh->check_falling_or_floating();
    REQUIRE_FALSE( veh->is_in_water() );

    // A solid, impassable obstacle directly ahead of the vehicle, ONLY on the
    // upper deck (z=1). If part_collision mis-classifies the upper deck as a
    // vertical collision, vertical_velocity (0) makes it phase straight through.
    // Facing 90_degrees drives along +x; place the wall a few tiles ahead at z=1.
    const tripoint_bub_ms wall_z1( 66, 60, 1 );
    here.ter_set( wall_z1, ter_id( "t_wall" ) );
    REQUIRE( here.impassable( wall_z1 ) );

    veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    veh->engine_on = true;
    veh->cruise_velocity = 400;
    veh->velocity = 400;

    bool collided = false;
    for( int cycle = 0; cycle < 12 && !collided; cycle++ ) {
        std::vector<veh_collision> colls;
        // pure-horizontal probe along the facing; carries the upper deck at z+1
        if( veh->collision( here, colls, tripoint_rel_ms( 1, 0, 0 ), true, false ) ) {
            collided = true;
        }
        here.vehmove();
    }
    CHECK( collided );
}
```

- [ ] **Step 2: Build, then run the test to verify it fails**

Run (via the NAS harness): build, then
`tests/cata_test "two_floor_bus_upper_deck_collides_horizontally"`
Expected: **FAIL** — no collision is ever detected (the upper deck phases through the z=1 wall) because `vert_coll` is `true` for the upper-deck part and `vertical_velocity == 0`.

- [ ] **Step 3: Add the `vertical` parameter to the declaration**

`src/vehicle.h`, change:

```cpp
        veh_collision part_collision( map &here, int part, const tripoint_abs_ms &p,
                                      bool just_detect, bool bash_floor );
```
to:
```cpp
        veh_collision part_collision( map &here, int part, const tripoint_abs_ms &p,
                                      bool just_detect, bool bash_floor, bool vertical );
```

- [ ] **Step 4: Consume the parameter in the definition**

`src/vehicle_move.cpp:836-842`, change the signature and the `vert_coll` line:

```cpp
veh_collision vehicle::part_collision( map &here, int part, const tripoint_abs_ms &p,
                                       bool just_detect, bool bash_floor, bool vertical )
{
    tripoint_bub_ms pos = here.get_bub( p );
    // Vertical collisions need to be handled differently. `vertical` is decided by the
    // MOVE (vehicle::collision already split any z-diagonal into pure H / pure V passes),
    // not by a part's absolute z — an upper-deck part moving horizontally is still a
    // horizontal collision. Keep `bash_floor` as a vertical trigger for the floor-bash path.
    const bool vert_coll = bash_floor || vertical;
```

(Delete the old `p.z() != sm_pos.z()` derivation. `sm_pos` may now be unused in this function — if the compiler warns, remove the now-dead reference.)

- [ ] **Step 5: Pass `vertical` from the two internal call sites**

`src/vehicle_move.cpp:734` already computes `const bool vertical = bash_floor || dp.z() != 0;`. Pass it at both call sites:

`:760`:
```cpp
        veh_collision coll = part_collision( here, p, dsp, just_detect, bash_floor, vertical );
```
`:764-765` (rotor sweep — same pass, so same `vertical`):
```cpp
                veh_collision rotor_coll = part_collision( here, p, here.get_abs( rotor_point ), just_detect,
                                           false, vertical );
```

- [ ] **Step 6: Fix the external call site in teleport**

`src/teleport.cpp:61` — a teleport is an instantaneous displacement, so classify by whether it crosses z:

```cpp
        veh_collision coll = veh.part_collision( *dest, part.part_index(), dp + rel_pos, true,
                             false, dp.z() != 0 );
```

- [ ] **Step 7: Rebuild and run the new test — verify it passes**

Run: `tests/cata_test "two_floor_bus_upper_deck_collides_horizontally"`
Expected: **PASS** — the upper deck now collides with the z=1 wall.

- [ ] **Step 8: Guard the single-floor no-op — run the full vehicle + ramp suites**

Run: `tests/cata_test "[vehicle]"` and `tests/cata_test "[ramp]"` (iterating), then the isolated batches per `build-scripts/gha_test_only.sh` before the PR.
Expected: **all green** — for single-floor vehicles `vertical == (p.z() != sm_pos.z())` held before, so this is a no-op there.

- [ ] **Step 9: astyle + commit**

```bash
# astyle the 3 changed C++ files (see Global Constraints), then:
git add src/vehicle.h src/vehicle_move.cpp src/teleport.cpp tests/vehicle_multifloor_test.cpp
git commit -m "fix(veh): classify collisions by move direction, not part z (multi-floor)"
```

---

### Task 2: Ground-contact invariant — `WHEEL` parts must sit on the ground deck

**Why:** The driving model's load-bearing assumption (design §5) is **"ground contact = `wheelcache` membership"**, not "z == 0". `wheelcache` is built in `refresh()` from `VPFLAG_WHEEL` (`src/vehicle.cpp:6894-6895`) with no z filter, and traction/rolling-resistance code (`:4614+`, `:5259`) sums wheels planarly. That is correct *as long as* no author puts a wheel on an upper deck. Add a cheap load-time guard so a future authoring mistake fails loudly instead of silently giving an upper-deck wheel phantom ground traction. Also lock the invariant with a test on the real bus.

**Files:**
- Modify: `src/veh_type.cpp` (`vpart_info::check()` — the same place the connector/flag validations debugmsg)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: nothing new. `vpart_info` exposes `has_flag( VPFLAG_WHEEL )`; part positions are validated per-vehicle at `install_part`/prototype finalize, but the *flag*-level guard lives on the part definition. Because `vpart_info` itself has no mount, the enforceable invariant at part-definition time is weak; enforce it where a vehicle prototype places the part instead (see Step 3).

- [ ] **Step 1: Write the failing test (positive invariant on the real bus)**

The clean, always-true statement is: *every wheel in the bus's `wheelcache` is on the ground deck.* Add to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "wheels_touch_ground_only_on_deck_zero", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );

    REQUIRE_FALSE( veh->wheelcache.empty() );
    for( const int w : veh->wheelcache ) {
        CAPTURE( w );
        CHECK( veh->part( w ).mount.z() == 0 );
    }
}
```

- [ ] **Step 2: Build, then run — verify it passes immediately**

Run: `tests/cata_test "wheels_touch_ground_only_on_deck_zero"`
Expected: **PASS** already (the bus authors all wheels at z=0). This test documents/locks the invariant. If `wheelcache` is empty here, the harness failed to build the vehicle — investigate before proceeding.

> This is a lock test, not TDD-red. The behavioral guard is Step 3; its "test" is the whole-JSON validation that `cata_test` runs at startup.

- [ ] **Step 3: Add the load-time guard**

Find the per-vehicle prototype finalize that iterates placed parts with their mounts (the same path M2 used to legalize `can_mount` — search `veh_type.cpp` for where a `vehicle_prototype`'s parts are checked, e.g. `finalize` / `check`). Where each part's `vpart_info` and its `point`/`tripoint` mount are both in scope, add:

```cpp
        if( vpi.has_flag( VPFLAG_WHEEL ) && part_mount.z() != 0 ) {
            debugmsg( "Vehicle prototype \"%s\": WHEEL part \"%s\" is mounted at z=%d; "
                      "wheels must be on the ground deck (z=0) — ground contact is wheelcache "
                      "membership.", proto_id.str(), vpi.id.str(), part_mount.z() );
        }
```

(Match the exact loop variable names present at that site. If prototype parts are still keyed 2D at that point, place the guard instead in `vehicle::install_part`'s validation, where the 3D `tripoint_rel_ms` mount is available — either location fires during `cata_test`'s JSON load.)

- [ ] **Step 4: Rebuild and run whole-JSON validation**

Run: `tests/cata_test "[multifloor]"` (its startup loads and validates all vehicle JSON).
Expected: **PASS**, no new debugmsg — core + `test_bus_2floor` have no z-mounted wheels.

- [ ] **Step 5: astyle + commit**

```bash
git add src/veh_type.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): assert wheels stay on the ground deck; lock invariant on test bus"
```

---

### Task 3: The mount.z × ramp precalc.z cross-term (the §6.3 cross-term the spec mandates)

**Why:** M3 added `precalc_z_composes_from_mount_z_across_rotations` (mount.z × rotation). The design §6.3 also requires the **ramp-displacement** cross-term: `precalc.z` must equal `mount.z + ramp_displacement` for `mount.z ∈ {0,1}` while a ramp displacement is in flight. `advance_precalc_mounts` (`src/vehicle.cpp:8867-8892`) applies the ramp displacement to `precalc[0].z()` and records `precalc_z_delta = precalc[0].z() - mount.z()`; `precalc_mounts` then reseeds `precalc.z = mount.z + precalc_z_delta` (`:3807`). This task asserts the composition holds for the upper deck, catching any place the ramp math assumes a z=0 base.

**Files:**
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `vehicle_part::mount` (3D), `vehicle_part::precalc[0]` (3D), `vehicle_part::precalc_z_delta` (transient ramp displacement above the mount deck).

- [ ] **Step 1: Write the test — drive the bus partway up a ramp, assert the deck offset holds**

The robust, engine-honest assertion is a *relative* one: **at every tick, each upper-deck part sits exactly one z above the lower-deck part sharing its (x,y) mount** — i.e. the deck gap is invariant to ramp displacement. Add to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "upper_deck_precalc_z_tracks_lower_deck_over_ramp", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();

    // Build a short up-ramp along the drive axis (mirror vehicle_ramp_test's terrain).
    const int y = 60;
    const int lowx = 66, highx = 67;
    here.ter_set( tripoint_bub_ms( lowx,  y, 0 ), ter_id( "t_ramp_up_low" ) );
    here.ter_set( tripoint_bub_ms( highx, y, 0 ), ter_id( "t_ramp_up_high" ) );
    here.ter_set( tripoint_bub_ms( lowx,  y, 1 ), ter_id( "t_ramp_down_low" ) );
    here.ter_set( tripoint_bub_ms( highx, y, 1 ), ter_id( "t_ramp_down_high" ) );

    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, y, 0 ), 90_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    veh->check_falling_or_floating();
    veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    veh->engine_on = true;
    veh->cruise_velocity = 400;
    veh->velocity = 400;

    auto deck_gap_holds = [&]() {
        // Map every occupied (x,y) mount to the set of z's present there.
        for( const vpart_reference &up : veh->get_all_parts() ) {
            const vehicle_part &pu = up.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            // find the lower-deck partner at the same (x,y)
            bool found = false;
            for( const vpart_reference &lo : veh->get_all_parts() ) {
                const vehicle_part &pl = lo.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ) {
                    continue;
                }
                if( pl.mount.xy() == pu.mount.xy() ) {
                    CAPTURE( pu.mount.xy() );
                    CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
                    found = true;
                }
            }
            CAPTURE( pu.mount.xy() );
            CHECK( found ); // every upper-deck tile has a lower-deck floor beneath it
        }
    };

    for( int cycle = 0; cycle < 8; cycle++ ) {
        CAPTURE( cycle );
        deck_gap_holds();
        here.vehmove();
    }
    deck_gap_holds();
}
```

- [ ] **Step 2: Build, then run the test**

Run: `tests/cata_test "upper_deck_precalc_z_tracks_lower_deck_over_ramp"`
Expected: this is the **discovery gate**. If M3's `precalc_z_delta` composition is fully correct it PASSES immediately. If it FAILS (deck gap collapses or explodes mid-ramp), the ramp block assumes a z=0 base somewhere.

- [ ] **Step 3 (conditional): Fix the composition if red**

If red, the fix is in `advance_precalc_mounts` (`src/vehicle.cpp:8867-8892`). The ramp block adds/subtracts to `precalc[0].z()` and then subtracts `ramp_offset` uniformly; verify those adjustments are applied **per-deck-relative**, not from an assumed 0. The invariant to restore: after the block, `precalc[0].z() - mount.z()` (i.e. `precalc_z_delta`) is **identical** for the upper and lower part of the same (x,y) column. Keep the single-floor path byte-identical (all `mount.z()==0`, so subtracting `mount.z()` changes nothing there). Re-run Step 2 to green, then the `[ramp]` suite to confirm the single-floor no-op.

- [ ] **Step 4: Commit**

```bash
git add tests/vehicle_multifloor_test.cpp   # + src/vehicle.cpp if Step 3 was needed
git commit -m "test(veh): lock upper/lower deck precalc.z gap across a ramp (mount.z × ramp cross-term)"
```

---

### Task 4: Drive the 2-floor bus on flat ground

**Why:** The stated end goal, easy case first. Mirrors `vehicle_ramp_test.cpp`'s harness but asserts multi-floor movement: both decks translate together every tick, the upper deck stays exactly one z above the lower deck, velocity holds, no skid, and the vehicle does not split.

**Files:**
- Test: `tests/vehicle_ramp_test.cpp` (reuses this file's `clear_game_and_set_ramp` with `use_ramp = false`)

**Interfaces:**
- Consumes: `here.add_vehicle`, `here.vehmove()`, `veh.get_all_parts()`, `veh.get_points()`, `vehicle_part::precalc[0]` (3D), `vehicle_part::mount` (3D). Add near the top of the file: `static const vproto_id vehicle_prototype_test_bus_2floor( "test_bus_2floor" );`.

- [ ] **Step 1: Write the failing/lock test**

Add to `tests/vehicle_ramp_test.cpp`:

```cpp
TEST_CASE( "two_floor_bus_drives_flat_keeping_deck_stack", "[vehicle][multifloor][ramp]" )
{
    map &here = get_map();
    clear_game_and_set_ramp( 75, /*use_ramp=*/false, /*up=*/false ); // flat pavement field

    const tripoint_bub_ms start( 79, 60, 0 );
    REQUIRE( here.ter( start ) == ter_id( "t_pavement" ) );
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_test_bus_2floor, start, 180_degrees, 1, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    REQUIRE_FALSE( veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    REQUIRE( veh.safe_velocity( here ) > 0 );

    const size_t parts_before = veh.part_count();

    for( int cycle = 0; cycle < 8; cycle++ ) {
        CAPTURE( cycle );
        here.vehmove();
        REQUIRE_FALSE( veh.skidding );
        CHECK( veh.velocity == target_velocity );
        // the vehicle stays whole (no spurious split while driving)
        CHECK( veh.part_count() == parts_before );
        // every upper-deck part is exactly one z above its lower-deck column partner,
        // and all lower-deck structure stays at the vehicle's own z-level.
        for( const vpart_reference &up : veh.get_all_parts() ) {
            const vehicle_part &pu = up.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            for( const vpart_reference &lo : veh.get_all_parts() ) {
                const vehicle_part &pl = lo.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ||
                    pl.mount.xy() != pu.mount.xy() ) {
                    continue;
                }
                CAPTURE( pu.mount.xy() );
                CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
            }
        }
    }
}
```

- [ ] **Step 2: Build, then run**

Run: `tests/cata_test "two_floor_bus_drives_flat_keeping_deck_stack"`
Expected: **PASS** (drives cleanly; deck stack preserved). If it fails on `part_count` (spurious split) or a collapsed deck gap, debug with `superpowers:systematic-debugging` — likely a cache-dirty-on-occupied-z issue carried from M3; fix minimally and note it.

- [ ] **Step 3: Commit**

```bash
git add tests/vehicle_ramp_test.cpp
git commit -m "test(veh): drive the 2-floor bus on flat ground, deck stack preserved"
```

---

### Task 5: Drive the 2-floor bus up and down a ramp — the roughest path

**Why:** Design §5 names this explicitly: a 2-floor bus on a ramp performs a simultaneous x/y + z transition and, mid-climb, **spans three z-levels at once** (rear lower deck at z0, rear upper / front lower at z1, front upper at z2). This is the roughest existing movement path (upstream #67712). It is the milestone's headline correctness test and where any remaining `precalc.z`/collision-split defect will surface.

**Files:**
- Test: `tests/vehicle_ramp_test.cpp`
- Possibly modify: `src/vehicle.cpp` (`advance_precalc_mounts` ramp block) and/or `src/vehicle_move.cpp` (`collision` z-split) if the test exposes a defect.

**Interfaces:**
- Consumes: the same movement API as Task 4, plus `clear_game_and_set_ramp( transition_x, /*use_ramp=*/true, up )` and the ramp terrain it lays down.

- [ ] **Step 1: Write the test — drive over the ramp, assert deck stack + z transition**

Add to `tests/vehicle_ramp_test.cpp`. This mirrors `ramp_transition_angled` but for the 2-floor bus and asserts the **deck stack invariant holds throughout**, plus the bus ends up one z-level higher, whole:

```cpp
static void drive_two_floor_bus_ramp( bool up )
{
    map &here = get_map();
    const int transition_x = 60;
    clear_game_and_set_ramp( transition_x, /*use_ramp=*/true, up );

    const tripoint_bub_ms start( transition_x + 4, 60, up ? 0 : 1 );
    REQUIRE( here.ter( start ) ); // terrain present at the start z
    vehicle *veh_ptr = here.add_vehicle( vehicle_prototype_test_bus_2floor, start, 180_degrees, 1, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    REQUIRE_FALSE( veh.is_in_water() );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    const int target_velocity = 400;
    veh.cruise_velocity = target_velocity;
    veh.velocity = target_velocity;
    REQUIRE( veh.safe_velocity( here ) > 0 );

    const size_t parts_before = veh.part_count();
    bool saw_base_z_change = false;
    const int base_z_start = veh.sm_pos.z();

    for( int cycle = 0; cycle < 10 && veh.safe_velocity( here ) > 0; cycle++ ) {
        CAPTURE( cycle );
        clear_creatures();
        here.vehmove();
        REQUIRE_FALSE( veh.skidding );
        // stays whole across the transition (no split when spanning 3 z-levels)
        CHECK( veh.part_count() == parts_before );
        // deck stack invariant holds even mid-ramp, per column
        for( const vpart_reference &pref : veh.get_all_parts() ) {
            const vehicle_part &pu = pref.part();
            if( pu.removed || pu.is_fake || pu.mount.z() != 1 ) {
                continue;
            }
            for( const vpart_reference &lref : veh.get_all_parts() ) {
                const vehicle_part &pl = lref.part();
                if( pl.removed || pl.is_fake || pl.mount.z() != 0 ||
                    pl.mount.xy() != pu.mount.xy() ) {
                    continue;
                }
                CAPTURE( pu.mount.xy() );
                CHECK( pu.precalc[0].z() == pl.precalc[0].z() + 1 );
            }
        }
        if( veh.sm_pos.z() != base_z_start ) {
            saw_base_z_change = true;
        }
    }
    // the bus actually changed z-level over the ramp
    CHECK( saw_base_z_change );
    CHECK( veh.part_count() == parts_before );
}

TEST_CASE( "two_floor_bus_drives_up_a_ramp", "[vehicle][multifloor][ramp]" )
{
    drive_two_floor_bus_ramp( /*up=*/true );
}

TEST_CASE( "two_floor_bus_drives_down_a_ramp", "[vehicle][multifloor][ramp]" )
{
    drive_two_floor_bus_ramp( /*up=*/false );
}
```

> Note on `sm_pos.z()` vs `pos_bub().z()`: assert the z transition via whichever the existing ramp test treats as the vehicle's z anchor after `vehmove` — cross-check against `ramp_transition_angled`'s `player_character.posz()`/`ppos.z()` usage and adapt the accessor if `sm_pos` isn't the right handle in this build.

- [ ] **Step 2: Build, then run both ramp-drive tests**

Run: `tests/cata_test "two_floor_bus_drives_up_a_ramp"` and `tests/cata_test "two_floor_bus_drives_down_a_ramp"`
Expected: the **de-risking gate**. Likely-failure modes and where to look:
- Deck gap collapses mid-ramp → `advance_precalc_mounts` ramp block (`src/vehicle.cpp:8881-8891`) applies the ramp `+1/-1` or `ramp_offset` without preserving the per-column `mount.z` base. Fix so `precalc_z_delta` is equal across a column (same fix locus as Task 3 Step 3).
- Spurious split (`part_count` drops) → the 3-z-span trips connectivity/`find_and_split_vehicles`; confirm the connector-gated `connected_neighbours()` helper (M3) tolerates a transient 3-level span. If a genuine M3 gap, fix minimally and record it in the PR body.
- Collision-driven stop on the ramp (a part hits ramp terrain it shouldn't) → revisit Task 1's `vertical` threading against the z-split in `vehicle::collision` (`:720-724`).

- [ ] **Step 3 (conditional): Apply the minimal production fix the test demands**

Use `superpowers:systematic-debugging` — reproduce, find root cause, fix at the deepest correct layer, keep the single-floor path byte-identical. After any `src/vehicle.cpp`/`vehicle_move.cpp` change, re-run `[ramp]` (single-floor) + `[vehicle]` to prove the no-op.

- [ ] **Step 4: astyle (if production changed) + commit**

```bash
git add tests/vehicle_ramp_test.cpp   # + src/*.cpp if a fix was needed
git commit -m "test(veh): drive the 2-floor bus over a ramp (3-z span); fix precalc.z composition if needed"
```

---

### Task 6: Document the planar-mass-center decision with a test

**Why:** Design §5 fixes that upper-deck mass folds into the **planar** (x,y) center — `calc_mass_center` (`src/vehicle.cpp:8657-8700`) accumulates only `.x()`/`.y()`, so "a 2-floor vehicle drives like a 1-floor vehicle of equal total mass/footprint." No production change; a test pins the decision so a later well-meaning "add z to the mass center" change trips a red test and a conversation, not silent physics drift.

**Files:**
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `vehicle::local_center_of_mass` / `vehicle::mass_center_no_precalc` accessor (confirm the exact public accessor name in `src/vehicle.h` near `mass_center_no_precalc` — use the existing public getter that returns the 2D center).

- [ ] **Step 1: Write the test — mass center is planar (a `point`, z absent by construction)**

Add to `tests/vehicle_multifloor_test.cpp`. The assertion is that the reported center is a 2D quantity whose value is unchanged by the presence of z=1 mass — expressed as: the center-of-mass X/Y of the bus equals that computed over the same parts ignoring z (trivially true, since the API is 2D). Concretely, pin that the accessor type/return is planar and the value is finite and inside the footprint:

```cpp
TEST_CASE( "two_floor_bus_mass_center_is_planar", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );

    // The center of mass is a 2D point by design (design §5): upper-deck mass folds
    // into the planar center; there is no z term. This asserts the decision holds.
    const point_rel_ms com = veh->local_center_of_mass( here ); // adapt to the real accessor
    // center lies within the footprint's x/y mount bounds (-1..2, 0..1 for the test bus)
    CHECK( com.x() >= -1 );
    CHECK( com.x() <=  2 );
    CHECK( com.y() >=  0 );
    CHECK( com.y() <=  1 );
}
```

> Adapt `local_center_of_mass` and its return type to the actual public accessor. If the only public getter returns a `point`/`tripoint` in a different space, assert on that instead — the point of the test is that **there is no z component participating in handling**, so if the accessor is a `tripoint`, additionally `CHECK` its z is 0 / ignored.

- [ ] **Step 2: Build, then run — verify it passes**

Run: `tests/cata_test "two_floor_bus_mass_center_is_planar"`
Expected: **PASS**. (Lock test; no production change.)

- [ ] **Step 3: Commit**

```bash
git add tests/vehicle_multifloor_test.cpp
git commit -m "test(veh): pin planar mass-center decision for multi-floor vehicles"
```

---

## Out of scope for M5 (carried / deferred — not gaps)

Called out so they aren't mistaken for omissions:
- **3D `loot_zones`/`labels` save-format keys** and **split z-base normalization** (carried from M3): only reachable on a *multi-floor split*, which M5's straight-line driving/ramp tests do not force. Keep the in-code single-floor-safe notes. Fold into M6 unless a Task 4/5 test unexpectedly triggers a cross-deck split (then fix minimally and note it).
- **Top-heavy / tip-over stability** and **`veh_interact` install-on-floor UX** — design §5/§milestones defer these to M6.
- **Open-top-deck shelter/"inside" semantics**, **monster AI targeting/melee onto z+1**, **NPC pathing to upper-deck seats**, **mapgen/loot spawn of z-part vehicles** — explicitly out of the vertical slice (design §"Explicitly out of the vertical slice").

## Self-Review

- **Spec §5 coverage:** ground contact = wheelcache (Task 2) ✓; mass center stays 2D (Task 6) ✓; collision correct across z incl. the H/V split (Task 1) ✓; 2-floor bus on a ramp (Task 5) ✓; stability deferred (documented) ✓.
- **Spec §6 testing:** composition matrix ramp cross-term (Task 3) ✓; "drive a 2-floor bus movement/collision test incl. ramp" (Tasks 4+5) ✓; whole-suite via `gha_test_only.sh` (Global Constraints) ✓.
- **Type consistency:** `part_collision(...)` gains one trailing `bool vertical` used identically at all three call sites (Task 1); `precalc[0].z()`, `mount.z()`, `mount.xy()`, `precalc_z_delta` used consistently across Tasks 3–5.
- **No placeholders:** every code step carries real code; conditional fix steps name the exact function + line locus (`advance_precalc_mounts` `src/vehicle.cpp:8867-8892`, `collision` z-split `src/vehicle_move.cpp:720-724`).
- **Known adaptation points flagged inline:** the mass-center accessor name (Task 6), the `sm_pos.z()` vs `pos_bub().z()` z anchor (Task 5), and the exact prototype-finalize loop variables (Task 2) are called out because they can't be verified without a compile — the implementer confirms against the real headers on first build.
