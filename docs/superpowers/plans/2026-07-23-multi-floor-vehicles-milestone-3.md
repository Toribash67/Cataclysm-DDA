# Multi-Floor Vehicles — Milestone 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the multi-floor vehicle from *authored-but-flat* into *genuinely 3D*: compose the final rendered z from `mount.z + ramp displacement`, make the bounding box / item / cargo sites z-aware, make part-removal and vehicle-splitting connectivity traverse z only through vertical connectors, and resolve/render the correct part per z-level.

**Architecture:** M1 widened `vehicle_part::mount` to `tripoint_rel_ms` (z pinned 0) and 3D-keyed the caches. M2 opened JSON authoring (`"z"`), added `VPFLAG_VERTICAL_CONNECTOR`, and ladder-gated `can_mount`. Through M2, `precalc.z` was deliberately left untouched (seeding it early breaks ramp tests). M3 flips the switch: `precalc_mounts` seeds `precalc.z = mount.z`; the ramp block in `advance_precalc_mounts` already composes incrementally on top of it. Everything that projected 3D mounts down to 2D (`.xy()`) or hardcoded `z == 0` is then audited and made true-3D, with deck connectivity gated by `VERTICAL_CONNECTOR` exactly as `can_mount` already is.

**Tech Stack:** C++17, Catch2 (`tests/cata_test`), CDDA JSON content, `generic_factory`. Build/test via the NAS Docker harness `.nas-build/cdda.sh`.

## Global Constraints

- **Baseline:** this fork's `master` on upstream 0.I "Ito"; M2 merged as `540131a2a6`. Branch off `master`.
- **Behavioral no-op for single-floor vehicles.** Every existing vehicle has `mount.z == 0` on every part. Seeding `precalc.z = mount.z` (= 0) and 3D-ifying the connectivity/bbox sites must be byte-identical in behavior for them. The entire existing `[vehicle]` suite — especially `vehicle_ramp_test_59/60/61` and `vehicle_level_test` — is the safety net and MUST stay green after every task.
- **`precalc.z` composition is the highest-risk surface (spec §1, Open Risks).** The composition lives across two sites that must agree: `precalc_mounts` (seeds the base z) and the ramp block in `advance_precalc_mounts` (adds the transient offset). Do not seed z in a way that assumes the ramp block resets from 0 — the ramp block is *incremental* and must keep composing on top of the seeded base.
- **Ladder-gated connectivity (design §1, option (a)).** Two parts stacked at `(x,y,0)` and `(x,y,1)` are the same vehicle ONLY through a `VERTICAL_CONNECTOR` part. A bare z-neighbour does **not** connect. Reuse `vehicle::has_vertical_connector_at( const tripoint_rel_ms & )` (added in M2) — do not invent a second rule.
- **Do NOT reuse `carried_part_data::mount`** (`src/vehicle.h:300`) / its `mount_z` serialization — that is the racking subsystem, a different axis of meaning. Preserve every existing `vp_flag::carried_flag` exclusion when 3D-ifying a structural check (dropping it silently changes carrier-vehicle behavior — this exact bug was caught in M2).
- **`coord_translate` still writes only x/y.** Do not add a z-write inside `coord_translate` — the z seed belongs in `precalc_mounts` (and only there), because `coord_translate` is also used for pure 2D projections (`mount_to_tripoint`, tow calculations) that must not acquire a spurious z.
- **Formatting:** C++ via astyle 3.1 (`.astylerc`); JSON via `make style-json`. Both are CI-gated (`astyle.yml`, `json.yml`). astyle 3.1 is unavailable in the harness image — hand-match the project style (spaces inside parens, `--align-pointer=name`, 4-space indent, 100 cols); CI is authoritative.
- **Build/test commands** (this machine has no host toolchain; apt is disabled — see the NAS build memory):
  - `.nas-build/cdda.sh build` — curses debug build (`obj/`, `cataclysm`)
  - `.nas-build/cdda.sh build-tiles` — tiles+sound build (`obj/tiles/`, `cataclysm-tiles`); **required from M3** because M3 touches rendering
  - `.nas-build/cdda.sh test "<case>"` — one Catch2 case
  - `.nas-build/cdda.sh suite` — full isolated suite (the milestone gate)
  - Never raise `JOBS` above 6 (15 GB RAM, zero swap). Switching flavor (curses↔tiles) is handled by the harness's flavor stamp.

## File map

| File | M3 responsibility |
|------|-------------------|
| `src/vehicle.cpp` | `precalc_mounts` z-seed; `refresh()` 3D bbox; `is_connected`/`can_unmount`/`find_and_split_vehicles`/`split_vehicles` z-aware; `part_displayed_at` 3D overload; audited `.xy()` sites (`place_spawn_items`, `refresh_insides`, `pldrive`). |
| `src/vehicle.h` | 3D overload declarations (`part_displayed_at`, `get_display_of_tile`); `new_mounts`/`split_mounts` type widen to `tripoint_rel_ms`. |
| `src/vehicle_display.cpp` | `get_display_of_tile` 3D overload. |
| `src/veh_type.cpp` | `cargo_spots` set → `tripoint_rel_ms` (finalize-time cargo validation). |
| `data/json/vehicles/custom_vehicles.json` | give `test_bus_2floor` an upper-deck cargo/loot part so item-placement paths are exercised. |
| `tests/vehicle_multifloor_test.cpp` | composition matrix; z-bbox; upper-deck cargo; 3D unmount/split cases. |
| `doc/JSON/VEHICLES_JSON.md` | document the z-composition + connectivity behavior now that it is live. |

---

### Task 1: Compose `precalc.z = mount.z` (+ composition matrix test)

The crux. `coord_translate` writes only x/y (`src/vehicle.cpp:3662-3679`), so `precalc_mounts` (`:3707`) must explicitly seed `precalc[idir].z() = mount.z()`. The ramp block in `advance_precalc_mounts` (`:8760-8781`) already applies its z deltas *incrementally* (`precalc[0] = precalc[1]`, `+= 1`, `-= ramp_offset`), so once the base is seeded it composes correctly with no further edit. For single-floor vehicles the seed is 0 → provable no-op → ramp tests prove it.

**Files:**
- Modify: `src/vehicle.cpp:3714-3726` (`precalc_mounts` loop)
- Test: `tests/vehicle_multifloor_test.cpp` (append)

**Interfaces:**
- Consumes: `vehicle_part::mount` (3D, M1); `precalc[idir]` is `tripoint_rel_ms` (already 3D).
- Produces:
  - `vehicle_part::precalc_z_delta` (new transient int): the ramp displacement *above the mount deck*, i.e. the invariant is **`precalc[idir].z() == mount.z() + precalc_z_delta`**.
  - post-`precalc_mounts` invariant **`part.precalc[idir].z() == part.mount.z() + part.precalc_z_delta`** (`== part.mount.z()` when not on a ramp, since the field defaults to 0).

> **DESIGN NOTE — why not just seed `precalc.z = mount.z` in `precalc_mounts`?**
> The first attempt at this task did exactly that and it broke `vehicle_ramp_test_59/60/61` + `vehicle_level_test`. Root cause (confirmed): `map::move_vehicle` (`src/map.cpp:808`) calls `precalc_mounts(1, …)` **every movement tick, mid-ramp**. Pre-M3, `coord_translate` left `precalc[1].z()` untouched, so it carried the ramp-elevated z that `advance_precalc_mounts` writes back each tick (`src/vehicle.cpp:8780`, `precalc[1].z() = precalc[0].z()`). The ramp displacement and the permanent deck-z have **no independent storage** — both live in `precalc.z` (the savegame `z_offset` field is just `precalc[0].z()` round-tripped, `savegame_json.cpp:3296/3362`). Unconditionally seeding `= mount.z()` each tick wipes the carried ramp climb. The fix separates the two sources into their own storage: `precalc.z = mount.z + precalc_z_delta`, where `advance_precalc_mounts` owns the delta and `precalc_mounts` rebuilds `precalc.z` from `mount.z + delta`. This touches the delicate ramp arithmetic by exactly **one added recording line** (no change to the existing z math), so it stays a provable no-op for single-floor vehicles (`mount.z==0` ⇒ `precalc_z_delta == precalc.z`, identical to today).

**Files:**
- Modify: `src/vehicle.h:460` (add `precalc_z_delta` after `precalc`)
- Modify: `src/vehicle.cpp:3719-3725` (`precalc_mounts` seed), `:8780` (`advance_precalc_mounts` record delta)
- Modify: `src/savegame_json.cpp:3291-3298` (load), `:3361-3363` (save)
- Modify: `tests/vehicle_ramp_test.cpp:272-278` (white-box `level_out` poke — maintain the new invariant, Step 7)
- Test: `tests/vehicle_multifloor_test.cpp` (append)

- [ ] **Step 1: Write the failing composition-matrix test**

Append to `tests/vehicle_multifloor_test.cpp` (the `#include`s and `vehicle_prototype_test_bus_2floor` already exist from M2). `real_parts()` is **private** (confirmed in the first attempt) — iterate the public `get_all_parts()` and skip removed parts:

```cpp
// M3 §6.3 composition matrix (mount side): after precalc_mounts, every part's
// precalc.z must equal its mount.z, across all 4 cardinal rotations and both
// decks (with no ramp displacement, precalc_z_delta == 0). This is the NEW logic
// M3 introduces; the ramp side (mount.z=0 x ramp) is covered by vehicle_ramp_test,
// and the cross term (mount.z=1 x ramp) lands with the driving test in M5.
TEST_CASE( "precalc_z_composes_from_mount_z_across_rotations", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Prove the bus actually has an upper deck, else the test is vacuous.
    bool saw_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            saw_upper = true;
            break;
        }
    }
    REQUIRE( saw_upper );

    for( const units::angle rot : {
                0_degrees, 90_degrees, 180_degrees, 270_degrees
            } ) {
        veh->precalc_mounts( 0, rot, point_rel_ms::zero );
        for( const vpart_reference &vpr : veh->get_all_parts() ) {
            const vehicle_part &vp = vpr.part();
            if( vp.removed ) {
                continue;
            }
            INFO( "rotation " << to_degrees( rot ) << " mount z " << vp.mount.z() );
            CHECK( vp.precalc[0].z() == vp.mount.z() );
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "precalc_z_composes_from_mount_z_across_rotations"`
Expected: FAIL — upper-deck parts (`mount.z == 1`) report `precalc[0].z() == 0` because `coord_translate` never wrote z and nothing seeds it yet.

- [ ] **Step 3: Add the `precalc_z_delta` field**

In `src/vehicle.h`, immediately after the `precalc` array (`:460`):

```cpp
        /** mount translated to face.dir [0] and turn_dir [1] */
        // NOLINTNEXTLINE(cata-use-named-point-constants)
        std::array<tripoint_rel_ms, 2> precalc = { { tripoint_rel_ms( -1, -1, 0 ), tripoint_rel_ms( -1, -1, 0 ) } };

        /** transient ramp displacement above the mount deck: precalc.z == mount.z + precalc_z_delta.
         *  0 unless the vehicle is mid-ramp. Persisted via the "z_offset" savegame field. */
        int precalc_z_delta = 0; // NOLINT(cata-serialize)
```

- [ ] **Step 4: Seed `precalc.z` from `mount.z + precalc_z_delta` in `precalc_mounts`**

In `src/vehicle.cpp`, the `precalc_mounts` loop (`:3719-3725`):

```cpp
        auto q = mount_to_precalc.find( p.mount );
        if( q == mount_to_precalc.end() ) {
            coord_translate( tdir, pivot, p.mount.xy(), p.precalc[idir] );
            // coord_translate writes only x/y. Compose the z from the permanent
            // deck (mount.z) and the transient ramp displacement (precalc_z_delta,
            // owned by advance_precalc_mounts). The memo key is the full 3D mount.
            p.precalc[idir].z() = p.mount.z() + p.precalc_z_delta;
            mount_to_precalc.insert( { p.mount, p.precalc[idir] } );
        } else {
            p.precalc[idir] = q->second;
        }
```

- [ ] **Step 5: Record the delta in `advance_precalc_mounts`**

In `src/vehicle.cpp`, the ramp block. Leave every existing `precalc[0].z()` arithmetic byte-for-byte unchanged; add ONE line right after the `precalc[1]` sync at `:8780`:

```cpp
        prt.precalc[0].z() -= ramp_offset;
        prt.precalc[1].z() = prt.precalc[0].z();
        // Record the ramp displacement above the mount deck so precalc_mounts can
        // rebuild precalc.z = mount.z + precalc_z_delta on later ticks/rotations
        // without clobbering an in-progress ramp climb.
        prt.precalc_z_delta = prt.precalc[0].z() - prt.mount.z();
        smzs.insert( prt.precalc[0].z() );
```

- [ ] **Step 6: Keep serialization consistent (`z_offset` now means the delta)**

`z_offset` must round-trip the ramp *displacement*, not the composed z, or a saved multi-floor vehicle would double-count `mount.z`. In `src/savegame_json.cpp` load (`:3291-3298`) — note `mount.z()` is already read at `:3282`:

```cpp
    if( data.has_int( "z_offset" ) ) {
        int z_offset = data.get_int( "z_offset" );
        if( std::abs( z_offset ) > 10 ) {
            data.throw_error_at( "z_offset", "z_offset out of range" );
        }
        precalc_z_delta = z_offset;
        precalc[0].z() = mount.z() + z_offset;
        precalc[1].z() = mount.z() + z_offset;
    }
```

Save (`:3361-3363`):

```cpp
    if( precalc[0].z() - mount.z() != 0 ) {
        json.member( "z_offset", precalc[0].z() - mount.z() );
    }
```

For single-floor vehicles `mount.z()==0`, so `z_offset` is byte-identical to before (`== precalc[0].z()`) — legacy saves and the M1 save→load round-trip test are unaffected.

- [ ] **Step 7: Maintain the new invariant in the white-box leveling test**

> **DESIGN NOTE — why this test edit is correct, not "gaming the test."** `vehicle_level_test`'s `level_out()` helper (`tests/vehicle_ramp_test.cpp:238`) simulates a vehicle mid-fall by **poking `precalc[i].z()` directly** on some parts (`:273-277`), bypassing the `precalc_mounts`/`advance_precalc_mounts` pipeline. That was fine when `precalc.z` was the sole store of the z-displacement. After this task, `precalc.z` carries the invariant `precalc.z == mount.z + precalc_z_delta`, and `map::move_vehicle` calls `precalc_mounts(1,…)` (which rebuilds `precalc.z` from the delta) *before* `level_vehicle` reads it. A poke that sets `precalc.z` without setting `precalc_z_delta` violates the invariant and is silently reverted by that rebuild. **This is a test-only issue:** a grep proves the *only* writers of `precalc.z` in production are `advance_precalc_mounts` (records the delta) and savegame load (sets the delta) — `vehicle::level_vehicle` only *reads* `precalc.z`, and real gameplay always reaches the split state *through* `advance_precalc_mounts`. So the fix is to make the white-box test maintain the invariant it now depends on; its assertions (vehicle lands at `z==0`) are unchanged.

In `tests/vehicle_ramp_test.cpp`, in `level_out()` (`:272-278`), set `precalc_z_delta` alongside each poke (the beetle is single-floor, `mount.z()==0`, so the delta equals the poked z):

```cpp
            if( drop_pos && prt->mount.x() < 0 ) {
                prt->precalc[0].z() = -1;
                prt->precalc[1].z() = -1;
                prt->precalc_z_delta = -1;
            } else if( !drop_pos && prt->mount.x() > 1 ) {
                prt->precalc[0].z() = 1;
                prt->precalc[1].z() = 1;
                prt->precalc_z_delta = 1;
            }
```

- [ ] **Step 8: Run the new test — expect PASS**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: all `[multifloor]` cases PASS (composition matrix green across both decks and 4 rotations).

- [ ] **Step 9: Prove the no-op — run the ramp/level regression**

Run: `.nas-build/cdda.sh test "[vehicle]"`
Expected: **all** `[vehicle]` cases pass, in particular `vehicle_ramp_test_59/60/61` and `vehicle_level_test`. For single-floor vehicles the delta equals `precalc.z` and the seed reproduces "leave z alone" exactly, so ramp climbs are unaffected. If any still fail: STOP and re-open `superpowers:systematic-debugging` — do not weaken the design.

- [ ] **Step 10: Commit**

```bash
git add src/vehicle.h src/vehicle.cpp src/savegame_json.cpp tests/vehicle_ramp_test.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat: compose vehicle precalc.z = mount.z + transient ramp delta"
```

---

### Task 2: z-aware bounding box + item/cargo projection audit

Now that parts genuinely live at z>0, the sites that flattened mounts to 2D or pinned z=0 must be audited. Two need real fixes (the `refresh()` bounding box, and finalize-time cargo validation); the item-spawn path needs a 3D lookup; the remaining `.xy()` projections are each either fixed or justified-as-planar-safe in a comment so a future reader does not re-flag them.

**Files:**
- Modify: `src/vehicle.cpp:6714-6741` (`refresh()` `mount_min`/`mount_max`), `:6383`/`:6431` region (`place_spawn_items`), `:7603` (`refresh_insides` roof lookup)
- Modify: `src/veh_type.cpp:1561`, `:1670`, `:1676` (`cargo_spots`)
- Modify: `data/json/vehicles/custom_vehicles.json` (`test_bus_2floor`: add an upper-deck cargo part)
- Test: `tests/vehicle_multifloor_test.cpp` (append)

**Interfaces:**
- Consumes: `part.mount` (3D); `parts_at_relative( const tripoint_rel_ms &, … )` (M1).
- Produces: `mount_min.z()` / `mount_max.z()` now hold the true deck range (0..1 for the bus, 0..0 for every existing vehicle). No new public function.

- [ ] **Step 1: Write the failing tests**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "refresh_bounding_box_tracks_upper_deck_z", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 1 );
}

TEST_CASE( "single_floor_vehicle_bounding_box_z_is_zero", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->refresh();
    // No-op guarantee: every shipped vehicle stays a single ground deck.
    CHECK( veh->mount_min_z() == 0 );
    CHECK( veh->mount_max_z() == 0 );
}
```

`mount_min`/`mount_max` are private (`src/vehicle.h:2371-2372`). Add tiny const accessors next to them so the test can read the z without widening access to the mutable fields:

```cpp
        int mount_min_z() const {
            return mount_min.z();
        }
        int mount_max_z() const {
            return mount_max.z();
        }
```

- [ ] **Step 2: Run to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "refresh_bounding_box_tracks_upper_deck_z"`
Expected: FAIL — `mount_max_z()` is 0 because `refresh()` hardcodes the z component of both corners to 0.

- [ ] **Step 3: Make the `refresh()` bounding box 3D**

In `src/vehicle.cpp`, initialise the z corners to a coverable range and let the per-part min/max include z. Replace `:6714-6715`:

```cpp
    mount_min = tripoint_rel_ms( 123, 123, 123 );
    mount_max = tripoint_rel_ms( -123, -123, -123 );
```

and replace the per-part update at `:6738-6741` — keep the x/y expressions exactly as they were; only the z argument (previously the literal `0`) changes:

```cpp
        mount_min = tripoint_rel_ms( std::min( mount_min.x(), pt.x() ),
                                     std::min( mount_min.y(), pt.y() ),
                                     std::min( mount_min.z(), pt.z() ) );
        mount_max = tripoint_rel_ms( std::max( mount_max.x(), pt.x() ),
                                     std::max( mount_max.y(), pt.y() ),
                                     std::max( mount_max.z(), pt.z() ) );
```

Then audit the empty-vehicle reset at `:6870` (`mount_min = mount_max = tripoint_rel_ms::zero;`) — that is already z=0 and correct, leave it. Every existing consumer of `mount_min`/`mount_max` reads only `.x()`/`.y()` (verified: `:811`, `:4595-4596`, `:4616-4630`, `:4944`, `:6865-6867`, `:7298-7302`), so the new z component is inert for them — a pure extension.

- [ ] **Step 4: Build and pass the bbox tests**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: both new bbox cases PASS; the single-floor case proves the no-op.

- [ ] **Step 5: Make finalize-time cargo validation 3D**

`src/veh_type.cpp:1561` declares `std::unordered_set<point_rel_ms> cargo_spots;`, filled at `:1670` via `cargo_spots.insert( pt.pos.xy() )` and queried at `:1676` via `cargo_spots.count( i.pos )`. With `pt.pos` now 3D (M2), flattening to `.xy()` makes an upper-deck cargo tile collide with a ground-deck tile at the same (x,y). Widen the set:

```cpp
        std::unordered_set<tripoint_rel_ms> cargo_spots;
```

```cpp
                cargo_spots.insert( pt.pos );
```

Confirm the query at `:1676` already compares against `i.pos` (3D) — if it reads `.xy()`, drop it:

```bash
sed -n '1674,1678p' src/veh_type.cpp
```

- [ ] **Step 6: Make item spawning resolve at the part's z**

`place_spawn_items` (`src/vehicle.cpp` around `:6383`/`:6431`) resolves the spawn part via a 2D lookup. Find the exact site:

```bash
grep -n "place_spawn_items" src/vehicle.cpp
grep -n "mount.xy()\|part_with_feature" src/vehicle.cpp | sed -n '1,40p'
```

At the `place_spawn_items` body, the part-with-feature / active-items insert use `vp.mount.xy()`. `active_items.add(...)` is keyed by 2D point (`:6323`, `:6383`) — that cache is planar by design and safe to keep. The site that must become 3D is any `part_with_feature( vp.mount.xy(), … )` used to *decide which tile gets the loot*: pass the full mount so upper-deck cargo parts receive their spawn. Change that specific call to the 3D `parts_at_relative`/`part_with_feature` overload (a `tripoint_rel_ms` overload exists from M1). If `part_with_feature` has no 3D overload, add the one-line delegating overload next to its 2D sibling:

```bash
grep -n "int vehicle::part_with_feature" src/vehicle.cpp
```

Keep the `active_items` planar key as-is and add a comment:

```cpp
        // active_items is a planar (x,y) cache; upper-deck items still index by
        // their x/y column here. The part is selected 3D above via mount.
```

- [ ] **Step 7: Add an upper-deck cargo part to the test bus and test loot placement**

In `data/json/vehicles/custom_vehicles.json`, add a cargo part to one `z:1` tile of `test_bus_2floor` (so the spawn path is exercised on the upper deck). Pick a real cargo id:

```bash
grep -rn '"id": "box_small"\|"id": "cargo_space"\|"id": "trunk"' data/json/vehicleparts/ | head
```

Change the upper-deck line, e.g.:

```json
      { "x": 1, "y": 1, "z": 1, "parts": [ "deck_floor", "seat", "cargo_space" ] }
```

Append the test:

```cpp
TEST_CASE( "two_floor_bus_upper_deck_can_carry_cargo", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int cargo = veh->part_with_feature( tripoint_rel_ms( 1, 1, 1 ), "CARGO", false );
    CHECK( cargo >= 0 );
}
```

- [ ] **Step 8: Format JSON, build, run**

Run: `.nas-build/cdda.sh run make style-json && .nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: all `[multifloor]` PASS, no `debugmsg` about the bus prototype (finalize still validates it).

- [ ] **Step 9: Audit the remaining `.xy()` projections (fix-or-justify)**

For each site below, either it becomes 3D or it gets a one-line comment stating why planar is correct. These are NOT split/connectivity sites (Tasks 3-4 own those) — they are the leftover projections:
- `refresh_insides` roof lookup `part_with_feature( vp.mount.xy(), "ROOF", true )` (`:7603`): a part's roof is on the same tile column; but with two decks the roof that matters is at the part's own z. Make it 3D: `part_with_feature( vp.mount, "ROOF", true )`.
- `pldrive`/steering `part_with_feature( wheel.mount.xy(), "STEERABLE", … )` (`:7122`): wheels are ground-deck (`mount.z==0`); keep planar and comment `// wheels are ground-deck only (mount.z==0)`.
- `add_fake_part(...).mount.xy()` (`:6935`), `loot_zones` (`:2378-2395`), tow (`:7299-7342`): comment each `// planar cache; upper-deck support deferred to later milestone` unless trivially 3D. Do not expand scope — a comment is an acceptable resolution for a driving/appliance-only path.

Run the audit sweep and record the disposition of each in the commit message:

```bash
grep -n "mount.xy()" src/vehicle.cpp
```

- [ ] **Step 10: Regression + commit**

Run: `.nas-build/cdda.sh test "[vehicle]"`
Expected: all pass.

```bash
git add src/vehicle.cpp src/vehicle.h src/veh_type.cpp data/json/vehicles/custom_vehicles.json tests/vehicle_multifloor_test.cpp
git commit -m "feat: z-aware vehicle bounding box, cargo validation, and item spawn"
```

---

### Task 3: z-aware connectivity for part removal (`is_connected` / `can_unmount`)

`can_unmount` (`src/vehicle.cpp:1416`) and its BFS helper `is_connected` (`:1516`) flood-fill over `four_adjacent_offsets` keyed on `mount.xy()` (2D). For a two-floor vehicle this treats both decks as one plane: removing a ground structure part could wrongly report the upper deck as still connected (a phantom planar path), or wrongly report a split. Make the BFS 3D, traversing z **only** through a `VERTICAL_CONNECTOR`.

**Files:**
- Modify: `src/vehicle.cpp:1466-1475` (`can_unmount` adjacency gather), `:1516-1564` (`is_connected` BFS)
- Test: `tests/vehicle_multifloor_test.cpp` (append)

**Interfaces:**
- Consumes: `has_vertical_connector_at( const tripoint_rel_ms & )` (M2); `parts_at_relative( const tripoint_rel_ms &, bool )` (M1).
- Produces: `is_connected` now works in `tripoint_rel_ms` space; vertical edges exist only where a connector legalises them (matching `can_mount`).

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "upper_deck_disconnects_when_connector_removed", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // The ladder at (0,0,0) is the ONLY vertical link to the upper deck.
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_CONNECTOR", false );
    REQUIRE( ladder >= 0 );

    // An upper-deck structure part and a ground structure part are connected
    // only via that ladder; with the ladder excluded, no path exists.
    const int upper = veh->part_at_relative_structure( tripoint_rel_ms( 0, 0, 1 ) );
    const int ground = veh->part_at_relative_structure( tripoint_rel_ms( 1, 0, 0 ) );
    REQUIRE( upper >= 0 );
    REQUIRE( ground >= 0 );

    CHECK( !veh->is_connected( veh->part( upper ), veh->part( ground ),
                               veh->part( ladder ) ) );
}
```

`is_connected` is private; expose it to the test the same way the codebase already does for other internals, or make the test a `friend` — check how existing `[vehicle]` tests reach private members:

```bash
grep -n "friend class\|FRIEND\|TEST_CASE.*vehicle" tests/vehicle_test.cpp | head
```

If there is no friend mechanism, promote the assertion to go through `can_unmount( vp_ladder, /*allow_splits=*/false )` (public) instead: removing the ladder with splits disallowed must FAIL once the upper deck can only reach ground through it. Replace the `CHECK` with:

```cpp
    CHECK( !veh->can_unmount( veh->part( ladder ), false ).success() );
```

and drop the direct `is_connected` reference. Also drop the `part_at_relative_structure` helper if it does not exist — use `parts_at_relative(...).front()` guarded by a structure check, or add a small test-local lambda.

- [ ] **Step 2: Run to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "upper_deck_disconnects_when_connector_removed"`
Expected: FAIL — the 2D BFS finds a phantom planar path (it flattens z), so it reports the decks connected and `can_unmount` succeeds.

- [ ] **Step 3: Make `is_connected` a 3D BFS with connector-gated vertical edges**

Rewrite `src/vehicle.cpp:1516-1564`. Key the queue/visited on `tripoint_rel_ms`; keep the four planar neighbours at the same z; add up/down neighbours gated by a connector on the *lower* tile:

```cpp
bool vehicle::is_connected( const vehicle_part &to, const vehicle_part &from,
                            const vehicle_part &excluded_part ) const
{
    const tripoint_rel_ms target = to.mount;
    const tripoint_rel_ms excluded = excluded_part.mount;

    std::queue<tripoint_rel_ms> queue;
    std::unordered_set<tripoint_rel_ms> visited;

    queue.push( from.mount );
    visited.insert( from.mount );
    while( !queue.empty() ) {
        const tripoint_rel_ms current_pt = queue.front();
        queue.pop();

        // Planar edges (same z) plus connector-gated vertical edges. A vertical
        // edge current<->above exists only if a VERTICAL_CONNECTOR sits on the
        // lower of the two tiles (same rule as can_mount).
        std::vector<tripoint_rel_ms> neighbours;
        for( const point &offset : four_adjacent_offsets ) {
            neighbours.emplace_back( current_pt + tripoint_rel_ms( offset.x, offset.y, 0 ) );
        }
        if( has_vertical_connector_at( current_pt ) ) {
            neighbours.emplace_back( current_pt + tripoint_rel_ms::above );
        }
        const tripoint_rel_ms below = current_pt + tripoint_rel_ms::below;
        if( has_vertical_connector_at( below ) ) {
            neighbours.push_back( below );
        }

        for( const tripoint_rel_ms &next : neighbours ) {
            if( next == target ) {
                return true; // found a path, bail out early from BFS
            }
            if( next == excluded ) {
                continue; // can't traverse excluded tile
            }
            const std::vector<int> parts_there = parts_at_relative( next, false );
            if( parts_there.empty() ) {
                continue; // can't traverse empty tiles
            }
            // 2022-08-27 assuming structure part is on 0th index is questionable
            // but it worked before so...
            const vehicle_part &vp_next = parts[ parts_there[ 0 ] ];
            if( vp_next.info().location != part_location_structure ||
                vp_next.info().has_flag( "PROTRUSION" ) ||
                vp_next.has_flag( vp_flag::carried_flag ) ) {
                continue; // can't connect if it's not a structure
            }
            if( visited.insert( vp_next.mount ).second ) {
                queue.push( vp_next.mount );
            }
        }
    }
    return false;
}
```

Confirm `tripoint_rel_ms::above` / `::below` exist (M2 used `::below`):

```bash
grep -n "above\|below" src/coordinates.h | grep tripoint | head
```

- [ ] **Step 4: Make the `can_unmount` adjacency gather 3D**

In `can_unmount` (`:1466-1475`), the "find adjacent tiles" loop projects to `.xy()` and forces z=0 (`:1470`). Gather the true 3D neighbours (planar + connector-gated vertical) so a two-tile-wide upper deck's split check runs on the real adjacency:

```cpp
    // find all the vehicle's tiles adjacent to the one we're removing
    std::vector<vehicle_part> adjacent_parts;
    std::vector<tripoint_rel_ms> neighbour_mounts;
    for( const point &offset : four_adjacent_offsets ) {
        neighbour_mounts.emplace_back( vp_to_remove.mount + tripoint_rel_ms( offset.x, offset.y, 0 ) );
    }
    if( has_vertical_connector_at( vp_to_remove.mount ) ) {
        neighbour_mounts.emplace_back( vp_to_remove.mount + tripoint_rel_ms::above );
    }
    const tripoint_rel_ms below_removed( vp_to_remove.mount + tripoint_rel_ms::below );
    if( has_vertical_connector_at( below_removed ) ) {
        neighbour_mounts.push_back( below_removed );
    }
    for( const tripoint_rel_ms &np : neighbour_mounts ) {
        const std::vector<int> parts_over_there = parts_at_relative( np, false );
        if( !parts_over_there.empty() ) {
            //Just need one part from the square to track the x/y
            adjacent_parts.push_back( parts[parts_over_there[0]] );
        }
    }
```

- [ ] **Step 5: Build, run new + regression**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]" && .nas-build/cdda.sh test "[vehicle]"`
Expected: the new case PASSES; all `[vehicle]` cases (which include multi-part removal/split tests on single-floor vehicles) stay green — planar behavior is unchanged because for a z=0-only vehicle no connector edges are ever added.

- [ ] **Step 6: Commit**

```bash
git add src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat: z-aware, connector-gated part-removal connectivity"
```

---

### Task 4: z-aware vehicle splitting (`find_and_split_vehicles` / `split_vehicles` / `new_mounts`)

When a collision or removal disconnects a vehicle, `find_and_split_vehicles` (`:2586`) flood-fills structure over `four_adjacent_offsets` keyed on `mount.xy()` (`:2632-2634`) and hands `split_vehicles` (`:2687`) mount lists typed `std::vector<point_rel_ms>` (2D, `src/vehicle.h:1176`). A two-floor split would drop z and reassemble the pieces flat. Make the flood-fill traverse z through connectors and carry z through the split.

**Files:**
- Modify: `src/vehicle.h:1168-1176` (`new_mounts`/`split_vehicles` signature), 
- Modify: `src/vehicle.cpp:2233`, `:2508-2541`, `:2632-2634`, `:2659-2701`, `:2730-2767` (split machinery)
- Test: `tests/vehicle_multifloor_test.cpp` (append)

**Interfaces:**
- Consumes: 3D `is_connected` neighbour rule from Task 3; `has_vertical_connector_at`.
- Produces: `split_vehicles(..., const std::vector<std::vector<tripoint_rel_ms>> &new_mounts, ...)`; `find_and_split_vehicles` flood-fill is 3D and connector-gated.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`. The cleanest black-box assertion: after removing the sole connector with splits allowed, the upper deck becomes a separate vehicle (or wreckage), so the original vehicle no longer reports any `mount.z == 1` part.

```cpp
TEST_CASE( "removing_connector_splits_off_upper_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_CONNECTOR", false );
    REQUIRE( ladder >= 0 );

    // Force the split: remove the only vertical link.
    veh->remove_part( ladder );
    veh->find_and_split_vehicles( here, {} );

    bool still_has_upper = false;
    for( const vehicle_part &vp : veh->real_parts() ) {
        if( vp.mount.z() == 1 && !vp.removed ) {
            still_has_upper = true;
            break;
        }
    }
    CHECK_FALSE( still_has_upper );
}
```

Confirm `remove_part` / `real_parts` signatures; adapt if needed:

```bash
grep -n "void vehicle::remove_part\|int vehicle::remove_part\|real_parts" src/vehicle.cpp src/vehicle.h | head
```

If `remove_part` needs a `map&`, pass `here`. If splitting requires the parts be physically disconnected first (the upper deck must have no *planar* path either), the bus geometry already guarantees that — the only link between decks is the ladder.

- [ ] **Step 2: Run to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "removing_connector_splits_off_upper_deck"`
Expected: FAIL — the 2D flood-fill flattens z, treats the decks as one plane, finds the upper deck "adjacent" to ground, and never splits it off.

- [ ] **Step 3: Widen the split mount type to 3D**

In `src/vehicle.h`, change the `split_vehicles` signature and the doc comment (`:1168-1176`):

```cpp
        bool split_vehicles( map &here, const std::vector<std::vector <int>> &new_vehs,
                             const std::vector<vehicle *> &new_vehicles,
                             const std::vector<std::vector<tripoint_rel_ms>> &new_mounts,
                             std::vector<vehicle *> *added_vehicles = nullptr );
```

- [ ] **Step 4: Update every `split_vehicles` caller and the `new_mounts` locals**

The three call sites and their `null_mounts`/`new_mounts` locals must become `tripoint_rel_ms`:
- `:2233` `const std::vector<std::vector<point_rel_ms>> null_mounts(...)` → `tripoint_rel_ms`.
- `:2508` `std::vector<point_rel_ms> new_mounts;` → `tripoint_rel_ms`; `:2534` `new_mounts.push_back( mount.xy() );` → `new_mounts.push_back( mount );` (carry z).
- `:2659` `const std::vector<std::vector<point_rel_ms>> null_mounts(...)` → `tripoint_rel_ms`.
- In `split_vehicles` body: `:2701` `std::vector<point_rel_ms> split_mounts = new_mounts[ i ];` → `tripoint_rel_ms`; and `:2730` `mnt_offset = parts[ split_part0 ].mount.xy();` / `:2767` `point_rel_ms cur_mount = vp_mov.mount.xy();` must carry z. Read the full `split_vehicles` body and widen each mount local that feeds `mount =` assignments, keeping any genuinely-planar offset math on `.xy()`:

```bash
sed -n '2687,2820p' src/vehicle.cpp
```

The rule: anything that ends up assigned into a `vehicle_part::mount` must be 3D and preserve z; pure 2D offset arithmetic (pivot/anchor) may stay `.xy()` but the z must be re-attached before the mount write.

- [ ] **Step 5: Make the `find_and_split_vehicles` flood-fill 3D + connector-gated**

In `:2632-2649`, replace the planar neighbour loop with the same planar+connector-gated neighbour set used in Task 3:

```cpp
            std::vector<tripoint_rel_ms> neighbour_mounts;
            const tripoint_rel_ms cur = parts[test_part].mount;
            for( const point &offset : four_adjacent_offsets ) {
                neighbour_mounts.emplace_back( cur + tripoint_rel_ms( offset.x, offset.y, 0 ) );
            }
            if( has_vertical_connector_at( cur ) ) {
                neighbour_mounts.emplace_back( cur + tripoint_rel_ms::above );
            }
            const tripoint_rel_ms cur_below( cur + tripoint_rel_ms::below );
            if( has_vertical_connector_at( cur_below ) ) {
                neighbour_mounts.push_back( cur_below );
            }
            for( const tripoint_rel_ms &dp : neighbour_mounts ) {
                std::vector<int> all_neighbor_parts = parts_at_relative( dp, true );
                int neighbor_struct_part = -1;
                for( const int p : all_neighbor_parts ) {
                    const vehicle_part &vp_neighbor = parts[p];
                    if( vp_neighbor.removed ) {
                        continue;
                    }
                    if( vp_neighbor.info().location == part_location_structure ) {
                        neighbor_struct_part = p;
                        break;
                    }
                }
                if( neighbor_struct_part != -1 ) {
                    push_neighbor( neighbor_struct_part, all_neighbor_parts );
                }
            }
```

- [ ] **Step 6: Build, run new + full vehicle regression**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]" && .nas-build/cdda.sh test "[vehicle]"`
Expected: the split case PASSES; all `[vehicle]` split/merge/wreck tests stay green (single-floor vehicles add no vertical edges, so the flood-fill is identical to before for them). Pay special attention to any `split`/`unmount`/`wreck` named cases.

- [ ] **Step 7: Commit**

```bash
git add src/vehicle.cpp src/vehicle.h tests/vehicle_multifloor_test.cpp
git commit -m "feat: z-aware, connector-gated vehicle splitting"
```

---

### Task 5: Rendering z-resolution, cross-z cache invalidation, and milestone gate

`part_displayed_at` (`:3599`) and `get_display_of_tile` (`src/vehicle_display.cpp:61`) take a 2D `point_rel_ms` and look up `parts_at_relative( tripoint_rel_ms( dp, 0 ), … )` — they can only ever resolve the ground deck. Add 3D overloads so a caller drawing the upper deck (when the avatar is on it) selects the upper-deck part. Also dirty the floor/transparency caches for **every** z the vehicle occupies, not just the base submap z. Per spec §3, the success criterion is "the upper deck is a solid, correctly-drawn space when you're on it; from below you see its roof" — not both decks at once.

**Files:**
- Modify: `src/vehicle.h:1429`, `:1443` (add 3D overloads)
- Modify: `src/vehicle.cpp:3599-3642` (`part_displayed_at` 3D body + 2D shim), `:2337-2341` (cache dirty loop)
- Modify: `src/vehicle_display.cpp:61` (`get_display_of_tile` 3D overload)
- Modify: `doc/JSON/VEHICLES_JSON.md`
- Test: `tests/vehicle_multifloor_test.cpp` (append)

**Interfaces:**
- Consumes: `parts_at_relative( const tripoint_rel_ms &, bool, bool )` (M1); `part.mount.z()`.
- Produces:
  - `int vehicle::part_displayed_at( const tripoint_rel_ms &dp, bool include_fake, bool below_roof, bool roof ) const` (3D); the existing 2D overload delegates with z=0.
  - `vpart_display vehicle::get_display_of_tile( const tripoint_rel_ms &dp, … ) const` (3D); 2D delegates with z=0.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "part_displayed_at_resolves_per_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Ground deck at (0,0,0) shows a ground structure part; upper deck at
    // (0,0,1) shows the deck floor. They must resolve to DIFFERENT parts.
    const int ground = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 0 ), false, true, false );
    const int upper  = veh->part_displayed_at( tripoint_rel_ms( 0, 0, 1 ), false, true, false );
    REQUIRE( ground >= 0 );
    REQUIRE( upper >= 0 );
    CHECK( ground != upper );
    CHECK( veh->part( upper ).mount.z() == 1 );
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "part_displayed_at_resolves_per_deck"`
Expected: compile error — no `part_displayed_at` overload takes `tripoint_rel_ms`.

- [ ] **Step 3: Add the 3D `part_displayed_at` body + 2D shim**

In `src/vehicle.h`, alongside `:1429`:

```cpp
        int part_displayed_at( const point_rel_ms &dp, bool include_fake = false,
                               bool below_roof = true, bool roof = false ) const;
        int part_displayed_at( const tripoint_rel_ms &dp, bool include_fake = false,
                               bool below_roof = true, bool roof = false ) const;
```

In `src/vehicle.cpp`, add a 2D shim and change the body signature at `:3599` to take `const tripoint_rel_ms &dp`, and use `dp` directly in the `parts_at_relative` call (drop the `tripoint_rel_ms( dp, 0 )` wrap at `:3608`):

```cpp
int vehicle::part_displayed_at( const point_rel_ms &dp, bool include_fake, bool below_roof,
                                bool roof ) const
{
    return part_displayed_at( tripoint_rel_ms( dp, 0 ), include_fake, below_roof, roof );
}

int vehicle::part_displayed_at( const tripoint_rel_ms &dp, bool include_fake, bool below_roof,
                                bool roof ) const
{
    // Z-order is implicitly defined in game::load_vehiclepart ...
    const int ON_ROOF_Z = 9;

    std::vector<int> parts_in_square = parts_at_relative( dp, true, include_fake );
    // ... rest of the body unchanged ...
```

- [ ] **Step 4: Add the 3D `get_display_of_tile` overload**

In `src/vehicle.h`, alongside `:1443`:

```cpp
        vpart_display get_display_of_tile( const point_rel_ms &dp, bool rotate = true,
                                           bool include_fake = true, bool below_roof = true,
                                           bool roof = false ) const;
        vpart_display get_display_of_tile( const tripoint_rel_ms &dp, bool rotate = true,
                                           bool include_fake = true, bool below_roof = true,
                                           bool roof = false ) const;
```

In `src/vehicle_display.cpp`, add a 2D shim and change the existing body to take `const tripoint_rel_ms &dp`. The body already forwards `dp` to `part_displayed_at` and uses `dp.x()`/`dp.y()` only in the debug string and `part_with_feature( dp, … )` — the 3D `part_with_feature` overload from M1/Task 2 handles it:

```cpp
vpart_display vehicle::get_display_of_tile( const point_rel_ms &dp, bool rotate,
        bool include_fake, bool below_roof, bool roof ) const
{
    return get_display_of_tile( tripoint_rel_ms( dp, 0 ), rotate, include_fake, below_roof, roof );
}

vpart_display vehicle::get_display_of_tile( const tripoint_rel_ms &dp, bool rotate,
        bool include_fake, bool below_roof, bool roof ) const
{
    const int part_idx = part_displayed_at( dp, include_fake, below_roof, roof );
    // ... rest unchanged; debugmsg keeps dp.x()/dp.y() ...
```

- [ ] **Step 5: Dirty floor/transparency caches across every occupied z**

At `src/vehicle.cpp:2337-2341`, the cache-dirty calls use `sm_pos.z()` (the base submap z) and `sm_pos.z() + 1`. A vehicle occupying z and z+1 must dirty the floor cache at z+1 **and** z+2 (the upper deck's roof), and transparency at both z and z+1. Read the surrounding function to see what `sm_pos` is and whether it iterates parts:

```bash
sed -n '2320,2345p' src/vehicle.cpp
```

Extend the dirty range using the bounding box from Task 2:

```cpp
        // A multi-floor vehicle occupies sm_pos.z() .. sm_pos.z()+mount_max.z().
        // Floor caches above each occupied level (and the roof above the top
        // deck) must be invalidated, plus transparency on each occupied level.
        for( int dz = mount_min.z(); dz <= mount_max.z(); dz++ ) {
            handler.set_transparency_cache_dirty( sm_pos.z() + dz );
            handler.set_floor_cache_dirty( sm_pos.z() + dz + 1 );
        }
```

Apply the analogous widening to the other three dirty sites (`:2103`, `:2841`, and any in this function) only where the code is per-vehicle (not per-part at a known z). If a site is already inside a per-part loop with the part's precalc z available, dirty `precalc.z()`-relative instead. Keep single-floor behavior identical: for `mount_min.z()==mount_max.z()==0` the loop runs exactly once, reproducing the old `sm_pos.z()` / `sm_pos.z()+1` calls.

- [ ] **Step 6: Build (curses), run rendering test + regression**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]" && .nas-build/cdda.sh test "[vehicle]"`
Expected: all PASS.

- [ ] **Step 7: Document the now-live composition + rendering**

In `doc/JSON/VEHICLES_JSON.md`, extend the `"z"` documentation added in M2 with the runtime behavior:

```markdown
As of the multi-floor work, a part authored at `"z": 1` renders on the upper
deck: its final on-screen z is `mount.z` composed with any ramp displacement the
vehicle currently has. From the ground deck you see the underside/roof of the
deck above; the upper deck is drawn as a solid space when the avatar is standing
on it. Parts on different decks are connected — for splitting, removal, and
rendering — only through a `VERTICAL_CONNECTOR` part directly below.
```

- [ ] **Step 8: Milestone gate — full isolated suite**

Run: `.nas-build/cdda.sh suite`
Expected: all batches green (both `[slow]` and `~[slow]`). A single `cata_test` call is **not** sufficient (order-dependent `[monster]` tests cross-contaminate). If the harness reports a `utf8_to_wstr`/`W_NO_PADDING_widget_flag` abort, that is the C-locale harness trap, not a regression — confirm `LANG=LC_ALL=C.UTF-8` is set (see the NAS build memory).

- [ ] **Step 9: Tiles flavor gate**

Run: `RELEASE=1 .nas-build/cdda.sh build-tiles`
Expected: `cataclysm-tiles` links clean. M3 is the first milestone touching rendering, so this must pass before merge.

- [ ] **Step 10: Format check + commit + push**

astyle 3.1 is unavailable locally; review the diff for project style. Then:

```bash
git diff --stat
git add src/vehicle.cpp src/vehicle.h src/vehicle_display.cpp doc/JSON/VEHICLES_JSON.md tests/vehicle_multifloor_test.cpp
git commit -m "feat: per-deck vehicle part rendering and cross-z cache invalidation"
git push -u origin explore/multi-floor-vehicles-m3
```

---

## Milestone exit criteria

1. `precalc.z == mount.z` after `precalc_mounts` for every part, across 4 rotations and both decks; ramp/level tests still green (composition no-op for single-floor).
2. `refresh()` bounding box, finalize-time cargo validation, and item spawning are z-aware; every existing vehicle stays `mount z-range == [0,0]`.
3. Part-removal connectivity (`is_connected`/`can_unmount`) traverses z only through `VERTICAL_CONNECTOR`; removing the sole connector fails an all-connected `can_unmount`.
4. Vehicle splitting (`find_and_split_vehicles`/`split_vehicles`/`new_mounts`) is z-aware and connector-gated; removing the connector splits off the upper deck.
5. `part_displayed_at`/`get_display_of_tile` resolve the correct part per z; floor/transparency caches dirty every occupied level.
6. Full suite green via `.nas-build/cdda.sh suite`; tiles flavor links; docs updated.

## Explicitly NOT in M3 (deferred)

- **Deck-to-deck traversal** (`game::vertical_move`/`has_floor` recognizing a `VERTICAL_CONNECTOR`) and **gravity-check on upper-floor destruction** — **M4** (the shippable stationary-livable-deck checkpoint).
- **Driving physics / collisions across z / mass center / the mount.z×ramp cross-term composition test** — **M5**. The composition matrix's ramp cross-term (mount.z=1 × ramp=±1) is completed there with a "drive a 2-floor bus up a ramp" test; M3 covers the mount side and preserves the existing ramp (z=0) side.
- **`veh_interact` install-on-floor UX**, top-heavy stability — **M6**.
- Monster AI / NPC pathing to the upper deck; mapgen loot spawning of z-part vehicles — deferred by design (spec "Explicitly out of the vertical slice").
