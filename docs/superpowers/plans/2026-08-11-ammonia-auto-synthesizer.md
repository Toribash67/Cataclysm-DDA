# Ammonia Auto-Synthesizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a data-driven `fluid_converter` vehicle-part behavior that, while toggled on and powered, converts clean water into liquid ammonia over time inside a vehicle — reconciled correctly across time-skips — plus the `ammonia_synthesizer` part/item/recipe that uses it.

**Architecture:** A new optional `vpslot_fluid_converter` on `vpart_info` (parsed exactly like the `WORKBENCH` slot), gated by a new `VPFLAG_FLUID_CONVERTER` part flag. A conversion block added to `vehicle::update_time` — right after the solar/wind/water generation blocks, so the battery already reflects backfilled generation — reads a water tank, caps output by `min(rate·elapsed, water, battery-energy/energy_per_unit, output-tank space)`, drains water vehicle-wide, adds ammonia to a compatible tank, and discharges the battery. `update_time` already runs every turn (throttled to ~1/min) via `vehicle::idle` AND serves as the long-elapsed catch-up on reload, so no separate hook is needed. JSON adds the `ammonia_synthesizer` part + base item + craft recipe (four machine parts + 10 salt) and reuses stock `FLUIDTANK` tanks (which already accept `ammonia_liquid`).

**Tech Stack:** C++ (Cataclysm-DDA engine, `src/`), Catch2 tests (`tests/`), CDDA JSON. Build/test via `make` → `tests/cata_test`.

## Global Constraints

- **Toolchain required to execute.** These tasks compile C++ and run the game's test binary. The plan-authoring sandbox had no compiler; the executor MUST have the CDDA build toolchain. Build with `make` (or CMake per the repo); run tests with `tests/cata_test "<tag/name>"`; full check with `make check`.
- **Energy unit:** a battery "charge" (the integer used by `fuel_left(...,battery)`, `charge_battery`, `discharge_battery`) is **1 kJ** (`vehicle::power_to_energy_bat`, `src/vehicle.cpp:1196`).
- **Water:ammonia ratio is 1:1**, no byproduct in the continuous loop (salt is conserved and folded into the build recipe as a one-time 10-salt reagent). This mirrors the manual recipe's 2 water → 2 ammonia.
- **`ammo_set` REPLACES a tank's contents** — to add `n` charges, always `tank->ammo_set(id, tank->ammo_remaining() + n)` and call `invalidate_mass()` afterward (funnel pattern, `src/vehicle.cpp:8607-8609`).
- **JSON house style** is enforced by CI `make style-all-json-parallel`; match surrounding formatting.
- **Do not touch `master` directly** — work continues on branch `ammonia-auto-synthesizer` (already created; holds the design spec commit).
- **Confirmed by research (do not re-derive):** any `FLUIDTANK` part (e.g. `tank_small`) already holds `ammonia_liquid` (no new tank needed); ids `water_clean`, `ammonia_liquid`, `salt`, `electrolyzer_makeshift`, `ammonia_machine_reactor`, `ammonia_machine_pipework`, `nitrogen_generator` all exist; the four machine parts are appliance pseudo-tools, so the synthesizer needs a NEW `vehicle_part`.

## File Structure

- `src/veh_type.h` — add `struct vpslot_fluid_converter`, the `std::optional<...> fluid_converter_info` member, and `VPFLAG_FLUID_CONVERTER` enum entry.
- `src/veh_type.cpp` — add the `{ "FLUID_CONVERTER", VPFLAG_FLUID_CONVERTER }` bitflag-map entry and the `fluid_converter` load block in `vpart_info::load`.
- `src/vehicle.h` — declare the `std::vector<int> fluid_converters;` part-index cache.
- `src/vehicle.cpp` — populate `fluid_converters` in `refresh()`; extend the `update_time` guard and add the conversion block.
- `src/vehicle_use.cpp` — add the electronics-menu toggle for `FLUID_CONVERTER`.
- `tests/vehicle_fluid_converter_test.cpp` — new Catch2 test file (auto-discovered).
- `data/json/vehicleparts/ammonia_synthesizer.json` — new vehicle part.
- `data/json/items/…` — the `ammonia_synthesizer` base item.
- `data/json/recipes/…` — craft recipe for the base item (4 machine parts + 10 salt).

---

## Task 1: Register the `FLUID_CONVERTER` flag and parse the `fluid_converter` slot

**Files:**
- Modify: `src/veh_type.h` (struct near :157; member near :301-305; enum near :107)
- Modify: `src/veh_type.cpp` (bitflag map near :141; load block after the WORKBENCH block ~:433)

**Interfaces:**
- Produces: `vpart_info::fluid_converter_info` (`std::optional<vpslot_fluid_converter>`) with fields `input` (`itype_id`), `output` (`itype_id`), `byproduct` (`std::optional<itype_id>`), `energy_per_unit` (`units::energy`), `max_rate` (`int`, charges/hour). Consumed by Task 3.
- Produces: `VPFLAG_FLUID_CONVERTER` bitflag + `"FLUID_CONVERTER"` string flag.

- [ ] **Step 1: Add the slot struct and members (`src/veh_type.h`).**

Near the other `vpslot_*` structs (`vpslot_workbench` is at ~`src/veh_type.h:157`), add:
```cpp
struct vpslot_fluid_converter {
    itype_id input;
    itype_id output;
    std::optional<itype_id> byproduct;
    units::energy energy_per_unit = 0_J;
    int max_rate = 0; // charges of output per hour
};
```
Alongside the optional slot members (~`src/veh_type.h:301-305`), add:
```cpp
        std::optional<vpslot_fluid_converter> fluid_converter_info;
```
In the `vpart_bitflags` enum (~`src/veh_type.h:58-124`, near `VPFLAG_FLUIDTANK` at :107), add an entry before the terminal count sentinel:
```cpp
    VPFLAG_FLUID_CONVERTER,
```

- [ ] **Step 2: Register the string→bitflag mapping and parse the slot (`src/veh_type.cpp`).**

In the `vpart_bitflag_map` (near `src/veh_type.cpp:141`, alongside `{ "FLUIDTANK", VPFLAG_FLUIDTANK }`), add:
```cpp
    { "FLUID_CONVERTER", VPFLAG_FLUID_CONVERTER },
```
In `vpart_info::load` (`src/veh_type.cpp:273`), immediately after the `WORKBENCH` block (~:433), add:
```cpp
    if( has_flag( "FLUID_CONVERTER" ) ) {
        if( !fluid_converter_info ) {
            fluid_converter_info.emplace();
        }
        JsonObject fc_jo = jo.get_object( "fluid_converter" );
        assign( fc_jo, "input", fluid_converter_info->input, strict );
        assign( fc_jo, "output", fluid_converter_info->output, strict );
        assign( fc_jo, "byproduct", fluid_converter_info->byproduct, strict );
        assign( fc_jo, "energy_per_unit", fluid_converter_info->energy_per_unit, strict );
        assign( fc_jo, "max_rate", fluid_converter_info->max_rate, strict );
    }
```
(`units::energy` and `std::optional<itype_id>` already have `assign` overloads — `src/assign.h:224` and `:306` — so no new overloads are needed.)

- [ ] **Step 3: Build.**

Run: `make` (or the repo's configured build).
Expected: compiles cleanly. If `assign` cannot deduce `units::energy`, confirm the include of `units.h`/`assign.h` (already present in `veh_type.cpp`).

- [ ] **Step 4: Commit.**

```bash
git add src/veh_type.h src/veh_type.cpp
git commit -m "feat(veh): add data-driven fluid_converter vehicle-part slot and flag"
```

---

## Task 2: Add the `ammonia_synthesizer` part + base item (exercises the new parser)

Adding the JSON part makes the game's JSON loader validate the new `fluid_converter` fields at startup, which is the first real check of Task 1.

**Files:**
- Create: `data/json/vehicleparts/ammonia_synthesizer.json`
- Create/modify: an items JSON for the base item `ammonia_synthesizer` (e.g. `data/json/items/tool/science.json` next to the machine parts, or a new file)

**Interfaces:**
- Produces: vehicle part id `ammonia_synthesizer` (flags include `FLUID_CONVERTER`) and item id `ammonia_synthesizer`. Consumed by Task 3 (test installs this part) and Task 5 (recipe builds the item).

- [ ] **Step 1: Add the base item.**

Add an item (model on the existing machine-part items in `data/json/items/tool/science.json`):
```json
{
  "id": "ammonia_synthesizer",
  "type": "GENERIC",
  "category": "veh_parts",
  "name": { "str": "ammonia synthesizer" },
  "description": "A compact chemical reactor that continuously electrolyzes brine and fixes nitrogen to synthesize ammonia.  Mounted on a vehicle with a water tank, an output tank, and a power supply, it runs on its own while it has water and electricity.",
  "weight": "40 kg",
  "volume": "60 L",
  "material": [ "steel" ],
  "symbol": ",",
  "color": "light_gray"
}
```

- [ ] **Step 2: Add the vehicle part.**

Create `data/json/vehicleparts/ammonia_synthesizer.json` (model structural fields on an existing appliance/converter-like part; the distinctive bits are the flag + the `fluid_converter` block):
```json
[
  {
    "id": "ammonia_synthesizer",
    "type": "vehicle_part",
    "name": { "str": "ammonia synthesizer" },
    "item": "ammonia_synthesizer",
    "location": "structure",
    "durability": 250,
    "description": "Converts clean water into liquid ammonia over time while toggled on and supplied with grid power.  Draws water from any tank on the vehicle and fills any tank that can hold ammonia.",
    "size": "60 L",
    "flags": [ "FLUID_CONVERTER", "ENABLED_DRAINS_EPOWER" ],
    "epower": "-50 W",
    "fluid_converter": {
      "input": "water_clean",
      "output": "ammonia_liquid",
      "energy_per_unit": "30000 kJ",
      "max_rate": 4
    },
    "requirements": {
      "install": { "skills": [ [ "mechanics", 4 ], [ "electronics", 3 ] ], "time": "60 m", "using": [ [ "vehicle_bolt", 1 ] ] },
      "removal": { "skills": [ [ "mechanics", 3 ] ], "time": "30 m", "using": [ [ "vehicle_bolt", 1 ] ] }
    },
    "breaks_into": [ { "item": "steel_chunk", "count": [ 4, 8 ] }, { "item": "scrap", "count": [ 6, 12 ] } ]
  }
]
```
(`energy_per_unit`/`max_rate` are the primary balance knobs — see Task 5. `ENABLED_DRAINS_EPOWER` gives a small idle draw and reuses the standard on/off toggle; the conversion's real power cost is the per-unit discharge in Task 3.)

- [ ] **Step 3: Load-check.**

Run: `tests/cata_test "~*"` (or any invocation — the binary loads and validates ALL JSON at startup).
Expected: no JSON load errors mentioning `ammonia_synthesizer` or `fluid_converter`. If `energy_per_unit`/`max_rate` mis-parse, fix Task 1's `assign` calls.

- [ ] **Step 4: Commit.**

```bash
git add data/json/vehicleparts/ammonia_synthesizer.json data/json/items/tool/science.json
git commit -m "feat(veh): add ammonia_synthesizer vehicle part and base item"
```

---

## Task 3: The conversion loop in `update_time` (core behavior, TDD)

**Files:**
- Modify: `src/vehicle.h` (declare `std::vector<int> fluid_converters;` near the `funnels`/`solar_panels` caches)
- Modify: `src/vehicle.cpp` (`refresh()` population; `update_time` guard ~:8566 and conversion block ~:8648)
- Create: `tests/vehicle_fluid_converter_test.cpp`

**Interfaces:**
- Consumes: `vpart_info::fluid_converter_info` (Task 1), part `ammonia_synthesizer` (Task 2).
- Behavior: for each ENABLED `VPFLAG_FLUID_CONVERTER` part, over `elapsed`, produce `qty = min(max_rate·elapsed_hours, water_available, battery_kJ / energy_per_unit_kJ, output_tank_free)`, then drain input vehicle-wide, add output to a compatible tank, discharge battery.

- [ ] **Step 1: Write the failing test (`tests/vehicle_fluid_converter_test.cpp`).**

```cpp
#include "cata_catch.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"
#include "type_id.h"
#include "calendar.h"

static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_ammonia_liquid( "ammonia_liquid" );
static const itype_id fuel_type_battery( "battery" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_tank_water( "tank_small" );
static const vpart_id vpart_tank_ammonia( "tank_small" );
static const vpart_id vpart_battery( "storage_battery" );
static const vpart_id vpart_synth( "ammonia_synthesizer" );
static const vproto_id vehicle_prototype_none( "none" );

static vehicle *build_rig( map &here, int &water_idx, int &ammonia_idx )
{
    clear_vehicles();
    reset_player();
    build_test_map( ter_id( "t_pavement" ) );
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, tripoint_bub_ms{ 5, 5, 0 }, 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );
    veh->install_part( here, point_rel_ms::zero, vpart_frame );
    water_idx   = veh->install_part( here, point_rel_ms::zero, vpart_tank_water );
    ammonia_idx = veh->install_part( here, point_rel_ms{ 1, 0 }, vpart_tank_ammonia );
    veh->install_part( here, point_rel_ms{ 2, 0 }, vpart_battery );
    veh->install_part( here, point_rel_ms{ 3, 0 }, vpart_synth );
    veh->refresh();
    here.add_vehicle_to_cache( veh );
    return veh;
}

TEST_CASE( "fluid_converter converts water to ammonia when powered", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1, ammonia_idx = -1;
    vehicle *veh = build_rig( here, water_idx, ammonia_idx );

    vehicle_part &water_pt = veh->part( water_idx );
    vehicle_part &ammonia_pt = veh->part( ammonia_idx );
    water_pt.ammo_set( itype_water_clean, 40 );
    ammonia_pt.ammo_unset();
    // give plenty of stored power
    for( const int b : veh->batteries ) { veh->part( b ).ammo_set( fuel_type_battery, 500000 ); }
    veh->refresh();

    const int water_before = water_pt.ammo_remaining();
    const int batt_before = veh->fuel_left( here, fuel_type_battery );

    calendar::turn = calendar::turn_zero;
    veh->update_time( here, calendar::turn );                 // prime last_update
    veh->update_time( here, calendar::turn + 1_hours );       // elapse 1 hour

    CHECK( water_pt.ammo_remaining() < water_before );
    CHECK( ammonia_pt.ammo_remaining() > 0 );
    CHECK( ammonia_pt.ammo_current() == itype_ammonia_liquid );
    // 1:1 water->ammonia
    CHECK( ( water_before - water_pt.ammo_remaining() ) == ammonia_pt.ammo_remaining() );
    // battery was drawn down
    CHECK( veh->fuel_left( here, fuel_type_battery ) < batt_before );
}

TEST_CASE( "fluid_converter makes nothing without power", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1, ammonia_idx = -1;
    vehicle *veh = build_rig( here, water_idx, ammonia_idx );
    veh->part( water_idx ).ammo_set( itype_water_clean, 40 );
    veh->part( ammonia_idx ).ammo_unset();
    for( const int b : veh->batteries ) { veh->part( b ).ammo_unset(); } // empty battery
    veh->refresh();

    calendar::turn = calendar::turn_zero;
    veh->update_time( here, calendar::turn );
    veh->update_time( here, calendar::turn + 1_hours );

    CHECK( veh->part( ammonia_idx ).ammo_remaining() == 0 );
    CHECK( veh->part( water_idx ).ammo_remaining() == 40 );
}

TEST_CASE( "fluid_converter makes nothing when toggled off", "[vehicle][power][fluid_converter]" )
{
    map &here = get_map();
    int water_idx = -1, ammonia_idx = -1;
    vehicle *veh = build_rig( here, water_idx, ammonia_idx );
    veh->part( water_idx ).ammo_set( itype_water_clean, 40 );
    veh->part( ammonia_idx ).ammo_unset();
    for( const int b : veh->batteries ) { veh->part( b ).ammo_set( fuel_type_battery, 500000 ); }
    for( const int c : veh->fluid_converters ) { veh->part( c ).enabled = false; }
    veh->refresh();

    calendar::turn = calendar::turn_zero;
    veh->update_time( here, calendar::turn );
    veh->update_time( here, calendar::turn + 1_hours );

    CHECK( veh->part( ammonia_idx ).ammo_remaining() == 0 );
}
```

- [ ] **Step 2: Run the test to confirm it fails.**

Run: `make && tests/cata_test "[fluid_converter]"`
Expected: FAIL — `veh->fluid_converters` does not exist yet (compile error) and/or no conversion happens.

- [ ] **Step 3: Declare and populate the converter cache.**

In `src/vehicle.h`, near `std::vector<int> funnels;` (search for it), add:
```cpp
        std::vector<int> fluid_converters;
```
In `vehicle::refresh()` (`src/vehicle.cpp`; search where `funnels.push_back` / the per-part flag dispatch happens), add a branch that pushes the part index when `vpi.has_flag( VPFLAG_FLUID_CONVERTER )`, and ensure `fluid_converters.clear()` is called alongside the other list clears at the top of `refresh()`.

- [ ] **Step 4: Extend the `update_time` guard.**

In `src/vehicle.cpp:8566`, change the early-return guard so a converter-only vehicle is not skipped:
```cpp
    if( funnels.empty() && solar_panels.empty() && wind_turbines.empty() &&
        water_wheels.empty() && fluid_converters.empty() ) {
        return;
    }
```

- [ ] **Step 5: Add the conversion block.**

Insert immediately before the closing brace of `update_time` (after the `water_wheels` block, ~`src/vehicle.cpp:8648`):
```cpp
    if( !fluid_converters.empty() ) {
        const double elapsed_hours = to_hours<double>( elapsed );
        for( const int idx : fluid_converters ) {
            vehicle_part &conv = parts[idx];
            if( conv.is_unavailable() || !conv.enabled || !conv.info().fluid_converter_info ) {
                continue;
            }
            const vpslot_fluid_converter &fc = *conv.info().fluid_converter_info;
            const item out_probe( fc.output );
            const item in_probe( fc.input );
            // locate a source tank with input liquid, and a tank that accepts output
            auto src = std::find_if( parts.begin(), parts.end(), [&]( const vehicle_part & e ) {
                return e.is_tank() && e.ammo_current() == fc.input && e.ammo_remaining() > 0;
            } );
            auto dst = std::find_if( parts.begin(), parts.end(), [&]( const vehicle_part & e ) {
                return e.is_tank() && e.can_reload( out_probe );
            } );
            if( src == parts.end() || dst == parts.end() ) {
                continue;
            }
            const int energy_per_unit_kj = std::max( 1,
                    static_cast<int>( units::to_millijoule( fc.energy_per_unit ) / 1000000 ) );
            const int water_avail = src->ammo_remaining();
            const int energy_cap  = fuel_left( here, itype_battery ) / energy_per_unit_kj;
            const int rate_cap    = fc.max_rate > 0
                    ? roll_remainder( fc.max_rate * elapsed_hours ) : water_avail;
            const int space_cap   = dst->ammo_capacity( out_probe.ammo_type() ) - dst->ammo_remaining();
            const int qty = std::min( { water_avail, energy_cap, rate_cap, space_cap } );
            if( qty > 0 ) {
                src->ammo_consume( qty, here, abs_part_pos( *src ) );
                dst->ammo_set( fc.output, dst->ammo_remaining() + qty );
                discharge_battery( here, qty * energy_per_unit_kj );
                invalidate_mass();
            }
        }
    }
```
(Adapt the exact `ammo_type()`/`ammo_consume` signatures to the versions in your tree — see `src/vehicle_part.cpp:270,372`. If `ammo_current()` on an empty tank returns a null id, the `src` predicate's `ammo_remaining() > 0` already excludes empties.)

- [ ] **Step 6: Run the tests to confirm they pass.**

Run: `make && tests/cata_test "[fluid_converter]"`
Expected: all three PASS (converts when powered; nothing without power; nothing when toggled off).

- [ ] **Step 7: Commit.**

```bash
git add src/vehicle.h src/vehicle.cpp tests/vehicle_fluid_converter_test.cpp
git commit -m "feat(veh): convert water to ammonia over time in update_time (fluid_converter)"
```

---

## Task 4: Player on/off toggle in the vehicle electronics menu

**Files:**
- Modify: `src/vehicle_use.cpp` (`build_electronics_menu`, ~:284-345)

**Interfaces:**
- Consumes: the `"FLUID_CONVERTER"` string flag (Task 1). Flips `vehicle_part::enabled` for converter parts, which Task 3's loop already respects.

- [ ] **Step 1: Add the toggle entry.**

In `build_electronics_menu` (`src/vehicle_use.cpp`, alongside the `TOGGLE_FRIDGE`/`TOGGLE_COOLER` calls ~:337-342), add:
```cpp
    add_toggle( _( "ammonia synthesizer" ), keybind( "TOGGLE_FLUID_CONVERTER" ), "FLUID_CONVERTER" );
```
Add a matching `"TOGGLE_FLUID_CONVERTER"` action to the vehicle keybindings if the `keybind(...)` lookup requires a registered action (search how `"TOGGLE_FRIDGE"` is registered, e.g. in `data/raw/keybindings.json` / the vehicle context); reuse an existing generic toggle key if simpler.

- [ ] **Step 2: Build and smoke-check.**

Run: `make && tests/cata_test "[fluid_converter]"`
Expected: still PASS (behavior unchanged; the enabled-gate test already covers the on/off semantics). The menu entry itself is a UI affordance verified in-game (not unit-tested): load a save, mount the part, open the vehicle "control electronics" menu, confirm "ammonia synthesizer" toggles.

- [ ] **Step 3: Commit.**

```bash
git add src/vehicle_use.cpp data/raw/keybindings.json
git commit -m "feat(veh): add electronics-menu toggle for the ammonia synthesizer"
```

---

## Task 5: Build recipe, balance, and keep the manual recipe

**Files:**
- Create/modify: a recipes JSON (e.g. `data/json/recipes/recipe_others.json` or `recipes/vehicle/`) for the `ammonia_synthesizer` item
- Modify (balance only): `data/json/vehicleparts/ammonia_synthesizer.json`

**Interfaces:**
- Consumes: item `ammonia_synthesizer` (Task 2), machine-part items, `salt`.

- [ ] **Step 1: Add the craft recipe (four machine parts + 10 salt).**

```json
{
  "type": "recipe",
  "activity_level": "MODERATE_EXERCISE",
  "result": "ammonia_synthesizer",
  "category": "CC_OTHER",
  "subcategory": "CSC_OTHER_PARTS",
  "skill_used": "mechanics",
  "skills_required": [ [ "electronics", 4 ], [ "chemistry", 4 ] ],
  "difficulty": 7,
  "time": "8 h",
  "autolearn": false,
  "book_learn": [ [ "adv_chemistry", 7 ], [ "recipe_labchem", 7 ] ],
  "using": [ [ "welding_standard", 200 ] ],
  "qualities": [ { "id": "HAMMER", "level": 2 }, { "id": "WRENCH", "level": 1 }, { "id": "SCREW", "level": 1 } ],
  "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_high_pressure_systems" }, { "proficiency": "prof_chemical_engineering" } ],
  "components": [
    [ [ "ammonia_machine_reactor", 1 ] ],
    [ [ "ammonia_machine_pipework", 1 ] ],
    [ [ "nitrogen_generator", 1 ] ],
    [ [ "electrolyzer_makeshift", 1 ] ],
    [ [ "salt", 10 ] ]
  ]
}
```
(Verify `prof_chemical_engineering` exists — it is used by the ammonia machine recipes; drop it if not.)

- [ ] **Step 2: Tune the balance knobs.**

`energy_per_unit` (`data/json/vehicleparts/ammonia_synthesizer.json`) is the main dial. Anchor it to the existing economy: the manual recipe spends ~65,650 charges (kJ) for 2 ammonia charges (~32,825 kJ each). A starting `"30000 kJ"` keeps rough parity. `max_rate: 4` charges/hour bounds throughput so a huge battery can't instantly convert a full tank over a long absence. Adjust after in-game testing.

- [ ] **Step 3: Load-check + recipe resolution.**

Run: `tests/cata_test "~*"` (full JSON load) and confirm no errors; verify the recipe result/components resolve.
Expected: clean load; `ammonia_synthesizer` craftable from the four parts + 10 salt.

- [ ] **Step 4: Confirm the manual recipe is untouched.**

The existing `ammonia_liquid` recipe (`data/json/recipes/chem/chemicals.json:385`) must be unchanged — the synthesizer is additive.

- [ ] **Step 5: Commit.**

```bash
git add data/json/recipes/ data/json/vehicleparts/ammonia_synthesizer.json
git commit -m "feat(recipe): craft the ammonia synthesizer from the four machines + salt; tune balance"
```

---

## Task 6: Full build, test, and JSON style

- [ ] **Step 1: Full check.**

Run: `make check` (builds the game + tests and runs the suite) and `tests/cata_test "[vehicle]"`.
Expected: green, including the new `[fluid_converter]` cases.

- [ ] **Step 2: JSON style.**

Run: `make style-all-json-parallel RELEASE=1` and confirm `git diff` is empty (or commit the formatter's corrections).

- [ ] **Step 3: In-game sanity (manual).**

Build a small vehicle with a water tank, a second tank, a storage battery, a solar panel, and the ammonia synthesizer; fill water; toggle it on; skip time (or wait) with sun; confirm ammonia accumulates and water/charge drop, and that with the battery empty and no sun nothing is produced.

- [ ] **Step 4: Commit any style fixes.**

```bash
git add -A && git commit -m "style: json formatter pass for ammonia synthesizer"
```

---

## Self-Review

**Spec coverage:**
- Data-driven `fluid_converter` slot + flag → Task 1. ✓
- Conversion in `update_time` with power/water/rate/space caps, honest post-absence energy budget → Task 3 (inserted after solar/wind/water backfill). ✓
- Vehicle-wide tank access, reuse stock tanks → Task 3 (find_if over parts; `tank_small`). ✓
- Single `ammonia_synthesizer` part built from four machines + 10 salt (salt as one-time reagent) → Tasks 2 & 5. ✓
- On/off toggle → Task 4 (+ enabled-gate tested in Task 3). ✓
- Works both live (throttled ~1/min) and across time-skip via the single `update_time` path → Task 3 (no separate hook, per research). ✓
- Manual recipe kept → Task 5 Step 4. ✓
- Balance knobs (`energy_per_unit`, `max_rate`) → Task 5. ✓
- Open items from spec: ammonia-capable tank (resolved: stock tanks work, Task 3), toggle wiring (Task 4), per-turn vs catch-up (resolved: `update_time` serves both), save/load (`last_update` already persisted; conversion is stateless beyond tank contents). ✓

**Placeholder scan:** No TBD. Code blocks are concrete, adapted from probed source patterns; spots requiring signature-matching against the live tree are called out explicitly (a normal part of C++ execution with a compiler).

**Type consistency:** `fluid_converter_info` / `vpslot_fluid_converter` field names, `VPFLAG_FLUID_CONVERTER`, `fluid_converters` cache, and the `"FLUID_CONVERTER"` string flag are used consistently across Tasks 1, 3, and 4. `ammonia_synthesizer` names the same part and item across Tasks 2, 3, 5.
