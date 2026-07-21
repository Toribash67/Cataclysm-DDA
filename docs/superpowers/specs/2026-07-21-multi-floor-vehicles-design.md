# Multi-Floor Vehicles — Design

**Date:** 2026-07-21
**Branch:** `explore/multi-floor-vehicles`
**Status:** Approved design, ready for implementation planning
**Baseline:** This fork's `master`, on upstream 0.I "Ito" (`27939e29b8`).

## Goal

Deliver a **playable, driveable two-floor vehicle** (double-decker bus / RV loft — a
"livable second deck" you can climb to, stand on, build on, and ride while driving).
End state is a single, genuinely 3D vehicle, implemented the way upstream
CleverRaven would likely do it (full 3D mount, clean JSON schema), so the work
could theoretically be rebased onto or contributed upstream.

Scope decisions locked during brainstorming:

- **Vertical slice, not just a spec** — aim for something playable.
- **Livable second deck** is the driving use case (not firing platform, not car-stacking).
- **Driving is required** in the finished slice (the hardest path — collisions/turning across z).
- **Upstream-aligned**, not fork-local hacks — full 3D mount, proper JSON.
- **Approach B — phased incremental** (see below), not big-bang.

## Background: what already exists (and what does not)

This fork is on 0.I "Ito", far newer than the 2019 discourse thread
(<https://discourse.cataclysmdda.org/t/multi-floor-vehicles/25241>) that first
discussed this. Two distinct concepts get conflated as "multi-floor," and only
one is done:

1. **Vehicles *spanning* z-levels via ramps — already works.** A vehicle driving
   up a z-ramp temporarily has parts on different z-levels but remains *one*
   vehicle. Evidence in the current code:
   - `vehicle_part::precalc[0/1]` (projected tile coords) is already
     `tripoint_rel_ms` — 3D (`src/vehicle.h:460`).
   - Ramp code adjusts `precalc[0].z()` up/down (`src/vehicle.cpp:8704-8706`).
   - Floor/transparency caches already handle `z+1`
     (`src/vehicle.cpp:2281`, `set_floor_cache_dirty(z+1)`).
   - Explicit "supported across z-levels" logic exists (`src/vehicle.h:2290`).
   - Upstream issue #67712 tracks *bugs* arising from this — it is real but rough.

2. **True multi-floor vehicles (a permanent second deck at mount z+1) — not done.**
   The blocker is exactly what the thread's devs predicted, still unresolved: the
   *permanent* mount coordinate is 2D.
   - `vehicle_part::mount` is `point_rel_ms`, not `tripoint_rel_ms`
     (`src/vehicle.h:456`).
   - The part index/cache is 2D: `relative_parts` is
     `std::map<point_rel_ms, std::vector<int>>` (`src/vehicle.h:2332`); `edges`
     likewise (`src/vehicle.h:2247`).
   - `parts_at_relative()`, `can_mount()`, `is_external_part()`, `part_at()`,
     `coord_translate()` all take `point_rel_ms`.

So `precalc` can *represent* a part at another z (for ramps), but there is no way
to *author* a part permanently at mount-z+1. That is the gap this project closes.

**Key insight — the ramp work is both a help and a trap.** The tile-level
plumbing for "a part at a different z" exists (helps rendering, caches, collision
iteration). But the transient ramp meaning of `precalc.z` must not be conflated
with the new permanent `mount.z` meaning (see §1).

## Approach: B — Phased incremental, full-3D end state

Same destination as a big-bang conversion, but sequenced so the tree stays green
at every step and the ~100 call-site change is de-risked in isolation. Rejected
alternatives: **A (big-bang full-3D)** — codebase red for weeks, no intermediate
validation, high debugging risk; **C (sub-vehicle-per-floor)** — keeps mounts 2D
and fakes unity, a dead end incompatible with the upstream-aligned goal.

---

## §1 — Data model & the mount-z / ramp-z composition

**The core problem.** After this change, a part's final rendered tile-z comes
from **two independent sources** that must compose:

- `mount.z` — the *permanent* floor the part is built on (0 = ground deck,
  1 = upper deck). **New.**
- `precalc.z` displacement — the *transient* ramp offset when the vehicle is
  mid-climb. **Already exists.**

Final `precalc.z = mount.z + ramp_displacement`. Today only ramps write
`precalc.z`; the refactor makes `precalc_mounts()` seed `precalc.z` from `mount.z`
first, then ramp logic adds on top. Getting this composition right is the crux.

**Structure changes:**

- `vehicle_part::mount`: `point_rel_ms` → `tripoint_rel_ms`.
- `relative_parts`: `std::map<point_rel_ms, …>` → `std::map<tripoint_rel_ms, …>`.
- `edges`: same 2D→3D key change.
- `parts_at_relative()`, `can_mount()`, `is_external_part()`, `part_at()`,
  `coord_translate()`, `precalc_mounts()` → 3D params/logic.
- ~50 `parts_at_relative` call sites, ~31 `relative_parts` uses, plus `edges`
  consumers — updated mechanically.

**Connectivity semantics (decided: option (a), ladder-gated).** Two parts stacked
at `(x,y,0)` and `(x,y,1)` are only "the same vehicle" if connected through a part
flagged as a vertical connector (`STAIRS` / vertical-connector flag). A bare
z-neighbor does **not** connect. This forces intentional design, matches how
upstream gates behavior with flags, and keeps `can_unmount` split-detection
tractable. (Rejected: "any vertical overlap connects" — allows floating
unsupported decks, complicates split detection.)

**Phase-1 is a behavioral no-op:** `mount.z` is pinned to 0 everywhere; the caches
become 3D-keyed but behave identically. Nothing gains a second floor yet, but the
scary 100-call-site change lands in isolation with the full existing test suite as
the safety net.

---

## §2 — JSON authoring schema

Parts are authored today as `{ "x": 0, "y": 0, "parts": [ … ] }`, parsed at
`src/veh_type.cpp:1323` (`point_rel_ms pos{ get_int("x"), get_int("y") }`).

- **Add optional `"z"` (default 0)** to each part entry →
  `tripoint_rel_ms pos{ x, y, z }`. Fully backward-compatible: every existing
  vehicle omits `z` and stays ground-deck. **None** of the 42 bundled mods'
  vehicle JSON needs changing.
- **Load-time validation in `can_mount`** enforces the ladder-gate: a part at
  `z=1` must connect — at the same `(x,y)` — to a `STAIRS`/vertical-connector part
  linking it to `z=0`, or be reachable through the upper deck's own connected
  structure to such a point. Because `cata_test` loads all JSON, a malformed test
  bus fails the suite — free validation.
- **New part types (data-only, `data/json/vehicleparts/`):**
  1. An **upper-floor** part — the deck you stand on: a roof from below, a floor
     from above.
  2. A **vehicle ladder/stairs** part carrying the vertical-connector flag.
  Both are pure JSON on top of the C++ flag support from §1.
- **Test vehicle** (`data/json/vehicles/custom_vehicles.json`): a small 2-floor
  bus — ground deck (frame / wheels / engine / controls + one stairwell tile),
  upper deck (floor parts + seats), stairwell part at matching `(x,y)` on both z.

---

## §3 — Rendering (`src/cata_tiles.cpp`)

The engine already draws vehicles per z-level and already renders a vehicle whose
`precalc.z` differs (ramps), so upper-deck parts largely "fall out" once `mount.z`
feeds `precalc.z`. Two real tasks:

1. The upper-floor part must register as a **floor** for the z+1 map cache (so you
   neither see through it nor fall), reusing the existing
   `set_floor_cache_dirty(z+1)` path (`src/vehicle.cpp:2281`).
2. When standing on the ground deck, the deck above draws as a roof — standard
   roof handling, not new code.

---

## §4 — Traversal & boarding

Boarding keys off `VPFLAG_BOARDABLE` (`vehicle::boarded_parts()`,
`src/vehicle.cpp:3667`) and already resolves a part at the character's exact
tripoint, so boarding an upper-deck seat works once the part sits at z+1.

The **new** piece is *moving between decks*: the ladder/stairs part becomes a
z-transition tile — stepping onto it and pressing up/down moves the character to
the same `(x,y)` at the other z, mirroring how map ramps/stairs already move
actors. Reuse existing climb/stairs movement, gated to the connector part.

---

## §5 — Driving physics (the hard phase)

- **Ground contact:** only `wheelcache` parts touch terrain; upper-deck parts must
  be excluded from wheel/terrain contact. Since wheels live at `z=0`, the rule is
  simply "wheel parts are z=0" — cheap.
- **Mass center:** `calc_mass_center` currently returns 2D `point_rel_ms`
  (`src/vehicle.cpp:3789`). Upper-deck mass still contributes to the *planar*
  (x,y) center of mass for handling — mass folds in ignoring z. Consequence: **no
  new physics dimension for the driving slice** — a 2-floor vehicle drives like a
  1-floor vehicle of equal total mass/footprint.
- **Collision:** collisions test `precalc` tiles; upper-deck parts collide with
  things at z+1. Existing collision iterates parts by their tile, so it composes
  once `precalc.z` is correct. **Risk area:** turning (`precalc_mounts` rotation)
  must preserve z through rotation — rotate x/y only, pin z.
- **Explicitly deferred:** top-heavy / tip-over stability (see milestones).

---

## §6 — Testing strategy

Per-phase pass bars:

1. **No-op refactor** → the *entire existing `cata_test` suite green, zero
   behavior change*. This is the gate for phase 1.
2. **Authoring** → the new test-bus JSON loads via `cata_test` (JSON-load-as-test).
3. **Later phases** → targeted C++ tests in `tests/vehicle_*` for: z-mount
   `parts_at_relative`, ladder traversal between decks, and a "drive a 2-floor bus"
   movement/collision test.

Run the whole suite via `build-scripts/gha_test_only.sh` (isolated, `--order lex`)
per CLAUDE.md — not a single `cata_test` call — because some `[monster]` tests
cross-contaminate in one process. A single `cata_test "name"` call is fine while
iterating on one test.

---

## Phase milestones (the deliverable spine)

1. **No-op `mount`→3D refactor** — full existing suite green, zero behavior change.
2. **JSON `z` + ladder-gated `can_mount` + test bus** — new bus loads via `cata_test`.
3. **Rendering** — upper deck visible; floor solid (no fall-through / see-through).
4. **Traversal** — climb between decks; board upper-deck seats.
5. **Driving** — drive the 2-floor bus; collisions correct across z.
6. *(Deferred / optional)* — stability / tip-over model; `veh_interact`
   install-on-floor UX polish.

Milestones 1–5 constitute the committed vertical slice. Milestone 6 is out of
scope for the slice and tracked separately.

## Open risks

- **precalc.z composition bugs** — the mount-z vs ramp-z conflation is the single
  most likely source of subtle rendering/collision errors. Phase 1's no-op gate
  and a dedicated composition test mitigate this.
- **Rotation preserving z** — `precalc_mounts` must be audited to rotate only x/y.
- **Split/merge detection** (`can_unmount`, vehicle splitting) with 3D structure
  and ladder-gated connectivity is the trickiest algorithmic corner after the
  refactor itself.
- **Scale** — ~100 call sites in a ~16k-line subsystem; even phased, this is a
  multi-week effort. Each milestone is independently reviewable to keep the fork
  usable throughout.
