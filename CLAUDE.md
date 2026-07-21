# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Cataclysm: Dark Days Ahead — a turn-based survival roguelike. A large, mature C++17 codebase (~840 files in `src/`) paired with an enormous **data-driven content layer** in `data/json/`. Most gameplay content (items, monsters, recipes, map generation, professions, etc.) is defined in JSON and loaded at runtime, not hardcoded in C++.

> **Fork note:** This fork's `master` is baselined on the upstream **0.I "Ito"** stable release (commit `27939e29b8b4ddc081490d9f51de59a459c88df6`), not upstream master HEAD. Upstream is `CleverRaven/Cataclysm-DDA`.

## Build / run / test commands

This machine (Apple Silicon, Homebrew, all SDL2 deps installed) builds the graphical tiles version with:

```sh
make NATIVE=osx TILES=1 SOUND=1 LOCALIZE=0 CLANG=1 -j8
```

- **Do not pass `FRAMEWORK` at all** — omitting it selects the Homebrew/pkg-config libSDL path. `FRAMEWORK=0` counts as *defined* (`ifdef FRAMEWORK`) and wrongly selects the framework path.
- Add `RELEASE=1` for an optimized (faster-running) binary; omit it for the debug build (`-Og -g`, assertions on) used during development.
- Omit `TILES`/`SOUND` for the ncurses (terminal) build.
- Full from-scratch debug build ≈ 7–8 min on 8 cores; incremental rebuilds recompile only changed translation units and are much faster.

Outputs: `./cataclysm-tiles` (the game) and `tests/cata_test` (the Catch2 test binary — the default `make` target builds both). Launch with `./cataclysm-tiles`.

### Tests

Tests live in `tests/`, written in the [Catch2](https://github.com/catchorg/Catch2) framework. See `doc/c++/TESTING.md`.

```sh
tests/cata_test                       # run all tests
tests/cata_test "sweet_junk_food"     # run one test case by name
tests/cata_test "[food]"              # run all tests with a tag
tests/cata_test --help                # all options
```

A large part of what the test suite does is **load and validate all game JSON**, so many C++ *and* JSON changes are caught by simply running `cata_test`.

### Formatting (required before PRs)

- **C++:** styled with **astyle 3.1** (exact version — others produce different output). Options are in `.astylerc`; `doc/c++/CODE_STYLE.md` has the full command. Note the project's distinctive style: spaces inside parens (`if( x )`, `foo( a, b )`), `--align-pointer=name`, 4-space indent, 100-col limit, 1TBS braces.
- **JSON:** run `make style-json`, or format individual files with the `json_formatter` tool (`make style-json` builds it). JSON style rules are in `doc/JSON/JSON_STYLE.md`.

Other lint/dev tooling lives in `tools/` and `build-scripts/` (clang-tidy plugin, IWYU, translation checks, etc.).

## Architecture — the big picture

### C++ core (`src/`)
A largely monolithic engine. A handful of very large files carry the bulk of gameplay logic — worth knowing where things live:

| File | Responsibility |
|------|----------------|
| `src/game.{h,cpp}` | Central game state and orchestration. A global singleton **`g`** (`extern std::unique_ptr<game> g;`) is the access point to the running game. |
| `src/main.cpp` → `src/do_turn.cpp` | Entry point and the main turn loop. |
| `src/character.cpp`, `avatar.cpp`, `npc*.cpp` | Creatures/actors. `Character` is the base for the player (`avatar`) and NPCs. |
| `src/item.cpp` (~16k lines) | Items — the single biggest file. |
| `src/map.cpp`, `mapgen.cpp`, `overmap.cpp` | The world: local map, procedural map generation, and the overmap. |
| `src/vehicle*.cpp` | Vehicles. |
| `src/iuse.cpp`, `iuse_actor.cpp`, `activity_actor.cpp` | Item-use effects and long-running player activities. |
| `src/cata_tiles.cpp` | SDL tiles rendering (only in `TILES=1` builds). |

**Type-safe IDs:** content is referenced through `string_id<T>` / `int_id<T>` (see `src/type_id.h`) rather than raw strings/ints. Most content types are stored in a **`generic_factory<T>`** (`src/generic_factory.h`) that handles loading, lookup, and `copy-from` inheritance uniformly.

### Data-driven content (`data/`)
- `data/json/` — the **core game content**. Each JSON object has a `"type"` field.
- `data/mods/` — 42 bundled mods (Magiclysm, Aftershock, DinoMod, Generic_Guns, …), same JSON format, layered on top of core.
- `data/raw/`, `data/names/`, `data/font/`, `gfx/`, `sound/` — assets and support data.

**How JSON becomes game objects:** `src/init.cpp` defines `DynamicDataLoader`, which maps each JSON `"type"` string to a C++ loader function via `add( "type_name", &loader )` (e.g. `add( "vitamin", &vitamin::load_vitamin )`). To add or change how a content type loads, find its `add(...)` registration in `init.cpp` and follow it to the owning class's `load()`.

**Loading order matters:** files are read by breadth-first search of `data/json/`, so shallower paths load before deeper ones, and same-depth files load lexically. Objects with dependencies (recipes → skills, scenarios → professions) rely on this ordering — getting it wrong typically manifests as a crash on load. See `data/json/LOADING_ORDER.md` and `doc/JSON/JSON_INHERITANCE.md` (`copy-from`).

### Coordinate system
The map uses layered coordinate types (`point`, `tripoint`, and typed coordinate wrappers) rather than bare ints. Read `doc/c++/POINTS_COORDINATES.md` before touching map/position code — the distinctions between coordinate spaces are load-bearing.

## Where to look things up

`doc/JSON/` is the authoritative modding reference — one file per subsystem. Especially:
- `JSON_INFO.md` — general JSON object reference
- `JSON_FLAGS.md` — the flags that drive much item/creature behavior
- Subsystem guides: `MONSTERS.md`, `MAPGEN.md`, `VEHICLES_JSON.md`, `NPCs.md`, `MAGIC.md`, `EFFECT_ON_CONDITION.md`, `MUTATIONS.md`, and more.

`doc/c++/` covers engine-side concerns (`COMPILING.md`, `CODE_STYLE.md`, `TESTING.md`, `DEVELOPER_TOOLING.md`, `POINTS_COORDINATES.md`). `doc/CONTRIBUTING.md` and `doc/DEVELOPER_FAQ.md` cover process.
