# Multi-Floor Vehicles — Design

**Date:** 2026-07-21
**Branch:** `explore/multi-floor-vehicles`
**Status:** Approved design, revised after multi-agent review, ready for implementation planning
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
  A **stationary livable deck is a committed intermediate checkpoint** (milestone 4)
  so the project has a shippable state even if driving physics slips.
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
   - Ramp code adjusts `precalc[0].z()` up/down (`src/vehicle.cpp:8704-8706`),
     via the block at `src/vehicle.cpp:8700-8715` that copies `precalc[1]`→`precalc[0]`
     then layers ramp deltas on top.
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
   - **Pre-existing 3D mount, do not conflate:** `vehicle_part::carried_part_data::mount`
     is *already* `tripoint_rel_ms` (`src/vehicle.h:300`) and serializes `mount_z`
     (`src/savegame_json.cpp:3384-3396`). That belongs to the **racking/carry**
     subsystem (loading a car on a flatbed), not to permanent floors. The new
     multi-floor `mount.z` is a different axis of meaning and must not reuse or be
     confused with it.

So `precalc` can *represent* a part at another z (for ramps), but there is no way
to *author* a part permanently at mount-z+1. That is the gap this project closes.

**Key insight — the ramp work is both a help and a trap.** The tile-level
plumbing for "a part at a different z" exists (helps rendering, caches, collision
iteration). But the transient ramp meaning of `precalc.z` must not be conflated
with the new permanent `mount.z` meaning (see §1).

## Approach: B — Phased incremental, full-3D end state

Same destination as a big-bang conversion, but sequenced so the tree stays green
at every step and the large call-site change is de-risked in isolation. Rejected
alternatives: **A (big-bang full-3D)** — codebase red for weeks, no intermediate
validation, high debugging risk; **C (sub-vehicle-per-floor)** — keeps mounts 2D
and fakes unity, a dead end incompatible with the upstream-aligned goal.

---

## §0 — Serialization & savegame migration (part of Phase 1)

`vehicle_part::mount` is persisted as 2D `mount_dx`/`mount_dy`
(`src/savegame_json.cpp:3279-3280` read, `3337-3338` write). Widening `mount` to
3D **requires** touching serialization even for the "no-op" phase 1:

- **Write** a `mount_dz` field (omit-when-zero to keep saves small and diffs quiet).
- **Read** `mount_dz` with **default 0** so pre-change saves load unchanged.
- Keep the existing `z_offset` field (`3289-3296`, `3357`) — which writes into
  `precalc.z` (the transient *ramp* displacement), **not** `mount.z` — semantically
  distinct and untouched.
- Do **not** confuse with `carried_part_data`'s existing `mount_z`
  (`3384-3396`), which is the racking subsystem's field.

**Phase-1 gate correction:** "the existing suite stays green" does **not** by
itself prove the no-op, because the suite does not exercise round-trip migration
of pre-change saves. Phase 1 must add a **save→load round-trip regression test**
(load a legacy-format save fixture, assert every `mount.z == 0` and geometry is
identical) as an explicit gate. Without this, a corrupted / dropped-z world would
surface only after the "green" merge — exactly the failure Approach B exists to avoid.

---

## §1 — Data model & the mount-z / ramp-z composition

**The core problem.** After this change, a part's final rendered tile-z comes
from **two independent sources** that must compose:

- `mount.z` — the *permanent* floor the part is built on (0 = ground deck,
  1 = upper deck). **New.**
- `precalc.z` displacement — the *transient* ramp offset when the vehicle is
  mid-climb. **Already exists.**

Final `precalc.z = mount.z + ramp_displacement`.

**Composition is NOT automatic today — this is the trickiest edit, not a rotation audit.**

- `coord_translate` (all three overloads, `src/vehicle.cpp:3591-3615`) writes only
  `q.x()` and `q.y()` — **it never assigns `q.z()`.** So precalc.z is whatever
  stale value already sat in the slot.
- `precalc_mounts` (`src/vehicle.cpp:3586`, memo at `3643-3660`) caches results in
  `std::unordered_map<point_rel_ms, tripoint_rel_ms> mount_to_precalc` keyed on the
  **2D** mount. Two parts at the same (x,y) but different z would collide in this
  cache and get identical precalc (including identical z).

Required edits: (a) make the `mount_to_precalc` memo key 3D; (b) have
`precalc_mounts` **explicitly seed `precalc.z = mount.z`** (0 in phase 1) after
the x/y rotation, since `coord_translate` won't; (c) ensure the ramp block at
`src/vehicle.cpp:8700-8715` **adds** its displacement to the mount-seeded z rather
than assuming z began at 0. The composition therefore lives across *both*
`precalc_mounts` and the ramp block — treat it as one crux spanning two sites.

**Structure changes:**

- `vehicle_part::mount`: `point_rel_ms` → `tripoint_rel_ms`.
- `relative_parts`: `std::map<point_rel_ms, …>` → `std::map<tripoint_rel_ms, …>`.
- `edges`: same 2D→3D key change; plus `get_edge_info(const point_rel_ms&)`
  (`src/vehicle.h:2249`) and the edge/external refresh loop over
  `four_adjacent_offsets` (`src/vehicle.cpp:1497`).
- `mount_min` / `mount_max` 2D bounding boxes (`src/vehicle.h:2363-2364`) rebuilt
  in `refresh()` — extend to z.
- `parts_at_relative()`, `can_mount()`, `is_external_part()`, `part_at()`,
  `coord_translate()`, `precalc_mounts()` → 3D params/logic.
- **Rendering resolve sites** (z-ambiguity-prone, must select by z, not just x/y):
  `part_displayed_at` (`src/vehicle.cpp:3536`) and `get_display_of_tile`
  (`src/vehicle_display.cpp:61`), reached from `draw_vpart` via `mount_pos()`.

**Call-site blast radius (refined estimate):** ~52 `parts_at_relative` +
~38 `relative_parts` + `edges`/`can_mount`/`coord_translate` consumers — roughly
**~90 container/API sites (mostly mechanical)**, **plus ~130 `.mount` field
accesses** that must each be audited for z semantics (mechanical vs. z-meaningful).
Plan the two categories separately; the field accesses are not uniformly trivial.

**Connectivity semantics (decided: option (a), ladder-gated).** Two parts stacked
at `(x,y,0)` and `(x,y,1)` are only "the same vehicle" if connected through a part
carrying a **new vertical-connector flag** (see §2). A bare z-neighbor does **not**
connect. This forces intentional design, matches how upstream gates behavior with
flags, and keeps split-detection tractable.

**Split/merge is a first-class work item, not just a risk.** The connectivity
flood-fill is hard-coded planar and must become z-aware (traversing z only through
connector parts):

- `find_and_split_vehicles` (`src/vehicle.cpp:2572`) and `can_unmount`
  (`src/vehicle.cpp:1434`) iterate `four_adjacent_offsets` (a 2D 4-neighbor set).
- `split_vehicles` / `new_mounts` are `std::vector<point_rel_ms>` (2D) throughout
  (`src/vehicle.cpp:2627-2641`, `src/vehicle.h:1168`) and must carry z.

This gets its own milestone (see below); it is not optional if collisions during
driving can split the vehicle.

**Phase-1 is a behavioral no-op** — but not a *pure* key swap. `mount.z` is pinned
to 0 everywhere; the caches become 3D-keyed; serialization gains a default-0
`mount_dz` (§0); `precalc_mounts` explicitly seeds `precalc.z = mount.z` (= 0);
`coord_translate`'s missing z-write is compensated. With those, behavior is
genuinely identical and the existing suite **plus the §0 save round-trip test** is
the safety net.

---

## §2 — JSON authoring schema

Parts are authored today as `{ "x": 0, "y": 0, "parts": [ … ] }`, parsed at
`src/veh_type.cpp:1322-1323` (`point_rel_ms pos{ get_int("x"), get_int("y") }`).

- **Add optional `"z"` (default 0)** to each part entry →
  `tripoint_rel_ms pos{ x, y, z }`. The parse loop must **unconditionally read
  `z`** (e.g. `get_int("z", 0)`), because this codebase enforces
  **unvisited-member reporting** (`src/flexbuffer_json.cpp:258-301`, on by default;
  the existing `blueprint` read at `veh_type.cpp:1317-1319` exists solely to
  satisfy it). An unread `"z"` key is a load **error**, not silently ignored. With
  the unconditional read, the change is backward-compatible: every existing vehicle
  omits `z` and stays ground-deck. Widening also requires `part_def::pos` and the
  `install_part`/`can_mount` signatures (`src/vehicle.cpp:1680-1696`, `1267`) to go
  3D (already in §1's inventory).
- **Load-time validation is genuinely "free" — mechanism confirmed.**
  `vehicles::finalize_prototypes` (`src/veh_type.cpp:1552`) builds a real
  `blueprint` vehicle and calls `blueprint.install_part(...)` per part
  (`veh_type.cpp:1580`), which invokes `can_mount` and `debugmsg`s on failure
  (`veh_type.cpp:1581-1584`; `vehicle.cpp:1696-1703`). A `debugmsg` during init
  sets the error flag the test harness checks (`tests/test_main.cpp:215`), failing
  the suite. **Therefore the ladder-gate must live inside `can_mount`** for this
  free validation to apply. Note the failure path is `debugmsg`+return-−1
  (recoverable), so a broken *shipped* prototype spawns degraded rather than
  crashing — fine for test detection, worth knowing for release.
- **New part types + a new flag (C++ + JSON, not JSON-only):**
  1. A **vertical-connector** part (ladder/stairs). This needs a **new
     `VPFLAG_*` enum entry** in `src/veh_type.h` (the `VPFLAG_*` block ~lines
     59-120) **plus a string→flag mapping** in the table at `src/veh_type.cpp:145`
     (cf. `{ "ROOF", VPFLAG_ROOF }`), so `has_flag(VPFLAG_...)` fast-path checks
     recognize it. A JSON-only `json_flag` would not. Then the JSON part.
  2. An **upper-floor** part — the deck you stand on. `VPFLAG_ROOF` alone is *not*
     a walkable floor (it means "keeps rain out / roof cache above",
     `src/vehicle.cpp:2280-2282`). The upper-floor part must combine
     **ROOF + BOARDABLE + a solid-floor** semantic (roof from below, stand-on floor
     from above) — spell this out rather than reusing bare ROOF.
- **Test vehicle** (`data/json/vehicles/custom_vehicles.json`): a small 2-floor
  bus — ground deck (frame / wheels / engine / controls + one stairwell tile),
  upper deck (floor parts + seats), connector part at matching `(x,y)` on both z.
- **Mods:** vehicle loading is C++-only (`src/init.cpp:340`,
  `add("vehicle", &vehicles::load_prototype)`); mods only supply more JSON. No
  bundled mod reads `z` or overrides the loader, so **none of the 42 mods need
  changing — contingent on the parser unconditionally reading `z`** (above).

---

## §3 — Rendering (`src/cata_tiles.cpp`)

**Correction to the earlier "upper deck just falls out / is visible" framing.**
The main draw loop only renders at or **below** the player's z: `draw_min_z =
posz - fov_3d_z_range` and the loops iterate downward from `center.z()`
(`cata_tiles.cpp:1390`, `1513`, `1521`, `1758`); only `draw_critter_above`
(`:1797`) reaches z+1. So:

- **From the ground deck**, the upper deck is *not* shown as visible space — you
  see its **roof underside** (`draw_vpart_roof`), which is correct "can't see the
  floor above you" behavior.
- **The upper deck renders when the avatar is on it** (center.z follows the
  player). Milestone 3's success criterion is therefore "the upper deck is a solid,
  correctly-drawn space when you're standing on it; from below you see its roof" —
  *not* both decks drawn simultaneously.

Mechanism that already works: an upper-deck **BOARDABLE floor writes
`floor_cache = true`** for its own z (`src/map.cpp:9512`), and ROOF/OPAQUE parts
populate the floor cache above via `vehicle_caching_internal_above`
(`src/map.cpp:9516`). So a solid stand-on deck is achievable.

Real tasks:

1. **Cache *invalidation* must key off the part's composed z, not the vehicle base
   z.** `set_floor_cache_dirty(sm_pos.z()+1)` (`vehicle.cpp:2281`) and the
   transparency-cache dirties (`vehicle.cpp:2044`, `2277`, `2781`,
   `set_transparency_cache_dirty(sm_pos.z())`) all use `sm_pos.z()` — the vehicle's
   **base** submap z. A vehicle occupying z and z+1 must dirty both levels (and
   z+2 for the upper roof). Audit these per-part.
2. `part_displayed_at` / `get_display_of_tile` must select the right part by z
   (already in §1) so a ground part and an upper part at the same (x,y) draw on
   their correct levels.

---

## §4 — Traversal & boarding

**Boarding is a reuse (verified).** `boarded_parts()` iterates
`get_avail_parts(VPFLAG_BOARDABLE)` (`src/vehicle.cpp:3667`); `board_vehicle`
(`src/map.cpp:1385`) resolves the part at the character's exact tripoint via
`veh_at(pos)`, and the vehicle cache is per-z (`add_vehicle_to_cache`,
`src/map.cpp:485`). Boarding an upper-deck seat works once the seat sits at the
character's z. No change needed here.

**Deck-to-deck movement is NEW logic, not a reuse.** `game::vertical_move`
(`src/game.cpp:12291`) is built around **terrain** flags (`TFLAG_GOES_UP/DOWN`,
`has_floor_or_support`), and `has_floor` (`src/map.cpp:2639`) checks **terrain
flags only** — neither consults vehicle parts or the floor cache. So a vehicle
connector part will need **new plumbing** to be recognized as a valid up/down
transition (model it on stairs: stepping onto the connector and pressing up/down
moves the character to the same (x,y) at the other z). Milestone 4 must budget for
this as new code, not inheritance.

**Falling / floor destruction (new task).** A boarded upper-deck rider is
currently protected by `in_vehicle` — `Character::gravity_check`
(`src/character.cpp:11280,11289`) early-returns when `in_vehicle`, and
`is_open_air` doesn't read the floor cache. But if the **upper-floor part is
destroyed** under a standing character, no existing path re-runs the gravity check,
so the character would silently hover over open air at z+1 (correctness bug, not a
crash). Add a task: **on upper-floor part destruction, force `gravity_check` on any
character/monster at that tile.**

---

## §5 — Driving physics (the hard phase)

- **Ground contact:** only `wheelcache` parts touch terrain. State the rule as
  **"ground contact = `wheelcache` membership"** (not "z == 0"), so it can't
  silently disagree with the schema, which does not forbid a part at z=1.
  Optionally add a load-time assertion that `WHEEL` parts have `mount.z == 0`.
- **Mass center:** `calc_mass_center` (`src/vehicle.cpp:8484`) is 2D — it
  accumulates only `.x()`/`.y()` (`8521-8527`) into `mass_center_precalc` /
  `mass_center_no_precalc` (`src/vehicle.h:2365-2366`). Upper-deck mass folds into
  the **planar** (x,y) center for handling; z is ignored. Consequence: **no new
  physics dimension for the driving slice** — a 2-floor vehicle drives like a
  1-floor vehicle of equal total mass/footprint.
- **Collision:** collisions iterate per-part via `next_pos = pos + dp + precalc[1]`
  (`src/vehicle.cpp:8219`), which carries z — so upper-deck parts collide at z+1
  once precalc.z is correct. **But** `vehicle::collision` splits any z-diagonal
  move into separate horizontal and vertical passes
  (`src/vehicle_move.cpp:722-723`, `part_collision` comment at `:840`: "all
  collisions have to be either fully vertical or fully horizontal for now"). A
  2-floor vehicle **driving up a ramp** (simultaneous x/y + z) therefore exercises
  the roughest existing path (upstream #67712). The milestone-5 driving test must
  include **a 2-floor bus on a ramp**, not only flat driving.
- **Explicitly deferred:** top-heavy / tip-over stability (see milestones).

---

## §6 — Testing strategy

Per-phase pass bars:

1. **No-op refactor** → the *entire existing `cata_test` suite green, zero behavior
   change* **AND the §0 save→load round-trip regression test passes**. Both are the
   gate for phase 1.
2. **Authoring** → the new test-bus JSON loads via `cata_test` (validated for free
   through `finalize_prototypes` → `install_part` → `can_mount`, §2).
3. **Composition matrix** (phase 1 / phase 5 gate): a dedicated test over
   `mount.z ∈ {0,1}` × `ramp displacement ∈ {-1,0,1}` × 4 rotations asserting the
   final `precalc.z` is correct — the mount-z/ramp-z composition is the highest-risk
   surface and must not be a manual "audit."
4. **Later phases** → targeted C++ tests in `tests/vehicle_*` for: z-mount
   `parts_at_relative`, 3D split/merge (`find_and_split_vehicles`), ladder traversal
   between decks, and a "drive a 2-floor bus" movement/collision test (incl. ramp).

Run the whole suite via `build-scripts/gha_test_only.sh` (isolated, `--order lex`)
per CLAUDE.md — not a single `cata_test` call — because some `[monster]` tests
cross-contaminate in one process. A single `cata_test "name"` call is fine while
iterating on one test.

---

## Phase milestones (the deliverable spine)

1. **No-op `mount`→3D refactor** — full existing suite green + save round-trip test
   (§0) green; zero behavior change. Includes serialization, precalc.z seeding, and
   3D-keyed caches.
2. **JSON `z` + new connector flag + ladder-gated `can_mount` + test bus** — new bus
   loads via `cata_test`.
3. **3D connectivity / split-merge** — `find_and_split_vehicles`, `can_unmount`,
   `new_mounts` z-aware, ladder-gated; targeted test. (Precedes driving because
   collisions can split vehicles.)
4. **Committed checkpoint — stationary livable deck** — rendering (upper deck solid
   & drawn when you're on it), deck-to-deck traversal, board upper-deck seats,
   gravity-check on floor destruction. **This is a shippable state.**
5. **Driving** (the stated end goal) — drive the 2-floor bus; collisions correct
   across z; ramp traversal test passes.
6. *(Deferred / optional)* — stability / tip-over model; `veh_interact`
   install-on-floor UX polish.

Milestones 1–5 constitute the committed vertical slice, with milestone 4 as the
de-risking shippable checkpoint. Milestone 6 is out of scope for the slice.

## Explicitly out of the vertical slice (deferred by decision, not oversight)

These are real and may matter for a "fully playable" bus, but are **not** required
for the slice and are called out so they aren't mistaken for gaps:

- **Monster AI targeting/melee onto the upper deck** (z+1 targeting).
- **NPC boarding & pathing to upper-deck seats** (passengers).
- **Mapgen / loot spawning** of a vehicle that has z-parts (the test bus is
  debug/spawn-placed for the slice).
- **`veh_interact` install-on-floor UX** and top-heavy stability (also in
  milestone 6).

## Open risks

- **precalc.z composition bugs** — the mount-z vs ramp-z conflation across
  `precalc_mounts` and the ramp block (`vehicle.cpp:8700-8715`) is the single most
  likely source of subtle rendering/collision errors. Mitigated by phase 1's no-op
  gate and the §6.3 composition matrix.
- **Savegame migration** — the most underweighted derailer in the original draft:
  "no-op green" is unprovable without the §0 round-trip test, and a botched
  `mount_dz` read silently drops z on existing worlds. Now a first-class phase-1
  item.
- **3D split/merge detection** (`find_and_split_vehicles`, `can_unmount`) with
  ladder-gated connectivity is the trickiest algorithmic corner after the refactor
  — now milestone 3, before driving.
- **Rotation / z preservation** — `coord_translate` never writes z, so precalc.z is
  seeded and maintained by `precalc_mounts`; a mistake shows only when mid-climb and
  turning. Covered by the composition matrix.
- **Scale** — ~90 API sites + ~130 `.mount` field accesses in a ~16k-line
  subsystem; even phased, this is a multi-week effort. Each milestone is
  independently reviewable to keep the fork usable throughout.
