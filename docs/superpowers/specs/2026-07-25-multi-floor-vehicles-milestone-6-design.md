# Multi-floor vehicles — Milestone 6 design

**Status:** approved (brainstormed 2026-07-25)
**Depends on:** M1–M5 + the post-M5 frame-connectivity refinement (PR #7), all merged to `master`.
**Branch:** `explore/multi-floor-vehicles-m6`
**Supersedes:** the M6 line item in `2026-07-21-multi-floor-vehicles-design.md`
(§Phase milestones item 6, "Deferred / optional"). This document scopes M6
down to two concrete, committed pieces and explicitly drops the tip-over
physics model.

## Goal

Two independent workstreams under one milestone:

- **A — loot_zones & labels 3D keys:** close the last known multi-floor
  *correctness* gap. Zone/label keys are currently 2D, so an upper-deck zone
  collides with the same x/y on the lower deck and a multi-floor split can lose
  or cross-assign them.
- **B — multi-floor construction UX:** make an upper deck *player-buildable*
  through `veh_interact`, rather than debug/spawn-only, via a z-aware
  construction cursor driven by `<` / `>`.

The workstreams share no code and may be built in either order; the
recommended order is A first (self-contained correctness fix, good warm-up),
B second.

The single-floor **no-op** invariant is the through-line of both workstreams,
exactly as in M1–M5:

- Workstream A: saves stay **byte-identical** for any vehicle with no z-parts.
- Workstream B: behavior is unchanged whenever `sel_z == 0`.

## Explicitly out of scope

Unchanged from the parent spec's deferral list — called out so they are not
mistaken for gaps:

- **Top-heavy / tip-over stability model.** Mass center stays planar by design
  (parent spec §5). Dropped from M6 by decision, not oversight.
- Monster AI z+1 targeting/melee onto the upper deck.
- NPC boarding / pathing to upper-deck seats.
- Mapgen / loot spawning of vehicles that already have z-parts.

---

## Workstream A — loot_zones & labels 3D keys

### Problem

- `loot_zones` is `std::unordered_multimap<point_rel_ms, zone_data>`
  (`src/vehicle.h:2392`).
- `labels` is `std::set<label>` where `struct label : point_rel_ms`
  (`src/vehicle.h:707, 2386`).

Both are keyed by the **2D** mount. Present code masks this with `.xy()`
band-aids carrying "deferred to a later milestone" comments:

- `vehicle::remove_part` — `src/vehicle.cpp:2431` (loot_zones erase),
  `:2449` (label erase).
- `split_vehicles` / carry-veh reattach loops — `src/vehicle.cpp:2770-2874`
  (`:2866` loot_zones `equal_range`, `:2874` erase) and the second zone-copy
  loop at `:8962-8982`.
- `vehicle::shift_parts` — `src/vehicle.cpp:7882-7892` (labels + loot_zones
  re-key by `delta`).

On a multi-floor split, a zone/label at `(x, y, 1)` and one at `(x, y, 0)`
share a key and collide: one can overwrite or follow the wrong sub-vehicle.
Single-floor games never hit this (only one z), which is why it was safe to
defer.

### Change

1. **Types.** Widen the `loot_zones` key to `tripoint_rel_ms`. Change
   `struct label` to extend `tripoint_rel_ms` instead of `point_rel_ms`.
2. **Call sites.** Thread the full 3D `vehicle_part::mount` (already 3D since
   M1) through every site listed above; remove the `.xy()` truncation and the
   deferral comments. `parts_at_relative` already has a 3D overload (M2) for the
   emptiness checks near the label erase.
3. **Save migration — the M1 `mount_dz` omit-when-zero pattern**
   (`src/savegame_json.cpp`):
   - **zones** (`:3526-3533` read, `:3602-3610` write): keep reading/writing
     the existing `"point"` member as the xy; add an optional integer `"z"`
     member read with a default of `0` and written **only when non-zero**.
     Reconstruct the key as `tripoint_rel_ms( p.x, p.y, z )`.
   - **labels** (`:3519` read, `:3601` write, via `label::serialize` /
     `label::deserialize`): add the same optional omit-when-zero `"z"` member
     to the label (de)serializer.
   - Net effect: pre-M6 saves (no `"z"`) load at z=0; any single-floor vehicle
     re-serializes byte-identically (no `"z"` emitted).

### Test (Workstream A)

A `[multifloor]` regression test (new file or an addition to the existing
multifloor vehicle test) that:

- Spawns `test_bus_2floor` (the M2 prototype).
- Places a loot zone **and** a label on the lower deck and on the upper deck at
  the **same** (x, y).
- Triggers a split (e.g. remove a connecting structural part / the path used by
  `find_and_split_vehicles`).
- Asserts both zones and both labels survive and stay on the correct deck —
  neither lost nor cross-assigned.

This is precisely the regression that is impossible to pass under the current
2D keys, so it is RED before the change and GREEN after.

Additionally: a save → load round-trip assertion that a two-deck zone/label set
round-trips through JSON intact, and that a single-floor vehicle's serialized
output contains no `"z"` member (byte-identical guard).

---

## Workstream B — multi-floor construction UX (`veh_interact`)

### Current shape

`veh_interact` is built around a **2D** cursor `point_rel_ms dd`
(`src/veh_interact.h:73`). The main loop reads a direction that is *already*
3D-capable and discards z:

```
src/veh_interact.cpp:516
if( const std::optional<tripoint_rel_ms> vec = main_context.get_direction_rel_ms( action ) ) {
    move_cursor( here, vec->xy() );   // z dropped here
}
```

`part_at()` (`:2163`) resolves through `veh->part_displayed_at( vd )`;
`move_cursor()` (`:2199`) rebuilds the `can_mount` candidate list and resolves
the target map tile. The underlying `part_displayed_at`, `can_mount`, and
`install_part` all already have **3D `tripoint_rel_ms` overloads** from M2/M3.
`dd`'s arithmetic is planar screen projection (`.rotate()` etc.) — z does not
rotate — so z is orthogonal to it.

### Change

1. **State.** Add `int sel_z = 0;` to `veh_interact`. Keep `dd` 2D. The pair
   `(dd, sel_z)` is the effective 3D cursor.
2. **Navigation.** Register two new actions in the `VEH_INTERACT` context
   (`src/veh_interact.cpp:275-300`): `SELECT_Z_UP` and `SELECT_Z_DOWN`,
   default-bound `<` and `>` respectively (both keys are currently free in this
   context). Add the default bindings to `data/raw/keybindings.json`. Handle
   them in the main-loop `handle_input` switch (near `:514`) by adjusting
   `sel_z` and re-running the cursor refresh that `move_cursor` performs.
   - **Clamp:** `sel_z ∈ [ mount_min_z(), mount_max_z() + 1 ]` using the
     accessors added in M3. This lets any vehicle step exactly one level above
     its current top to start a new deck, but not wander into dead air.
3. **Cursor resolution.** Route `part_at()` and `move_cursor()` through the 3D
   overloads at `sel_z`: `part_displayed_at( tripoint_rel_ms( vd, sel_z ) )`,
   and target-tile / `can_mount` / install computed at the same z. The install
   flow (`do_install`, `:969`) passes the 3D mount to `install_part`.
   `can_mount`'s frame-support rules (PR #7 — an upper frame needs a structural
   frame directly below, climbing needs a `VERTICAL_TRAVERSAL` part) already
   enforce build legality; the UI only surfaces the existing verdict.
4. **Preview (`display_veh`, `:2311`).** Draw `sel_z`'s parts normally. When
   `sel_z > 0`, first draw the deck directly below (`sel_z - 1`) **dimmed** as a
   reference layer, so the player can see where the supporting frames — hence
   the legal build spots — are. When `sel_z == 0`, rendering is unchanged.
5. **Header indicator.** Show the current deck (e.g. `Deck: N` or a z readout)
   in the interact header so the active level is always visible. Minimal, but
   without it the z-state is invisible.

### No-op guarantee

At `sel_z == 0` every 3D call collapses to its existing 2D behavior (the
overloads delegate at z=0, as established M2), the preview draws exactly as
before (no `sel_z - 1` layer), and the header reads deck 0. A single-floor
vehicle's construction UI is unchanged.

### Test (Workstream B)

A `[multifloor]` test exercising the cursor/install path (at the `veh_interact`
helper level, or a direct exercise of the same resolution + `install_part`
calls, whichever is reachable without a live UI loop — mirror how prior
milestones tested install/boarding):

- Start on a single-floor vehicle; assert `sel_z` starts at 0 and clamps at 0
  on `SELECT_Z_DOWN` and at `max_z + 1 == 1` on `SELECT_Z_UP` (range check).
- At `sel_z == 1`, over a tile with a structural frame directly below: install a
  frame — succeeds.
- At `sel_z == 1`, over a tile with **no** support below: install attempt is
  rejected (surfacing the `can_mount` verdict).
- Confirm `sel_z == 0` resolution is identical to the pre-change 2D path (no-op
  guard).

---

## Validation (both workstreams)

Same gates and harness as M1–M5:

- Full **isolated** suite via `build-scripts/gha_test_only.sh` (not a single
  `cata_test` call), plus the targeted `[multifloor]` / `[vehicle]` cases.
- Tiles build compiles.
- `astyle` 3.1 + `make style-json` clean.
- Whole-branch opus review = 0 Crit / 0 Imp before merge.
- Built/tested via the NAS Docker harness (`.nas-build/`, `build-retry.sh`,
  **foreground** — the jobserver stall is recurring; background builds get
  reaped). See the `nas-build-status` memory.

Merged via PR with a merge commit titled
`Multi-floor vehicles — Milestone 6: … (#N)` (the M2–M4 convention); branch
deleted after.

## Carried backlog NOT addressed by M6

For the record, the remaining post-M5 deferrals that M6 deliberately does not
touch (they are smaller and independent; can be follow-ups):

- Split z-base normalization to non-negative.
- Passenger fall-edge when `parts_to_move` is non-empty (dropping upper rider).
- Down-direction upper-deck-rider ramp test; ramp + floor-collapse-fall combined
  test.
- Open-top-deck "inside" / shelter semantics.
- `try_vehicle_deck_move` displacing a creature already on the destination deck
  tile.
