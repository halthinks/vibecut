# VibeCut — Current Roadmap Implementation Status

**Branch:** `agent/vibecut-architecture-slices`  
**Release authority:** do not merge to `vibecut` or make a release-ready claim until compile/tests/package and hands-on smoke gates pass.  
**Verification:** local `scripts/vibecut-verify.sh` is authoritative; branch CI may supplement the hardening cycle but does not replace local or hands-on verification.  
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
- Local verification gate plus optional branch-scoped CI hardening.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

### Timeline / bulk / groups
- Clip move/split/trim/ripple/delete.
- Selection inspect/set/clear.
- Group create/ungroup and group-relative movement.
- Transactional `bulk_delete`, `bulk_clip_move`, `bulk_clip_copy_to` with dry-run/rollback/verification.
- **New source implementation:** reusable governed `timeline_range_remove` with explicit `lift` / `ripple` semantics, native `TimelineFunctions::extractZoneWithUndo`, locked-track refusal, live postcondition verification and one Undo transaction.
- **New source implementation:** `repeated_take_selection_execute` revalidates explicit human keep choices, rejects overlapping removals, executes rejected ranges right-to-left through the same range-removal transaction path, rolls back on failure and commits one atomic Undo step.

### Effects / transitions / mixes
- Installed-effect discovery; effect inspect/add/remove/parameter edit.
- Effect groups, keyframes and stack copy.
- Track/master bus effects.
- Transition add/move/resize/remove, parameter inspection/edit and composition A-track control.
- Same-track mix inspect/add/resize/remove.
- **Open:** same-track mix type/parameter editing only where Kdenlive exposes a sufficiently stable non-widget backend seam.

### Titles
- Native title creation/update.
- Embedded title inspection.
- Indexed text-item text/style/position/font/color/weight/z-index editing.
- **Open:** richer shape/image editing, reusable templates/lower-thirds and brand packs.

### Tracks / audio
- Track list/create/rename/move/lock/mute-hide/delete.
- Audio/video insertion targeting and stream routing.
- Audio monitoring state/set.
- **Open:** mixer gain/pan/solo only where a stable backend seam is available; do not bridge widget-only state as an editing API.

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

## Phase 2 — hard release/merge gate — ACTIVE BLOCKER

The integration branch has broad source implementation, but **source-complete is not merge-safe by itself**. The current release gate is:

1. Run repository-local `scripts/vibecut-verify.sh` on a host with the required Kdenlive stack.
2. CMake configure succeeds.
3. Full Kdenlive/VibeCut compile and link succeed.
4. All `vibecut*` tests pass.
5. Smoke `timeline_range_remove` in both lift and ripple modes, including locked-track refusal and verified postconditions.
6. Smoke repeated-take candidate → review → explicit selection → execution → one-step Undo/redo, including overlap refusal.
7. `vibecut-halthinks_<version>_<arch>.deb` builds from the verified build tree.
8. Install/uninstall package smoke on a clean Debian-compatible host.
9. Hands-on editor smoke for plan → approve → edit → verify/diff → Undo across major edit families.
10. Whisper/render/cancel smoke.
11. Review/Auto/Turbo and `.vibecutpolicy.json` smoke.
12. Only then merge to `vibecut`; an upstream VibeCut PR remains optional and subject to upstream maintainer interest.

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

## Phase 4 — editorial intelligence foundation — GOLDEN MUTATION EVAL BASELINE + REPEATED-TAKE EXECUTION LANDED IN SOURCE

### Repeated takes
- Transcript/subtitle repeated-take candidate grouping.
- Evidence-backed review and take-quality context.
- Explicit human-choice selection planning.
- **Landed in source:** final repeated-take selection execution; it no longer stops at candidate/review/selection planning.
- **Landed in source:** reusable governed `timeline_range_remove` primitive underneath destructive timeline-range work.
- **Still gated:** compile, runtime smoke and quantitative Undo-fidelity fixtures before release-quality claims.

### Golden mutation evaluation
- **Landed in source:** deterministic `VibeCutEvaluator::evaluateMutation` contract with normalized verified-success, Undo-fidelity and Redo-fidelity scores.
- **Landed in source:** exact canonical-state comparison for Undo/Redo and requested-postcondition comparison that ignores unrelated live state.
- **Landed in source:** explicit `Applied`, `Refused` and `RolledBack` mutation outcomes; refusal/rollback only pass when canonical state preservation is verified.
- **Landed in source:** deterministic golden contract fixtures for successful ripple range removal, stale-plan refusal, repeated-take overlap refusal, locked-track refusal and rollback after partial failure.
- **Still gated:** bind those fixture IDs to executable tiny Kdenlive projects and live tool invocations; the JSON contract corpus does not replace compile/runtime/hands-on verification.

### Remaining work — dependency sequence

#### A. Golden mutation evaluation — BASELINE LANDED, LIVE FIXTURES NEXT
- Bind golden editing fixture IDs to executable tiny Kdenlive projects.
- Capture canonical live state before edit, after edit, after one Undo and after Redo.
- Feed live observations through the same verified-success / Undo-fidelity / Redo-fidelity thresholds now implemented in `VibeCutEvaluator`.
- Add executable stale-plan, rollback, overlap and locked-track refusal fixtures so refusal semantics are measured against the editor, not only the evaluator contract.
- Keep this evaluation layer cross-cutting as each new destructive or synthesizing capability lands.

#### B. Richer evidence extraction
- Speaker diarization.
- User-governed speaker naming/identity association.
- OCR/on-screen text.
- Richer noise/room-tone characterization and speech/music/audio-event analysis.
- Visual subject/object/action evidence with exact source/timeline ranges and provenance.

#### C. Semantic retrieval and duplicate understanding
- Transcript/text embeddings.
- Visual embeddings.
- Cross-modal semantic search across transcript, OCR and visual evidence.
- Stronger duplicate/near-duplicate detection combining deterministic similarity, embeddings, text/OCR and temporal context.

#### D. Editorial synthesis
- Rough-cut synthesis.
- Highlights/shorts extraction.
- B-roll opportunity detection, candidate retrieval and reviewable B-roll planning.
- Pacing analysis.
- Narrative/section/continuity analysis.
- Finishing-pass synthesis remains downstream of evidence, retrieval and safe editing primitives.

#### E. Presentation and audio breadth
- Richer title shapes/images/templates/lower-thirds/brand packs.
- Mixer gain/pan/solo only where Kdenlive exposes a safe backend seam.
- Mix type/parameter editing only where Kdenlive exposes a safe backend seam.

#### F. Provider scale after task contracts stabilize
- Additional local/hosted model-provider adapters.
- Capability declaration/failover.
- Per-task routing for planning, OCR, diarization, vision, embeddings and transcription.
- Provider/model/version/cost/latency provenance where relevant to evaluation/evidence.

## Distribution and README lineage

The repository preserves three documentation layers:

1. halthinks/VibeCut — current capability-expanded fork;
2. original VibeCut — the AI-scriptable Kdenlive adaptation this fork descends from;
3. Kdenlive — the original editor and upstream foundation.

The halthinks distribution layer is separate from inherited Kdenlive packaging. `packaging/vibecut/build-deb.sh` creates `vibecut-halthinks`, installed under `/opt/vibecut-halthinks`, so it can coexist with a normal Kdenlive install.

The README lineage must remain intact when the branch merges; capability/status edits belong in the halthinks section above the preserved original VibeCut and Kdenlive layers.

## Current engineering rule

Do not broaden capability by guessing private Kdenlive internals. Prefer native public model/request paths, accumulated undo/redo APIs and live postcondition verification. If an upstream seam is ambiguous, keep it explicitly open rather than claiming unsafe support. A feature that can only be proposed is not “finished” when the product requirement is governed execution; a consequential edit is complete only when its execution, verification and Undo story are real. Quantitative mutation evaluation is now part of that definition: requested postconditions and reversible editor state must be measured, not inferred from tool return values.
