# Fork CI — build & test on PRs to master

**Date:** 2026-07-21
**Status:** Approved (design)
**Context:** Solo, personal-use fork of Cataclysm-DDA (see memory `fork-purpose-solo-personal`). Master is baselined on the 0.I "Ito" release. Goal: compile and run the test suite on every PR (and direct push) to `master`, without the contributor-coordination machinery inherited from upstream.

## Problem

The fork inherited upstream's full CI suite (37 workflows). The main build-and-test workflow (`matrix.yml`) is branch-filtered to `0.I-branch`, so it never runs on this fork's `master`. Meanwhile ~34 inherited workflows are contributor-coordination / release / translation / bot automation that is irrelevant to a solo fork and, in several cases, actively fails (missing upstream secrets, schedules, labels).

## Goal / non-goals

**Goal:** A minimal, self-owned CI that, on PRs and pushes to `master`:
1. Compiles the game (Linux, tiles + sound).
2. Runs the `cata_test` suite.
3. Validates JSON and (when C++ changes) C++ formatting.

**Non-goals:** Multi-platform build matrix, Windows/macOS/emscripten/android builds, clang-tidy/IWYU/CodeQL static analysis, translation sync, release automation, PR labeling/reviewer/stale bots. Not chasing upstream parity.

## Design

End state of `.github/workflows/` — **3 workflows**:

### 1. `pr-build.yml` (new) — build & test
- **Triggers:** `push` to `master` and `pull_request` targeting `master`.
- **Concurrency:** group by PR number / ref; cancel superseded in-progress runs for PRs.
- **Job `linux-tiles`** on `ubuntu-latest`:
  1. `actions/checkout@v4`
  2. Install deps: `build-essential`, `libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev`, `libflac-dev`, `ccache`.
  3. Restore ccache (e.g. `hendrikmuhs/ccache-action`).
  4. Build: `make -j$(nproc) NATIVE=linux64 RELEASE=1 TILES=1 SOUND=1 LOCALIZE=0 CCACHE=1`.
  5. Run tests: `tests/cata_test` (full default suite; hidden `[.]`/benchmark tests excluded by default).
  6. On failure, upload the `cata_test` binary / relevant output as an artifact.
- **Rationale:** `RELEASE=1` keeps CI fast and tests quick; Linux tiles is the cheapest coverage (1× minutes) and complements local macOS debug builds. ccache makes reruns cheap.

### 2. `astyle.yml` (kept as-is)
Already triggers on `pull_request` (all branches, path-gated to `**.cpp/**.h/**.c`), so it runs on master PRs unchanged. Runs `make astyle-check`; skips automatically on JSON-only PRs.

### 3. `json.yml` (kept as-is)
Already triggers on `pull_request` (path-gated to `**.json`). Runs `make style-all-json-parallel RELEASE=1`. Highest-value check for a content-modding fork.

### Removed — the other 34 workflows (deleted)
Delete outright (recoverable from upstream / git history):

`matrix.yml`, `msvc-full-features.yml`, `clang-tidy.yml`, `iwyu.yml`, `codeql-analysis.yml`, `emscripten.yml`, `format_emscripten.yml`, `flake8.yml`, `cmake-format.yml`, `linter.yml`, `release.yml`, `release-android-bundle.yaml`, `pull-translations.yml`, `push-translation-template.yml`, `detect-translation-file-changes.yml`, `check-translation.yml`, `update-tilesets.yml`, `weekly-changelog.yml`, `stale.yml`, `gh-pages-rebuild.yml`, `post-spell-check-result.yml`, `comment-commands.yml`, `toc.yml`, `CBA.yml`, `request-review.yml`, `labeler.yml`, `summary-labeler.yml`, `label-first-time-contributor.yml`, `test_labeler.yml`, `check-branch-name.yml`, `pr-validator.yml`, `text-changes-analyzer.yml`, `assign_mission_target_needs_om_special.yml`.

(Exact final list = everything under `.github/workflows/` except `pr-build.yml`, `astyle.yml`, `json.yml`. That's the source of truth; the enumeration above is illustrative.)

Optionally also prune now-orphaned support files referenced only by deleted workflows (e.g. `.github/reviewers.yml`, `.github/summary-labels.yml`, `.github/labeler.yml`, `.github/comment-commands.yml`) — to be confirmed during implementation by grepping for references.

## Verification
- `pr-build.yml` and the two kept workflows are valid YAML and parse as GitHub Actions.
- The build command matches the known-good local recipe (adjusted for Linux/gcc).
- After deletion, `.github/workflows/` contains exactly the 3 intended files.
- Real confirmation is a green run: open a trivial PR to master and confirm build+test (and json) execute and pass.

## Risks
- First CI run may reveal Linux/gcc-specific warnings-as-errors that don't appear in the local macOS clang build. Fix forward.
- `cata_test` runtime on CI could be long; ccache addresses build time, not test time. Acceptable for a personal fork; can scope tests later if needed.
