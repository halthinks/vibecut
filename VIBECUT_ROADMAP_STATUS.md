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
- **Governance hardening:** a code-defined `confirmationRequired=true` is a non-waivable lower bound. Project `auto_allow` can no longer clear hard confirmation; `always_confirm` may still make policy stricter and wins conflicts.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

### Timeline / bulk / groups
- Clip move/split/trim/ripple/delete.
- Selection inspect/set/clear.
- Group create/ungroup and group-relative movement.
- Transactional `bulk_delete`, `bulk_clip_move`, `bulk_clip_copy_to` with dry-run/rollback/verification.
- **Source implementation:** reusable governed `timeline_range_remove` with explicit `lift` / `ripple` semantics, native `TimelineFunctions::extractZoneWithUndo`, locked-track refusal, live postcondition verification and one Undo transaction.
- **Source implementation:** `repeated_take_selection_execute` revalidates explicit human keep choices, rejects overlapping removals, executes rejected ranges right-to-left through the same range-removal transaction path, rolls back on failure and commits one atomic Undo step.

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
4. All `vibecut*` tests pass, including live mutation, extractor evidence, speaker-identity integrity, policy override and built-in provider discovery fixtures.
5. Smoke `timeline_range_remove` in both lift and ripple modes, including locked-track refusal and verified postconditions.
6. Smoke repeated-take candidate → review → explicit selection → execution → one-step Undo/redo, including overlap refusal.
7. Smoke stale-plan refusal after a real intervening project edit and verify unchanged canonical state.
8. Smoke local pyannote setup/status/start/cancel/persist flow, including missing-token and stale-source behavior.
9. Smoke user-governed speaker entity creation/assignment/unassignment and verify Turbo cannot waive confirmation.
10. Smoke local Tesseract `media_ocr_refresh`, cancellation, language/model availability errors and persisted geometry/confidence provenance.
11. `vibecut-halthinks_<version>_<arch>.deb` builds from the verified build tree with explicit FFmpeg/Python/venv/Tesseract runtime dependencies.
12. Install/uninstall package smoke on a clean Debian-compatible host.
13. Hands-on editor smoke for plan → approve → edit → verify/diff → Undo across major edit families.
14. Whisper/render/cancel smoke.
15. Review/Auto/Turbo and `.vibecutpolicy.json` smoke, including the non-waivable hard-confirmation invariant.
16. Only then merge to `vibecut`; an upstream VibeCut PR remains optional and subject to upstream maintainer interest.

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
- Built-in provider registration occurs before first provider discovery/start; discovery no longer depends on a prior status/setup call.
- Capability-specific evidence admission now constrains diarization and OCR before provider output can enter the persistent ledger.

## Phase 4 — editorial intelligence foundation — MUTATION EVAL + DIARIZATION + OCR FOUNDATIONS LANDED IN SOURCE

### Repeated takes
- Transcript/subtitle repeated-take candidate grouping.
- Evidence-backed review and take-quality context.
- Explicit human-choice selection planning.
- **Landed in source:** final repeated-take selection execution; it no longer stops at candidate/review/selection planning.
- **Landed in source:** reusable governed `timeline_range_remove` primitive underneath destructive timeline-range work.
- **Landed in source:** model-bound repeated-take mutation core used by production execution and headless fixtures without duplicating weaker test-only editing logic.
- **Still gated:** authoritative compile/runtime smoke before release-quality claims.

### Golden mutation evaluation
- **Landed in source:** deterministic `VibeCutEvaluator::evaluateMutation` contract with normalized verified-success, Undo-fidelity and Redo-fidelity scores.
- **Landed in source:** exact canonical-state comparison for Undo/Redo and requested-postcondition comparison that ignores unrelated live state.
- **Landed in source:** explicit `Applied`, `Refused` and `RolledBack` mutation outcomes; refusal/rollback only pass when canonical state preservation is verified.
- **Landed in source:** deterministic golden contract corpus for successful ripple range removal, stale-plan refusal, repeated-take overlap refusal, locked-track refusal and rollback after partial failure.
- **Landed in source:** `vibecut_mutation_state_v1`, a revision-independent live-state schema covering timeline topology/order, track state/effects, clip source/timing/effects, composition parameters, groups, subtitles and master effects.
- **Landed in source:** model-bound mutation-state capture so the same canonical schema is used by headless Kdenlive tests and the running editor.
- **Landed in source:** executable headless Kdenlive ripple range-removal fixture measuring requested postcondition, exact Undo fidelity and exact Redo fidelity.
- **Landed in source:** executable locked-track refusal fixture proving canonical state is unchanged when destructive work is denied.
- **Landed in source:** executable plan-runtime rollback fixture that performs a real timeline mutation, deliberately fails afterward, and requires the native checkpoint macro to restore exact canonical pre-edit state.
- **Landed in source:** executable stale-plan refusal fixture bound to a real intervening project edit and canonical unchanged-state verification.
- **Landed in source:** executable repeated-take overlap refusal bound to unchanged canonical live state.
- **Landed in source:** executable successful repeated-take selection fixture requiring exactly one added Undo-stack command, one Undo restoring exact canonical pre-state and one Redo restoring exact committed post-state.
- **Still gated:** authoritative compile/test execution on a Kdenlive development host; source tests are not claimed passing until that gate runs.

### Speaker diarization — SOURCE FOUNDATION LANDED
- **Landed in source:** provider-neutral `diarization` capability with strict `speaker_segment` evidence admission and authoritative requested frame bounds.
- **Landed in source:** diarizers have clustering authority only; evidence carrying human identity/name/entity fields is rejected before persistence.
- **Landed in source:** built-in `local_pyannote` provider using the open `pyannote/speaker-diarization-community-1` path through a VibeCut-owned isolated environment.
- **Landed in source:** bounded source excerpts, CPU/CUDA/auto routing, optional min/max speaker bounds, exclusive diarization, JobManager cancellation and result-schema validation.
- **Landed in source:** `speaker_diarization_status`, always-confirm/cancellable `speaker_diarization_setup`, and first-class `speaker_diarization_start` so callers need a real bin id rather than a provider/source path.
- **Landed in source:** Hugging Face credentials are sourced from process environment or KWallet and are never accepted/echoed by chat-facing tool schemas; pyannote telemetry is disabled by the local adapter.
- **Still gated:** real package installation, model acquisition/terms/token flow, CPU/GPU runtime, cancellation and evidence smoke on the Kdenlive development host.

### User-governed speaker naming — SOURCE FOUNDATION LANDED
- **Landed in source:** separate bounded `.vibecutspeakers.json` entity/association ledger; diarization evidence cannot write it.
- **Landed in source:** associations bind human-readable entities to the full source id + source fingerprint + extractor id + extractor version + anonymous cluster id, preventing stale source/model results from silently inheriting identity.
- **Landed in source:** speaker entity upsert and cluster assign/unassign are hard-confirm external side effects; Turbo/project `auto_allow` cannot waive that confirmation.
- **Landed in source:** association sidecars fail closed when stored `cluster_key` hashes do not match their component fields, and resolution independently rechecks all key components before returning a human identity.
- **Landed in source:** tamper regression covers source-fingerprint modification with a stale stored hash.
- **Still gated:** compile/runtime interaction smoke and UX review for naming multiple recurring speakers across clips/projects.

### OCR / on-screen text — SOURCE FOUNDATION LANDED
- **Landed in source:** strict `ocr` evidence contract permitting only one-frame `ocr_text` observations with non-empty text, normalized confidence, authoritative `sample_frame`, bounded pixel rectangle, image dimensions, language and engine provenance.
- **Landed in source:** built-in `local_tesseract` provider and first-class `media_ocr_refresh` tool; callers supply a bin id and bounded sampling parameters, never arbitrary source/FFmpeg/Tesseract paths.
- **Landed in source:** VibeCut helper performs one bounded FFmpeg decode/sampling pass and Tesseract TSV recognition, aggregates word evidence into line text/geometry, and returns JSON to the provider rather than writing evidence itself.
- **Landed in source:** adapter records actual Tesseract engine version in extractor provenance, enforces output-size/result-schema limits and uses JobManager cancellation.
- **Landed in source:** Debian packaging explicitly depends on FFmpeg, Python 3/venv and Tesseract because executable runtime dependencies are invisible to `dpkg-shlibdeps`.
- **Still gated:** compile/runtime OCR smoke, exact frame-sampling verification on CFR/VFR media, Tesseract language-pack behavior, cancellation/process-tree smoke and quantitative OCR accuracy fixtures.

### Remaining work — dependency sequence

#### A. Golden mutation evaluation — LIVE SOURCE BASELINE COMPLETE; KEEP CROSS-CUTTING
- Extend mutation-state schema/version when a new edit family needs state currently outside v1 rather than weakening the fidelity threshold.
- Add golden fixtures for effects, transitions, titles, relink/proxy, render and later synthesis mutations.
- Keep exact requested-postcondition + Undo/Redo measurement active as each new destructive/synthesizing feature lands.

#### B. Richer evidence extraction — ACTIVE
- Harden/verify speaker diarization and user-governed naming on real media.
- Harden/verify OCR and add temporal text persistence/deduplication so repeated sampled observations can become evidence-backed on-screen-text spans without pretending an unobserved frame was OCR'd.
- Richer noise/room-tone characterization and speech/music/audio-event analysis.
- Visual subject/object/action evidence with exact source/timeline ranges and provenance.
- Camera-motion/shot-scale/composition evidence where it materially improves editing decisions.

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

The halthinks distribution layer is separate from inherited Kdenlive packaging. `packaging/vibecut/build-deb.sh` creates `vibecut-halthinks`, installed under `/opt/vibecut-halthinks`, so it can coexist with a normal Kdenlive install. Executable runtime dependencies needed by VibeCut-owned intelligence paths are explicit package dependencies rather than assumed to appear through ELF dependency discovery.

The README lineage must remain intact when the branch merges; capability/status edits belong in the halthinks section above the preserved original VibeCut and Kdenlive layers.

## Current engineering rule

Do not broaden capability by guessing private Kdenlive internals. Prefer native public model/request paths, accumulated undo/redo APIs and live postcondition verification. If an upstream seam is ambiguous, keep it explicitly open rather than claiming unsafe support. A feature that can only be proposed is not “finished” when the product requirement is governed execution; a consequential edit is complete only when its execution, verification and Undo story are real. Quantitative mutation evaluation is part of that definition: requested postconditions and reversible editor state must be measured, not inferred from tool return values. Live/headless fixtures must exercise the same canonical mutation-state schema; do not create a weaker test-only representation.

Derived evidence has **observation authority only for what the extractor actually observed within its authoritative source/range contract**. A diarization cluster is not a person identity; an OCR sample does not imply that text existed on unsampled frames. Promotion from anonymous/inferred machine evidence to user-governed human identity or broader temporal claims requires an explicit governed path and independent supporting evidence.
