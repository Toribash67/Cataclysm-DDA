# Multi-Floor Vehicles — Milestone 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let vehicle JSON author parts on an upper deck via an optional `"z"` field, gated by a new vertical-connector (ladder) flag, and ship a two-floor test bus that loads and validates through the existing test suite.

**Architecture:** Milestone 1 already widened `vehicle_part::mount` to `tripoint_rel_ms` (z pinned 0) and made `parts_at_relative`, `relative_parts`, and `edges` 3D-keyed. M2 opens the *authoring* path: `part_def::pos` becomes 3D, the prototype parser reads `"z"`, and `can_mount`/`install_part` gain `tripoint_rel_ms` overloads. The existing 2D overloads are retained and delegate with `z = 0`, so all 57 existing call sites are untouched. A new `VPFLAG_VERTICAL_CONNECTOR` gates upward growth: a part at `z > 0` is only mountable if supported at its own z or sitting directly above a connector.

**Tech Stack:** C++17, Catch2 (`tests/cata_test`), CDDA JSON content, `generic_factory`. Build/test via the NAS Docker harness `.nas-build/cdda.sh`.

## Global Constraints

- **Baseline:** this fork's `master` on upstream 0.I "Ito"; M1 merged as `a250f09f1a`.
- **Behavioral no-op for existing content:** every currently-shipped vehicle omits `"z"` and must remain byte-identical in behavior (ground deck, `mount.z == 0`).
- **`"z"` must be read unconditionally** (`get_int( "z", 0 )`), never behind `has_member`. This codebase enforces unvisited-member reporting (`src/flexbuffer_json.cpp:258-301`); an unread `"z"` key is a load **error**.
- **The ladder gate must live inside `can_mount`.** `vehicles::finalize_prototypes` (`src/veh_type.cpp:1552`) builds a blueprint and calls `install_part` per part (`:1580`), which calls `can_mount` and `debugmsg`s on failure (`:1581-1584`). A `debugmsg` during init sets the flag `tests/test_main.cpp:215` checks, failing the suite. This is what makes prototype validation free.
- **Do NOT touch `precalc.z`.** `coord_translate` still writes only x/y; seeding `precalc.z` from `mount.z` is milestone 3. Doing it here wipes ramp offsets and breaks `vehicle_ramp_test_59/60/61`.
- **Do NOT reuse `carried_part_data::mount`** (`src/vehicle.h:300`) or its `mount_z` serialization — that is the racking/carry subsystem, a different axis of meaning.
- **Formatting:** C++ via astyle 3.1 (`.astylerc`); JSON via `make style-json`. Both are CI-gated.
- **Build/test commands** (this machine has no host toolchain; apt is disabled):
  - `.nas-build/cdda.sh build` — curses debug build
  - `.nas-build/cdda.sh test "<case>"` — one Catch2 case
  - `.nas-build/cdda.sh suite` — full isolated suite
  - Never raise `JOBS` above 6 (15 GB RAM, zero swap).

---

### Task 1: Widen the authoring path to 3D (behavioral no-op)

Makes `part_def::pos` and the mount APIs 3D without changing any behavior. `"z"` is parsed but every shipped vehicle omits it, so all positions stay `z = 0`.

**Files:**
- Modify: `src/veh_type.h:490` (`part_def::pos`)
- Modify: `src/veh_type.cpp:1292`, `:1308`, `:1323` (lambdas + parse loop)
- Modify: `src/vehicle.h:824-826` (`has_structural_part`), `:1078` (`can_mount`), `:1085-1096` (`install_part`)
- Modify: `src/vehicle.cpp:1239` (`has_structural_part`), `:1267` (`can_mount`), `:1681-1700` (`install_part`)
- Test: `tests/vehicle_multifloor_test.cpp` (create)

**Interfaces:**
- Consumes: `parts_at_relative( const tripoint_rel_ms &, bool, bool )` — already 3D from M1.
- Produces:
  - `ret_val<void> vehicle::can_mount( const tripoint_rel_ms &dp, const vpart_info &vpi ) const`
  - `int vehicle::install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type )` (plus the three sibling overloads)
  - `bool vehicle::has_structural_part( const tripoint_rel_ms &dp ) const`
  - `part_def::pos` is now `tripoint_rel_ms`
  - All existing `point_rel_ms` overloads remain and delegate with `z = 0`.

- [ ] **Step 1: Write the failing test**

Create `tests/vehicle_multifloor_test.cpp`:

```cpp
#include "cata_catch.h"
#include "map.h"
#include "map_helpers.h"
#include "vehicle.h"
#include "veh_type.h"
#include "type_id.h"
#include "coordinates.h"

static const vproto_id vehicle_prototype_car( "car" );

TEST_CASE( "vehicle_prototype_parts_default_to_z_zero", "[vehicle][multifloor]" )
{
    // Every shipped vehicle omits "z", so all authored parts must land on z == 0.
    const vehicle_prototype &proto = vehicle_prototype_car.obj();
    REQUIRE( !proto.parts.empty() );
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        CHECK( pt.pos.z() == 0 );
    }
}

TEST_CASE( "vehicle_install_part_accepts_tripoint_mount", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // The 3D overload with z == 0 must behave exactly like the 2D one.
    const tripoint_rel_ms mount( 0, 0, 0 );
    CHECK( !veh->parts_at_relative( mount, false, false ).empty() );
}
```

Register it in the test build if the tests Makefile does not glob sources:

```bash
grep -n "SOURCES" tests/Makefile | head -3
```

If `SOURCES` uses a wildcard (it does: `$(wildcard *.cpp)`), no registration is needed.

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "vehicle_prototype_parts_default_to_z_zero"`

Expected: compile error — `pt.pos.z()` does not exist because `part_def::pos` is still `point_rel_ms`.

- [ ] **Step 3: Widen `part_def::pos`**

In `src/veh_type.h`, change line 490:

```cpp
        struct part_def {
            tripoint_rel_ms pos;
            vpart_id part;
```

- [ ] **Step 4: Parse `"z"` unconditionally**

In `src/veh_type.cpp`, change both lambda signatures and the parse loop:

```cpp
    const auto add_part_obj = [&]( const JsonObject & part, tripoint_rel_ms pos ) {
```

```cpp
    const auto add_part_string = [&]( const std::string & part, tripoint_rel_ms pos ) {
```

```cpp
    for( JsonObject part : jo.get_array( "parts" ) ) {
        // "z" is read unconditionally (never behind has_member): this codebase
        // reports unvisited JSON members as load errors.
        tripoint_rel_ms pos{ part.get_int( "x" ), part.get_int( "y" ), part.get_int( "z", 0 ) };
```

- [ ] **Step 5: Add 3D overloads that the 2D ones delegate into**

In `src/vehicle.h`, alongside the existing declarations:

```cpp
        bool has_structural_part( const point &dp ) const;
        bool has_structural_part( const point_rel_ms &dp ) const;
        bool has_structural_part( const tripoint_rel_ms &dp ) const;
```

```cpp
        ret_val<void> can_mount( const point_rel_ms &dp, const vpart_info &vpi ) const;
        ret_val<void> can_mount( const tripoint_rel_ms &dp, const vpart_info &vpi ) const;
```

```cpp
        int install_part( map &here, const point_rel_ms &dp, const vpart_id &type );
        int install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type );
        int install_part( map &here, const point_rel_ms &dp, const vpart_id &type, item &&base );
        int install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type, item &&base );
        int install_part( map &here, const point_rel_ms &dp, const vpart_id &type, item &&base,
                          std::vector<item> &installed_with );
        int install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type, item &&base,
                          std::vector<item> &installed_with );
        int install_part( map &here, const point_rel_ms &dp, vehicle_part &&vp );
        int install_part( map &here, const tripoint_rel_ms &dp, vehicle_part &&vp );
```

- [ ] **Step 6: Implement the delegating bodies**

In `src/vehicle.cpp`, replace the `can_mount` signature at line 1267 and add a 2D shim above it:

```cpp
ret_val<void> vehicle::can_mount( const point_rel_ms &dp, const vpart_info &vpi ) const
{
    return can_mount( tripoint_rel_ms( dp, 0 ), vpi );
}

ret_val<void> vehicle::can_mount( const tripoint_rel_ms &dp, const vpart_info &vpi ) const
{
    const std::vector<int> parts_in_square = parts_at_relative( dp,
            /* use_cache = */ false, /* include_fake = */ false );
```

Note the hardcoded `tripoint_rel_ms( dp, 0 )` at the old line 1269 is now gone — `dp` carries its own z.

The adjacency block near line 1330 must stay planar for now (z-awareness is Task 3). Keep it operating on the x/y of `dp` at its own z:

```cpp
    if( !parts.empty() ) {
        const tripoint_rel_ms east( dp + tripoint_rel_ms::east );
        const tripoint_rel_ms south( dp + tripoint_rel_ms::south );
        const tripoint_rel_ms west( dp + tripoint_rel_ms::west );
        const tripoint_rel_ms north( dp + tripoint_rel_ms::north );
        if( !is_structural_part_removed() &&
            !has_structural_part( dp ) &&
            !has_structural_part( east ) &&
            !has_structural_part( south ) &&
            !has_structural_part( west ) &&
            !has_structural_part( north ) ) {
            return ret_val<void>::make_failure( _( "Part needs to be adjacent to or on existing structure." ) );
        }
    }
```

Add the 3D `has_structural_part` next to the existing one at line 1239:

```cpp
bool vehicle::has_structural_part( const point_rel_ms &dp ) const
{
    return has_structural_part( tripoint_rel_ms( dp, 0 ) );
}

bool vehicle::has_structural_part( const tripoint_rel_ms &dp ) const
{
    for( const int elem : parts_at_relative( dp, false ) ) {
        const vehicle_part &vp = part( elem );
        const vpart_info &vpi = vp.info();
        if( vpi.location == part_location_structure &&
            !vp.has_flag( vp_flag::carried_flag ) &&
            !vpi.has_flag( "PROTRUSION" ) ) {
            return true;
        }
    }
    return false;
}
```

The `carried_flag` check is load-bearing: it excludes racked/carried parts from counting as structural support. Preserve it exactly — dropping it silently changes behavior for vehicles carrying other vehicles.

Replace each `install_part` body with a 3D version plus a 2D shim:

```cpp
int vehicle::install_part( map &here, const point_rel_ms &dp, const vpart_id &type )
{
    return install_part( here, tripoint_rel_ms( dp, 0 ), type );
}

int vehicle::install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type )
{
    return install_part( here, dp, type, item( type.obj().base_item ) );
}

int vehicle::install_part( map &here, const point_rel_ms &dp, const vpart_id &type, item &&base )
{
    return install_part( here, tripoint_rel_ms( dp, 0 ), type, std::move( base ) );
}

int vehicle::install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type, item &&base )
{
    return install_part( here, dp, vehicle_part( type, std::move( base ) ) );
}

int vehicle::install_part( map &here, const point_rel_ms &dp, const vpart_id &type, item &&base,
                           std::vector<item> &installed_with )
{
    return install_part( here, tripoint_rel_ms( dp, 0 ), type, std::move( base ), installed_with );
}

int vehicle::install_part( map &here, const tripoint_rel_ms &dp, const vpart_id &type, item &&base,
                           std::vector<item> &installed_with )
{
    return install_part( here, dp, vehicle_part( type, std::move( base ), installed_with ) );
}

int vehicle::install_part( map &here, const point_rel_ms &dp, vehicle_part &&vp )
{
    return install_part( here, tripoint_rel_ms( dp, 0 ), std::move( vp ) );
}
```

Then change the main body's signature to take `const tripoint_rel_ms &dp`. Its interior needs no other change: it already assigns `vp.mount = dp` against M1's 3D `mount` field. Verify that line reads `vp.mount = dp;` and not a 2D construction:

```bash
grep -n "mount = dp" src/vehicle.cpp
```

If it constructs a `tripoint_rel_ms( dp, 0 )`, drop the `, 0` so the authored z survives.

- [ ] **Step 7: Fix the one internal caller that passes a `part_def::pos`**

`src/veh_type.cpp:1580` passes `pt.pos`, now a `tripoint_rel_ms`, which selects the new 3D overload automatically. Confirm it compiles unchanged:

```bash
sed -n '1578,1584p' src/veh_type.cpp
```

- [ ] **Step 8: Build and run the new tests**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: PASS, 2 test cases.

- [ ] **Step 9: Run the vehicle regression tags**

Run: `.nas-build/cdda.sh test "[vehicle]"`
Expected: all 49 `[vehicle]` cases pass — especially `vehicle_ramp_test_59/60/61` and `vehicle_level_test`, which prove `precalc.z` was not disturbed.

- [ ] **Step 10: Format and commit**

```bash
git add src/veh_type.h src/veh_type.cpp src/vehicle.h src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "refactor: widen vehicle prototype authoring path to 3D mounts (z pinned 0)"
```

---

### Task 2: Add the `VERTICAL_CONNECTOR` vehicle part flag

A JSON-only `json_flag` will not work — `has_flag( VPFLAG_… )` fast-path checks require a real enum entry plus a string mapping.

**Files:**
- Modify: `src/veh_type.h:118-120` (enum, before `NUM_VPFLAGS`)
- Modify: `src/veh_type.cpp:153` (flag string table)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Produces: `VPFLAG_VERTICAL_CONNECTOR` enum value; JSON flag string `"VERTICAL_CONNECTOR"`; recognized by `vpart_info::has_flag( VPFLAG_VERTICAL_CONNECTOR )`.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
static const vpart_id vpart_ladder_internal( "ladder_internal" );

TEST_CASE( "vertical_connector_flag_is_recognized", "[vehicle][multifloor]" )
{
    // The flag must resolve through the fast-path enum, not just the string set.
    CHECK( vpart_ladder_internal.obj().has_flag( VPFLAG_VERTICAL_CONNECTOR ) );
}
```

This test also depends on the JSON part from Task 4; it will fail until both land. That is intentional — it is the seam between the two tasks.

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh build`
Expected: compile error — `VPFLAG_VERTICAL_CONNECTOR` is not declared.

- [ ] **Step 3: Add the enum entry**

In `src/veh_type.h`, immediately before `NUM_VPFLAGS`:

```cpp
    VPFLAG_INOPERABLE_SMALL,
    VPFLAG_IGNORE_HEIGHT_REQUIREMENT,
    // Connects this tile to the tile directly above/below it, letting a vehicle
    // have a permanent second deck. See docs multi-floor-vehicles design §2.
    VPFLAG_VERTICAL_CONNECTOR,

    NUM_VPFLAGS
};
```

- [ ] **Step 4: Add the string mapping**

In `src/veh_type.cpp`, after the `IGNORE_HEIGHT_REQUIREMENT` row at line 153:

```cpp
    { "IGNORE_HEIGHT_REQUIREMENT", VPFLAG_IGNORE_HEIGHT_REQUIREMENT },
    { "VERTICAL_CONNECTOR", VPFLAG_VERTICAL_CONNECTOR },
```

- [ ] **Step 5: Verify it compiles**

Run: `.nas-build/cdda.sh build`
Expected: builds clean. The new test still fails at runtime (`ladder_internal` does not exist yet) — that is expected until Task 4.

- [ ] **Step 6: Commit**

```bash
git add src/veh_type.h src/veh_type.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat: add VPFLAG_VERTICAL_CONNECTOR vehicle part flag"
```

---

### Task 3: Ladder-gated `can_mount` validation for `z > 0`

Connectivity rule (design §1, option (a)): a bare z-neighbour does **not** connect decks. A part at `z > 0` is valid only if it is supported at its own z (on/adjacent to existing structure there) **or** sits directly above a `VERTICAL_CONNECTOR`.

**Files:**
- Modify: `src/vehicle.cpp` (`can_mount`, the 3D body from Task 1)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `can_mount( const tripoint_rel_ms &, const vpart_info & )` from Task 1; `VPFLAG_VERTICAL_CONNECTOR` from Task 2.
- Produces: `bool vehicle::has_vertical_connector_at( const tripoint_rel_ms &dp ) const`

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "upper_deck_mount_requires_vertical_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const vpart_info &floor = vpart_id( "hdframe" ).obj();

    // A tile far above open ground with no connector beneath must be rejected.
    const tripoint_rel_ms unsupported( 0, 0, 1 );
    CHECK( !veh->can_mount( unsupported, floor ).success() );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "upper_deck_mount_requires_vertical_connector"`
Expected: FAIL — after Task 1 the planar adjacency check operates at `dp`'s own z, and z=1 is empty, so it may already reject. If it PASSES here, the test is not yet proving the gate; keep it and rely on Step 5's positive case to prove the connector path.

- [ ] **Step 3: Add the connector query**

In `src/vehicle.cpp`, near `has_structural_part`:

```cpp
bool vehicle::has_vertical_connector_at( const tripoint_rel_ms &dp ) const
{
    for( const int elem : parts_at_relative( dp, false ) ) {
        if( part( elem ).info().has_flag( VPFLAG_VERTICAL_CONNECTOR ) ) {
            return true;
        }
    }
    return false;
}
```

Declare it in `src/vehicle.h` beside `has_structural_part`:

```cpp
        bool has_vertical_connector_at( const tripoint_rel_ms &dp ) const;
```

- [ ] **Step 4: Gate upward growth inside `can_mount`**

In the 3D `can_mount`, replace the adjacency block from Task 1 Step 6 with:

```cpp
    // All parts after the first must be installed on or next to an existing part.
    // The exception is when a single tile only structural object is being repaired.
    if( !parts.empty() ) {
        const tripoint_rel_ms east( dp + tripoint_rel_ms::east );
        const tripoint_rel_ms south( dp + tripoint_rel_ms::south );
        const tripoint_rel_ms west( dp + tripoint_rel_ms::west );
        const tripoint_rel_ms north( dp + tripoint_rel_ms::north );
        const bool supported_in_plane =
            has_structural_part( dp ) ||
            has_structural_part( east ) ||
            has_structural_part( south ) ||
            has_structural_part( west ) ||
            has_structural_part( north );
        // Decks connect only through an explicit vertical connector: a bare
        // z-neighbour is NOT connectivity (multi-floor design section 1).
        const bool supported_from_below =
            dp.z() > 0 &&
            has_vertical_connector_at( dp + tripoint_rel_ms::below );
        if( !is_structural_part_removed() && !supported_in_plane && !supported_from_below ) {
            return ret_val<void>::make_failure(
                       _( "Part needs to be adjacent to or on existing structure." ) );
        }
    }
```

- [ ] **Step 5: Add the positive case**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
TEST_CASE( "upper_deck_mount_allowed_above_connector", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Install a connector on the ground deck, then a floor directly above it.
    const tripoint_rel_ms ground( 0, 0, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_ladder_internal ) >= 0 );

    const tripoint_rel_ms above( 0, 0, 1 );
    CHECK( veh->can_mount( above, vpart_id( "hdframe" ).obj() ).success() );
}
```

- [ ] **Step 6: Run both tests**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: PASS (the positive case requires Task 4's JSON; run it again after Task 4 if `ladder_internal` is missing).

- [ ] **Step 7: Full vehicle regression**

Run: `.nas-build/cdda.sh test "[vehicle]"`
Expected: all pass. The gate only adds an *additional* way to succeed for `z > 0`; z=0 behavior is unchanged.

- [ ] **Step 8: Commit**

```bash
git add src/vehicle.h src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat: ladder-gated can_mount validation for upper-deck parts"
```

---

### Task 4: New JSON parts — internal ladder and upper-deck floor

`VPFLAG_ROOF` alone is **not** a walkable floor (it means "keeps rain out", `src/vehicle.cpp:2280-2282`). The upper-deck part must combine ROOF + BOARDABLE so it is a roof from below and a stand-on floor from above.

**Files:**
- Modify: `data/json/vehicleparts/vehicle_parts.json` (or a new `data/json/vehicleparts/multifloor.json`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Produces: vpart ids `ladder_internal` and `deck_floor`, plus their item ids.

- [ ] **Step 1: Inspect an existing part to copy the shape**

```bash
grep -n '"id": "hdframe"' -A 25 data/json/vehicleparts/vehicle_parts.json
```

Match the surrounding conventions exactly (`type`, `id`, `name`, `item`, `location`, `requirements`, `breaks_into`).

- [ ] **Step 2: Add the two parts**

Create `data/json/vehicleparts/multifloor.json`:

```json
[
  {
    "type": "vehicle_part",
    "id": "ladder_internal",
    "name": { "str": "internal ladder" },
    "categories": [ "structure" ],
    "item": "ladder",
    "location": "center",
    "symbol": "H",
    "color": "light_gray",
    "broken_symbol": "x",
    "broken_color": "light_gray",
    "durability": 200,
    "description": "A fixed ladder connecting this deck to the one above it.",
    "size": "150 L",
    "flags": [ "VERTICAL_CONNECTOR", "BOARDABLE", "OBSTACLE" ],
    "breaks_into": [ { "item": "steel_chunk", "count": [ 1, 2 ] } ],
    "requirements": {
      "install": { "skills": [ [ "mechanics", 3 ] ], "time": "60 m", "using": [ [ "vehicle_bolt", 1 ] ] },
      "removal": { "skills": [ [ "mechanics", 2 ] ], "time": "30 m", "using": [ [ "vehicle_bolt", 1 ] ] }
    }
  },
  {
    "type": "vehicle_part",
    "id": "deck_floor",
    "name": { "str": "deck floor" },
    "categories": [ "structure" ],
    "item": "sheet_metal",
    "location": "structure",
    "symbol": "%",
    "color": "light_gray",
    "broken_symbol": "*",
    "broken_color": "light_gray",
    "durability": 300,
    "description": "A solid metal floor panel forming the deck above.",
    "flags": [ "ROOF", "BOARDABLE", "OPAQUE" ],
    "breaks_into": [ { "item": "steel_chunk", "count": [ 2, 4 ] } ],
    "requirements": {
      "install": { "skills": [ [ "mechanics", 3 ] ], "time": "60 m", "using": [ [ "vehicle_weld_removal", 1 ] ] },
      "removal": { "skills": [ [ "mechanics", 2 ] ], "time": "30 m", "using": [ [ "vehicle_weld_removal", 1 ] ] }
    }
  }
]
```

Verify every referenced item id and requirement id exists:

```bash
grep -rn '"id": "ladder"' data/json/items/ | head -2
grep -rn '"id": "sheet_metal"' data/json/items/ | head -2
grep -rn '"id": "vehicle_bolt"' data/json/requirements/ | head -2
grep -rn '"id": "vehicle_weld_removal"' data/json/requirements/ | head -2
```

Replace any that do not resolve with ids that do — an unresolved id is a load error.

- [ ] **Step 3: Format the JSON**

Run: `.nas-build/cdda.sh run make style-json`
Expected: file reformatted in place, exit 0.

- [ ] **Step 4: Build and verify the parts load**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "vertical_connector_flag_is_recognized"`
Expected: PASS — this is the Task 2 test finally going green.

- [ ] **Step 5: Re-run the Task 3 positive case**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: all `[multifloor]` cases PASS, including `upper_deck_mount_allowed_above_connector`.

- [ ] **Step 6: Commit**

```bash
git add data/json/vehicleparts/multifloor.json
git commit -m "feat: add internal ladder and deck floor vehicle parts"
```

---

### Task 5: Two-floor test bus prototype

Validation is free: `finalize_prototypes` installs every part through `can_mount`, and any failure `debugmsg`s, which fails the suite.

**Files:**
- Modify: `data/json/vehicles/custom_vehicles.json`
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Produces: vproto id `test_bus_2floor`.

- [ ] **Step 1: Write the failing test**

Append to `tests/vehicle_multifloor_test.cpp`:

```cpp
static const vproto_id vehicle_prototype_test_bus_2floor( "test_bus_2floor" );

TEST_CASE( "two_floor_bus_prototype_has_upper_deck_parts", "[vehicle][multifloor]" )
{
    const vehicle_prototype &proto = vehicle_prototype_test_bus_2floor.obj();
    int upper = 0;
    for( const vehicle_prototype::part_def &pt : proto.parts ) {
        if( pt.pos.z() == 1 ) {
            upper++;
        }
    }
    CHECK( upper > 0 );
}

TEST_CASE( "two_floor_bus_spawns_with_parts_on_both_decks", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 0 ), false, false ).empty() );
    CHECK( !veh->parts_at_relative( tripoint_rel_ms( 0, 0, 1 ), false, false ).empty() );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.nas-build/cdda.sh test "two_floor_bus_prototype_has_upper_deck_parts"`
Expected: FAIL — `test_bus_2floor` is not a known vproto id.

- [ ] **Step 3: Add the prototype**

Append to `data/json/vehicles/custom_vehicles.json` (inside the top-level array). The ladder at `(0,0,0)` is what legalises every `z:1` tile via planar spread from `(0,0,1)`:

```json
  {
    "id": "test_bus_2floor",
    "type": "vehicle",
    "name": "Test Double-Decker Bus",
    "blueprint": [ "-----" ],
    "parts": [
      { "x": 0, "y": 0, "parts": [ "hdframe", "ladder_internal" ] },
      { "x": 0, "y": 1, "parts": [ "hdframe", "seat" ] },
      { "x": 1, "y": 0, "parts": [ "hdframe", "controls", "engine_electric" ] },
      { "x": 1, "y": 1, "parts": [ "hdframe", "storage_battery" ] },
      { "x": -1, "y": 0, "parts": [ "hdframe", "wheel_wide" ] },
      { "x": -1, "y": 1, "parts": [ "hdframe", "wheel_wide" ] },
      { "x": 2, "y": 0, "parts": [ "hdframe", "wheel_wide" ] },
      { "x": 2, "y": 1, "parts": [ "hdframe", "wheel_wide" ] },

      { "x": 0, "y": 0, "z": 1, "parts": [ "deck_floor" ] },
      { "x": 0, "y": 1, "z": 1, "parts": [ "deck_floor", "seat" ] },
      { "x": 1, "y": 0, "z": 1, "parts": [ "deck_floor" ] },
      { "x": 1, "y": 1, "z": 1, "parts": [ "deck_floor", "seat" ] }
    ]
  }
```

Confirm each referenced part id exists:

```bash
for p in hdframe seat controls engine_electric storage_battery wheel_wide; do
  printf '%s: ' "$p"; grep -rl "\"id\": \"$p\"" data/json/vehicleparts/ | head -1 || echo MISSING
done
```

- [ ] **Step 4: Format the JSON**

Run: `.nas-build/cdda.sh run make style-json`

- [ ] **Step 5: Build and run**

Run: `.nas-build/cdda.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: all PASS. If a `debugmsg` about an invalid vehicle appears, `can_mount` rejected an upper-deck tile — check that `(0,0,1)` sits directly above the ladder and that every other `z:1` tile is planar-adjacent to an already-placed `z:1` tile. **Part order within the `parts` array matters**: parts are installed in order, so a tile must be reachable at the moment it is installed.

- [ ] **Step 6: Commit**

```bash
git add data/json/vehicles/custom_vehicles.json tests/vehicle_multifloor_test.cpp
git commit -m "feat: add two-floor test bus prototype"
```

---

### Task 6: Full-suite gate and documentation

**Files:**
- Modify: `doc/JSON/VEHICLES_JSON.md`
- Modify: `doc/JSON/JSON_FLAGS.md`

- [ ] **Step 1: Document the `"z"` field**

In `doc/JSON/VEHICLES_JSON.md`, find the section describing part `"x"`/`"y"` and add:

```markdown
| `"z"` | (optional, integer, default `0`) The deck this part is mounted on. `0` is the ground deck; `1` is the upper deck. A part at `z > 0` must sit directly above a part with the `VERTICAL_CONNECTOR` flag, or be adjacent to another part already placed on the same `z`. |
```

- [ ] **Step 2: Document the flag**

In `doc/JSON/JSON_FLAGS.md`, in the vehicle parts flag table, add in alphabetical position:

```markdown
- ```VERTICAL_CONNECTOR``` Connects this tile to the tile directly above it, allowing a vehicle to have a permanent upper deck. Without it, parts at different z-levels are not considered connected.
```

- [ ] **Step 3: Run the full suite**

Run: `.nas-build/cdda.sh suite`
Expected: all batches green. This is the milestone gate — a single `cata_test` call is **not** sufficient (order-dependent `[monster]` tests cross-contaminate in one process).

- [ ] **Step 4: Verify the tiles flavor still compiles**

Run: `RELEASE=1 .nas-build/cdda.sh build-tiles`
Expected: builds clean. M2 touches no rendering code, but this catches any `TILES`-only breakage before milestone 3 inherits it.

- [ ] **Step 5: Check formatting the way CI will**

astyle 3.1 is not available in the harness image; CI (`astyle.yml`) is the authoritative check. Review the diff for the project style (spaces inside parens, `--align-pointer=name`, 4-space indent, 100 cols) before pushing.

```bash
git diff --stat
```

- [ ] **Step 6: Commit and push**

```bash
git add doc/JSON/VEHICLES_JSON.md doc/JSON/JSON_FLAGS.md
git commit -m "docs: document vehicle part z field and VERTICAL_CONNECTOR flag"
git push -u origin explore/multi-floor-vehicles-m2
```

---

## Milestone exit criteria

1. `data/json` authors an upper deck with `"z": 1`; every existing vehicle is unaffected (`mount.z == 0`).
2. `VPFLAG_VERTICAL_CONNECTOR` exists as a real enum + string mapping and gates upper-deck mounting inside `can_mount`.
3. `ladder_internal` and `deck_floor` parts exist; `deck_floor` is ROOF + BOARDABLE, not bare ROOF.
4. `test_bus_2floor` loads through `finalize_prototypes` with no `debugmsg` and spawns with parts at both `z=0` and `z=1`.
5. Full suite green via `.nas-build/cdda.sh suite`; tiles flavor compiles.
6. `precalc.z` untouched — ramp tests still pass.

## Explicitly NOT in M2

Deferred to later milestones; do not implement here:

- Seeding `precalc.z` from `mount.z` and the rendering work (**M3** — doing it now breaks ramp tests).
- 3D connectivity in `find_and_split_vehicles` / `can_unmount` / `new_mounts` (**M3**).
- Deck-to-deck traversal (`vertical_move`, `has_floor`) and gravity-check on floor destruction (**M4**).
- Driving physics, mass centre, collisions across z (**M5**).
- `veh_interact` install-on-floor UX (**M6**).
