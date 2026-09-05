# VibeCut — Current Roadmap Implementation Status

**Branch:** `agent/vibecut-architecture-slices`  
**Release authority:** do not merge to `vibecut` or make a release-ready claim until compile/tests/package and hands-on smoke gates pass.  
**Verification:** local `scripts/vibecut-verify.sh` is authoritative for the editor; `python3 runtime/verify.py` is authoritative for the extracted runtime boundary once run against the exact current tree. Source-landed tests/static audits do not replace either gate.  
**Distribution:** a halthinks-specific Debian package is produced only after successful verification. A proprietary runtime SKU is **not** release-qualified until `EXTRACT_AND_LICENSE.md` Section 10 passes.

This is the concise live-state ledger. The full dependency/product roadmap remains in `TODO.md`; the commercial-runtime split is governed by `EXTRACT_AND_LICENSE.md`.

## R0 — governed agent kernel — LANDED IN SOURCE

- Revision-bound `EditPlan`, deterministic plan runtime, checkpoints, stale-plan rejection and native Undo integration.
- Review / Auto / Turbo trust modes plus project `.vibecutpolicy.json`.
- Code-defined `confirmationRequired=true` is a non-waivable lower bound; project `auto_allow` cannot clear it.
- Shared cancellable `JobManager`, project rules/memory, provider registry/hooks, KWallet secret storage and VibeScript plan-only sandbox.
- Repository-local verification gate exists; merge/release authority remains withheld until it passes.
- Integrated checkpoint rollback now records the undo-stack origin and never blindly undoes a pre-existing command when a mutating-policy tool fails before pushing an edit.

## Phase 1 — native professional editing vocabulary — STRONG SOURCE BASELINE

- Native clip move/split/trim/ripple/delete, selection, groups, bulk operations and multi-sequence inspection.
- Reusable governed `timeline_range_remove` with lift/ripple semantics, locked-track refusal, live verification and one accumulated Undo transaction.
- `repeated_take_selection_execute` revalidates explicit keep choices and uses the same governed range-removal transaction path.
- Effects/groups/keyframes/stack copy, transitions/compositions, same-track mix baseline, titles, tracks/routing, bin/project resilience, relink/proxy/preflight and render/export baseline.
- **Open by design:** mixer gain/pan/solo and mix type/parameter editing only where Kdenlive exposes a stable safe backend seam; richer title shapes/images/templates/brand packs remain downstream work.

## Phase 2 — hard editor release/merge gate — ACTIVE BLOCKER

No merge to `vibecut` until all of the following are green on a real Kdenlive development host:

1. `bash scripts/vibecut-verify.sh` from a clean build tree.
2. CMake configure, full compile/link and every `vibecut*` test.
3. Live mutation smoke for `timeline_range_remove`, repeated-take execution, stale-plan refusal and exact Undo/Redo fidelity.
4. Failure-before-mutation regression proving rollback never undoes an unrelated previous command.
5. Runtime/setup/cancellation/evidence smoke for pyannote, Tesseract, R128, AST, DETR and X-CLIP.
6. Semantic setup/runtime smoke for MiniLM plus shared-vision SigLIP, including model acquisition, CPU/GPU paths, cancellation, stale-source behavior and bounded result handling.
7. Hybrid lexical+MiniLM and raw MiniLM current-only search smoke proving stale text/source/fingerprint embeddings are excluded before final ranking.
8. Pairwise MPEG-7, fused duplicate scoring and bounded project-wide duplicate-candidate smoke.
9. Rough-cut context/objective-ranking/alternative-comparison smoke proving revision/context hash refusal and zero mutation authority.
10. Highlight/short proposal smoke proving exact integer budgets, objective/context provenance, overlap rejection and zero mutation authority.
11. B-roll opportunity → SigLIP candidate retrieval → placement-proposal smoke, including cancellation, context-change refusal and proof that sampled visual frames are not silently promoted into invented source excerpts.
12. Pacing/narrative/continuity analysis smoke proving exact source/context freshness, semantic fallback behavior and zero normative/edit authority.
13. Editorial agreement + frozen-case + blinded-review smoke proving explicit-reference semantics, exact proposal/context binding, duplicate-reviewer refusal and zero automatic execution gate.
14. Retrieval/duplicate evaluation smoke proving deterministic top-k metrics, order-independent duplicate-pair identity and zero semantic/duplicate-truth claim.
15. GPL runtime-protocol adapter compile/link and process smoke: hello → inspect → propose → authorize → invoke → verify → complete/abort.
16. Out-of-process checkpoint smoke: consecutive sync mutations, failure rollback, async boundary, post-macro revision resync, runtime disconnect and user Undo/Redo.
17. Protocol-owned job containment: unrelated editor jobs never cross to the proprietary child.
18. Debian package build plus clean-host install/uninstall/coexistence smoke.
19. Hands-on editor plan → authorize → execute → verify/diff → Undo across major edit families.
20. Review/Auto/Turbo and policy-override smoke, including non-waivable hard confirmation.

Only after those gates pass may the integration branch merge to `vibecut`. An upstream PR remains optional.

## Commercial runtime extraction / licensing seam — SUBSTANTIAL SOURCE FOUNDATION; COMMERCIAL GATE CLOSED

`EXTRACT_AND_LICENSE.md` is the plan of record. The GPL editor and proprietary runtime are now separated in source by a versioned process protocol, but the runtime is **not yet a release-qualified commercial SKU**.

### Open public contract — SOURCE-LANDED
- `runtime/protocol.md` defines v1 authority, production topology and lifecycle.
- Apache-2.0 schemas cover EditPlan, tool policy, evidence, jobs, envelopes and type-specific messages.
- Live/effective tool schema+policy export comes from `VibeCutToolSurface::runtimeContractSnapshot()`; the runtime does not hand-invent the Kdenlive capability table.
- v1 NDJSON record bound is exactly 2 MiB on GPL and proprietary sides.
- Evidence confidence is exactly `-1` unknown or `[0,1]`; bounded frame queries do not treat unknown-range evidence as intersecting.

### GPL Kdenlive adapter — SOURCE-LANDED; HOST VERIFICATION OPEN
- Production topology is **Kdenlive/VibeCut GPL parent → proprietary runtime child** through `QProcess`; live Kdenlive state never moves into a standalone proprietary helper.
- Adapter stores the exact plan, issues opaque authorization IDs, resolves approved tool/input itself, and rejects post-approval substitution.
- `base_revision` is immutable provenance; `expected_revision` is the moving execution token.
- GPL-only invoke preflight validates authorization/revision/order/dependencies/policy **before** the transport opens a Kdenlive Undo checkpoint; native dispatch repeats all checks.
- Async `job_update` crosses the boundary only when its job ID belongs to the active protocol plan.
- Consecutive synchronous project mutations share one adapter-side Undo macro; current macro commits before async and on successful completion.
- Rollback restores the exact captured undo-stack index, so an empty failed checkpoint cannot undo the previous unrelated user command.
- Already committed checkpoints before async are not falsely claimed as rolled back.
- Post-macro moving revision is resynchronized from editor-authoritative state.
- C++ checkpoint/protocol/access regressions are source-registered; authoritative build/runtime execution remains open.

### Clean-room proprietary runtime — SOURCE-LANDED; EXACT-TREE GATE OPEN
- `runtime/src/halthinks_runtime/` implements contracts, policy, moving revision, jobs, evidence, protocol, provider client, session orchestration and two stdio topologies without editor imports.
- Production child stdio is synchronous/thread-free after a real shutdown-race defect was found during subprocess testing.
- Runtime supports read-only inspect-driven revision refresh before planning.
- Default remote provider transport requires HTTPS; cleartext HTTP is loopback-only and URL-embedded credentials/fragments are rejected.
- `runtime/verify.py` checks required modules/tests/schemas, proprietary SPDX markers, forbidden GPL/editor imports/markers, Python compilation and all standalone fake-adapter/process tests.
- Prior reconstructed tests drove real fixes, but **`python3 runtime/verify.py` has not yet been executed against an exact current branch checkout in this environment**.

### Commercial acceptance still open
- Exact-tree `python3 runtime/verify.py` pass.
- Full Kdenlive compile/link/test pass for the GPL adapter/checkpoint seam.
- Production child process smoke inside Kdenlive.
- Live Undo/Redo/checkpoint/disconnect parity.
- Clean package/install smoke.
- Final SPDX/license review and pre-sale counsel review.

## Phase 3 — persistent media evidence and deterministic analysis — LANDED IN SOURCE

- Bounded atomic `.vibecutmedia.json` ledger with source fingerprint + extractor-version freshness.
- Provenance-aware evidence list/summary/freshness and media-index integration.
- Deterministic source metadata, silence, loudness, EBU-R128, shot, black, freeze and blur extractors.
- Whisper transcript evidence, pairwise MPEG-7 similarity evidence and stale-only `media_analyze_refresh` orchestration.
- Provider-neutral extractor registry and one shared authoritative dispatch path: live source normalization → provider → `JobManager` → capability-specific admission contract → bounded persistence.
- GPL evidence parser/public schema/clean-room evidence store now share the exact confidence and field-bound contract.

## Phase 4 — rich media intelligence + retrieval — STRONG SOURCE FOUNDATION

### Golden mutation evaluation
- `VibeCutEvaluator::evaluateMutation` measures verified success plus canonical Undo/Redo fidelity.
- `vibecut_mutation_state_v1` is revision-independent canonical editable state.
- Source fixtures cover ripple removal, locked refusal, partial-failure rollback, stale-plan refusal, repeated-take overlap refusal and successful repeated-take execution as one Undo command.
- Failure-before-mutation source fixture now protects pre-existing undo history from empty-checkpoint rollback.
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
- `semantic_search_text` now pre-filters stored MiniLM records against current producer/model/anchor/range/source ID/source fingerprint/full-text SHA **before cosine ranking** and reloads store/index again when the async query completes.
- `media_search_hybrid` fuses the canonical lexical index with MiniLM ranking, excludes stale text/source semantic anchors, binds the result to project revision and labels its score as a derived ranking rather than probability.
- Hybrid parent/child cancellation and already-terminal child races are handled explicitly.

### Duplicate / near-duplicate understanding — SOURCE-LANDED
- Existing deterministic FFmpeg MPEG-7 pairwise comparison remains an independent signal.
- `media_duplicate_fusion` combines available current evidence from MPEG-7, SigLIP visual similarity, temporal alignment, MiniLM source-text similarity, transcript/OCR lexical overlap and duration similarity.
- Missing signals are renormalized away rather than treated as zero; malformed scores are excluded.
- Strong classification requires multiple independent signals; output authority is `derived_candidate`, never duplicate probability/fact.
- `media_duplicate_candidates` performs bounded project-wide discovery (max 100 assets / 2,000 pairs) without silently launching expensive missing extractors.

### Retrieval and duplicate evaluation — SOURCE FOUNDATION LANDED
- `retrieval_ranking_evaluate` measures explicit-reference precision@k, recall@k, AP@k, binary nDCG@k, reciprocal rank and full-list recall.
- Evaluation supports the full legal 2,000-pair project duplicate scan bound.
- `tests/dataset/vibecut/retrieval_ranking_cases.json` provides synthetic deterministic metric-regression fixtures.
- `duplicate_ranking_evaluate` canonicalizes each unordered asset pair to one SHA-256 identity, so `(A,B)` and `(B,A)` cannot count twice.
- Duplicate evaluation reuses retrieval ranking metrics and separately reports fusion classification/evidence-coverage diagnostics; it explicitly makes no duplicate-truth claim.
- `tests/dataset/vibecut/duplicate_ranking_cases.json` provides synthetic deterministic duplicate-ranking metric fixtures.
- **Still open:** representative-project relevance/duplicate references and measured performance on real workloads.

### Retrieval/evidence work still open before autonomous synthesis consumes it by default
- Quantitative retrieval precision/recall and duplicate-ranking evaluation on representative projects using the now-landed evaluator contracts.
- Camera-motion, shot-scale and composition evidence where it demonstrably improves editorial decisions.
- Runtime verification and quality/calibration fixtures for all learned providers.
- Privacy-safe person/face evidence only if a governed identity boundary and product need justify it.

## Phase 5 — editorial synthesis — PROPOSAL + EVALUATION PIPELINE EXPANDING IN SOURCE

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

### Highlights / shorts — PROPOSAL FOUNDATION LANDED
- `highlight_proposal_build` builds deterministic `highlight_reel`, `short` or `quote` proposals from the exact completed rough-cut objective ranking.
- Selection is bounded by exact integer segment/frame budgets, minimum relevance, overlap rejection and optional source-order preservation.
- Direct invocation revalidates integer/boolean/numeric types instead of relying only on the JSON schema.
- Ranked candidate provenance must exactly match the canonical rough-cut context and every final selection is revalidated through `validateVibeCutRoughCutProposal`.
- Output discloses skipped overlap/budget/provenance/relevance candidates, remains `proposal`, `executable=false`, `mutation_authority=none` and makes no quality-probability claim.

### B-roll opportunity / retrieval / placement — PROPOSAL FOUNDATION LANDED
- `broll_opportunity_validate` binds a model/user-proposed visual need to exactly one canonical A-roll candidate, bounded visual query and fixed editorial-purpose vocabulary.
- A-roll target frame geometry/provenance is resolved from the canonical rough-cut context; raw target geometry is never caller-supplied.
- `broll_candidate_search` asynchronously delegates to current SigLIP text→visual retrieval, propagates cancellation and revalidates project revision plus the full transcript/evidence context at completion.
- `broll_placement_plan_validate` may select only a visual anchor returned exactly once by that completed current search result.
- The selected visual frame remains a retrieval reference/center only. VibeCut explicitly leaves source excerpt in/out **unresolved**; a later governed execution translator must resolve and verify a real excerpt of the required duration.
- B-roll outputs remain proposal/derived-ranking authority with `mutation_authority=none`; there is still no synthesis mutation path.

### Pacing analysis — DESCRIPTIVE FOUNDATION LANDED
- `media_source_pacing` consumes only current exact-fingerprint `shot_segment`, `silence`, `transcript_segment` and `speaker_segment` evidence.
- Reports shot/transcript duration distributions, positive transcript gaps, merged silence coverage and raw speaker-cluster coverage/dominance.
- `rough_cut_pacing_analyze` revalidates candidate IDs through the canonical rough-cut proposal contract, then reports duration/rhythm variability, chronology, overlap warnings and bounded transcript-density measurements.
- Pacing applies **no good/bad thresholds** and remains `derived_analysis`, `mutation_authority=none`.

### Narrative analysis — RELATIVE FOUNDATION LANDED
- `rough_cut_narrative_analyze` validates the exact current candidate sequence before analysis.
- Current MiniLM vectors are used only when anchor ID, source ID/fingerprint and full-text SHA match the canonical candidate; otherwise lexical Jaccard is used as an explicit fallback.
- Adjacent similarities are reported as continuity measurements; the lowest relative adjacency similarities become possible section-boundary candidates.
- Highest non-adjacent similarities become possible repetition candidates.
- No absolute story-quality threshold is applied; output is relative `derived_analysis`, not narrative fact and not edit authority.

### Structural continuity warnings — SOURCE FOUNDATION LANDED
- `rough_cut_continuity_analyze` revalidates the exact current candidate sequence through the canonical rough-cut proposal contract.
- Reports source chronology reversals, overlapping authoritative ranges, repeated full normalized transcript hashes and source/provenance changes as explicit review candidates.
- Frame chronology/overlap/gap comparisons run only when adjacent candidates share the same source/fingerprint coordinate domain; cross-source edges report provenance change instead of comparing unrelated frame numbers.
- Positive source gaps are ranked only relative to one another; no gap threshold is applied.
- Findings are `derived_analysis`, `quality_claim=false`, `executable=false`, `mutation_authority=none`.

### Editorial agreement evaluation — SOURCE FOUNDATION LANDED
- `editorial_selection_evaluate` compares an actual candidate-ID sequence against an **explicit** human/golden reference using precision, recall, F1, exact set/order match and pairwise order agreement.
- `tests/dataset/vibecut/editorial_selection_cases.json` establishes the first reusable deterministic agreement corpus.
- Agreement metrics explicitly declare `not_editorial_quality`; they do not manufacture a reference answer and do not grant execution authority.

### Frozen editorial evaluation cases — SOURCE FOUNDATION LANDED
- `editorial_case_validate` binds one frozen context to 2–5 exact proposal IDs under opaque labels and emits deterministic `case_sha256` identity.
- Optional structural references must declare explicit `golden` or `human_consensus` provenance.
- `VIBECUT_EDITORIAL_EVALUATION.md` defines the durable blinded-review/evaluation protocol and explicitly forbids metrics or reviews from self-elevating into execution authority.

### Blinded human review harness — SOURCE FOUNDATION LANDED
- Fixed rubric `VibeCutEditorialReview-v1`: objective relevance, narrative coherence, pacing fit, source fidelity and overall preference, each 1–5.
- v1 requires `blind=true`; unknown rubric criteria and out-of-range/fractional scores fail closed.
- Every review is cryptographically bound to exact `context_sha256` + `proposal_id` and one case/candidate/task.
- Aggregation requires unique reviewers and exact matching case/candidate/task/context/proposal, then reports mean/stddev/min/max per rubric criterion.
- Review aggregates explicitly state `quality_ground_truth=false` and `automatic_execution_gate=false`.

### Synthesis execution gate — CLOSED
The architecture needed to measure proposal agreement and collect blinded human reviews now exists, but **synthesis execution is not authorized yet**. The gate remains closed because:
- current agreement/retrieval/duplicate fixtures are structural or synthetic metric-regression fixtures, not representative editorial benchmarks;
- no representative blinded human-review corpus has been collected;
- no acceptance thresholds have been justified from measured workloads;
- learned retrieval/evidence providers still require runtime/calibration validation;
- the authoritative Kdenlive compile/test/smoke gate has not run.

### Next source sequence
1. Run the exact-tree commercial runtime verifier and the real Kdenlive editor/protocol gate; repair every failure before commercial or merge claims.
2. Build representative rough-cut/highlight/B-roll evaluation cases and collect blinded reviews bound to exact proposal IDs.
3. Build representative retrieval and duplicate-reference datasets, then score them with `retrieval_ranking_evaluate` / `duplicate_ranking_evaluate`.
4. Compare proposal versions/providers using agreement metrics plus human-review distributions; do **not** collapse subjective review into an automatic pass/fail score.
5. Richer highlight/B-roll ranking may incorporate measured audiovisual quality only where calibration is demonstrated.
6. **Only after evaluation evidence justifies it:** design an explicit approved-proposal → existing governed `EditPlan` translation with normal authorization, verification and Undo. Do not create a parallel synthesis mutation path.

## Distribution and README lineage

The repository preserves three documentation layers: halthinks/VibeCut capability-expanded fork → original VibeCut → Kdenlive. `packaging/vibecut/build-deb.sh` produces the separate `vibecut-halthinks` distribution under `/opt/vibecut-halthinks`; large ML environments/models remain governed setup-time assets. The proprietary runtime, if/when release-qualified, is a separate SKU/process and does not relicense the editor.

## Current engineering rules

- Do not guess private Kdenlive internals; prefer public native request/model paths and verified accumulated Undo/Redo.
- Retrieved/model-derived content has proposal/evidence authority only; consequential edits still pass the governed execution path.
- Deterministic measurements describe only what was measured.
- OCR/object extraction over sampled frames does not observe unsampled frames.
- Diarization clusters are not identities.
- AudioSet/DETR/X-CLIP outputs are model predictions, not facts.
- Temporal tracks, room-tone/subject/duplicate candidates, hybrid rankings and synthesis comparison scores are derived inference, not promoted observations or probabilities.
- A sampled B-roll retrieval frame is not a source excerpt; excerpt resolution remains a separate governed step.
- Agreement with a reference is not intrinsic editorial quality; subjective human review is not ground truth.
- Human-review records must bind to the exact proposal/context that was actually reviewed.
- Duplicate-pair benchmark identity is order-independent; reversed pair ordering cannot create a second judgment.
- Runtime proposal authority never controls Kdenlive tool/input resolution, checkpoint/Undo, live revision or authorization state.
- Evidence persistence is not project truth.
- A synthesis feature is not complete because a model can suggest it. Execution is complete only when its native mutation, verification and Undo story are real and quantitatively evaluated.
