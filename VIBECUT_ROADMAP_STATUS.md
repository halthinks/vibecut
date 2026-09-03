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
- **Governance hardening:** code-defined `confirmationRequired=true` is a non-waivable lower bound. Project `auto_allow` cannot clear hard confirmation; `always_confirm` may make policy stricter and wins conflicts.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

### Timeline / bulk / groups
- Clip move/split/trim/ripple/delete.
- Selection inspect/set/clear.
- Group create/ungroup and group-relative movement.
- Transactional `bulk_delete`, `bulk_clip_move`, `bulk_clip_copy_to` with dry-run/rollback/verification.
- Reusable governed `timeline_range_remove` with explicit lift/ripple semantics, native `TimelineFunctions::extractZoneWithUndo`, locked-track refusal, live postcondition verification and one Undo transaction.
- `repeated_take_selection_execute` revalidates explicit human keep choices, rejects overlapping removals, executes rejected ranges right-to-left through the same range-removal transaction path, rolls back on failure and commits one atomic Undo step.

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
4. All `vibecut*` tests pass, including live mutation, extractor evidence, policy override, speaker identity, OCR/audio/vision provider and derived-evidence fixtures.
5. Smoke `timeline_range_remove` in lift and ripple modes, including locked-track refusal and verified postconditions.
6. Smoke repeated-take candidate → review → explicit selection → execute → one-step Undo/Redo, including overlap refusal.
7. Smoke stale-plan refusal after a real intervening project edit and verify unchanged canonical state.
8. Smoke pyannote setup/status/start/cancel/persist, including missing-token and stale-source behavior.
9. Smoke user-governed speaker entity creation/assignment/unassignment and verify Turbo cannot waive confirmation.
10. Smoke Tesseract OCR on CFR and VFR media, cancellation, language-pack errors, geometry/confidence provenance and temporal-track behavior.
11. Smoke EBU-R128 bounded sampling/orchestration and room-tone candidate derivation against silence evidence.
12. Smoke AST AudioSet setup/start/cancel, CPU/GPU behavior, exact window provenance and prediction-summary semantics.
13. Smoke shared vision setup and DETR object detection on CPU/GPU, exact sampled frames, bounding-box provenance, object continuity and subject ranking.
14. Smoke X-CLIP action inference, fixed action-set/hash verification, exact eight-frame window support, cancellation and temporal action summaries.
15. `vibecut-halthinks_<version>_<arch>.deb` builds from the verified build tree with explicit executable runtime dependencies.
16. Install/uninstall package smoke on a clean Debian-compatible host.
17. Hands-on editor smoke for plan → approve → edit → verify/diff → Undo across major edit families.
18. Whisper/render/cancel smoke.
19. Review/Auto/Turbo and `.vibecutpolicy.json` smoke, including the non-waivable hard-confirmation invariant.
20. Only then merge to `vibecut`; an upstream VibeCut PR remains optional and subject to upstream maintainer interest.

## Phase 3 — persistent media evidence and deterministic analysis — LANDED IN SOURCE

- Persistent `.vibecutmedia.json` evidence ledger with bounded atomic persistence.
- Source fingerprint + extractor-version freshness.
- Provenance-aware `media_evidence_summary`, `media_evidence_list`, `media_evidence_freshness`.
- Media-index integration with confidence/provenance-aware retrieval.
- Source metadata, silence, source-wide loudness, EBU-R128 windowed loudness, shot-boundary, black, freeze and blur extractors.
- Stale-only `media_analyze_refresh` orchestration; R128 is included for audio assets.
- Whisper transcript segments persisted with exact timeline snapshot provenance.
- Pairwise MPEG-7 video similarity evidence.
- Provider-neutral ML extractor registry and constrained evidence-persistence contract.
- Built-in provider registration occurs before first provider discovery/start.
- First-class provider tools share one authoritative dispatch path: live source normalization → provider → JobManager → capability-specific evidence contract → bounded ledger persistence.
- Capability-specific evidence admission constrains diarization, OCR, AudioSet predictions, DETR object detections and X-CLIP action predictions before provider output can enter persistent state.

## Phase 4 — editorial intelligence foundation — AUDIO + VISUAL EVIDENCE BASELINE ADVANCING

### Repeated takes and golden mutation evaluation — SOURCE BASELINE COMPLETE
- Transcript/subtitle repeated-take candidate grouping, evidence-backed review and explicit human-choice planning.
- Final repeated-take execution and reusable governed `timeline_range_remove` are landed.
- Model-bound repeated-take mutation core is shared by production and headless fixtures.
- Quantitative `VibeCutEvaluator::evaluateMutation` measures verified success, exact canonical Undo fidelity and Redo fidelity.
- Deterministic fixture corpus covers successful ripple removal, stale-plan refusal, repeated-take overlap refusal, locked-track refusal and rollback after partial failure.
- `vibecut_mutation_state_v1` captures revision-independent canonical editable state.
- Live/headless source fixtures cover ripple removal, locked refusal, runtime rollback, stale-plan refusal, overlap refusal and successful repeated-take execution with exactly one Undo command plus exact Undo/Redo state restoration.
- **Still gated:** authoritative compile/test execution on a Kdenlive development host.

### Speaker diarization — SOURCE FOUNDATION LANDED
- Strict `speaker_segment` evidence admission with authoritative source ranges.
- Diarizers have clustering authority only; human identity/name/entity assertions are rejected before persistence.
- Built-in `local_pyannote` provider uses `pyannote/speaker-diarization-community-1` through a VibeCut-owned isolated environment.
- Bounded excerpts, CPU/CUDA/auto routing, optional speaker bounds, exclusive diarization, JobManager cancellation and result-schema validation.
- `speaker_diarization_status`, hard-confirm/cancellable setup and first-class start tools.
- Credentials remain in environment/KWallet, never chat schemas or process arguments; provider telemetry is disabled.
- **Still gated:** real package/model/token/terms flow, CPU/GPU runtime, cancellation and evidence smoke.

### User-governed speaker naming — SOURCE FOUNDATION LANDED
- Separate bounded `.vibecutspeakers.json`; diarization evidence cannot write it.
- Associations bind entities to source id + source fingerprint + extractor id/version + anonymous cluster id.
- Entity/cluster identity writes are hard-confirm external side effects even in Turbo.
- Stored cluster hashes are recomputed/validated and resolution independently checks all components.
- Tamper regression covers a modified source fingerprint paired with a stale stored hash.
- **Still gated:** compile/runtime interaction smoke and UX review for recurring speakers.

### OCR / on-screen text — SOURCE FOUNDATION LANDED
- Strict one-frame `ocr_text` observations: text, normalized confidence, sample frame, image dimensions, bounded pixel bbox, language and engine provenance.
- Built-in `local_tesseract` + `media_ocr_refresh`; no source/FFmpeg/Tesseract path injection.
- Helper performs one bounded exact-frame FFmpeg sampling pass and Tesseract TSV recognition.
- Engine version is included in extractor provenance; output/schema/geometry limits and JobManager cancellation are enforced.
- `media_ocr_tracks` derives temporal text persistence using text + normalized geometry continuity while preserving exact observed frames and explicit unobserved gaps.
- **Still gated:** compile/runtime OCR smoke, CFR/VFR exact-frame behavior, language packs, cancellation/process-tree smoke and quantitative OCR accuracy.

### Deterministic audio profile / room tone — SOURCE FOUNDATION LANDED
- `media_audio_profile_refresh` persists bounded FFmpeg EBU-R128 `audio_loudness_sample` observations.
- Momentary/short-term loudness are treated as windowed measurements; integrated loudness/LRA/true peak are explicitly cumulative measurements.
- Requested cadence is quantized to EBU-R128 metadata cadence and automatically coarsened **before execution** when necessary to honor `max_samples`.
- R128 participates in evidence freshness and normal `media_analyze_refresh` audio orchestration.
- `media_room_tone_candidates` is read-only derived inference over consecutive stable R128 measurements and excludes ranges intersecting known `silence` evidence.
- Room-tone output preserves contributing observed frames and remains `authority=derived_candidate`; silence/low loudness is never automatically relabeled as semantic room tone.
- **Still gated:** authoritative compile/runtime R128 parser/filter smoke and room-tone evaluation on representative recordings.

### Speech / music / general audio events — SOURCE FOUNDATION LANDED
- Strict `audio_event_prediction` admission requires exact source windows, normalized score, label/id/rank, model/taxonomy provenance and `authority=model_prediction`.
- Built-in `local_ast_audioset` uses a pinned MIT Audio Spectrogram Transformer AudioSet checkpoint through its own isolated Transformers/Torch environment.
- `audio_event_status`, hard-confirm/cancellable setup and `media_audio_events_refresh` are landed.
- Excerpts/window counts are bounded before inference; invalid cadence/limits fail closed rather than silently clamp.
- `media_audio_event_tracks` groups repeated same-label predictions only across exact source/model/taxonomy provenance and remains a derived prediction summary.
- Speech/music/background/environmental labels therefore become searchable model predictions without being promoted to observations.
- **Still gated:** actual model download, CPU/GPU runtime, cancellation, class-calibration/precision fixtures and representative speech/music/event smoke.

### Visual objects and editorial subjects — SOURCE FOUNDATION LANDED
- Strict `object_detection_prediction` contract permits only one-frame sampled predictions with normalized score, exact sample frame, bounded pixel bbox, label/id, model revision/taxonomy and `authority=model_prediction`.
- Built-in `local_detr_coco` uses pinned `facebook/detr-resnet-50` through a shared isolated vision environment.
- The DETR helper performs exact arithmetic FFmpeg frame sampling; the C++ provider independently verifies the expected sample sequence, runtime/model revision and all returned geometry before persistence.
- A helper install/runtime filename mismatch found during audit was corrected so installed `objects_detr.py` matches provider lookup.
- `media_objects_refresh` exposes only bin/range/sampling/device policy, never source path, FFmpeg path, provider id or model/revision injection.
- `media_object_tracks` derives same-label geometry-continuity tracks only across exact source/extractor/model/taxonomy provenance, with exact observed frames/normalized boxes and explicit unobserved intervals.
- `media_subject_candidates` transparently ranks those tracks for editorial prominence using fixed weights over mean model confidence, screen area, center proximity and observed-sample density. It remains `authority=derived_candidate`, not person/object identity or semantic importance fact.
- **Still gated:** vision environment/model acquisition, CPU/GPU runtime, cancellation, exact frame sampling on real media, DETR accuracy and subject-ranking evaluation.

### Visual actions — SOURCE FOUNDATION LANDED
- The obvious noncommercial action-model route was rejected for the built-in product path; VibeCut uses an MIT-licensed Microsoft X-CLIP checkpoint instead.
- Strict `action_prediction` admission requires a non-empty source window, label/prompt/id/rank, normalized score, model revision/taxonomy, `authority=model_prediction` and exactly eight strictly increasing observed source frames inside the window.
- Built-in `local_xclip_actions` reuses the isolated vision runtime and is pinned to `microsoft/xclip-base-patch32` at a fixed safetensors-bearing revision.
- `VibeCutActionSet-v1` is a fixed 47-label zero-shot vocabulary. Its canonical SHA-256 is `005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc`; provider output with any other candidate-set hash is rejected.
- Scores declare `softmax_over_fixed_action_set`; they are relative compatibility scores over that exact candidate set, not calibrated factual probabilities.
- Python and C++ use the same positive half-up seconds→frame conversion to avoid cross-language provenance drift.
- The provider reconstructs and verifies every expected window and exact eight-frame support set, label/prompt pair, model/runtime revision and action-set hash before persistence.
- `media_actions_refresh` exposes only bin/range/window/cadence/device policy; callers cannot supply arbitrary models, revisions, labels or prompts.
- `media_action_tracks` groups same-label windows only across exact source/extractor/model/taxonomy/action-set provenance and retains every supporting prediction window/eight-frame sample set.
- Canonical vocabulary/versioning rules are documented in `VIBECUT_ACTION_SET.md`.
- **Still gated:** shared vision setup/model acquisition, CPU/GPU X-CLIP runtime, cancellation, action-set evaluation/calibration and representative real-video smoke.

### Remaining work — dependency sequence

#### A. Golden mutation evaluation — KEEP CROSS-CUTTING
- Extend mutation-state schema/version when new edit families need state outside v1 rather than weakening fidelity thresholds.
- Add golden fixtures for effects, transitions, titles, relink/proxy, render and later synthesis mutations.
- Keep exact requested-postcondition + Undo/Redo measurement active as each destructive/synthesizing capability lands.

#### B. Richer evidence extraction — ACTIVE
- Harden/verify diarization, naming, OCR, R128, AudioSet, DETR and X-CLIP on real media.
- Add camera-motion, shot-scale and composition evidence where it materially improves editorial decisions.
- Add richer deterministic/acoustic audio measurements where they improve noise/room-tone discrimination without falsely promoting semantic labels.
- Consider privacy-safe person/face evidence only with a governed identity boundary; never infer human identity as fact from a visual model.

#### C. Semantic retrieval and duplicate understanding — NEXT MAJOR DEPENDENCY
- Transcript/text embeddings.
- OCR text embeddings.
- Visual embeddings.
- Cross-modal semantic search across transcript, OCR, object/subject/action evidence and visual similarity.
- Stronger duplicate/near-duplicate detection combining deterministic similarity, embeddings, transcript/OCR and temporal context.
- Retrieval precision/recall fixtures before editorial synthesis consumes semantic search automatically.

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
- Additional local/hosted adapters where they materially improve a task.
- Capability declaration/failover and per-task routing.
- Provider/model/version/license/cost/latency provenance where relevant.
- Never silently substitute a model with incompatible licensing, taxonomy or score semantics.

## Distribution and README lineage

The repository preserves three documentation layers:

1. halthinks/VibeCut — current capability-expanded fork;
2. original VibeCut — the AI-scriptable Kdenlive adaptation this fork descends from;
3. Kdenlive — the original editor and upstream foundation.

The halthinks distribution layer is separate from inherited Kdenlive packaging. `packaging/vibecut/build-deb.sh` creates `vibecut-halthinks`, installed under `/opt/vibecut-halthinks`, so it can coexist with a normal Kdenlive install. Executable runtime dependencies needed by VibeCut-owned intelligence paths are explicit package dependencies rather than assumed to appear through ELF dependency discovery. Large ML environments/models remain governed setup-time assets rather than being silently embedded into the Debian package.

The README lineage must remain intact when the branch merges; capability/status edits belong in the halthinks section above the preserved original VibeCut and Kdenlive layers.

## Current engineering rules

Do not broaden capability by guessing private Kdenlive internals. Prefer native public model/request paths, accumulated undo/redo APIs and live postcondition verification. If an upstream seam is ambiguous, keep it explicitly open rather than claiming unsafe support.

A feature that can only be proposed is not “finished” when the product requirement is governed execution. A consequential edit is complete only when its execution, verification and Undo story are real. Quantitative mutation evaluation is part of that definition.

Evidence authority must remain explicit:

- deterministic measurements are observations only of what was actually measured;
- OCR/object extraction over sampled frames does not observe unsampled frames;
- diarization clusters are not human identities;
- AudioSet/DETR/X-CLIP outputs are model predictions, not facts;
- temporal tracks and subject/room-tone candidates are derived summaries/candidates, not promoted observations;
- X-CLIP softmax values are relative to the exact fixed action set and must not be interpreted as calibrated factual probabilities;
- any promotion to human identity, broader temporal fact or consequential edit requires the appropriate governed path and independent verification.
