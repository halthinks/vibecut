# VibeCut — Current Roadmap Implementation Status

**Branch:** `agent/vibecut-architecture-slices`  
**Release authority:** do not merge to `vibecut` or make a release-ready claim until compile/tests/package and hands-on smoke gates pass.  
**Verification:** local `scripts/vibecut-verify.sh` is authoritative; source-landed tests and static audits do not replace that gate.  
**Distribution:** a halthinks-specific Debian package is produced only after successful verification.

This is the concise live-state ledger. The full dependency/product roadmap remains in `TODO.md`.

## R0 — governed agent kernel — LANDED IN SOURCE

- Revision-bound `EditPlan`, deterministic plan runtime, checkpoints, stale-plan rejection and native Undo integration.
- Review / Auto / Turbo trust modes plus project `.vibecutpolicy.json`.
- Code-defined `confirmationRequired=true` is a non-waivable lower bound; project `auto_allow` cannot clear it.
- Shared cancellable `JobManager`, project rules/memory, provider registry/hooks, KWallet secret storage and VibeScript plan-only sandbox.
- Repository-local verification gate exists; merge/release authority remains withheld until it passes.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

- Native clip move/split/trim/ripple/delete, selection, groups, bulk operations and multi-sequence inspection.
- Reusable governed `timeline_range_remove` with lift/ripple semantics, locked-track refusal, live verification and one accumulated Undo transaction.
- `repeated_take_selection_execute` revalidates explicit keep choices and uses the same governed range-removal transaction path.
- Effects/groups/keyframes/stack copy, transitions/compositions, same-track mix baseline, titles, tracks/routing, bin/project resilience, relink/proxy/preflight and render/export baseline.
- **Open by design:** mixer gain/pan/solo and mix type/parameter editing only where Kdenlive exposes a stable safe backend seam; richer title shapes/images/templates/brand packs remain downstream work.

## Phase 2 — hard release/merge gate — ACTIVE BLOCKER

No merge to `vibecut` until all of the following are green on a real Kdenlive development host:

1. `bash scripts/vibecut-verify.sh` from a clean build tree.
2. CMake configure, full compile/link and every `vibecut*` test.
3. Live mutation smoke for `timeline_range_remove`, repeated-take execution, stale-plan refusal and exact Undo/Redo fidelity.
4. Runtime/setup/cancellation/evidence smoke for pyannote, Tesseract, R128, AST, DETR and X-CLIP.
5. Semantic setup/runtime smoke for MiniLM plus shared-vision SigLIP, including model acquisition, CPU/GPU paths, cancellation, stale-source behavior and bounded result handling.
6. Hybrid lexical+MiniLM search smoke proving stale text and source-fingerprint semantic anchors are excluded from final ranking.
7. Pairwise MPEG-7, fused duplicate scoring and bounded project-wide duplicate-candidate smoke.
8. Rough-cut context/objective-ranking/alternative-comparison smoke proving revision/context hash refusal and zero mutation authority.
9. Debian package build plus clean-host install/uninstall/coexistence smoke.
10. Hands-on editor plan → authorize → execute → verify/diff → Undo across major edit families.
11. Review/Auto/Turbo and policy-override smoke, including non-waivable hard confirmation.

Only after those gates pass may the integration branch merge to `vibecut`. An upstream PR remains optional.

## Phase 3 — persistent media evidence and deterministic analysis — LANDED IN SOURCE

- Bounded atomic `.vibecutmedia.json` ledger with source fingerprint + extractor-version freshness.
- Provenance-aware evidence list/summary/freshness and media-index integration.
- Deterministic source metadata, silence, loudness, EBU-R128, shot, black, freeze and blur extractors.
- Whisper transcript evidence, pairwise MPEG-7 similarity evidence and stale-only `media_analyze_refresh` orchestration.
- Provider-neutral extractor registry and one shared authoritative dispatch path: live source normalization → provider → `JobManager` → capability-specific admission contract → bounded persistence.

## Phase 4 — rich media intelligence + retrieval — STRONG SOURCE FOUNDATION

### Golden mutation evaluation
- `VibeCutEvaluator::evaluateMutation` measures verified success plus canonical Undo/Redo fidelity.
- `vibecut_mutation_state_v1` is revision-independent canonical editable state.
- Source fixtures cover ripple removal, locked refusal, partial-failure rollback, stale-plan refusal, repeated-take overlap refusal and successful repeated-take execution as one Undo command.
- **Still gated:** authoritative host execution.

### Speaker diarization and governed naming
- Strict anonymous `speaker_segment` admission and built-in local pyannote provider.
- Diarizer has clustering authority only; it cannot assert human identity.
- Separate bounded `.vibecutspeakers.json` binds user-governed entities to source fingerprint + extractor/version + anonymous cluster.
- Stored association hashes are recomputed and resolution independently checks all key components; tamper regression landed.
- Identity writes remain hard-confirm external side effects even in Turbo.

### OCR / on-screen text
- Strict one-sampled-frame `ocr_text` observations with confidence, exact frame, bounded pixel geometry, language and engine provenance.
- Built-in local Tesseract provider and `media_ocr_refresh`.
- `media_ocr_tracks` derives repeated text persistence while preserving observed frames and explicit unobserved gaps.

### Audio profile / room tone / events
- Bounded EBU-R128 `audio_loudness_sample` observations integrated into freshness/orchestration.
- `media_room_tone_candidates` is a read-only derived candidate over stable measured windows and excludes known silence.
- Built-in MIT AST AudioSet classifier persists only `audio_event_prediction` evidence; `media_audio_event_tracks` remains a derived prediction summary.

### Visual objects / subjects / actions
- Built-in Apache-2.0 DETR object provider with strict one-frame geometry/provenance admission.
- `media_object_tracks` preserves sampled-only continuity; `media_subject_candidates` uses transparent editorial-prominence weights and remains a derived candidate.
- Built-in MIT X-CLIP action provider uses fixed `VibeCutActionSet-v1`, exact eight-frame support and candidate-set SHA-256 `005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc`.
- Action scores explicitly mean `softmax_over_fixed_action_set`, not factual probability; temporal action tracks preserve that authority.

### Semantic embeddings and retrieval — SOURCE-LANDED
- Separate bounded atomic `.vibecutembeddings.json` keeps raw vectors out of the human-readable evidence ledger.
- Embedding admission requires exact anchor/source/model/revision/producer provenance and unit-normalized finite vectors.
- Atomic producer/model refresh removes stale prior slices instead of accumulating old fingerprints.
- MiniLM (`sentence-transformers/all-MiniLM-L6-v2`, 384-D) provides transcript/OCR text embeddings and text semantic search in an isolated runtime.
- SigLIP (`google/siglip-base-patch16-224`, 768-D) provides exact sampled-frame visual embeddings and text→image cross-modal search through the isolated vision runtime.
- Embedding spaces cannot be mixed accidentally: cosine search requires exact model revision and dimension compatibility.
- MiniLM and SigLIP tool families are registered on the normal product surface; first-class schemas expose bounded intent, not arbitrary model/path/vector injection.
- `media_search_hybrid` fuses the canonical lexical index with MiniLM ranking, excludes semantic hits whose text **or source ID/fingerprint** is no longer current, binds the result to project revision and labels its score as a derived ranking rather than probability.
- Hybrid parent/child cancellation and already-terminal child races are handled explicitly.

### Duplicate / near-duplicate understanding — SOURCE-LANDED
- Existing deterministic FFmpeg MPEG-7 pairwise comparison remains an independent signal.
- `media_duplicate_fusion` combines available current evidence from MPEG-7, SigLIP visual similarity, temporal alignment, MiniLM source-text similarity, transcript/OCR lexical overlap and duration similarity.
- Missing signals are renormalized away rather than treated as zero; malformed scores are excluded.
- Strong classification requires multiple independent signals; output authority is `derived_candidate`, never duplicate probability/fact.
- `media_duplicate_candidates` performs bounded project-wide discovery (max 100 assets / 2,000 pairs) without silently launching expensive missing extractors.

### Retrieval/evidence work still open before autonomous synthesis consumes it by default
- Quantitative retrieval precision/recall fixtures and duplicate-ranking fixtures on representative projects.
- Raw `semantic_search_text` still annotates rather than pre-filters source-fingerprint staleness; `media_search_hybrid` is the preferred current-only path and now enforces both text and source identity.
- Camera-motion, shot-scale and composition evidence where it demonstrably improves editorial decisions.
- Runtime verification and quality/calibration fixtures for all learned providers.
- Privacy-safe person/face evidence only if a governed identity boundary and product need justify it.

## Phase 5 — editorial synthesis — FIRST PROPOSAL PIPELINE LANDED IN SOURCE

### Rough-cut proposal context — LANDED
- `rough_cut_context` builds a bounded canonical candidate universe from current transcript/subtitle documents only.
- Duplicate transcript representations at the same range/text are collapsed, preferring source-backed `transcript_segment` evidence over subtitle-track duplicates.
- Context is project-revision bound and SHA-256 identified.
- Candidate previews are bounded, but each candidate also carries `text_sha256` over the **full normalized transcript**, so changes beyond the preview cutoff invalidate the context.
- Candidates expose stable IDs and authoritative ranges/provenance from VibeCut; execution authority is explicitly `none`.

### Rough-cut proposal validation — LANDED
- `rough_cut_proposal_validate` accepts only context identity, objective, ordered candidate IDs and optional duration budget.
- Caller cannot submit raw source paths, frame ranges or edit operations.
- Unknown/duplicate IDs, stale revision/context, context tamper and duration-budget overflow fail closed.
- Exact ranges/provenance are resolved from the canonical context.
- Result authority remains `proposal`, with `executable=false` and `mutation_authority=none`.

### Objective relevance — LANDED
- `rough_cut_objective_rank` delegates only to current-only `media_search_hybrid` and filters results back into the exact rough-cut candidate universe.
- Hybrid kind/range/source-fingerprint provenance must match the canonical candidate.
- Parent/child cancellation and already-terminal child races are handled.
- On child completion the candidate context is rebuilt and re-hashed, so transcript/evidence changes are refused even if timeline revision did not move.
- Output is `derived_ranking` with `current_hybrid_relevance_not_probability`, never an edit or quality probability.

### Alternative comparison — LANDED
- `rough_cut_alternatives_compare` compares 2–5 candidate-ID alternatives only after a completed exact-context objective-ranking job.
- Every alternative is revalidated through the canonical proposal validator before scoring.
- Fixed disclosed rubric: objective relevance 0.60, retrieval coverage 0.15, chronology 0.10, overlap cleanliness 0.10, provenance coverage 0.05.
- Missing relevance is represented separately as retrieval coverage rather than silently becoming zero-valued evidence.
- Weights are code-defined and not caller-adjustable.
- `top_ranked_alternative_id` means top under the declared rubric only; output authority is `derived_comparison`, `executable=false`, `mutation_authority=none`.

### Next source sequence
1. Highlights/shorts candidate contract with explicit objective/rubric and source-range provenance.
2. B-roll opportunity detection, candidate retrieval and reviewable placement proposal.
3. Pacing, section/narrative and continuity analysis.
4. Quantitative editorial-quality fixtures for rough-cut/highlight alternatives.
5. **Only after proposal/evaluation quality is measurable:** translate an explicitly approved synthesis proposal into the existing governed `EditPlan`/native mutation path with normal authorization, verification and Undo. Do not create a parallel synthesis mutation path.

## Distribution and README lineage

The repository preserves three documentation layers: halthinks/VibeCut capability-expanded fork → original VibeCut → Kdenlive. `packaging/vibecut/build-deb.sh` produces the separate `vibecut-halthinks` distribution under `/opt/vibecut-halthinks`; large ML environments/models remain governed setup-time assets.

## Current engineering rules

- Do not guess private Kdenlive internals; prefer public native request/model paths and verified accumulated Undo/Redo.
- Retrieved/model-derived content has proposal/evidence authority only; consequential edits still pass the governed execution path.
- Deterministic measurements describe only what was measured.
- OCR/object extraction over sampled frames does not observe unsampled frames.
- Diarization clusters are not identities.
- AudioSet/DETR/X-CLIP outputs are model predictions, not facts.
- Temporal tracks, room-tone/subject/duplicate candidates, hybrid rankings and synthesis comparison scores are derived inference, not promoted observations or probabilities.
- A synthesis feature is not complete because a model can suggest it. Execution is complete only when its native mutation, verification and Undo story are real and quantitatively evaluated.
