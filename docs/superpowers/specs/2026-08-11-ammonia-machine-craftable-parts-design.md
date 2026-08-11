# Craftable parts for the ammonia machines

**Date:** 2026-08-11
**Repo:** `Toribash67/Cataclysm-DDA`
**File touched:** `data/json/recipes/tools/tool.json`

## Problem

Liquid ammonia is produced by four large machines. Three of them are craftable
recipes (`ammonia_machine_reactor`, `ammonia_machine_pipework`,
`nitrogen_generator`; the fourth is the in-world synthesizer assembled from
these). Between them those three recipes require eight niche spare parts that
have **no craft recipe** and can only be found:

- `hd_valve` (heavy duty valve)
- `large_pressure_vessel`
- `pyrometer`
- `pressure_gauge`
- `hd_pipe_fittings` (set of heavy duty pipe fittings)
- `small_pressure_vessel`
- `hd_compressor` (industrial compressor)
- `nitrogen_membrane_filter`

All eight are defined in `data/json/items/generic/spares.json` and are used
*only* by the ammonia machines, so they are extremely niche. The goal is to make
the machines buildable end-to-end.

## Strategy

**Craft 7, swap the 1 exotic part.** Seven of the parts are ordinary steelwork or
basic electronics with clean existing analogues, so they get new recipes. The
`nitrogen_membrane_filter` is the only genuinely high-tech part (a gas-separation
membrane that is not plausibly hand-crafted), so instead of a recipe it is
removed from `nitrogen_generator` and replaced with a stack of common air
filters.

Each new recipe is modeled on a real recipe already in the game; quantities are
tuned to be heavier/industrial versions of their templates. All recipes live in
`data/json/recipes/tools/tool.json`, next to the machines that consume them.

## The 7 new recipes

### Group A — steel/fabrication parts (autolearn; difficulty + proficiencies gate)

| result | modeled on | skill / difficulty | skills_required | time | components | using / tools |
|---|---|---|---|---|---|---|
| `hd_pipe_fittings` | `pipe_fittings` recipe | fabrication 6 | — | 4 h | — | using: blacksmithing_standard 3, steel_standard 2; tool: swage |
| `hd_valve` | `water_faucet` | fabrication 6 | mechanics 2 | 3 h | hd_pipe 1, hd_pipe_fittings 1, spring 1, scrap 4 | using: welding_standard 40 |
| `small_pressure_vessel` | blacksmith `metal_tank` | fabrication 6 | mechanics 2 | 5 h | sheet_metal 4, hd_pipe_fittings 1 | using: welding_standard 200, steel_standard 6 |
| `large_pressure_vessel` | scaled-up vessel (300 kg) | fabrication 8 | mechanics 4 | 20 h | sheet_metal 24, hd_pipe_fittings 4 | using: welding_standard 900, steel_standard 40 |

- Qualities: `hd_pipe_fittings` needs the swage tool (as its template does).
  `hd_valve` uses HAMMER 2 / WRENCH 1 / SCREW 1. Vessels use HAMMER 2 (large adds
  SAW_M 2).
- Proficiencies for the valve and both vessels: `prof_metalworking`,
  `prof_high_pressure_systems` (large vessel also `prof_welding`) — the same
  proficiencies the ammonia machines already require.
- `autolearn: true`, matching the autolearn behavior of the templates
  (`pipe_fittings`, `metal_tank`). Difficulty + proficiencies are the gate.

### Group B — instruments + compressor

| result | modeled on | skill / difficulty | skills_required | time | components | using |
|---|---|---|---|---|---|---|
| `pressure_gauge` | mechanical bourdon gauge | fabrication 4 | mechanics 1 | 1 h | pipe 1, spring 2, scrap 3, glass_sheet 1 | — |
| `pyrometer` | `motor_small`-style electronics | electronics 4 | fabrication 1 | 1 h 30 m | e_scrap 8, cable 15, scrap_copper 3, scrap 2 | soldering_standard 10 |
| `hd_compressor` | `motor_small` + pump/pipework | fabrication 7 | mechanics 4, electronics 2 | 6 h | motor 1, hd_pipe 2, hd_pipe_fittings 1, hd_valve 1, sheet_metal 4, cable 50, power_supply 1 | welding_standard 80, soldering_standard 10 |

- `hd_compressor` qualities: HAMMER 2 / WRENCH 1 / SCREW 1; proficiencies
  `prof_metalworking`, `prof_basic_engines`, `prof_high_pressure_systems`.
- Gating: `pressure_gauge` autolearns (mechanics ~3). `pyrometer` and
  `hd_compressor` gate behind higher skill via `autolearn` thresholds; if a
  suitable book id is confirmed during implementation (e.g. the
  `adv_chemistry` / `recipe_labchem` books the machines already use, or an
  electronics/mechanics manual), prefer `book_learn` over bare autolearn for
  these two.

### Composition

`hd_valve` and `hd_compressor` both consume `hd_pipe_fittings`, and
`hd_compressor` consumes `hd_valve`. The recipes intentionally chain so one new
base part feeds the others.

## The swap — `nitrogen_generator`

In the `nitrogen_generator` recipe, replace the component line:

```json
[ [ "nitrogen_membrane_filter", 1 ] ]
```

with a stack of common air filters standing in for the separation membrane:

```json
[ [ "filter_air", 4 ], [ "filter_air_makeshift", 6 ] ]
```

Everything else in `nitrogen_generator` is unchanged. It still requires
`hd_compressor` ×1, which is now craftable. `nitrogen_membrane_filter` the item
is left defined in `spares.json` (still findable), just no longer required.

## Ingredient / requirement ids (verified present)

Items: `steel_chunk`, `steel_lump`, `sheet_metal`, `sheet_metal_small`, `pipe`,
`hd_pipe`, `scrap`, `spring`, `e_scrap`, `cable`, `power_supply`, `motor`,
`filter_air`, `filter_air_makeshift`, `scrap_copper`, `glass_sheet`.
Requirements/toolsets (via `using`): `steel_standard`, `blacksmithing_standard`,
`welding_standard`, `soldering_standard`. Tool: `swage`.

## Out of scope

- No changes to the three machine recipes other than the one `nitrogen_generator`
  component swap.
- The in-world ammonia synthesizer (the "fourth machine") is unchanged — it does
  not consume the raw parts directly.
- `nitrogen_membrane_filter` item definition is left in place.

## Validation

- Every referenced id must resolve; run the game's JSON load / `cata_test` full
  JSON check, matching the practice used for the anvil recipe (PR #11).
- Run the repo's JSON formatter so the new recipes match house style
  (`astyle.yml` / `json.yml` CI).

## Deployment

Committing to `master` in this repo fires `.github/workflows/notify-guide.yml`,
which dispatches `game-updated` to `Toribash67/cdda-guide`. That regenerates the
guide data and redeploys `cdda-guide-web` (host port 18082) via watchtower, so
the new recipes appear in the guide automatically.
