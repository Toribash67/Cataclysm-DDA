# Ammonia auto-synthesizer (data-driven powered fluid converter)

**Date:** 2026-08-11
**Repo:** `Toribash67/Cataclysm-DDA` (modified fork)
**Type:** New engine feature (C++) + supporting JSON. Branch `ammonia-auto-synthesizer`.

## Problem

Liquid ammonia is produced only by a manual craft recipe
(`data/json/recipes/chem/chemicals.json:385`, `result: ammonia_liquid`): a 30-minute
craft requiring four separately-built grid appliances as pseudo-tools
(`electrolyzer_makeshift`, `ammonia_machine_reactor`, `ammonia_machine_pipework`,
`nitrogen_generator`) that together draw ~65,650 charges of grid power to yield only
2 charges of ammonia (+ 1 salt_water byproduct) from 10 salt + 2 clean water. The
player must re-run the craft by hand every cycle. Conceptually the machine *ought*
to run on its own — as long as it has electricity and clean water it should keep
filling a vessel with ammonia while draining one of water.

## What the engine supports (research findings)

- **Reality bubble / time skip.** Nothing is simulated turn-by-turn while the map is
  unloaded (`map::process_items` only touches loaded submaps; `map::actualize` is the
  reload catch-up). Time-based results are computed lazily from stored timestamps
  (brewing, smoking, compost, base-camp missions). So "works while you're away" is
  achievable *if* completion is reconciled from stored state on the next relevant event.
- **`vehicle::update_time(map&, update_to)`** (`src/vehicle.cpp:8543`) is the catch-up
  hook. Over `elapsed = update_to - last_update` it backfills passive generation from
  **historical weather**: solar panels, wind turbines, water wheels each
  `charge_battery(...)` for the elapsed window. The existing **funnel + water purifier**
  block (`src/vehicle.cpp:8576-8611`) is the exact precedent for our converter: it locates
  a target tank with a `std::find_if` over `this->parts`, checks/consumes grid battery via
  `fuel_left(here, itype_battery)` / `discharge_battery(...)`, and writes the tank with
  `ammo_set`.
- **Power over an absence is knowable, within limits.** At catch-up the honest energy
  budget is: battery charge frozen at unload **+** solar/wind/water backfilled over the
  elapsed window. A fuel generator does **not** run while unloaded, so a genset-only rig
  produces nothing during an absence — only stored battery + passive generation count.
  This is physically fair and matches how the funnel purifier already behaves.
- **Fluids are per-vehicle; only electricity is shared across the cable grid.**
  `vehicle::fuel_left` / `fuel_capacity` / `drain` (`src/vehicle.cpp:4012+`) iterate this
  vehicle's own `fuel_containers` for liquids, but special-case `battery` to walk the whole
  cabled grid via `search_connected_vehicles` / `discharge_battery`. Cabled appliances only
  merge into one vehicle when both are power-grid parts; otherwise each keeps its own liquid
  pool. **Consequence:** the converter and its tanks must live on the *same vehicle*.
- **Tanks are per-part `FLUIDTANK` vehicle parts** (`VPFLAG_FLUIDTANK`,
  `src/vehicle_part.cpp`: `is_tank`, `ammo_current`, `ammo_remaining`,
  `ammo_capacity`, `ammo_set`, `ammo_consume`, `can_reload`). One vehicle can hold
  multiple tanks of different liquids on different tiles (fire-truck prototypes already
  do this). Player fills via the vehicle/appliance refill menu and harvests via siphon
  (needs a hose) — no new UI required.

## Design

Build the synthesizer as a **vehicle part** on a small player-built "ammonia plant"
vehicle, NOT as a single-tile power-grid appliance. Because `fuel_left`/`drain` and
`update_time` are vehicle-wide, the converter reaches every tank on its vehicle
regardless of tile — which removes any single-tile tank-colocation constraint.

### The player experience

1. Build a small vehicle: a frame carrying **water tank(s)**, an **ammonia tank**, a
   **battery bank**, **solar/wind** (for production while away), and the **ammonia
   synthesizer** part. The synthesizer part is crafted/installed from the four existing
   machine components **+ 10 salt** (the conserved internal reagent — see below).
2. Pour clean water into a water tank (standard vehicle fill).
3. Toggle the synthesizer **on** (standard vehicle part toggle).
4. Leave. While it is on and the vehicle has water + power, it converts water → ammonia
   over time, correctly reconciled on your return.
5. Siphon liquid ammonia out of the ammonia tank (standard siphon; needs a hose).

Salt is conserved by the reaction (`10 salt → 1 salt_water` in the manual recipe), so
it is modeled as a **one-time reagent folded into the synthesizer's build recipe (10
salt)** and recycled internally — never tracked in the continuous loop.

### New engine capability: a data-driven `fluid_converter` part behavior

Add optional fields on `vpart_info` (parsed near `epower` in `src/veh_type.cpp`, stored
in `src/veh_type.h`), so the behavior is reusable, not ammonia-specific:

```jsonc
"fluid_converter": {
  "input":  "water_clean",       // liquid drained from the vehicle
  "output": "ammonia_liquid",    // liquid produced into a compatible tank
  "byproduct": null,             // optional; omitted for ammonia (salt recycled)
  "energy_per_unit": "<power/energy per charge of output>",
  "max_rate": "<charges per hour throughput cap>"
}
```

Enable/disable via a toggle flag so the player can switch it off (reuse the existing
`ENABLED_DRAINS_EPOWER`-style on/off part-toggle machinery; the converter only runs
when the part is toggled on).

### The processing loop (C++, modeled on the funnel block)

In `vehicle::update_time` (catch-up over `elapsed`) — and, for responsiveness while the
vehicle is loaded, in the same per-turn power path the funnel/solar code already uses —
for each enabled converter part:

```
elapsed          = update_to - last_update            // already computed in update_time
energy_budget    = fuel_left(here, itype_battery)     // stored + solar/wind/water backfill
water_available  = fuel_left(here, input_fluid)       // vehicle-wide
ammonia_space    = free capacity of a tank that can_reload(output_fluid)

produced = min( max_rate * elapsed,
                water_available,
                energy_budget / energy_per_unit,
                ammonia_space )

if produced > 0:
    drain(here, input_fluid, produced)                // vehicle-wide input draw
    <add produced of output_fluid to the compatible tank via ammo_set/ammo_remaining>
    discharge_battery(here, produced * energy_per_unit)
    (if byproduct set) <add byproduct proportionally>
```

Because `update_time` backfills solar/wind/water into the battery *before* this runs, the
`energy_budget` is the honest post-absence figure. No power or no water → no ammonia. The
input is located vehicle-wide by liquid via `drain`; the output tank is found by
`can_reload(output_fluid)` (role-flags are unnecessary given vehicle-wide access, though a
future config could add an optional tank-selection flag).

### JSON deliverables

- New vehicle part `ammonia_synthesizer` (the converter) with the `fluid_converter`
  config + toggle flag, plus item + install/build recipe consuming the four machine
  parts + 10 salt.
- Tank parts for the plant: reuse existing `FLUIDTANK` tank parts (water, and a tank
  that accepts `ammonia_liquid`); confirm an ammonia-capable tank exists or add one.
- Keep the existing manual `ammonia_liquid` recipe unchanged (on-demand alternative).

### Balance knobs (tunable)

`energy_per_unit` (anchor to the existing item comments, e.g. "200 MJ per kg NH3"),
`max_rate` (throughput), water:ammonia ratio, tank capacities.

## Open items to resolve first in the plan

1. **Ammonia-capable tank.** Confirm a `FLUIDTANK` part will hold `ammonia_liquid`
   (phase/`can_reload` acceptance); add or adjust a tank part if needed.
2. **Toggle wiring.** Confirm the cleanest way to gate the loop on an on/off part state
   (reuse `ENABLED_DRAINS_EPOWER` toggle vs a dedicated flag).
3. **Per-turn vs catch-up only.** Decide whether to also tick while loaded (nicer to
   watch) or only reconcile in `update_time` (simpler); `update_time` handles both
   absence and, if called periodically, live operation.
4. **Save/load.** Ensure `last_update` semantics are correct so the converter neither
   double-counts nor loses elapsed time across save/reload (update_time already tracks
   `last_update`).

## Execution constraint

This is a C++ change. The authoring sandbox has **no C++ toolchain** (no `make`/compiler),
so it cannot be compiled or run here — build and in-game verification fall to the
maintainer's environment / CI. The change is well-precedented (the funnel+purifier block
is a near-exact template), but it is real engine work, not JSON-only.

## Out of scope

- Changing the existing manual ammonia recipe or the four existing machine appliances
  (beyond consuming them as build components for the synthesizer part).
- A generic fluid-network across the cable grid (does not exist; not adding one).
- Automatic water resupply (player refills the water tank; rain/plumbing integration is a
  possible later extension).
