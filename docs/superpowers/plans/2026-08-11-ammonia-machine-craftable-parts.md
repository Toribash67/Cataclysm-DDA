# Craftable Ammonia-Machine Parts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 7 new craft recipes and one component swap so the three ammonia machines (`ammonia_machine_reactor`, `ammonia_machine_pipework`, `nitrogen_generator`) can be built entirely from craftable or common findable parts.

**Architecture:** All changes are pure JSON data edits in the game repo `Toribash67/Cataclysm-DDA`. Seven new `recipe` objects are appended to `data/json/recipes/tools/tool.json` (the file that already holds the three machines). One component line in the existing `nitrogen_generator` recipe is swapped from the exotic `nitrogen_membrane_filter` to a stack of common air filters. No C++ or item-definition changes; the eight spare-part items in `data/json/items/generic/spares.json` are left as-is.

**Tech Stack:** Cataclysm-DDA JSON recipe format. Local validation via Python 3 (`json` module). Authoritative style/load gates are the repo's CI (`.github/workflows/json.yml` → `make style-all-json-parallel`, and the build's full `cata_test` JSON load).

## Global Constraints

- **Single file for new recipes:** `data/json/recipes/tools/tool.json`. Insert the seven new recipe objects as one contiguous block **immediately after the `nitrogen_generator` recipe object** (after its closing `},`) and before the next recipe (`can_sealer`).
- **Field order** in each recipe object follows the existing recipes in this file: `type`, `activity_level`, `result`, `category`, `subcategory`, `skill_used`, `skills_required`, `difficulty`, `time`, `autolearn`/`book_learn`, `using`, `proficiencies`, `qualities`, `tools`, `components`.
- **`skills_required` format:** a single required skill is a flat pair `[ "mechanics", 2 ]`; two or more is a list of pairs `[ [ "mechanics", 4 ], [ "electronics", 2 ] ]` (matches the existing recipes in this file).
- **Only reference verified ids.** All ingredient items, requirements (`*_standard`), proficiencies (`prof_*`), qualities (`HAMMER`/`WRENCH`/`SCREW`/`SAW_M`), books (`manual_electronics`, `textbook_electronics`, `advanced_electronics`, `manual_mechanics`), and the `swage` tool used below have been confirmed present in the current baseline.
- **Whitespace is owned by the JSON formatter.** Write the recipes matching the style of the surrounding recipes; the formatter (CI) is authoritative. If the local toolchain can run it, do so before committing and include any corrections.
- **Do not touch `master` directly.** Work continues on branch `ammonia-craftable-parts` (already created; holds the design spec commit). Merging to `master` is what fires the guide-rebuild dispatch, so that is a separate, deliberate step after review.

---

## Task 0: Create the local validation helper (setup)

This helper is reused by every task's test cycle. It (1) syntax-checks the target file and (2) confirms every id referenced by the touched recipes resolves against the whole `data/json` universe — a fast local proxy for the CI's full JSON load.

**Files:**
- Create: `/tmp/claude-3000/-mnt-ssd-pool-martin-repos-cdda-guide/d647a43a-4f5c-41d3-9c66-00c740016c11/scratchpad/check_recipes.py` (scratchpad, not committed)

- [ ] **Step 1: Write the helper script**

```python
# check_recipes.py — run from the Cataclysm-DDA repo root
import json, glob, sys

ROOT = "data/json"
TARGET = "data/json/recipes/tools/tool.json"
TOUCHED = {
    "hd_pipe_fittings", "hd_valve", "small_pressure_vessel",
    "large_pressure_vessel", "pressure_gauge", "pyrometer",
    "hd_compressor", "nitrogen_generator",
}

def load(f):
    try:
        return json.load(open(f, encoding="utf-8")), None
    except Exception as e:
        return None, f"{f}: {e}"

# 1) syntax of the file we edit
data, err = load(TARGET)
if err:
    print("SYNTAX FAIL:", err); sys.exit(1)
print("syntax OK:", TARGET)

# 2) build the id universe
all_ids, req_ids, prof_ids, qual_ids = set(), set(), set(), set()
for f in glob.glob(ROOT + "/**/*.json", recursive=True):
    d, _ = load(f)
    if isinstance(d, dict): d = [d]
    if not isinstance(d, list): continue
    for o in d:
        if not isinstance(o, dict): continue
        t, i = o.get("type"), o.get("id")
        if isinstance(i, str):
            all_ids.add(i)
            if t == "requirement": req_ids.add(i)
            elif t == "proficiency": prof_ids.add(i)
            elif t == "tool_quality": qual_ids.add(i)
        if isinstance(o.get("id"), list):
            all_ids.update(x for x in o["id"] if isinstance(x, str))

def comp_ids(comps):
    for grp in comps or []:
        for alt in grp:
            if isinstance(alt, list) and alt and isinstance(alt[0], str):
                yield alt[0]

problems = []
for o in data:
    if not isinstance(o, dict) or o.get("type") != "recipe": continue
    r = o.get("result")
    if r not in TOUCHED: continue
    if r not in all_ids:
        problems.append(f"{r}: result item not defined")
    for cid in comp_ids(o.get("components")):
        if cid not in all_ids:
            problems.append(f"{r}: component id '{cid}' not found")
    for u in o.get("using", []):
        name = u[0] if isinstance(u, list) else u
        if name not in req_ids and name not in all_ids:
            problems.append(f"{r}: using requirement '{name}' not found")
    for tl in o.get("tools", []):
        for alt in tl:
            if isinstance(alt, list) and alt and isinstance(alt[0], str) and alt[0] not in all_ids:
                problems.append(f"{r}: tool id '{alt[0]}' not found")
    for p in o.get("proficiencies", []):
        pid = p.get("proficiency")
        if pid and prof_ids and pid not in prof_ids:
            problems.append(f"{r}: proficiency '{pid}' not found")
    for q in o.get("qualities", []):
        qid = q.get("id")
        if qid and qual_ids and qid not in qual_ids:
            problems.append(f"{r}: quality '{qid}' not found")

if problems:
    print("ID RESOLUTION FAIL:")
    for p in problems: print("  -", p)
    sys.exit(1)

found = {o.get("result") for o in data if isinstance(o, dict) and o.get("type") == "recipe"}
missing = {x for x in TOUCHED if x != "nitrogen_generator"} - found
if missing:
    print("MISSING RECIPES:", ", ".join(sorted(missing))); sys.exit(1)
print("id resolution OK; all touched recipes present")
```

- [ ] **Step 2: Run it against the current (unedited) file to confirm it runs**

Run (from repo root `/mnt/ssd_pool/martin/repos/Cataclysm-DDA`):
```bash
python3 <scratchpad>/check_recipes.py
```
Expected: `syntax OK`, then it FAILS at `MISSING RECIPES: hd_compressor, hd_pipe_fittings, ...` (the new recipes don't exist yet) and `nitrogen_generator` still references `nitrogen_membrane_filter` (which resolves, so no id failure). This confirms the script detects absence — the "failing test" for Tasks 1–3.

---

## Task 1: Group A — steel/fabrication parts (4 recipes)

Four recipes modeled on `pipe_fittings`, `water_faucet`, and the blacksmith `metal_tank`. These autolearn; difficulty + proficiencies are the gate.

**Files:**
- Modify: `data/json/recipes/tools/tool.json` (insert after the `nitrogen_generator` recipe object)
- Test: `<scratchpad>/check_recipes.py`

**Interfaces:**
- Produces item recipes for results `hd_pipe_fittings`, `hd_valve`, `small_pressure_vessel`, `large_pressure_vessel`. `hd_valve`, both vessels consume `hd_pipe_fittings`; Task 2's `hd_compressor` consumes both `hd_pipe_fittings` and `hd_valve`.

- [ ] **Step 1: Confirm the failing state**

Run: `python3 <scratchpad>/check_recipes.py`
Expected: FAIL with `MISSING RECIPES:` listing (among others) `hd_pipe_fittings, hd_valve, small_pressure_vessel, large_pressure_vessel`.

- [ ] **Step 2: Insert the four recipes**

Insert this block immediately after the `nitrogen_generator` recipe's closing `},`:

```json
  {
    "type": "recipe",
    "activity_level": "MODERATE_EXERCISE",
    "result": "hd_pipe_fittings",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_MATERIALS",
    "skill_used": "fabrication",
    "difficulty": 6,
    "time": "4 h",
    "autolearn": true,
    "using": [ [ "blacksmithing_standard", 3 ], [ "steel_standard", 2 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_blacksmithing" }, { "proficiency": "prof_toolsmithing" } ],
    "tools": [ [ [ "swage", -1 ] ] ]
  },
  {
    "type": "recipe",
    "activity_level": "MODERATE_EXERCISE",
    "result": "hd_valve",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_PARTS",
    "skill_used": "fabrication",
    "skills_required": [ "mechanics", 2 ],
    "difficulty": 6,
    "time": "3 h",
    "autolearn": true,
    "using": [ [ "welding_standard", 40 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_high_pressure_systems" } ],
    "qualities": [ { "id": "HAMMER", "level": 2 }, { "id": "WRENCH", "level": 1 }, { "id": "SCREW", "level": 1 } ],
    "components": [ [ [ "hd_pipe", 1 ] ], [ [ "hd_pipe_fittings", 1 ] ], [ [ "spring", 1 ] ], [ [ "scrap", 4 ] ] ]
  },
  {
    "type": "recipe",
    "activity_level": "MODERATE_EXERCISE",
    "result": "small_pressure_vessel",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_CONTAINERS",
    "skill_used": "fabrication",
    "skills_required": [ "mechanics", 2 ],
    "difficulty": 6,
    "time": "5 h",
    "autolearn": true,
    "using": [ [ "welding_standard", 200 ], [ "steel_standard", 6 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_welding" }, { "proficiency": "prof_high_pressure_systems" } ],
    "qualities": [ { "id": "HAMMER", "level": 2 } ],
    "components": [ [ [ "sheet_metal", 4 ] ], [ [ "hd_pipe_fittings", 1 ] ] ]
  },
  {
    "type": "recipe",
    "activity_level": "BRISK_EXERCISE",
    "result": "large_pressure_vessel",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_CONTAINERS",
    "skill_used": "fabrication",
    "skills_required": [ "mechanics", 4 ],
    "difficulty": 8,
    "time": "20 h",
    "autolearn": true,
    "using": [ [ "welding_standard", 900 ], [ "steel_standard", 40 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_welding" }, { "proficiency": "prof_high_pressure_systems" } ],
    "qualities": [ { "id": "HAMMER", "level": 3 }, { "id": "SAW_M", "level": 2 } ],
    "components": [ [ [ "sheet_metal", 24 ] ], [ [ "hd_pipe_fittings", 4 ] ] ]
  },
```

- [ ] **Step 3: Validate**

Run: `python3 <scratchpad>/check_recipes.py`
Expected: `syntax OK`; still FAILS at `MISSING RECIPES:` but now only listing the Task 2 results (`hd_compressor, pressure_gauge, pyrometer`). No `ID RESOLUTION FAIL` lines for the four new recipes. If any component/using/proficiency id is reported missing, fix the typo before continuing.

- [ ] **Step 4: Commit**

```bash
git add data/json/recipes/tools/tool.json
git commit -m "feat(recipe): craftable heavy-duty fittings, valve, and pressure vessels"
```

---

## Task 2: Group B — instruments + compressor (3 recipes)

Three recipes modeled on a mechanical gauge, `crude_heating_element`/`motor_small` electronics, and `motor_small` + pipework. `pressure_gauge` autolearns at mechanics 3; `pyrometer` and `hd_compressor` gate behind skill thresholds and books.

**Files:**
- Modify: `data/json/recipes/tools/tool.json` (append to the same block, after `large_pressure_vessel`)
- Test: `<scratchpad>/check_recipes.py`

**Interfaces:**
- Consumes from Task 1: `hd_pipe_fittings`, `hd_valve`, `hd_pipe` (existing item).
- Produces item recipes for `pressure_gauge`, `pyrometer`, `hd_compressor`.

- [ ] **Step 1: Confirm the failing state**

Run: `python3 <scratchpad>/check_recipes.py`
Expected: FAIL with `MISSING RECIPES: hd_compressor, pressure_gauge, pyrometer`.

- [ ] **Step 2: Insert the three recipes**

Insert immediately after the `large_pressure_vessel` recipe's closing `},` (still before `can_sealer`):

```json
  {
    "type": "recipe",
    "activity_level": "LIGHT_EXERCISE",
    "result": "pressure_gauge",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_PARTS",
    "skill_used": "fabrication",
    "skills_required": [ "mechanics", 1 ],
    "difficulty": 4,
    "time": "1 h",
    "autolearn": [ [ "mechanics", 3 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" } ],
    "qualities": [ { "id": "HAMMER", "level": 1 }, { "id": "SCREW", "level": 1 } ],
    "components": [ [ [ "pipe", 1 ] ], [ [ "spring", 2 ] ], [ [ "scrap", 3 ] ], [ [ "glass_sheet", 1 ] ] ]
  },
  {
    "type": "recipe",
    "activity_level": "LIGHT_EXERCISE",
    "result": "pyrometer",
    "category": "CC_ELECTRONIC",
    "subcategory": "CSC_ELECTRONIC_COMPONENTS",
    "skill_used": "electronics",
    "skills_required": [ "fabrication", 1 ],
    "difficulty": 4,
    "time": "1 h 30 m",
    "autolearn": [ [ "electronics", 4 ] ],
    "book_learn": [ [ "manual_electronics", 2 ], [ "textbook_electronics", 2 ], [ "advanced_electronics", 1 ] ],
    "using": [ [ "soldering_standard", 10 ] ],
    "proficiencies": [ { "proficiency": "prof_elec_soldering" } ],
    "qualities": [ { "id": "SCREW", "level": 1 } ],
    "components": [ [ [ "e_scrap", 8 ] ], [ [ "cable", 15 ] ], [ [ "scrap_copper", 3 ] ], [ [ "scrap", 2 ] ] ]
  },
  {
    "type": "recipe",
    "activity_level": "MODERATE_EXERCISE",
    "result": "hd_compressor",
    "category": "CC_OTHER",
    "subcategory": "CSC_OTHER_PARTS",
    "skill_used": "fabrication",
    "skills_required": [ [ "mechanics", 4 ], [ "electronics", 2 ] ],
    "difficulty": 7,
    "time": "6 h",
    "autolearn": [ [ "fabrication", 7 ], [ "mechanics", 6 ] ],
    "book_learn": [ [ "manual_mechanics", 3 ], [ "advanced_electronics", 3 ] ],
    "using": [ [ "welding_standard", 80 ], [ "soldering_standard", 10 ] ],
    "proficiencies": [ { "proficiency": "prof_metalworking" }, { "proficiency": "prof_basic_engines" }, { "proficiency": "prof_high_pressure_systems" } ],
    "qualities": [ { "id": "HAMMER", "level": 2 }, { "id": "WRENCH", "level": 1 }, { "id": "SCREW", "level": 1 } ],
    "components": [ [ [ "motor", 1 ] ], [ [ "hd_pipe", 2 ] ], [ [ "hd_pipe_fittings", 1 ] ], [ [ "hd_valve", 1 ] ], [ [ "sheet_metal", 4 ] ], [ [ "cable", 50 ] ], [ [ "power_supply", 1 ] ] ]
  },
```

- [ ] **Step 3: Validate**

Run: `python3 <scratchpad>/check_recipes.py`
Expected: `syntax OK`; `id resolution OK; all touched recipes present`. (All seven new recipes now exist and every id resolves.) Fix any reported missing id before continuing.

- [ ] **Step 4: Commit**

```bash
git add data/json/recipes/tools/tool.json
git commit -m "feat(recipe): craftable pressure gauge, pyrometer, and industrial compressor"
```

---

## Task 3: Swap the nitrogen membrane filter in `nitrogen_generator`

Replace the one exotic component with a stack of common air filters, so the generator no longer needs the un-craftable membrane.

**Files:**
- Modify: `data/json/recipes/tools/tool.json` (inside the existing `nitrogen_generator` recipe's `components`)
- Test: `<scratchpad>/check_recipes.py`

**Interfaces:**
- Consumes `hd_compressor` (now craftable, Task 2) — unchanged, already present in this recipe.

- [ ] **Step 1: Make the swap**

In the `nitrogen_generator` recipe's `components`, replace this exact line:

```json
      [ [ "nitrogen_membrane_filter", 1 ] ],
```

with:

```json
      [ [ "filter_air", 4 ], [ "filter_air_makeshift", 6 ] ],
```

Leave every other component in `nitrogen_generator` unchanged.

- [ ] **Step 2: Verify the swap and full resolution**

Run:
```bash
python3 <scratchpad>/check_recipes.py
grep -n "nitrogen_membrane_filter" data/json/recipes/tools/tool.json || echo "membrane no longer referenced in tool.json"
```
Expected: `id resolution OK; all touched recipes present`, and the grep prints `membrane no longer referenced in tool.json` (the item's own definition in `spares.json` is untouched and still valid).

- [ ] **Step 3: End-to-end craftability check**

Confirm every component of the three machine recipes is now either one of the seven new craftable results or a common findable/craftable item (no remaining niche part except via the intended swap). Run:
```bash
python3 - <<'PY'
import json
d = json.load(open("data/json/recipes/tools/tool.json"))
machines = {"ammonia_machine_reactor","ammonia_machine_pipework","nitrogen_generator"}
niche = {"nitrogen_membrane_filter"}  # the only part we deliberately did NOT make craftable
for o in d:
    if isinstance(o,dict) and o.get("result") in machines:
        used = {a[0] for g in o.get("components",[]) for a in g if isinstance(a,list)}
        bad = used & niche
        print(o["result"], "OK" if not bad else f"STILL USES {bad}")
PY
```
Expected: all three print `OK`.

- [ ] **Step 4: Commit**

```bash
git add data/json/recipes/tools/tool.json
git commit -m "feat(recipe): build nitrogen_generator from common air filters, not the exotic membrane"
```

---

## Task 4: Style pass and final review

**Files:**
- Modify (whitespace only, if the formatter runs): `data/json/recipes/tools/tool.json`

- [ ] **Step 1: Run the JSON formatter if the toolchain is available**

```bash
make style-all-json-parallel RELEASE=1
git diff --stat
```
If `make`/the C++ toolchain is unavailable in this environment (as in the authoring session), skip this step — the CI job `.github/workflows/json.yml` (`make style-all-json-parallel`) is the authoritative style gate on the PR. If the formatter did produce corrections, review them (whitespace only) and amend/commit:
```bash
git add data/json/recipes/tools/tool.json
git commit -m "style: json formatter pass for ammonia-machine part recipes"
```

- [ ] **Step 2: Final full-file syntax + id sanity**

Run: `python3 <scratchpad>/check_recipes.py`
Expected: `syntax OK` and `id resolution OK; all touched recipes present`.

- [ ] **Step 3: Confirm branch state**

```bash
git log --oneline -6
git branch --show-current   # expect: ammonia-craftable-parts
```
Do NOT merge to `master` here — merging fires the `notify-guide` dispatch that rebuilds and redeploys the guide. Leave that as an explicit follow-up once the branch is reviewed (open a PR like the anvil change, PR #11).

---

## Self-Review

**Spec coverage:**
- 7 new recipes (hd_pipe_fittings, hd_valve, small/large_pressure_vessel, pressure_gauge, pyrometer, hd_compressor) → Tasks 1–2. ✓
- Membrane swap in `nitrogen_generator` → Task 3. ✓
- `filter_air ×4 / filter_air_makeshift ×6` swap quantity → Task 3 Step 1. ✓
- `large_pressure_vessel` punishing cost (20 h, steel_standard 40) → Task 1. ✓
- Gating: steel parts autolearn; pyrometer/compressor book+skill gated → Tasks 1–2 (matches spec's tunable-knob decision). ✓
- Validation via JSON load / formatter, matching anvil-recipe practice → Tasks 0/4. ✓
- Deployment note (merge fires notify-guide) → Task 4 Step 3 / spec. ✓
- `nitrogen_membrane_filter` item left defined → Task 3 (only the recipe reference is removed). ✓

**Placeholder scan:** No TBD/TODO; every recipe is given as complete JSON; every step has a concrete command and expected output.

**Type consistency:** `hd_pipe_fittings` (Task 1) is consumed by `hd_valve`, both vessels (Task 1) and `hd_compressor` (Task 2); `hd_valve` (Task 1) is consumed by `hd_compressor` (Task 2) — names match across tasks. `skills_required` uses flat form for single-skill recipes and nested form for the two-skill `hd_compressor`, consistent with the file's existing recipes.
