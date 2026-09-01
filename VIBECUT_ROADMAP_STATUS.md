# VibeCut — Current Roadmap Implementation Status

**Branch:** `agent/vibecut-architecture-slices`  
**Release authority:** do not merge to `vibecut` or make a release-ready claim until compile/tests/package and hands-on smoke gates pass.  
**Verification:** local `scripts/vibecut-verify.sh` remains authoritative and is also exercised in GitHub Actions for the current hardening cycle.  
**Distribution:** a halthinks-specific Debian package is produced only after successful verification.

This file is the concise live-state ledger for the halthinks/VibeCut fork. It intentionally distinguishes source implementation from build/runtime verification.

## R0 — governed agent kernel — LANDED IN SOURCE

- Revision-bound EditPlan, deterministic plan runtime, approval/checkpoints and stale-plan rejection.
- Review / Auto / Turbo trust modes and per-project `.vibecutpolicy.json`.
- Kdenlive undo integration, rollback and before/after project diffs.
- Shared JobManager with progress, cancellation and async synchronization.
- Conversation compaction, `.vibecutrules`, `.vibecutmemory.json`.
- Provider registry/hooks, KWallet secret storage and provider hot reload.
- VibeScript plan-only sandbox.
- Local verification gate plus branch-scoped CI hardening workflow.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

### Timeline / bulk / groups
- Clip move/split/trim/ripple/delete.
- Selection inspect/set/clear.
- Group create/ungroup and group-relative movement.
- Transactional `bulk_delete`, `bulk_clip_move`, `bulk_clip_copy_to` with dry-run/rollback/verification.

### Effects / transitions / mixes
- Installed-effect discovery; effect inspect/add/remove/parameter edit.
- Effect groups, keyframes and stack copy.
- Track/master bus effects.
- Transition add/move/resize/remove, parameter inspection/edit and composition A-track control.
- Same-track mix inspect/add/resize/remove.
- **Open:** same-track mix type/parameter editing where Kdenlive does not expose a sufficiently stable backend seam.

### Titles
- Native title creation/update.
- Embedded title inspection.
- Indexed text-item text/style/position/font/color/weight/z-index editing.
- **Open:** shape/image editing and reusable brand/template packs.

### Tracks / audio
- Track list/create/rename/move/lock/mute-hide/delete.
- Audio/video insertion targeting and stream routing.
- Audio monitoring state/set.
- **Open:** gain/pan/solo only when a stable non-widget backend seam is available.

### Bin / project resilience
- Bin list/import/timeline insert/source replacement.
- Folder list/create/move/rename/empty-only delete.
- Bin metadata edits.
- Missing-media inspection, single/batch relink and directory discovery.
- Proxy lifecycle baseline.
- Project preflight and long-job enforcement.

### Render/export
- Installed preset discovery.
- Native asynchronous rendering with JobManager cancellation and output verification.
- Deterministic destination-aware preset recommendations.
- Named export policy for YouTube, review proxy, archive master, vertical social, square social and audio master.
- Explicit original/proxy/conform requirements.

### Multi-sequence
- Project-wide sequence listing and inspection so planning does not assume the visible timeline is the whole project.

## Phase 2 — hard release gate — IN PROGRESS

The branch now has a real CI compile/test/package path and has already passed environment/bootstrap/dependency validation. Compiler failures discovered by CI are being repaired in batches using Ninja keep-going diagnostics.

The current release gate is:

1. CI/local dependency contract passes.
2. CMake configure succeeds.
3. Full Kdenlive/VibeCut compile and link succeed.
4. All `vibecut*` tests pass.
5. `vibecut-halthinks_<version>_<arch>.deb` builds from the verified build tree.
6. Install/uninstall package smoke on a clean Debian-compatible host.
7. Hands-on editor smoke for plan → approve → edit → verify/diff → Undo across major edit families.
8. Whisper/render/cancel smoke.
9. Review/Auto/Turbo and `.vibecutpolicy.json` smoke.
10. Only then merge to `vibecut`; an upstream VibeCut PR remains optional and subject to upstream maintainer interest.

## Phase 3 — persistent media evidence and deterministic analysis — LANDED IN SOURCE

- Persistent `.vibecutmedia.json` evidence ledger with bounded atomic persistence.
- Source fingerprint + extractor-version freshness.
- Provenance-aware `media_evidence_summary`, `media_evidence_list`, `media_evidence_freshness`.
- Media-index integration with confidence/provenance-aware retrieval.
- Source metadata, silence, loudness, shot-boundary, black, freeze and blur extractors.
- Stale-only `media_analyze_refresh` orchestration.
- Whisper transcript segments persisted with exact timeline snapshot provenance.
- Pairwise MPEG-7 video similarity evidence.
- Provider-neutral ML extractor registry and constrained evidence-persistence contract.

## Phase 4 — editorial intelligence — PARTIALLY LANDED IN SOURCE

### Dead-air
- Reviewable dead-air cleanup planning.
- Lift and single-track ripple execution.
- Linked/group-aware dead-air execution with rollback/Undo constraints.

### Repeated takes
- Transcript/subtitle repeated-take candidate grouping.
- Evidence-backed review and take-quality context.
- Explicit human-choice selection planning.
- **Open:** reusable governed `timeline_range_remove` primitive and final repeated-take selection execution.

### Next evidence/editorial depth
- Speaker diarization + user speaker naming.
- Noise/room-tone and richer audio-event segmentation.
- OCR/on-screen text.
- Visual subject/object/action descriptors.
- Visual/transcript embeddings and cross-modal semantic search.
- Broader duplicate/near-duplicate clustering.
- Rough-cut synthesis, highlights/shorts, B-roll planning, pacing/narrative diagnostics and finishing-pass synthesis.

## Phase 5 — evaluation / providers / advanced workflows

Still planned:
- golden editing fixtures and Undo-fidelity/verified-success metrics;
- search precision/recall and editorial review rubrics;
- additional model/provider adapters and per-task routing;
- governed stock/generation/music/publishing adapters;
- reusable template/meme systems, multicam, advanced audio/color and deeper nested/conform workflows;
- longer-horizon/headless/collaborative surfaces.

## Distribution and README lineage

The repository now preserves three documentation layers:

1. halthinks/VibeCut — current capability-expanded fork;
2. original VibeCut — the AI-scriptable Kdenlive adaptation this fork descends from;
3. Kdenlive — the original editor and upstream foundation.

The halthinks distribution layer is separate from inherited Kdenlive packaging. `packaging/vibecut/build-deb.sh` creates `vibecut-halthinks`, installed under `/opt/vibecut-halthinks`, so it can coexist with a normal Kdenlive install.

## Current engineering rule

Do not broaden capability by guessing private Kdenlive internals. Prefer native public model/request paths, accumulated undo/redo APIs and live postcondition verification. If an upstream seam is ambiguous, keep it explicitly open rather than claiming unsafe support.