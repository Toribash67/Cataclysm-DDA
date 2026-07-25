# Vehicle Vertical Frame-Connectivity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a vehicle's vertical structure obey the same frame-based rule as its horizontal structure — retire `VERTICAL_CONNECTOR` as the *structural* gate in favor of frames, and narrow the renamed flag to govern only deck-to-deck climbing.

**Architecture:** Three predicates carry all vertical behavior today, all gated on one flag (`has_vertical_connector_at`). We (1) rename the flag/helper to reflect a traversal-only meaning, then (2) swap `can_mount`'s upper-deck support test and (3) `connected_neighbours`'s vertical edges from the flag to `has_structural_part` (frames). Traversal (`allows_deck_traversal`) keeps using the renamed flag, so structure and climbing become independent concerns.

**Tech Stack:** C++17 (`src/vehicle.cpp`, `src/veh_type.{h,cpp}`, `src/game.{h,cpp}`), JSON content (`data/json/vehicleparts/`), Catch2 tests (`tests/vehicle_multifloor_test.cpp`, `tests/vehicle_ramp_test.cpp`). Build/test via the NAS Docker harness.

## Global Constraints

- **Design source of truth:** `docs/superpowers/specs/2026-07-25-vehicle-vertical-frame-connectivity-design.md`. This plan implements exactly that; it supersedes §1 of `docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md`.
- **New flag name:** `VERTICAL_TRAVERSAL` (enum `VPFLAG_VERTICAL_TRAVERSAL`); new helper name `has_traversal_part_at`. Use these exact identifiers everywhere.
- **Structural rule (final):** an upper-deck **frame** is supported iff there is a structural part on the tile **directly below** it (`dp.z() > 0` only). Non-frame upper parts continue to attach via in-plane adjacency (`supported_in_plane`). Rule form is "frame directly below" — never "below-or-above" or "any 6-neighbour."
- **Traversal stays flag-gated:** only `allows_deck_traversal` consults `has_traversal_part_at`; `ladder_internal` is the only part carrying `VERTICAL_TRAVERSAL`; frames never carry it.
- **Build (curses, incremental):** run in the FOREGROUND — `.nas-build/build-retry.sh build` (recurring jobserver stall; background builds get reaped). Tiles build is not needed (pure logic).
- **Single test:** `.nas-build/cdda.sh test "<test name or tag>"`. **Full isolated suite + build gate:** `.nas-build/gate.sh` (per CLAUDE.md, never a single `cata_test` call for the whole suite — `[monster]` tests cross-contaminate).
- **C++ style:** astyle 3.1 conventions — spaces inside parens `if( x )`, `--align-pointer=name`, 4-space indent, 100-col. Match surrounding code.
- **Commits:** conventional `type(veh): …`, ending with the trailer `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Work happens on branch `feat/vehicle-vertical-frame-connectivity` (already created; the design doc is committed there as `795420296e`).

---

## File Structure

| File | Responsibility | Touched in |
|---|---|---|
| `src/veh_type.h` | `VPFLAG_*` enum entry + doc comment | T1 |
| `src/veh_type.cpp` | flag string→enum map row | T1 |
| `src/vehicle.h` | `has_traversal_part_at` decl + `connected_neighbours` doc comment | T1, T3 |
| `src/vehicle.cpp` | the three predicates: helper def, `can_mount` support, `connected_neighbours` | T1, T2, T3 |
| `src/game.h` | `try_vehicle_deck_move` doc comment | T1 |
| `data/json/vehicleparts/vp_flags.json` | `json_flag` definition | T1 |
| `data/json/vehicleparts/multifloor.json` | `ladder_internal` flag list | T1 |
| `tests/vehicle_multifloor_test.cpp` | connectivity/traversal/mount tests | T1–T4 |
| `tests/vehicle_ramp_test.cpp` | one flag-string reference | T1 |
| `docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md` | superseding note in §1 | T5 |

---

## Task 1: Rename `VERTICAL_CONNECTOR` → `VERTICAL_TRAVERSAL` (behavior-preserving)

Pure identifier/string rename across C++, JSON, and tests. No logic or invariant changes: after this task every existing test still passes for the same reason as before, just under the new names. This isolates the mechanical churn from the two behavioral changes in Tasks 2–3.

**Files:**
- Modify: `src/veh_type.h:120-122`, `src/veh_type.cpp:154`, `src/vehicle.h:831`, `src/vehicle.cpp` (7 sites), `src/game.h:328`
- Modify: `data/json/vehicleparts/vp_flags.json:703-707`, `data/json/vehicleparts/multifloor.json:16`
- Test: `tests/vehicle_multifloor_test.cpp` (lines 44-48, 272, 364, 501), `tests/vehicle_ramp_test.cpp:251`

**Interfaces:**
- Produces: `enum vpart_bitflags::VPFLAG_VERTICAL_TRAVERSAL`; `bool vehicle::has_traversal_part_at( const tripoint_rel_ms & ) const`; JSON flag string `"VERTICAL_TRAVERSAL"`. Tasks 2–4 rely on all three.

- [ ] **Step 1: Rewrite the enum entry and its comment**

In `src/veh_type.h`, replace lines 120-122:
```cpp
    // Connects this tile to the tile directly above/below it, letting a vehicle
    // have a permanent second deck. See docs multi-floor-vehicles design §2.
    VPFLAG_VERTICAL_CONNECTOR,
```
with:
```cpp
    // Marks a climbable opening (ladder/hatch/stairwell) between this tile and the
    // deck directly above/below it. Governs deck-to-deck TRAVERSAL only; structural
    // connectivity between decks is carried by frames. See docs
    // 2026-07-25-vehicle-vertical-frame-connectivity-design.
    VPFLAG_VERTICAL_TRAVERSAL,
```

- [ ] **Step 2: Rename the flag string→enum map row**

In `src/veh_type.cpp:154`, replace:
```cpp
    { "VERTICAL_CONNECTOR", VPFLAG_VERTICAL_CONNECTOR },
```
with:
```cpp
    { "VERTICAL_TRAVERSAL", VPFLAG_VERTICAL_TRAVERSAL },
```

- [ ] **Step 3: Rename the helper and every use (C++ token substitution)**

Run these exact substitutions (both tokens are unique in `src/`):
```bash
cd /mnt/ssd_pool/martin/repos/Cataclysm-DDA
sed -i 's/has_vertical_connector_at/has_traversal_part_at/g' src/vehicle.h src/vehicle.cpp
sed -i 's/VPFLAG_VERTICAL_CONNECTOR/VPFLAG_VERTICAL_TRAVERSAL/g' src/vehicle.cpp
sed -i 's/\bVERTICAL_CONNECTOR\b/VERTICAL_TRAVERSAL/g' src/vehicle.cpp src/vehicle.h src/game.h
```
(The last line updates the flag name inside prose comments at `src/vehicle.cpp:1278`, `src/vehicle.h`'s `connected_neighbours` comment, and `src/game.h:328`. The `can_mount`/`connected_neighbours` *logic* comments are rewritten in Tasks 2–3; leaving the renamed flag token here is harmless in the interim.)

- [ ] **Step 4: Rename the JSON `json_flag` definition**

In `data/json/vehicleparts/vp_flags.json`, replace lines 703-707:
```json
  {
    "id": "VERTICAL_CONNECTOR",
    "type": "json_flag",
    "info": "Connects this tile to the tile directly above/below it, letting a vehicle have a permanent second deck."
  },
```
with:
```json
  {
    "id": "VERTICAL_TRAVERSAL",
    "type": "json_flag",
    "info": "A climbable opening (ladder/hatch/stairwell) letting a creature move between this deck and the one directly above or below it."
  },
```

- [ ] **Step 5: Rename the flag on `ladder_internal`**

In `data/json/vehicleparts/multifloor.json:16`, replace:
```json
    "flags": [ "VERTICAL_CONNECTOR", "BOARDABLE", "OBSTACLE" ],
```
with:
```json
    "flags": [ "VERTICAL_TRAVERSAL", "BOARDABLE", "OBSTACLE" ],
```

- [ ] **Step 6: Update test references (flag enum, flag strings, test name)**

Run:
```bash
cd /mnt/ssd_pool/martin/repos/Cataclysm-DDA
sed -i 's/VPFLAG_VERTICAL_CONNECTOR/VPFLAG_VERTICAL_TRAVERSAL/g' tests/vehicle_multifloor_test.cpp
sed -i 's/"VERTICAL_CONNECTOR"/"VERTICAL_TRAVERSAL"/g' tests/vehicle_multifloor_test.cpp tests/vehicle_ramp_test.cpp
```
Then in `tests/vehicle_multifloor_test.cpp`, rename the test case at line 44 — replace:
```cpp
TEST_CASE( "vertical_connector_flag_is_recognized", "[vehicle][multifloor]" )
{
    // The flag must resolve through the fast-path enum, not just the string set.
    CHECK( vpart_ladder_internal.obj().has_flag( VPFLAG_VERTICAL_TRAVERSAL ) );
}
```
with:
```cpp
TEST_CASE( "vertical_traversal_flag_is_recognized", "[vehicle][multifloor]" )
{
    // The flag must resolve through the fast-path enum, not just the string set.
    CHECK( vpart_ladder_internal.obj().has_flag( VPFLAG_VERTICAL_TRAVERSAL ) );
}
```

- [ ] **Step 7: Build**

Run: `.nas-build/build-retry.sh build`
Expected: build completes cleanly (exit 0), no `error:` diagnostics. A pure rename must compile.

- [ ] **Step 8: Run the affected suites to confirm no behavior change**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Then: `.nas-build/cdda.sh test "vehicle_ramp"`
Expected: both PASS, all cases green. (Behavior is identical; only names changed.)

- [ ] **Step 9: Commit**

```bash
git add src/veh_type.h src/veh_type.cpp src/vehicle.h src/vehicle.cpp src/game.h \
        data/json/vehicleparts/vp_flags.json data/json/vehicleparts/multifloor.json \
        tests/vehicle_multifloor_test.cpp tests/vehicle_ramp_test.cpp
git commit -m "$(cat <<'EOF'
refactor(veh): rename VERTICAL_CONNECTOR flag to VERTICAL_TRAVERSAL

Behavior-preserving rename ahead of the frame-based connectivity change: the
flag and its helper (has_traversal_part_at) now name a climb affordance, not a
structural connector.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Frame-based upper-deck mount (`can_mount`)

Switch `can_mount`'s upper-deck support test from "traversal part below" to "frame below," mirroring the in-plane rule. Fold the two now-obsolete connector-gated mount tests into frame-based ones.

**Files:**
- Modify: `src/vehicle.cpp:1405-1409` (`supported_from_below`)
- Test: `tests/vehicle_multifloor_test.cpp` (rewrite `upper_deck_mount_requires_vertical_connector`; delete `upper_deck_mount_allowed_above_connector`; add one in-plane test)

**Interfaces:**
- Consumes: `bool vehicle::has_structural_part( const tripoint_rel_ms & ) const` (`src/vehicle.cpp:1244`, already 3D).
- Produces: `can_mount` accepts an upper-deck frame iff a structural part sits directly below; no traversal part required.

- [ ] **Step 1: Write the failing test (rewrite the connector-mount test)**

In `tests/vehicle_multifloor_test.cpp`, replace the entire `upper_deck_mount_requires_vertical_connector` case (lines 52-81) with:
```cpp
TEST_CASE( "upper_deck_frame_supported_by_frame_below", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const vpart_info &hdframe = vpart_id( "hdframe" ).obj();

    // Empty tile directly below -> nothing supports an upper-deck frame there.
    const tripoint_rel_ms unsupported( 0, 0, 1 );
    CHECK( !veh->can_mount( unsupported, hdframe ).success() );

    // A plain structural frame on a new ground tile ((-1,-2) is planar-adjacent to the
    // car's real structure at (-1,-1)). A frame directly below now legalizes an
    // upper-deck frame stacked on it -- no ladder/traversal part required.
    const tripoint_rel_ms ground( -1, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );

    const tripoint_rel_ms above( -1, -2, 1 );
    CHECK( veh->can_mount( above, hdframe ).success() );
}
```

- [ ] **Step 2: Delete the obsolete "allowed above connector" test**

In `tests/vehicle_multifloor_test.cpp`, delete the entire `upper_deck_mount_allowed_above_connector` case (originally lines 83-103). Its intent — "a frame below legalizes the upper frame" — is now covered by Step 1, and the ladder is no longer what legalizes a mount.

- [ ] **Step 3: Run the test to verify it fails**

Run: `.nas-build/build-retry.sh build && .nas-build/cdda.sh test "upper_deck_frame_supported_by_frame_below"`
Expected: FAIL at the final `CHECK( veh->can_mount( above, hdframe ).success() )` — under the traversal gate a plain frame below does not legalize the upper frame, and `(-1,-2,1)` has no in-plane upper neighbour.

- [ ] **Step 4: Implement the frame-based support rule**

In `src/vehicle.cpp`, replace lines 1405-1409:
```cpp
        // Decks connect only through an explicit vertical VERTICAL_TRAVERSAL: a bare
        // z-neighbour is NOT connectivity (multi-floor design section 1).
        const bool supported_from_below =
            dp.z() > 0 &&
            has_traversal_part_at( dp + tripoint_rel_ms::below );
```
with:
```cpp
        // Upper-deck parts are supported by a frame directly below, exactly as
        // in-plane parts are supported by an adjacent frame (frame-based vertical
        // structure; see 2026-07-25-vehicle-vertical-frame-connectivity-design).
        const bool supported_from_below =
            dp.z() > 0 &&
            has_structural_part( dp + tripoint_rel_ms::below );
```
(The sed in Task 1 will have rewritten the old comment's flag token to `VERTICAL_TRAVERSAL`; match on whatever text is actually present and replace the whole block.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `.nas-build/build-retry.sh build && .nas-build/cdda.sh test "upper_deck_frame_supported_by_frame_below"`
Expected: PASS.

- [ ] **Step 6: Add the in-plane (external-attach) characterization test**

In `tests/vehicle_multifloor_test.cpp`, add after the Step 1 test:
```cpp
TEST_CASE( "upper_deck_nonframe_part_needs_no_frame_directly_below", "[vehicle][multifloor]" )
{
    // A non-frame upper-deck part (seat) mounts by planar adjacency to an existing
    // upper-deck frame, with NO frame directly beneath the seat itself -- the vertical
    // analog of the in-plane / external-attach rule. (Setup itself depends on the
    // frame-below mount rule: installing the upper frame requires a frame below it.)
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    const tripoint_rel_ms ground( 2, -2, 0 );
    REQUIRE( veh->install_part( here, ground, vpart_frame ) >= 0 );
    const tripoint_rel_ms upper_frame( 2, -2, 1 );
    REQUIRE( veh->install_part( here, upper_frame, vpart_frame ) >= 0 );

    // Seat one tile further out on the upper deck: planar-adjacent to the upper frame,
    // but the tile directly beneath it (3,-2,0) is empty. It must still mount, supported
    // purely by the in-plane upper frame.
    const tripoint_rel_ms seat_tile( 3, -2, 1 );
    REQUIRE( veh->parts_at_relative( tripoint_rel_ms( 3, -2, 0 ), false ).empty() );
    CHECK( veh->can_mount( seat_tile, vpart_id( "seat" ).obj() ).success() );
}
```

- [ ] **Step 7: Run both mount tests**

Run: `.nas-build/cdda.sh test "upper_deck_frame_supported_by_frame_below"` then `.nas-build/cdda.sh test "upper_deck_nonframe_part_needs_no_frame_directly_below"`
Expected: both PASS. (The second's `install_part( upper_frame )` only succeeds because of the Step 4 change — it would fail to set up under the old gate.)

- [ ] **Step 8: Commit**

```bash
git add src/vehicle.cpp tests/vehicle_multifloor_test.cpp
git commit -m "$(cat <<'EOF'
feat(veh): support upper-deck frames by the frame directly below

can_mount now legalizes an upper-deck frame when a structural frame sits directly
beneath it, mirroring the in-plane adjacency rule -- no traversal part needed.
Non-frame upper parts still attach in-plane.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Frame-based vertical connectivity (`connected_neighbours`)

Switch the vertical edges in `connected_neighbours` from the traversal flag to frames, so split/merge detection (which routes through this single predicate) becomes frame-aware. Traversal stays flag-gated in `allows_deck_traversal` — the two now diverge, which is the point.

**Files:**
- Modify: `src/vehicle.cpp:1271-1289` (`connected_neighbours` vertical edges + comment)
- Modify: `src/vehicle.h` (`connected_neighbours` doc comment above its declaration)
- Test: `tests/vehicle_multifloor_test.cpp` (rewrite `removing_connector_splits_off_upper_deck` and `upper_deck_removal_blocked_when_only_link_is_connector`)

**Interfaces:**
- Consumes: `has_structural_part` (as Task 2).
- Produces: vertical connectivity edge between a lower tile and the tile above it exists iff the lower tile holds a structural frame. `allows_deck_traversal` is unchanged (still flag-gated).

- [ ] **Step 1: Write the failing test (rewrite the split-on-connector test)**

In `tests/vehicle_multifloor_test.cpp`, replace the entire `removing_connector_splits_off_upper_deck` case (originally lines 264-287) with:
```cpp
TEST_CASE( "removing_ladder_keeps_decks_connected", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_TRAVERSAL", false );
    REQUIRE( ladder >= 0 );

    // Removing the climb affordance must NOT split the vehicle: the decks are held
    // together structurally by the stacked frames, independent of traversal.
    veh->remove_part( veh->part( ladder ) );
    veh->find_and_split_vehicles( here, {} );

    bool still_has_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( vpr.part().mount.z() == 1 && !vpr.part().removed ) {
            still_has_upper = true;
            break;
        }
    }
    CHECK( still_has_upper );
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.nas-build/build-retry.sh build && .nas-build/cdda.sh test "removing_ladder_keeps_decks_connected"`
Expected: FAIL — under the traversal-gated `connected_neighbours`, the ladder is the only vertical edge, so removing it splits the upper deck away and `still_has_upper` is false.

- [ ] **Step 3: Implement frame-based vertical edges**

In `src/vehicle.cpp`, replace the body of `connected_neighbours` (lines 1271-1289) with:
```cpp
std::vector<tripoint_rel_ms> vehicle::connected_neighbours( const tripoint_rel_ms &mount ) const
{
    std::vector<tripoint_rel_ms> neighbours;
    neighbours.reserve( 6 );
    for( const point &offset : four_adjacent_offsets ) {
        neighbours.emplace_back( mount + tripoint_rel_ms( offset.x, offset.y, 0 ) );
    }
    // Vertical edges are carried by frames, exactly like planar edges: the edge
    // between a lower tile and the tile above it exists iff the LOWER tile holds a
    // structural frame (which is what supports the upper part). Climbing between decks
    // is a separate concern gated by VERTICAL_TRAVERSAL in allows_deck_traversal().
    if( has_structural_part( mount ) ) {
        neighbours.emplace_back( mount + tripoint_rel_ms::above );
    }
    const tripoint_rel_ms below = mount + tripoint_rel_ms::below;
    if( has_structural_part( below ) ) {
        neighbours.push_back( below );
    }
    return neighbours;
}
```

- [ ] **Step 4: Update the `connected_neighbours` doc comment in the header**

In `src/vehicle.h`, the comment block above the `connected_neighbours` declaration (the block just after the `has_traversal_part_at` declaration, ~lines 832-836) currently describes connector-gated adjacency. Replace that comment block with:
```cpp
        // Frame-gated adjacency of a mount: its four planar neighbours (same z), plus
        // the tile above (iff this tile holds a structural frame) and the tile below
        // (iff that lower tile holds a structural frame). Frames carry vertical
        // structure the same way they carry planar structure; a bare non-frame
        // z-neighbour never connects. Single source of the deck-connectivity rule
        // shared by is_connected / can_unmount / find_and_split_vehicles. Traversal
        // (climbing) is separate -- see allows_deck_traversal / VERTICAL_TRAVERSAL.
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `.nas-build/build-retry.sh build && .nas-build/cdda.sh test "removing_ladder_keeps_decks_connected"`
Expected: PASS — frames now hold the decks together after the ladder is gone.

- [ ] **Step 6: Rewrite the "only link is connector" removal test to a frame bridge**

In `tests/vehicle_multifloor_test.cpp`, replace the entire `upper_deck_removal_blocked_when_only_link_is_connector` case (originally lines 234-262), **including its long NOTE comment**, with:
```cpp
// A single stacked frame is the sole structural bridge to a second upper-deck tile.
// Removing the bridge frame would strand that tile, so can_unmount must refuse it --
// the 3D, frame-gated "would this split the vehicle?" check. A traversal-gated (pre-
// change) connected_neighbours gives the bridge frame only its planar neighbour, sees
// a removable end tile, and wrongly allows the removal.
TEST_CASE( "upper_deck_removal_blocked_when_only_link_is_frame_bridge", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // b0: ground bridge frame, planar-adjacent to the car's real structure.
    const tripoint_rel_ms b0( 2, -2, 0 );
    REQUIRE( veh->install_part( here, b0, vpart_frame ) >= 0 );

    // u0: upper frame stacked on b0. Its 3D neighbours are b0 (down, through the frame)
    // and u1 (planar, same z).
    const tripoint_rel_ms u0( 2, -2, 1 );
    REQUIRE( veh->install_part( here, u0, vpart_frame ) >= 0 );

    // u1: a second upper-deck tile reachable from the rest of the vehicle ONLY through
    // u0 -- there is no frame under u1 at (3,-2,0).
    const tripoint_rel_ms u1( 3, -2, 1 );
    REQUIRE( veh->install_part( here, u1, vpart_frame ) >= 0 );

    const int idx_u0 = structure_part_at( *veh, u0 );
    REQUIRE( idx_u0 >= 0 );

    CHECK( !veh->can_unmount( veh->part( idx_u0 ), false ).success() );
}
```

- [ ] **Step 7: Run the frame-bridge removal test**

Run: `.nas-build/cdda.sh test "upper_deck_removal_blocked_when_only_link_is_frame_bridge"`
Expected: PASS — under frame-gated connectivity `u0` has two neighbours (`b0` down, `u1` planar); removing it strands `u1`, so `can_unmount` fails. (This is discriminating: under the old gate `u0`'s only neighbour was `u1`, so removal was wrongly allowed.)

- [ ] **Step 8: Run the whole multifloor suite to catch collateral**

Run: `.nas-build/cdda.sh test "[multifloor]"`
Expected: all PASS. Pay attention to `try_vehicle_deck_move_*`, `allows_deck_traversal_*` (traversal must still work only at the ladder) and `two_floor_bus_*`.

- [ ] **Step 9: Commit**

```bash
git add src/vehicle.cpp src/vehicle.h tests/vehicle_multifloor_test.cpp
git commit -m "$(cat <<'EOF'
feat(veh): carry vertical vehicle connectivity through frames

connected_neighbours now links a tile to the deck above/below it through a
structural frame instead of the VERTICAL_TRAVERSAL flag, so split/merge detection
is frame-based. Removing a ladder no longer splits a frame-stacked vehicle;
climbing remains gated separately by allows_deck_traversal.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Structure-vs-traversal guard tests

Lock the invariant that the two concepts are now independent, and guard the split-breadth risk (a multiply-connected deck must not split when one of several frame bridges is removed).

**Files:**
- Test: `tests/vehicle_multifloor_test.cpp` (two new cases)

**Interfaces:**
- Consumes: everything from Tasks 1–3 (`VERTICAL_TRAVERSAL` string, frame-based `connected_neighbours`, `allows_deck_traversal`).
- Produces: none (tests only).

- [ ] **Step 1: Add the "connected but not climbable" guard**

In `tests/vehicle_multifloor_test.cpp`, add:
```cpp
TEST_CASE( "frame_stacked_decks_connect_but_do_not_allow_climbing", "[vehicle][multifloor]" )
{
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    const int ladder = veh->part_with_feature( tripoint_rel_ms( 0, 0, 0 ),
                       "VERTICAL_TRAVERSAL", false );
    REQUIRE( ladder >= 0 );
    veh->remove_part( veh->part( ladder ) );
    veh->find_and_split_vehicles( here, {} );

    // Still one vehicle with an upper deck: frames connect the decks structurally...
    bool still_has_upper = false;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            still_has_upper = true;
            break;
        }
    }
    REQUIRE( still_has_upper );

    // ...but with the ladder gone there is no way to climb between the decks.
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 0 ), 1 ) );
    CHECK_FALSE( veh->allows_deck_traversal( tripoint_rel_ms( 0, 0, 1 ), -1 ) );
}
```

- [ ] **Step 2: Add the split-breadth regression guard**

In `tests/vehicle_multifloor_test.cpp`, add:
```cpp
TEST_CASE( "removing_one_of_several_frame_bridges_does_not_split", "[vehicle][multifloor]" )
{
    // The bus stacks a frame on every upper tile, so the decks are connected at
    // multiple points. Removing a single ground frame under one stack must not split
    // the vehicle: the upper tile above it still reaches ground through neighbouring
    // stacks.
    map &here = get_map();
    clear_map();
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_bus_2floor,
                                     tripoint_bub_ms( 60, 60, 0 ), 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    // Ground frame under the (1,1) stack; its upper (1,1,1) also neighbours upper
    // frames at (1,0,1) and (0,1,1), which keep their own ground frames.
    const int gi = structure_part_at( *veh, tripoint_rel_ms( 1, 1, 0 ) );
    REQUIRE( gi >= 0 );
    veh->remove_part( veh->part( gi ) );
    veh->find_and_split_vehicles( here, {} );

    int upper_tiles = 0;
    for( const vpart_reference &vpr : veh->get_all_parts() ) {
        if( !vpr.part().removed && vpr.part().mount.z() == 1 ) {
            upper_tiles++;
        }
    }
    // All four original upper tiles (0,0)/(0,1)/(1,0)/(1,1) remain on the one vehicle.
    CHECK( upper_tiles >= 4 );
}
```

- [ ] **Step 3: Run the two new guards**

Run: `.nas-build/build-retry.sh build && .nas-build/cdda.sh test "[multifloor]"`
Expected: all PASS, including the two new cases.

- [ ] **Step 4: Commit**

```bash
git add tests/vehicle_multifloor_test.cpp
git commit -m "$(cat <<'EOF'
test(veh): guard structure-vs-traversal split of vertical connectivity

Frame-stacked decks stay one vehicle yet are unclimbable without a ladder, and a
multiply-connected deck survives removal of one frame bridge.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Supersede note + full-suite gate

Record the decision in the older design doc and run the full isolated suite as the final gate.

**Files:**
- Modify: `docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md` (§1 note)

**Interfaces:** none.

- [ ] **Step 1: Add the superseding note to the multi-floor design §1**

In `docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md`, find the paragraph beginning `**Connectivity semantics (decided: option (a), ladder-gated).**` (in §1) and insert, immediately before it, this line:
```markdown
> **Superseded 2026-07-25:** the ladder-gated connectivity decided below was
> replaced by frame-based vertical structure (frames connect decks like they
> connect the x/y plane) with the renamed `VERTICAL_TRAVERSAL` flag gating only
> climbing. See `2026-07-25-vehicle-vertical-frame-connectivity-design.md`.

```

- [ ] **Step 2: Run the full build + isolated suite gate**

Run: `.nas-build/gate.sh`
Expected: `GATE END … exit=0` — fresh build succeeds and the entire isolated suite passes. If any non-multifloor vehicle test fails, investigate before proceeding (do not paper over it).

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md
git commit -m "$(cat <<'EOF'
docs(veh): note frame-based connectivity supersedes ladder-gated §1

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Offer to finish the branch**

Invoke the `superpowers:finishing-a-development-branch` skill to decide how to integrate (PR / merge / cleanup).

---

## Self-Review

**Spec coverage:**
- §1 new structural rule → Task 2 (`can_mount` `supported_from_below` → `has_structural_part(below)`), plus the frame-directly-below "rule form" pinned in Global Constraints. ✓
- §2 connectivity/splits follow the frame → Task 3 (`connected_neighbours`), routing through `can_unmount`/`find_and_split_vehicles` unchanged. ✓
- §3 traversal splits into its own flag → Task 1 (rename `VPFLAG_VERTICAL_TRAVERSAL`, helper `has_traversal_part_at`, comments), with `allows_deck_traversal` left as the sole consumer. ✓
- §4 touch list → all files covered (T1–T5 table). ✓
- §5 tests → Task 2 (frame-below mount + in-plane exception), Task 3 (frame-gated split/removal rewrites), Task 4 (structure≠traversal guard + multi-edge split). ✓
- §6 out of scope (no physics) → nothing in the plan touches mass/collision. ✓
- Risks (test churn, structure-vs-traversal regressions, split breadth) → Tasks 3–4 tests + Task 5 full-suite gate. ✓

**Placeholder scan:** no TBD/TODO; every code step shows the exact replacement text and every command shows expected output. ✓

**Type consistency:** `VPFLAG_VERTICAL_TRAVERSAL`, `has_traversal_part_at`, `has_structural_part`, `"VERTICAL_TRAVERSAL"`, `connected_neighbours`, `allows_deck_traversal`, `structure_part_at` (existing test helper at `tests/vehicle_multifloor_test.cpp:204`), `vpart_frame`, `vpart_ladder_internal` — all used consistently across tasks and match the current signatures verified in the source. ✓
