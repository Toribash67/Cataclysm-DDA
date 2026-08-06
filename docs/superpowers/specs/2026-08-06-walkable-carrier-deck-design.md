# Walkable carrier deck — design

**Date:** 2026-08-06
**Status:** Approved (design); implementation plan to be written next
**Builds on:** the multi-floor vehicles effort (PR #2–#9, merged) and the
drive-on carrying feature Milestone 1 (branch `veh-drive-on-carrying`, PR #10).
See `docs/superpowers/specs/2026-08-05-vehicle-drive-on-carrying-design.md` and
the memory notes `multi-floor-vehicles` / `vehicle-ramp-lock-feature`.

## Goal

Let a **person walk up onto, and around, the raised deck of a parked flatbed** —
standing next to the carried car. Concretely: walk on foot up the loading ramp to
the bed (z+1), move around the deck freely, and walk back down. Everyone —
avatar, NPCs, and monsters — can be on the deck.

This is the on-foot counterpart to Milestone 1, which taught *vehicles* to drive
up the ramp. It does **not** cover the vehicles-lock-and-drive-as-one work; see
Non-goals.

## Origin: this started as a bug, and the bug reframed the feature

The original task was "fix board/unboard error spam when walking a character onto
the flatbed" (`map::unboard_vehicle: passenger not found` / `vehicle not found`
from `game::place_player`). Reproduction attempts in `tests/vehicle_flatbed_test.cpp`
established that this is **not an independent bug**:

- Walking the avatar across the *parked* flatbed at z0 (ground → ramp → frames →
  seats) raises no debugmsg.
- Driving the car up with the avatar aboard to z+1, then probing board state /
  `unboard_vehicle` at the avatar's z+1 tile, is clean — `veh_at` finds the
  boardable car part and unboard succeeds. The Milestone 1 `displace_vehicle`
  rider-sync (`c244ce7063`) already keeps a boarded rider consistent.

The only thing that actually breaks when a character is up on the deck is that
**the deck is not a character-floor**: walking onto/across it drops into open-air
/ ledge handling. So "fix the board spam" collapses into "make the deck
walkable." The board handling is folded into this feature rather than patched in
isolation.

## Key empirical finding (this shrinks the feature)

The memory assumed characters never recognize a vehicle roof as support, implying
we'd need to invent a whole floor model. **That is wrong.** Measured on a truck
roof-deck tile at z+1 (a tile whose z0 part below has a roof):

| Predicate | Value | Meaning |
|---|---|---|
| `map::is_open_air(deck)` | `true` | terrain-wise it is open sky |
| `map::has_floor(deck)` | `false` | no terrain floor |
| `map::valid_move(deck, below)` | `false` | **cannot fall — the roof part below blocks the down-move** |
| `map::has_floor_or_support(deck)` | `true` | supported |

An avatar placed on the deck and run through `gravity_check` **does not fall**.
The support already exists via `map::valid_move` → `vehicle::roof_at_part`
(map.cpp:2621). **Standing on the deck already works.**

The gap is purely *movement*: a deck tile is `is_open_air()==true` with no vehicle
part at z+1, so `game::walk_move` classifies a step onto/across it as a **ledge**
(`game.cpp:10546`, because `veh_dest` at `game.cpp:10492` only checks for a part
*at* the destination z+1 tile — there is none). With only a "ledge" hazard,
`walk_move` calls `iexamine::ledge` (`game.cpp:10730`), which opens the climb-down
menu. In test mode that menu `assert(!test_mode)`s in `uilist::init`; in play it
is a spurious climb-down prompt on every deck step.

## Established design decisions

Settled during brainstorming; these constrain everything below.

1. **Opt-in flagged deck.** Only roof parts carrying a new `WALKABLE_ROOF` flag
   become walkable decks. Generic `roof`, tarps, and ordinary car roofs are
   unaffected — no surprise walkable surfaces.
2. **Stationary deck only.** The person walks on a parked (stopped/braked) truck's
   deck. Being *carried while the truck drives* is out of scope (see Non-goals) —
   it requires the person to be a passenger with a z+1 boardable part, which
   collides with the car resting on the bare roof, and only matters once the rig
   drives as one unit (M2).
3. **Everyone, including monsters.** Avatar, NPCs, and monsters can be on the
   deck. Therefore the "reads as floor" logic lives in the shared map layer and
   the per-mover movement/pathfinding gates, not only the avatar path.
4. **Approach A — new predicate, targeted integration.** A single new map
   predicate is the source of truth; each mover's decision point is routed
   through it. We do **not** synthesize a z+1 vehicle part (Approach B) — that
   would put a part at z+1 and collide with the car driving up (the exact
   same-tile collision M1 was built to avoid) — and we do **not** use explicit
   multi-floor deck parts (Approach C) for the same collision reason.

## Data model (content)

- **New vpart flag `WALKABLE_ROOF`**, with its required `json_flag` entry in
  `data/json/vehicleparts/vp_flags.json` (a vpart flag used in JSON without a
  `vp_flags.json` entry debugmsgs "unknown flag" on load).
- **New deck part `veh_flatbed_deck`** — a roof-type part (`location: on_roof` or
  the `ROOF` flag, so it still provides fall-support through `roof_at_part`) that
  additionally carries `WALKABLE_ROOF`. `test_flatbed_truck`'s bed (mounts
  x = -1, 0) uses this instead of the generic `roof`.
- The **ramp is unchanged** (`veh_ramp_fixed`, already `VEH_RAMP_UP` /
  `VEH_RAMP_DOWN` + `BOARDABLE`); it is the way up.

**Definition.** A *deck tile* is an open-air tile at z+1 whose vehicle part one
z-level below carries `WALKABLE_ROOF`.

## Core predicate (single source of truth)

```cpp
// true when p has no real floor (open air) AND the tile directly below (p.z-1)
// has a vehicle part flagged WALKABLE_ROOF.
bool map::deck_floor_below( const tripoint_bub_ms &p ) const;
```

Every mover's "is this walkable ground?" gate routes through this one predicate,
which keeps the movers from disagreeing. Fall-prevention
(`valid_move` → `roof_at_part`) already works and is **left untouched**; this
predicate only governs *walking / pathing onto* the deck.

## Mover integration (the audit)

The risk in Approach A is that different movers ask "can I stand here?" in their
own code; a synthetic deck floor must be taught to each. The audit, by mover:

- **Avatar** — the ledge gate at `game.cpp:10546` (and the mounted-creature
  variant ~`game.cpp:10472`): add `&& !deck_floor_below(dest)` so no false
  "ledge" hazard is raised, the climb-down menu never opens, and the step
  proceeds. This is the change proven necessary by reproduction.
- **NPCs & monsters — pathfinding**: teach the pathfinder to accept a deck tile as
  a valid route node. Part of this may already hold, since `map::passable_through`
  folds in `has_floor_or_support`; the audit confirms and fills gaps.
- **NPCs & monsters — move execution**: verify the actual step onto a deck tile is
  not rejected as open air (expected already fine — horizontal `valid_move` treats
  open air as passable and the roof prevents the fall), and add the predicate
  wherever it is not.

## The way up (ramp lift)

A creature stepping from the ground onto the ramp rises to the deck at z+1,
mirroring terrain `RAMP_UP`, reusing the existing `veh_ramp_dir()` helper so the
on-foot path and the drive-up path cannot diverge. Applied in the avatar move path
(`avatar_action::move`, currently terrain-`RAMP_UP`-only at avatar_action.cpp:235)
and the monster / NPC move paths.

This is the **highest-risk piece**: vehicle-ramp geometry differs from terrain
ramps (the lift happens transitioning *onto the deck*, not by standing on the ramp
tile), and each mover executes movement differently. It gets the most test
attention. If monster ramp-climbing proves gnarly, avatar + NPC ramp-up can land
first and monster-up follow as a later task within this feature.

## Testing

Per-mover coverage, to *prove* no mover's gate was missed:

- **Avatar** — walk up the ramp → onto the deck → across it → back down. Assert:
  no `debugmsg`, no ledge/climb-down menu (no `uilist` assert), correct z at each
  step, no fall. This is what the trimmed exploratory probe in
  `tests/vehicle_flatbed_test.cpp` becomes.
- **NPC** — a companion routes onto the deck.
- **Monster** — a monster paths up onto the deck.
- **Negative / opt-in respected** — a vehicle with a generic (unflagged) `roof`
  stays non-walkable: stepping onto it still reads as open air / ledge. Confirms
  tarps and ordinary roofs are unaffected.
- **Regressions stay green** — `[flatbed]`, `[ramp]`, `[vehicle]`, `[multifloor]`
  suites; the car still drives up and rests on the deck (no new z+1 part → no
  collision).

## Non-goals (explicitly deferred)

- **Person carried while the truck drives** → Milestone 2 (vertical lock /
  drive-as-one). Requires the person to be a passenger (z+1 boardable part), which
  conflicts with the car resting on the bare roof, and only matters once the
  loaded rig drives as one unit.
- **The vertical lock / grid-join itself** → Milestone 2.
- **Making all roofs walkable** — only `WALKABLE_ROOF`-flagged decks.
- **Any non-stationary deck behavior.**

## Files expected to change

- `data/json/vehicleparts/vp_flags.json` — `WALKABLE_ROOF` flag entry.
- `data/json/vehicleparts/vehicle_ramp.json` or a nearby parts file — the
  `veh_flatbed_deck` part.
- `data/json/vehicles/custom_vehicles.json` — `test_flatbed_truck` bed uses
  `veh_flatbed_deck`.
- `src/map.h` / `src/map.cpp` — `deck_floor_below` predicate.
- `src/game.cpp` — avatar ledge gate(s) route through the predicate.
- `src/avatar_action.cpp` — on-foot ramp lift for vehicle ramps.
- Monster / NPC pathfinding + move execution (`src/pathfinding.cpp`,
  `src/monmove.cpp`, `src/npcmove.cpp` — exact sites confirmed during planning).
- `tests/vehicle_flatbed_test.cpp` — per-mover walkable-deck tests.
