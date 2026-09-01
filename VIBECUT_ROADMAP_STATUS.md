# VibeCut — Current Roadmap Implementation Status

**Branch:** `agent/vibecut-architecture-slices`  
**Release authority:** no merge to `vibecut` until local compile/tests/smoke gate passes.  
**CI policy:** GitHub Actions are intentionally not used; verification is repository-local.

This file is the concise current-state ledger for `TODO.md`. It exists because implementation has moved faster than the original checklist. `TODO.md` remains the full product roadmap; this file records the live implementation frontier.

## R0 / Phase 0 — governed agent kernel — LANDED IN SOURCE

- Revision-bound EditPlan, approval/runtime/checkpoints, stale-plan rejection.
- Review / Auto / Turbo trust modes and per-tool `.vibecutpolicy.json`.
- Kdenlive undo integration, rollback and before/after diffs.
- Shared JobManager, cancellation and external-side-effect separation.
- Conversation compaction, `.vibecutrules`, `.vibecutmemory.json`.
- Provider registry/hooks, Anthropic provider, KWallet credentials/hot reload.
- VibeScript plan-only sandbox.
- Local `scripts/vibecut-verify.sh` release gate.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

### Timeline / bulk / groups
- Clip move/split/trim/ripple/delete.
- Selection inspect/set/clear.
- Group create/ungroup and group-relative frame move.
- Transactional `bulk_delete`, `bulk_clip_move`, `bulk_clip_copy_to` with dry-run/rollback/verification.

### Effects / transitions / mixes
- Installed effect discovery; inspect/add/remove/parameter edit.
- Effect-group expansion with child-by-child verification.
- Effect keyframe inspect/add/remove/move.
- Verified effect-stack copy to many clips.
- Track/master bus effect inspect/add/remove/parameter edit.
- Transition add/move/resize/remove and parameter inspect/set.
- Stable composition A-track inspect/set with internal Kdenlive↔MLT identity conversion.
- Same-track mix inspect/add/resize/remove.
- **Open:** mix type/parameter editing remains blocked on an ambiguous upstream mix-stack seam; do not fake it.

### Titles
- Native simple-title create/update.
- Arbitrary embedded title inspection.
- Indexed text-item text update.
- Indexed text-item style/position/font/color/weight/z-index update preserving unrelated title XML.
- **Open:** shape/image element editing, reusable style/template/brand packs.

### Tracks / audio
- Track list/create/rename/move/lock/mute-hide/delete.
- Audio/video insertion targeting and source-stream routing.
- Audio monitoring status/set.
- Track/master effect stacks.
- **Open:** stable backend gain/pan/solo interfaces; private mixer widgets are intentionally not bypassed.

### Bin / source resilience
- Bin list/import/timeline insert/source replacement.
- Folder list/create/move/rename/empty-only delete.
- Bin metadata name/description/tags/rating.
- Source inspect and missing-media enumeration.
- Single/batch relink, directory discovery and dry-run mapping.
- Proxy status/governed proxy actions.
- Project preflight and long-job enforcement.

### Render/export
- Installed preset discovery, native render process, shared job/cancel/output verification.
- Destination-aware deterministic preset recommendation.
- Named export policy: YouTube, review proxy, archive master, vertical social, square social, audio master.
- Original/proxy and conform requirements surfaced explicitly.

### Multi-sequence
- Project-wide sequence list and sequence inspection; planning no longer assumes the active timeline is the entire project.

## Phase 2 — HARD RELEASE GATE — NOT YET PASSED

The branch is still **source-implemented, not build-verified**.

Required before merge/release claims:

1. `bash scripts/vibecut-verify.sh` on a host with Kdenlive dependencies.
2. Repair every compiler/linker/test failure.
3. Re-run from a clean build directory.
4. Hands-on smoke: plan/approve/edit/diff/Undo across every native edit family.
5. Whisper/render/cancel smoke.
6. Review/Auto/Turbo and `.vibecutpolicy.json` smoke.
7. Only then merge to `vibecut` and consider an upstream PR.

## Phase 3 — source/project resilience — MOSTLY LANDED IN SOURCE

- Source-state inspection, true-missing vs proxy-only distinction.
- Explicit single and batch relink.
- Directory candidate discovery without ambiguous auto-selection.
- Proxy lifecycle baseline.
- Reusable project preflight consumed by Whisper/render paths.
- Source replacement/relink uses Kdenlive-native undo paths.

Remaining resilience depth:
- broader project normalization diagnostics;
- richer proxy quality/settings controls where a stable backend exists;
- additional preflight checks discovered through real-world projects.

## Phase 4 — media intelligence — ACTIVE IMPLEMENTATION FRONTIER

### Evidence/provenance/cache — LANDED IN SOURCE
- Persistent `.vibecutmedia.json` sidecar, separate from chat/project memory.
- Atomic, bounded, versioned, fail-closed evidence loading/saving.
- Evidence fields: source ID/fingerprint, extractor ID/version, kind, frame range, text, confidence, UTC production time, metadata.
- No generic LLM evidence-write tool; evidence is extractor-owned.
- Read-only `media_evidence_summary`, `media_evidence_list`, `media_evidence_freshness`.
- Freshness invalidates on source fingerprint **or extractor-version** change.
- Evidence automatically joins `VibeCutMediaIndex` with provenance/confidence-aware retrieval ranking.
- Unit tests cover record round-trip, invalid provenance/range/confidence and malformed/unsupported sidecars.

### Basic deterministic extractors — LANDED IN SOURCE
- `media_source_metadata_refresh`: file/path/stat/A-V/duration metadata with fingerprint.
- `media_silence_refresh`: async FFmpeg `silencedetect` → silence/dead-air frame ranges.
- `media_loudness_refresh`: async `volumedetect` → mean/max dB + near-clipping evidence.
- `media_shots_refresh`: Kdenlive-style scene-change detection → shot boundaries + shot segments.
- `media_black_refresh`: async `blackdetect` → black-frame ranges.
- `media_freeze_refresh`: async `freezedetect` → frozen-video ranges.
- All long extractors use Kdenlive's configured FFmpeg and shared JobManager/cancellation.
- Direct long extractors fail before work if project evidence cannot be persisted.
- `media_analyze_refresh`: one-call suite orchestration, defaults to **only missing/stale extractors**.

### Next media-intelligence work
- Whisper transcript output → provenance ledger and fingerprint freshness.
- Speaker diarization + user-provided speaker naming.
- Noise/room-tone and audio-event segmentation beyond coarse silence/loudness.
- OCR/on-screen text.
- Visual subject/object/action descriptors.
- Visual/transcript embeddings and cross-modal semantic search.
- Duplicate/near-duplicate shot detection.
- Blur/flash/error-frame evidence beyond black/freeze.

## Phase 5 — editorial reasoning — PLANNED AFTER EVIDENCE DEPTH

- Repeated-take/interview cleanup.
- Dead-air cleanup with editorial cadence and reviewable alternatives.
- Rough-cut synthesis.
- Highlights/shorts extraction.
- B-roll opportunity evidence and guide-first workflow.
- Narrative/pacing/speaker-balance diagnostics.
- Finishing pass synthesis.
- Reference-style matching.

All autonomous work must still resolve to explicit governed native operations with preconditions, verification and Undo.

## Phase 6+ — evaluation/providers/external/advanced

Still planned:
- golden editing fixtures, undo fidelity, success-vs-verified-success metrics, search precision/recall and rough-cut review rubrics;
- Ollama/OpenAI/other provider adapters and per-task provider routing;
- stock/generation/music/publishing adapters with external-side-effect governance;
- reusable template/meme systems, multicam, advanced audio/color, nested/conform workflows;
- long-horizon node compositor, TUI/headless/collaborative frontends.

## Current engineering rule

Do not broaden by guessing private Kdenlive internals. Prefer native public request/model paths, accumulated undo/redo APIs and live verification. If an upstream seam is ambiguous (currently same-track mix parameter/type access and some mixer controls), keep it explicitly open rather than claiming unsafe support.
