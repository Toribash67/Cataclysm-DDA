# Vehicle Vertical Connectivity — Frame-Based Structure, Ladder-Based Traversal

**Date:** 2026-07-25
**Branch:** (to be created off `master`)
**Status:** Approved design, ready for implementation planning
**Baseline:** This fork's `master`, multi-floor milestones M1–M5 merged (PRs #2–#6).

**Supersedes:** §1 "Connectivity semantics" of
`docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md`, which decided
option (a) *ladder-gated connectivity* (a single `VERTICAL_CONNECTOR` flag gating
both structure and climbing). That decision is revised here.

## Goal

Make a vehicle's **vertical** structure obey the **same rule as its horizontal
structure**: tiles are tied into one vehicle by **frames**, not by a special
connector part. Retire `VERTICAL_CONNECTOR` as the *structural* gate and replace
it with the ordinary "is there a frame?" test already used on the x/y axes. A
narrower, renamed flag continues to govern the one thing frames genuinely cannot:
whether a creature can physically **climb** between decks.

The motivation is that the current model conflates two distinct concepts into one
flag — "is this structurally one vehicle" and "can I walk up here." Frames handle
the first cleanly; only the second needs a dedicated opening (ladder / hatch /
stairwell). Separating them makes multi-floor authoring feel like normal vehicle
building and removes the special-case connector from the structural path.

## Background: how connectivity works today

Every vertical concern currently hangs off a single predicate,
`vehicle::has_vertical_connector_at()` (the `VPFLAG_VERTICAL_CONNECTOR` flag,
carried only by `ladder_internal`). It is consulted in exactly three places:

| Concern | Site | Gated on today |
|---|---|---|
| **Building** — "can I mount this upper-deck part?" | `can_mount` → `supported_from_below` (`src/vehicle.cpp:1407-1409`) | connector directly below |
| **Connectivity** — "one vehicle? where do splits cut?" | `connected_neighbours` (`src/vehicle.cpp:1281-1287`), feeding `can_unmount` (`:1510`) and `find_and_split_vehicles` BFS (`:1572`, `:2682`) | connector on lower tile |
| **Traversal** — "can a creature climb decks?" | `allows_deck_traversal` (`src/vehicle.cpp:1291`) → `game::try_vehicle_deck_move` (`src/game.cpp:12306`) | connector on from/below tile |

The planar analog this design mirrors already sits next door: `supported_in_plane`
checks `has_structural_part()` on the four in-plane neighbours
(`src/vehicle.cpp:1399-1404`), and `connected_neighbours` returns all four planar
neighbours unconditionally.

Two facts confirmed against the data:

- A bare **frame** (`frame_abstract`, `data/json/vehicleparts/frames.json`) is
  `location: structure` with flags `MOUNTABLE, INITIAL_PART` — **not** boardable or
  roofed. So `deck_floor` (`ROOF, BOARDABLE`) remains the walkable upper surface;
  the frame beneath it is pure structure. This is already the frame+floor pairing
  the test bus uses (`data/json/vehicles/custom_vehicles.json:309-312`).
- Non-frame parts (tank, seat, cargo) attach **in-plane** to a frame; only frames
  form the load-bearing skeleton. So the "external tank" exception the design must
  respect falls out for free — an upper-deck tank attaches in-plane to an upper
  frame and never needs a frame directly beneath *it*; only the frame does.

## Decision: frame-based structure, traversal split into its own flag

Keep the physical climb affordance (ladder / hatch / stairwell) as the sole gate
on **traversal**. Move **structure and connectivity** onto frames, identical to the
x/y axes.

### §1 — The one new structural rule

An upper-deck **frame** is supported iff there is a **structural part on the tile
directly below it**. In `can_mount` (`src/vehicle.cpp:1407-1409`):

```cpp
const bool supported_from_below =
    dp.z() > 0 &&
    has_structural_part( dp + tripoint_rel_ms::below );   // was has_vertical_connector_at
```

This is the exact 3D mirror of the existing `supported_in_plane`
`has_structural_part` neighbour check. `has_structural_part` is already 3D-ready
(`src/vehicle.cpp:1244`). Nothing else about upper-deck building changes: every
non-frame upper part (seat, floor, cargo, tank) already rides the existing
`supported_in_plane` path off the upper frame. Only the **frame** consults the new
below-rule.

Chosen rule form (from brainstorming): **frame directly below only** — not
"below-or-above" and not "any frame in the 6-neighbourhood." Building is bottom-up,
this is the smallest and most predictable change, and it preserves the "you build a
floor on top of what's under it" intuition.

### §2 — Connectivity and splits follow the frame

In `connected_neighbours` (`src/vehicle.cpp:1281-1287`), the vertical edge exists
when a **frame** spans it rather than a connector:

- up-edge from `mount` iff `has_structural_part( mount )` (this tile is a frame);
- down-edge iff `has_structural_part( mount + below )`.

`can_unmount` and `find_and_split_vehicles` both already iterate
`connected_neighbours`, so split/merge detection becomes frame-aware with no
further edits — and strictly more robust than today: a frame-stacked deck ties the
two levels together at *every* stacked frame pair, not only at the single ladder
tile.

### §3 — Traversal splits off into its own flag

Rename the flag so its name reflects its now-narrower meaning:

- `VPFLAG_VERTICAL_CONNECTOR` → `VPFLAG_VERTICAL_TRAVERSAL`
  (enum in `src/veh_type.h:121-122`; string→flag row in `src/veh_type.cpp:154`).
- `vehicle::has_vertical_connector_at` → `vehicle::has_traversal_part_at`
  (declaration `src/vehicle.h:831`; definition `src/vehicle.cpp:1258`).
- Update the explanatory comment at `src/game.h:328` and the multi-floor comments
  at `src/vehicle.cpp:1278-1280`, `1296-1301`, `1405-1406`, `1322`.

The renamed predicate now feeds **only** `allows_deck_traversal`
(`src/vehicle.cpp:1291`). `ladder_internal` keeps the (renamed) flag; frames never
carry it. Result: a creature still climbs **only** at the ladder tile, while the
decks are structurally one vehicle wherever they are frame-stacked.

The rename is recommended, not cosmetic: leaving a flag named "connector" after it
no longer governs structural connection would misdescribe the code.

### §4 — Touch list

- **`src/vehicle.cpp`**: `can_mount` support test (§1); `connected_neighbours`
  vertical edges (§2); rename helper + its call sites in `allows_deck_traversal`.
- **`src/veh_type.h`, `src/veh_type.cpp`**: flag enum + string-map rename (§3).
- **`src/vehicle.h`, `src/game.h`, `src/game.cpp`**: predicate rename + comments.
- **`data/json/vehicleparts/multifloor.json`**, **`vp_flags.json`**: rename the flag
  on `ladder_internal` and its `vp_flags` definition. `deck_floor` unchanged.
- **`data/json/vehicles/custom_vehicles.json`** (`test_bus_2floor`): **unchanged** —
  already valid under the new rule, and more solidly so (all four upper frames sit
  directly over ground frames).

### §5 — Tests

The invariant genuinely changes, so the tests that pin the old invariant are
**rewritten to the new one**, not deleted:

- Tests asserting *connector-gated structure* (a `z=1` part cannot mount without a
  connector below; connector-gated split behaviour) become **frame-gated**: a `z=1`
  frame mounts iff a frame sits directly below; splits cut where frames stop
  stacking. Same shape of assertion, new gate.
- **Traversal** tests (climb succeeds only at the ladder tile; a bare frame stack is
  *not* climbable) stay, and gain an explicit case proving structure ≠ traversal:
  two decks joined only by stacked frames are **one vehicle** yet **not** climbable
  without a ladder/hatch.
- New/updated `can_mount` cases: upper frame needs frame-below; non-frame upper part
  (seat/tank/cargo) mounts in-plane off an upper frame with **no** frame directly
  beneath itself (the external-attach exception).

Run the whole suite via `build-scripts/gha_test_only.sh` per CLAUDE.md (isolated,
`--order lex`), not a single `cata_test` call; a single named call is fine while
iterating.

### §6 — Out of scope (unchanged)

No physics change, no top-heavy / tip-over model, no `veh_interact` install-on-floor
UX. Those remain deferred to M6. This design covers only the connectivity and
authoring model.

## Risks

- **Test churn is the real cost.** The logic change is ~3 functions; the effort
  concentrates in migrating the M2/M3 tests to the frame-gated invariant. Mitigated
  by the shapes being identical (swap the gate, not the assertion structure).
- **Structure-vs-traversal regressions.** The whole point is that a frame stack now
  connects but does *not* let you climb. A missed call site that still equates the
  two would either wrongly split a frame-stacked vehicle or wrongly let a creature
  climb a solid deck. The dedicated §5 "one vehicle yet not climbable" test is the
  guard.
- **Split-detection breadth.** Frame-stacked decks now connect at every stacked
  frame pair, so `find_and_split_vehicles` explores more vertical edges. This is
  correct, but the multi-edge case (removing one frame from a multiply-connected
  stack must *not* split) needs a targeted test.
