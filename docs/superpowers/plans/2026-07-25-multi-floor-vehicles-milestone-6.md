# Multi-floor Vehicles — Milestone 6 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the last multi-floor correctness gap (2D-keyed `loot_zones`/`labels`) and make upper decks player-buildable through a z-aware `veh_interact` construction cursor.

**Architecture:** Two independent workstreams on branch `explore/multi-floor-vehicles-m6`. **A** widens the `loot_zones` map key and the `label` struct base from `point_rel_ms` to `tripoint_rel_ms`, letting the compiler flag every 2D-keyed access, then adds omit-when-zero save migration. **B** adds an orthogonal `int sel_z` to `veh_interact` (the planar `dd` cursor is left 2D), routes cursor resolution / `can_mount` / install through the existing 3D overloads at `sel_z`, threads `sel_z` through the install activity, and draws the deck below dimmed as a build-support reference.

**Tech Stack:** C++17, Catch2 (`tests/cata_test`), CDDA `generic_factory`/type-id content, curses/SDL veh_interact UI, JSON keybindings.

## Global Constraints

- **Single-floor no-op is mandatory.** Workstream A: any vehicle with no z-parts must re-serialize **byte-identically** (no new `"z"` member emitted). Workstream B: behavior at `sel_z == 0` must be identical to today. Every task guards this.
- **Save back-compat:** new save fields use the M1 `mount_dz` pattern — read optional, default `0`; write **only when non-zero**.
- **Coordinate types:** use the typed wrappers (`point_rel_ms`, `tripoint_rel_ms`), never bare ints/points. A part's true 3D mount is `veh.part( idx ).mount` — `vpart_position::mount_pos()` is **2D and drops z** (M4 lesson); do not use it where z matters.
- **Frame-connectivity model (PR #7) is the law for legality:** an upper-deck frame is supported iff a structural frame sits directly below; climbing needs a `VPFLAG_VERTICAL_TRAVERSAL` part. Workstream B only *surfaces* the existing `can_mount` verdict — it must not re-implement support rules.
- **Build/test via the NAS Docker harness** (`.nas-build/`, **foreground** — background builds get reaped; jobserver stall is recurring). See the `nas-build-status` memory. **`.nas-build/cdda.sh test "<name>"` does NOT compile** — it runs a possibly-stale binary. The authoritative iteration protocol is: (1) `.nas-build/build-retry.sh build` (foreground, curses — also builds `tests/cata_test`); (2) then `.nas-build/cdda.sh test "<name>"`. Never run a test step without a preceding successful build, or a "green" may be a false green on old objects. Tiles link check: `.nas-build/build-retry.sh build-tiles`. Full isolated suite: `.nas-build/cdda.sh suite` (wraps `build-scripts/gha_test_only.sh`); full gate (build + suite): `.nas-build/gate.sh`.
- **Formatting before any commit that lands:** `astyle` 3.1 on changed C++; `make style-json` on changed JSON.
- **`[multifloor]` tag** for all new tests, alongside `[vehicle]`.

---

## Workstream A — loot_zones & labels 3D keys

### File Structure (Workstream A)

- Modify `src/vehicle.h` — `loot_zones` map key type; `struct label` base type + ctors.
- Modify `src/vehicle.cpp` — every `.xy()` truncation at zone/label sites (`~2431`, `~2449`, `~2858`, `~2866`, `~2874`, `~3336`, `~3347`, `~7884`, `~7890`, `~8962`).
- Modify `src/clzones.cpp` — game-side zone insertion (`~1418`, `~1853`) uses 3D mount.
- Modify `src/savegame_json.cpp` — zone (de)serialize (`~3526`, `~3602`); `label::deserialize`/`label::serialize` (`3409`, `3417`).
- Test `tests/vehicle_multifloor_test.cpp` — new cases (append).

---

### Task 1: [A1] Widen `loot_zones` + `label` keys to 3D

**Files:**
- Modify: `src/vehicle.h:707-716` (struct label), `src/vehicle.h:2392` (loot_zones)
- Modify: `src/vehicle.cpp` (all zone/label `.xy()` sites listed above)
- Modify: `src/clzones.cpp:1418, 1853`
- Modify: `src/savegame_json.cpp:3409-3424` (label ser/deser — keep xy-only for now), `3526-3533` + `3602-3610` (zones — keep xy-only for now)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: existing 3D overloads `vehicle::parts_at_relative( const tripoint_rel_ms&, bool, bool )`, `vehicle::install_part( map&, const tripoint_rel_ms&, const vpart_id& )`.
- Produces: `std::unordered_multimap<tripoint_rel_ms, zone_data> vehicle::loot_zones`; `struct label : public tripoint_rel_ms` with ctors `label(const tripoint_rel_ms&)` and `label(const tripoint_rel_ms&, std::string)`. Later tasks and the UX workstream rely on these 3D keys.

- [ ] **Step 1: Write the failing tests** (append to `tests/vehicle_multifloor_test.cpp`)

```cpp
// --- M6 Workstream A: 3D zone/label keys ---
#include "clzones.h" // add near the other includes at the top of the file

TEST_CASE( "labels_on_different_decks_are_distinct", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    // Same (x,y), different deck. Under 2D keys the set treats these as equal and
    // silently keeps only one; under 3D keys both coexist.
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 0 ), "ground" ) );
    veh->labels.insert( label( tripoint_rel_ms( 0, 0, 1 ), "upper" ) );
    CHECK( veh->labels.size() == 2 );
    CHECK( veh->labels.count( label( tripoint_rel_ms( 0, 0, 0 ) ) ) == 1 );
    CHECK( veh->labels.count( label( tripoint_rel_ms( 0, 0, 1 ) ) ) == 1 );
}

TEST_CASE( "loot_zone_erase_is_per_deck", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const zone_data z_ground( "g", zone_type_id( "LOOT_UNSORTED" ), your_fac,
                              false, true, tripoint_abs_ms::zero, tripoint_abs_ms::zero );
    const zone_data z_upper = z_ground;
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 0 ), z_ground );
    veh->loot_zones.emplace( tripoint_rel_ms( 0, 0, 1 ), z_upper );
    // Erasing the ground-deck key must leave the upper-deck zone intact (2D erase
    // by (0,0) would remove both).
    veh->loot_zones.erase( tripoint_rel_ms( 0, 0, 0 ) );
    CHECK( veh->loot_zones.count( tripoint_rel_ms( 0, 0, 0 ) ) == 0 );
    CHECK( veh->loot_zones.count( tripoint_rel_ms( 0, 0, 1 ) ) == 1 );
}
```

Add near the top with the other `static const` ids:

```cpp
static const faction_id your_fac( "your_followers" );
```

(The `zone_data` ctor is `zone_data( name, type, faction, invert, enabled, start, end, options = nullptr, is_displayed = false )` — `src/clzones.h`. The exact zone type is irrelevant to the test; it only needs two zones sharing an (x,y) on different z.)

- [ ] **Step 2: Run tests to verify they fail (compile error)**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: **compile failure** — `label( tripoint_rel_ms )` ctor and `loot_zones.count( tripoint_rel_ms )` do not exist while the key is 2D. That compile failure is the RED.

- [ ] **Step 3: Widen the types in `src/vehicle.h`**

`struct label` (currently `: public point_rel_ms`):

```cpp
struct label : public tripoint_rel_ms {
    label() = default;
    explicit label( const tripoint_rel_ms &p ) : tripoint_rel_ms( p ) {}
    label( const tripoint_rel_ms &p, std::string text ) : tripoint_rel_ms( p ), text( std::move( text ) ) {}

    std::string text;

    void deserialize( const JsonObject &data );
    void serialize( JsonOut &json ) const;
};
```

`loot_zones` declaration:

```cpp
std::unordered_multimap<tripoint_rel_ms, zone_data> loot_zones;
```

(Confirm `std::hash<tripoint_rel_ms>` exists — `point.h`/coordinate headers provide hashes for the coord wrappers; if a hash is missing the compiler will say so and it is added the same way `point_rel_ms`'s is.)

- [ ] **Step 4: Fix every site the compiler now flags**

The type change turns every 2D-keyed access into a compile error — walk them all. Known sites and their fixes:

- `src/vehicle.cpp` remove_part (~2431/2437): `loot_zones.find( vp.mount.xy() )` → `loot_zones.find( vp.mount )`; `loot_zones.erase( vp.mount.xy() )` → `loot_zones.erase( vp.mount )`. Delete the "deferred to a later milestone" comment.
- `src/vehicle.cpp` label erase (~2449-2452): `const tripoint_rel_ms vp_mount = vp.mount;` then `labels.find( label( vp_mount ) )` and the `parts_at_relative( vp_mount, false )` emptiness check (already 3D-capable). Delete the deferral comment.
- `src/vehicle.cpp` split loops (~2858-2874): `labels.find( label( cur_mount ) )`, `new_labels.insert( label( new_mount, label_str ) )`, `loot_zones.equal_range( cur_mount )`, `loot_zones.erase( cur_mount )` — use the full 3D `cur_mount`/`new_mount` (drop `.xy()`).
- `src/vehicle.cpp` second zone-copy loop (~8962-8982): use full 3D mount keys.
- `src/vehicle.cpp` shift_parts (~7884/7890): `label( l - delta.raw(), l.text )` and `new_zones.emplace( z.first - delta.raw(), z.second )` — `delta` here is planar; keep the z component of the key unchanged by subtracting only the xy. Use `l - tripoint_rel_ms( delta.raw().xy(), 0 )` and `z.first - tripoint_rel_ms( delta.raw().xy(), 0 )` so z is preserved.
- `src/vehicle.cpp:3336-3347` `vpart_position`: `vehicle().labels.find( label( mount_pos() ) )` → use the true 3D mount: `const tripoint_rel_ms m = vehicle().part( part_index() ).mount;` then `label( m )`. In `set_label` (3347) likewise build the label from the 3D mount.
- `src/clzones.cpp:1418`: `vehicle.loot_zones.emplace( mount_point, new_zone )` — ensure `mount_point` is the 3D mount (widen the local to `tripoint_rel_ms`).
- `src/clzones.cpp:1853`: `vp->vehicle().loot_zones.emplace( vp->mount_pos(), zone )` → `vp->vehicle().loot_zones.emplace( vp->vehicle().part( vp->part_index() ).mount, zone )` (mount_pos() drops z).
- Any other `loot_zones`/`labels` access the compiler flags (e.g. clzones refresh/query `equal_range`/`find`): pass the 3D mount.

**Serialization stays 2D for now** (byte-identical guard until Task 2). In `savegame_json.cpp`:
- zones read (~3530): keep `sdata.read( "point", p )` into a `point p`, then `loot_zones.emplace( tripoint_rel_ms( p.x, p.y, 0 ), zd )`.
- zones write (~3606): `json.member( "point", z.first.xy() )` (emit xy only).
- `label::deserialize`: read `x`,`y` into the xy of the tripoint, leave z 0. `label::serialize`: emit `x`,`y` only (drop z). Leave the omit-when-zero z for Task 2.

- [ ] **Step 5: Run the new tests to verify they pass**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS, including the two new cases.

- [ ] **Step 6: Run the broader zone/vehicle suites for no-op regressions**

Run: `.nas-build/cdda.sh test "[vehicle]"` then `.nas-build/cdda.sh test "[zones]"`
Expected: PASS — no behavior change for single-floor.

- [ ] **Step 7: astyle + commit**

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n src/vehicle.h src/vehicle.cpp src/clzones.cpp src/savegame_json.cpp tests/vehicle_multifloor_test.cpp && chown 3000:3003 src/vehicle.h src/vehicle.cpp src/clzones.cpp src/savegame_json.cpp tests/vehicle_multifloor_test.cpp"
git add src/vehicle.h src/vehicle.cpp src/clzones.cpp src/savegame_json.cpp tests/vehicle_multifloor_test.cpp
git commit -m "fix(veh): 3D keys for loot_zones and labels (in-memory)

Widen loot_zones map key and label base from point_rel_ms to
tripoint_rel_ms so upper- and lower-deck zones/labels no longer collide.
Serialization stays xy-only (byte-identical) pending the z-migration.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: [A2] Save migration — omit-when-zero `z` for zones + labels

**Files:**
- Modify: `src/savegame_json.cpp:3409-3424` (label ser/deser), `3526-3533` (zones read), `3602-3610` (zones write)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: 3D `loot_zones`/`label` from Task 1.
- Produces: save format where a zone/label carries an integer `"z"` member **only when non-zero**; absent `"z"` reads as `0`.

- [ ] **Step 1: Write the failing tests** (append)

Add includes near the top if absent: `#include "json.h"`, `#include "json_loader.h"`, `#include "flexbuffer_json.h"`, `#include <sstream>`.

The `label` cases test the (de)serializer in isolation (robust — no coupling to unrelated vehicle fields). The zone case uses the proven whole-vehicle round-trip helper (`vehicle` has no default ctor and a deleted copy ctor, so the destination is `vehicle{ vproto_id() }`, mirroring `tests/vehicle_savegame_mount_test.cpp`).

```cpp
TEST_CASE( "label_z_roundtrip", "[vehicle][multifloor]" )
{
    const label l( tripoint_rel_ms( 1, 0, 1 ), "upper" );
    std::ostringstream os;
    JsonOut jout( os );
    l.serialize( jout );
    CHECK( os.str().find( "\"z\":1" ) != std::string::npos ); // non-zero z persisted

    JsonValue jv = json_loader::from_string( os.str() );
    label l2;
    l2.deserialize( jv.get_object() );
    CHECK( l2 == label( tripoint_rel_ms( 1, 0, 1 ) ) );
    CHECK( l2.text == "upper" );
}

TEST_CASE( "single_floor_label_omits_z", "[vehicle][multifloor]" )
{
    const label l( tripoint_rel_ms( 1, 0, 0 ), "ground" );
    std::ostringstream os;
    JsonOut jout( os );
    l.serialize( jout );
    // z==0 must not emit a "z" member (byte-identical guard).
    CHECK( os.str().find( "\"z\":" ) == std::string::npos );
}

TEST_CASE( "vehicle_zone_z_roundtrip", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const zone_data z_upper( "u", zone_type_id( "LOOT_UNSORTED" ), your_fac,
                             false, true, tripoint_abs_ms::zero, tripoint_abs_ms::zero );
    veh->loot_zones.emplace( tripoint_rel_ms( 1, 0, 1 ), z_upper );

    std::ostringstream os;
    JsonOut jout( os );
    veh->serialize( jout );
    CHECK( os.str().find( "\"z\":1" ) != std::string::npos ); // zone deck persisted

    JsonValue jv = json_loader::from_string( os.str() );
    vehicle after{ vproto_id() };
    after.deserialize( jv.get_object() );
    CHECK( after.loot_zones.count( tripoint_rel_ms( 1, 0, 1 ) ) == 1 );
    CHECK( after.loot_zones.count( tripoint_rel_ms( 1, 0, 0 ) ) == 0 );
}
```

(`your_fac` and the `zone_data` include were added in Task 1.)

- [ ] **Step 2: Run to verify failure**

Run: `.nas-build/cdda.sh test "label_z_roundtrip"`
Expected: FAIL — no `"z"` is emitted yet (label serialize is xy-only from A1).

- [ ] **Step 3: Add omit-when-zero z to label ser/deser** (`savegame_json.cpp`)

```cpp
void label::deserialize( const JsonObject &data )
{
    data.allow_omitted_members();
    data.read( "x", x() );
    data.read( "y", y() );
    int zz = 0;
    data.read( "z", zz );
    z() = zz;
    data.read( "text", text );
}

void label::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "x", x() );
    json.member( "y", y() );
    if( z() != 0 ) {
        json.member( "z", z() );
    }
    json.member( "text", text );
    json.end_object();
}
```

- [ ] **Step 4: Add omit-when-zero z to zone read/write**

Zone read (~3526-3533):

```cpp
point p;
int pz = 0;
zone_data zd;
for( JsonObject sdata : data.get_array( "zones" ) ) {
    sdata.allow_omitted_members();
    sdata.read( "point", p );
    pz = 0;
    sdata.read( "z", pz );
    sdata.read( "zone", zd );
    loot_zones.emplace( tripoint_rel_ms( p.x, p.y, pz ), zd );
}
```

Zone write (~3602-3610):

```cpp
for( auto const &z : loot_zones ) {
    json.start_object();
    json.member( "point", z.first.xy() );
    if( z.first.z() != 0 ) {
        json.member( "z", z.first.z() );
    }
    json.member( "zone", z.second );
    json.end_object();
}
```

- [ ] **Step 5: Run the new tests**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS (all four A-tests).

- [ ] **Step 6: Full vehicle + savegame regression**

Run: `.nas-build/cdda.sh test "[vehicle]"` and `.nas-build/cdda.sh test "[savegame]"`
Expected: PASS.

- [ ] **Step 7: astyle + commit**

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n src/savegame_json.cpp tests/vehicle_multifloor_test.cpp && chown 3000:3003 src/savegame_json.cpp tests/vehicle_multifloor_test.cpp"
git add src/savegame_json.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): persist zone/label deck via omit-when-zero z

Old saves (no z) load at z=0; single-floor vehicles emit no z member and
stay byte-identical. Upper-deck zones/labels now round-trip.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Workstream B — multi-floor construction UX (`veh_interact`)

### File Structure (Workstream B)

- Modify `src/veh_interact.h` — add `int sel_z = 0;`; declare helpers `clamp_deck`, `select_preview_decks`.
- Modify `src/veh_interact.cpp` — register `SELECT_Z_UP/DOWN`; main-loop handling; `part_at`/`move_cursor`/install-preview at `sel_z`; activity z-threading; `display_veh` reference layer; header readout.
- Modify `data/raw/keybindings.json` — default `<`/`>` bindings for the two actions.
- Test `tests/vehicle_multifloor_test.cpp` — pure-helper tests (append).

Order: B1 (nav + clamp) → B2 (resolution + install threading) → B3 (preview layer).

---

### Task 3: [B1] `sel_z` state, `<`/`>` navigation, clamp, header readout

**Files:**
- Modify: `src/veh_interact.h:73` area (add `int sel_z = 0;`), method declarations
- Modify: `src/veh_interact.cpp:275-300` (register actions), `:514-517` (main loop), `display_veh` header
- Modify: `data/raw/keybindings.json` (VEH_INTERACT category)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Produces: free function `int veh_interact_clamp_deck( int desired, int min_z, int max_z )` returning `std::clamp( desired, min_z, max_z + 1 )`. B2/B3 read `sel_z`.

- [ ] **Step 1: Write the failing test** (append)

```cpp
// declared in veh_interact.h, defined in veh_interact.cpp
int veh_interact_clamp_deck( int desired, int min_z, int max_z );

TEST_CASE( "veh_interact_deck_clamp", "[vehicle][multifloor]" )
{
    // Single-floor vehicle: min=max=0 -> may step to 1 (start a deck) but not below 0.
    CHECK( veh_interact_clamp_deck( 1, 0, 0 ) == 1 );
    CHECK( veh_interact_clamp_deck( 2, 0, 0 ) == 1 );
    CHECK( veh_interact_clamp_deck( -1, 0, 0 ) == 0 );
    // Two-floor vehicle: min=0,max=1 -> up to 2, down to 0.
    CHECK( veh_interact_clamp_deck( 3, 0, 1 ) == 2 );
    CHECK( veh_interact_clamp_deck( -5, 0, 1 ) == 0 );
    CHECK( veh_interact_clamp_deck( 1, 0, 1 ) == 1 );
}
```

- [ ] **Step 2: Run to verify failure**

Run: `.nas-build/cdda.sh test "veh_interact_deck_clamp"`
Expected: link/compile FAIL — `veh_interact_clamp_deck` undefined.

- [ ] **Step 3: Add the helper + `sel_z` state**

In `src/veh_interact.h`, near `dd` (line ~73):

```cpp
point_rel_ms dd = point_rel_ms::zero;
int sel_z = 0; // deck currently being edited; dd stays planar
```

Declare the free helper in `src/veh_interact.h` (outside the class, after it):

```cpp
int veh_interact_clamp_deck( int desired, int min_z, int max_z );
```

Define it in `src/veh_interact.cpp` (near the top, after includes):

```cpp
int veh_interact_clamp_deck( int desired, int min_z, int max_z )
{
    return std::clamp( desired, min_z, max_z + 1 );
}
```

- [ ] **Step 4: Register actions + handle them + header readout**

In the action registration block (`:275-300`):

```cpp
main_context.register_action( "SELECT_Z_UP" );
main_context.register_action( "SELECT_Z_DOWN" );
```

In the main input loop (`:514-517`), after the direction handling, add branches (call `move_cursor( here, point_rel_ms::zero )` to re-run cursor refresh at the new deck):

```cpp
} else if( action == "SELECT_Z_UP" ) {
    sel_z = veh_interact_clamp_deck( sel_z + 1, veh->mount_min_z(), veh->mount_max_z() );
    move_cursor( here, point_rel_ms::zero );
} else if( action == "SELECT_Z_DOWN" ) {
    sel_z = veh_interact_clamp_deck( sel_z - 1, veh->mount_min_z(), veh->mount_max_z() );
    move_cursor( here, point_rel_ms::zero );
```

In `display_veh` (before `wnoutrefresh( w_disp )`, `:2369`), show the deck when non-zero:

```cpp
if( sel_z != 0 ) {
    mvwprintz( w_disp, point( 0, getmaxy( w_disp ) - 1 ), c_yellow, _( "Deck %d" ), sel_z );
}
```

- [ ] **Step 5: Add default keybindings** (`data/raw/keybindings.json`, VEH_INTERACT category — mirror the `INSTALL` entry shape at `:1838`)

```json
{
  "type": "keybinding",
  "id": "SELECT_Z_UP",
  "category": "VEH_INTERACT",
  "bindings": [ { "input_method": "keyboard_char", "key": "<" } ]
},
{
  "type": "keybinding",
  "id": "SELECT_Z_DOWN",
  "category": "VEH_INTERACT",
  "bindings": [ { "input_method": "keyboard_char", "key": ">" } ]
}
```

- [ ] **Step 6: Run tests + JSON style**

Run: `.nas-build/cdda.sh test "veh_interact_deck_clamp"` → PASS.
Run: `make style-json` (or format `data/raw/keybindings.json` with `json_formatter`); confirm no diff churn beyond the new entries.

- [ ] **Step 7: astyle + commit**

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp && chown 3000:3003 src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp"
git add src/veh_interact.h src/veh_interact.cpp data/raw/keybindings.json tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): sel_z deck cursor + </> navigation in veh_interact

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: [B2] Route cursor resolution + install through `sel_z`

**Files:**
- Modify: `src/veh_interact.cpp` — `part_at` (`:2163`), `move_cursor` (`:2199`), install can_mount preview (`:909`), activity values packing (`:189`), `complete_vehicle` (`:3076, 3121`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `sel_z`; 3D overloads `can_mount( const tripoint_rel_ms&, ... )`, `install_part( map&, const tripoint_rel_ms&, ... )`, `part_displayed_at( const tripoint_rel_ms&, ... )`.
- Produces: free function `tripoint_rel_ms veh_interact_install_mount( const std::vector<int> &activity_values )` reconstructing the 3D mount from a completed install activity, with a back-compat size guard.

- [ ] **Step 1: Write the failing test** (append) — locks the activity z round-trip incl. back-compat

```cpp
tripoint_rel_ms veh_interact_install_mount( const std::vector<int> &activity_values );

TEST_CASE( "veh_interact_install_mount_from_activity", "[vehicle][multifloor]" )
{
    // values[4],values[5] = -dd (xy mount); values[7] = sel_z.
    std::vector<int> v{ 0, 0, 0, 0, 2, 3, 0, 1 };
    CHECK( veh_interact_install_mount( v ) == tripoint_rel_ms( 2, 3, 1 ) );
    // Back-compat: an activity queued before M6 has no values[7] -> z 0.
    std::vector<int> old{ 0, 0, 0, 0, 2, 3, 0 };
    CHECK( veh_interact_install_mount( old ) == tripoint_rel_ms( 2, 3, 0 ) );
}
```

- [ ] **Step 2: Run to verify failure**

Run: `.nas-build/cdda.sh test "veh_interact_install_mount_from_activity"`
Expected: link FAIL — helper undefined.

- [ ] **Step 3: Add the reconstruction helper**

Declare in `src/veh_interact.h` (with the other free helper):

```cpp
tripoint_rel_ms veh_interact_install_mount( const std::vector<int> &activity_values );
```

Define in `src/veh_interact.cpp`:

```cpp
tripoint_rel_ms veh_interact_install_mount( const std::vector<int> &activity_values )
{
    const int z = activity_values.size() > 7 ? activity_values[7] : 0;
    return tripoint_rel_ms( activity_values[4], activity_values[5], z );
}
```

- [ ] **Step 4: Thread `sel_z` through packing + completion**

At the activity-values packing (`:189`, right after `values[6]`):

```cpp
res.values.push_back( sel_z ); // values[7] : deck to install on
```

In `complete_vehicle` (`:3076`), replace the 2D reconstruction and the install call (`:3121`):

```cpp
const tripoint_rel_ms d = veh_interact_install_mount( you.activity.values );
```

and

```cpp
const int partnum = veh.install_part( here, d, part_id, std::move( base ), installed_with );
```

(`install_part` resolves the 3D overload automatically now that `d` is a `tripoint_rel_ms`.) Fix the neighbouring `debugmsg` that prints `dx=%d dy=%d` to also print `d.z()` if desired.

- [ ] **Step 5: Route cursor resolution to `sel_z`**

`part_at` (`:2163`):

```cpp
int veh_interact::part_at( const point_rel_ms &d )
{
    const point_rel_ms vd{ -dd + d.rotate( 1 ) };
    return veh->part_displayed_at( tripoint_rel_ms( vd, sel_z ) );
}
```

In `move_cursor` (`:2210-2216`), resolve the target tile at the deck's z and use the 3D display lookup:

```cpp
cpart = part_at( point_rel_ms::zero );
const point_rel_ms vd = -dd;
const point_rel_ms q = veh->coord_translate( vd );
tripoint_bub_ms vehp = veh->pos_bub( here ) + q;
vehp.z() += sel_z;
```

The install-preview `can_mount` (`:909`):

```cpp
const ret_val<void> can_mount = veh->can_mount( tripoint_rel_ms( -dd, sel_z ), *sel_vpart_info );
```

Any other `can_mount( -dd, ... )` / `part_displayed_at( -dd )` uses in the install/overview path: wrap the mount as `tripoint_rel_ms( -dd, sel_z )`.

- [ ] **Step 6: Run helper test + the multifloor install coverage**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: PASS (helper test + the existing `upper_deck_frame_supported_by_frame_below` install coverage still green — the 3D `install_part`/`can_mount` it exercises are the same ones the UX now drives).

- [ ] **Step 7: Build tiles + astyle + commit**

```bash
.nas-build/build-retry.sh build-tiles   # foreground; confirm tiles build links
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp && chown 3000:3003 src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp"
git add src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): install/resolve at selected deck (sel_z) in veh_interact

Thread sel_z through the install activity (values[7], back-compat guarded)
and route part_at/move_cursor/can_mount preview through the 3D overloads.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: [B3] Preview — draw the deck below dimmed as a support reference

**Files:**
- Modify: `src/veh_interact.h` (declare `select_preview_decks`), `src/veh_interact.cpp` `display_veh` (`:2346-2359`)
- Test: `tests/vehicle_multifloor_test.cpp`

**Interfaces:**
- Consumes: `sel_z`.
- Produces: free function `std::pair<int, std::optional<int>> veh_interact_preview_decks( int sel_z )` → `{ primary_z = sel_z, reference_z = (sel_z > 0 ? sel_z - 1 : nullopt) }`.

- [ ] **Step 1: Write the failing test** (append)

```cpp
std::pair<int, std::optional<int>> veh_interact_preview_decks( int sel_z );

TEST_CASE( "veh_interact_preview_decks", "[vehicle][multifloor]" )
{
    CHECK( veh_interact_preview_decks( 0 ) == std::pair<int, std::optional<int>>{ 0, std::nullopt } );
    CHECK( veh_interact_preview_decks( 1 ) == std::pair<int, std::optional<int>>{ 1, 0 } );
    CHECK( veh_interact_preview_decks( 2 ) == std::pair<int, std::optional<int>>{ 2, 1 } );
}
```

- [ ] **Step 2: Run to verify failure**

Run: `.nas-build/cdda.sh test "veh_interact_preview_decks"`
Expected: link FAIL — helper undefined.

- [ ] **Step 3: Define the helper**

Declare in `src/veh_interact.h`; define in `src/veh_interact.cpp`:

```cpp
std::pair<int, std::optional<int>> veh_interact_preview_decks( int sel_z )
{
    if( sel_z > 0 ) {
        return { sel_z, sel_z - 1 };
    }
    return { sel_z, std::nullopt };
}
```

- [ ] **Step 4: Use it in `display_veh`**

Replace the single structural-part loop (`:2346-2359`) with a two-pass draw. First the reference deck dimmed (if any), then the primary deck. Filter structural parts by `vp.mount.z()`:

```cpp
const auto [primary_z, ref_z] = veh_interact_preview_decks( sel_z );

// reference deck (deck below), dimmed, for build-support orientation
if( ref_z ) {
    for( const int idx : veh->all_parts_at_location( "structure" ) ) {
        const vehicle_part &vp = veh->part( idx );
        if( vp.mount.z() != *ref_z ) {
            continue;
        }
        const vpart_display vd = veh->get_display_of_tile( vp.mount, false, false );
        const point_rel_ms q = ( vp.mount.xy() + dd ).rotate( 3 );
        mvwputch( w_disp, h_size + q.raw(), c_dark_gray, vd.symbol_curses );
    }
}

// primary deck (the one being edited)
for( const int structural_part_idx : veh->all_parts_at_location( "structure" ) ) {
    const vehicle_part &vp = veh->part( structural_part_idx );
    if( vp.mount.z() != primary_z ) {
        continue;
    }
    const vpart_display vd = veh->get_display_of_tile( vp.mount, false, false );
    const point_rel_ms q = ( vp.mount.xy() + dd ).rotate( 3 );
    if( q != point_rel_ms::zero ) { // cursor is not on this part
        mvwputch( w_disp, h_size + q.raw(), vd.color, vd.symbol_curses );
        continue;
    }
    cpart = structural_part_idx;
    col_at_cursor = vd.color;
    sym_at_cursor = vd.symbol_curses;
}
```

(`get_display_of_tile( vp.mount, ... )` uses the 3D overload. At `sel_z == 0`, `ref_z` is empty and only the primary loop runs, filtering to z==0 — identical to today's single-deck vehicles, whose structural parts are all z==0.)

- [ ] **Step 5: Run helper test + no-op check**

Run: `.nas-build/cdda.sh test "[multifloor]"` → PASS.

- [ ] **Step 6: Manual tiles verification**

Build tiles (`.nas-build/build-retry.sh build-tiles`, foreground). Launch, spawn `test_bus_2floor`, open the vehicle-construction UI (`e`), press `<` to go to Deck 1: confirm the upper deck's parts draw solid, the lower deck draws dimmed underneath, and the "Deck 1" readout shows; installing a frame over a supported tile succeeds while an unsupported tile is rejected; `>` returns to Deck 0 with unchanged appearance. (Rendering glue is not unit-testable; this manual pass is the check, consistent with how M3/M4 verified rendering.)

- [ ] **Step 7: astyle + commit**

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c \
  "apt-get update -qq && apt-get install -y -qq astyle && astyle --options=.astylerc -n src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp && chown 3000:3003 src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp"
git add src/veh_interact.h src/veh_interact.cpp tests/vehicle_multifloor_test.cpp
git commit -m "feat(veh): draw deck-below dimmed as build-support reference

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Final validation (before PR)

- [ ] Full **isolated** suite: `.nas-build/cdda.sh suite` (wraps `build-scripts/gha_test_only.sh`) — not a single `cata_test` call. All green. Or `.nas-build/gate.sh` for build + suite in one.
- [ ] Tiles release build compiles (`.nas-build/build-retry.sh build-tiles`, foreground).
- [ ] `astyle` 3.1 + `make style-json` clean.
- [ ] Whole-branch opus review (per prior-milestone workflow) = 0 Crit / 0 Imp.
- [ ] PR with merge commit titled `Multi-floor vehicles — Milestone 6: loot_zones/labels 3D + construction UX (#N)`; delete branch after.
- [ ] Update the `multi-floor-vehicles` memory: M6 merged; note remaining carried backlog (split z-base normalization, passenger fall-edge, ramp/collapse tests, open-top shelter semantics, `try_vehicle_deck_move` destination-occupant displacement).

## Out of scope (unchanged from spec)

Tip-over / top-heavy stability; monster AI z-targeting; NPC upper-deck pathing; mapgen z-part spawning.
```
