# Vehicle ramp + drive-on carrying — design

**Date:** 2026-08-05
**Status:** Approved (design); Milestone 1 to be planned next
**Builds on:** the multi-floor vehicles effort (PR #2–#9, merged). See
`docs/superpowers/specs/2026-07-21-multi-floor-vehicles-design.md` and the memory
notes `multi-floor-vehicles` / `vehicle-ramp-lock-feature`.

## Goal

Simulate a **towtruck carrying a car on its flatbed**: drive a smaller, separate
vehicle up a ramp onto a bigger vehicle's raised bed, then lock the two grids so
they drive as one. The end state is analogous to the bike rack, but the carried
vehicle sits on the deck **above** (z+1) rather than beside the carrier.

## Established design decisions

These were settled during brainstorming and constrain everything below:

1. **Full simulation, drive-up + lock** — not a park-and-merge shortcut. The
   player physically drives the car up a ramp, then it locks.
2. **Ramp/traversal is built first** (Milestone 1), the lock second (Milestone 2).
   Rationale: the traversal is the novel, unproven engine work; proving it first
   de-risks the rest. The lock reuses the proven bike-rack pipeline.
3. **The truck is stationary during loading.** The car is only ever driven onto a
   parked (stopped/braked) truck. This is the key scope lever: it reduces
   traversal to "one vehicle climbing a static support" — the terrain-ramp case —
   and avoids ever simulating two vehicles in relative motion.
4. **Geometry / collision model.** Two vehicles can never share a tile (same
   x/y/z). The truck is a normal ground-level (z0) vehicle whose rear section has
   a strong **bed-roof**. The ramp lets the car climb from the ground (z0) onto
   that bed at **z+1**, where it is supported by the truck's roof at z0 below.
   The car at z+1 therefore never shares a tile with a truck part at z+1 —
   sidestepping the collision problem. Any deck railings are edge parts the car
   does not overlap.
5. **Two ramp part types exist** (mirroring how CDDA has both openable doors and
   fixed walls): a **deployable** ramp (open = slope deployed / climbable,
   closed = stowed flush / drivable) and an **always-on fixed** slope. Both share
   the same underlying "acts as a RAMP_UP/DOWN for another vehicle" behavior.
6. **Chosen approach: reuse the terrain-ramp machinery, driven by a vehicle
   part** (Approach A below). Rejected: projecting the bed as virtual terrain
   (brittle, desyncs); a custom vehicle-on-vehicle physics layer (overkill given
   the parked-truck constraint).

## Milestones

- **M1 (this spec's focus): traversal.** Drive a separate car up a ramp part onto
  a parked truck's bed; it comes to rest at z+1, supported by the truck's roof,
  still a *separate* vehicle. Proves geometry, collision-avoidance, and
  cross-vehicle support. No merge yet — driving the truck away would leave the car
  behind; that intermediate state is acceptable as a feasibility milestone.
- **M2 (later, separate spec/plan): capture/lock.** Merge the resting car into the
  truck as `carried_flag` parts via a vertical adaptation of the bike-rack
  pipeline (`find_vehicles_to_rack`, `merge_rackable_vehicle`,
  `merge_vehicle_parts`, `find_vehicles_to_unrack` in `src/vehicle.cpp`), so the
  rig drives as one; plus unlock / drive-off.
- **Out of scope entirely:** any two-vehicles-in-motion physics; driving a car
  around on a moving deck.

## Milestone 1 design

### A. Ramp vehicle parts (JSON + a new vpart flag)

- Add a new `vpart_bitflags` entry — **`VEH_RAMP_UP`** / **`VEH_RAMP_DOWN`** — a
  part tile that acts as a climb/descend slope for *another* vehicle. Register it
  in `src/veh_type.cpp`'s flag table, the same pattern as the vertical-connector
  flag added in the multi-floor work.
- **Fixed ramp part** — always presents `VEH_RAMP_UP`/`DOWN`.
- **Deployable ramp part** — an `OPENABLE`, door-like part: "open" = deployed
  (flags active, climbable), "closed" = stowed flush (flags inert, drivable).
  Reuses the existing door open/close toggle; the ramp flag is only live while
  open.
- Author both in a new `data/json/vehicleparts/` file. Add a
  `test_flatbed_truck` vehicle prototype for tests (mirroring `test_bus_2floor`):
  a z0 truck with a strong bed-roof over the rear and a ramp part at the tail.

### B. Traversal mechanism (core engine change)

Extend `vehicle::advance_precalc_mounts` (`src/vehicle.cpp` ~8895). Today it
probes ground terrain for `TFLAG_RAMP_UP/DOWN` at `src + dp + ground_probe` and,
on a hit, does `precalc[0].z() += 1` / `-= 1`. Add a parallel probe: query the
map (`here->veh_at(...)`) for a vehicle part carrying `VEH_RAMP_UP/DOWN` at that
same point. If found on a **different, stationary** vehicle, apply the identical
z-transition. Everything downstream — `precalc_z_delta`, the `precalc_mounts`
memo, rendering — is unchanged; the climbing car spans z0→z1 exactly as it does on
a terrain ramp today.

### C. Cross-vehicle support (feasibility spike — gate this first)

The single unproven assumption in Approach A: a vehicle resting at z+1 must be
**supported** by the truck's bed-roof at z0, or it reads as open-air / falls.
Validate before committing the rest of the plan:

- Author the truck bed with a strong roof; in a test, hand-place a car at z+1 over
  it; assert it does not fall and the tiles report a floor.
- If the multi-floor support/floor check does not already treat a *foreign*
  vehicle's roof as floor for a vehicle one z above, extend that check (same
  family as the M3 z-projection audit sites in the multi-floor work) so a vehicle
  roof / BOARDABLE part counts as floor-support for a vehicle one level up.

This is gated first because it is the only thing that could invalidate Approach A.

### D. Testing

- New `tests/vehicle_flatbed_test.cpp`, mirroring `tests/vehicle_ramp_test.cpp`:
  place a parked `test_flatbed_truck`, drive a `test_car` up the ramp tile, assert
  its parts transition to z+1, come to rest on the bed, remain supported, and
  remain a *separate* vehicle (the M1 boundary — no merge yet).
- Deployable ramp: assert the ramp flag is inert when closed (car cannot climb)
  and active when open.
- Re-run the existing terrain-based `vehicle_ramp_test` to prove no regression on
  the terrain-ramp path (the shared `advance_precalc_mounts` code).

## Deferred / not in Milestone 1

- The capture/lock merge and driving the loaded rig (Milestone 2).
- Two-vehicles-in-motion physics (out of scope entirely).
- NPC/AI use of the ramp; weight / footprint validation and UI polish.

## Watch-outs (carried from the multi-floor work)

The z-projection caveats in the `multi-floor-vehicles` memo apply: several
`part_with_feature` / adjacency / `mount_pos()` sites still flatten z to 0.
Cross-vehicle support and the vehicle-ramp probe will exercise more of them; audit
as encountered. Build/test commands and the PR-green-then-merge flow are unchanged
(see the `build-setup-macos` and `ci-setup` memos).
