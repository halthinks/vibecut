# VibeCut — Full Product Roadmap

Living implementation roadmap for the governed agentic video editor. `VIBECUT_ARCHITECTURE.md` is the architecture contract, `DESIGN_SPECS.md` contains product/behavior rules, and `CLAUDE.md` remains the operational handoff.

This roadmap is ordered by dependency and product value. A later phase may be researched before an earlier one is complete, but **merge/release authority follows the gates below**. Repository-local verification remains authoritative; branch CI may supplement it during hardening but does not replace the local gate or hands-on smoke.

## North-star product

VibeCut should let a user describe editorial intent at the level a human editor thinks, while preserving the things professional editors cannot give up:

- exact project/timeline state;
- predictable authority and confirmation;
- reversible native edits;
- stale-plan protection;
- inspectable evidence;
- deterministic execution after model reasoning;
- long-job progress/cancellation;
- truthful distinction between observation, inference, proposal, execution and verified result;
- provider independence;
- local/project-specific memory and rules;
- extensibility without rebuilding the agent core.

The end-state interaction is not “LLM calls random Kdenlive functions.” It is:

`intent → inspect → understand → propose EditPlan → authorize → execute native Kdenlive operations → verify → expose diff/evidence → continue`.

---

# PHASE 0 — Governed agent kernel — LANDED

## Goal
Turn the original chat-driven prototype into a state-aware execution system that can safely operate a professional NLE.

## Landed
- [x] Revision-bound `EditPlan` contract.
- [x] Plan → authorize → execute-with-checkpoints runtime.
- [x] Review / Auto / Turbo trust modes.
- [x] Per-tool `.vibecutpolicy.json` overrides: `deny`, `always_confirm`, `auto_allow`.
- [x] Irreversible work cannot bypass confirmation.
- [x] Monotonic project revision / stale-plan protection.
- [x] Kdenlive undo-stack checkpoints for synchronous project mutations.
- [x] Rollback on failed synchronous checkpoints.
- [x] Project before/after snapshots and diffs.
- [x] Distinction between project mutations and external side effects.
- [x] Shared asynchronous `JobManager` with stable IDs, progress, cancellation request and terminal result.
- [x] Governed `job_cancel`.
- [x] Bounded conversation compaction preserving complete tool exchanges.
- [x] `.vibecutrules` project-local instructions.
- [x] `.vibecutmemory.json` durable bounded project memory with provenance and fail-closed parsing.
- [x] Lifecycle/context hooks for model/tool/plan/job/trust/error events.
- [x] Provider registry and provider-owned request/stream normalization.
- [x] Optional KWallet secret backend plus dock credential editor/hot reload.
- [x] `vibescript_plan` bounded JavaScript sandbox that may only produce governed plans.
- [x] Local verification script: `scripts/vibecut-verify.sh`.
- [x] Code-defined hard confirmation is a non-waivable lower bound; project `auto_allow` cannot weaken it.

## User outcome
The AI is no longer trusted just because it generated text. Plans are state-bound, governed, undo-aware and verifiable.

---

# PHASE 1 — Native professional editing vocabulary — MOSTLY LANDED

## Goal
Give the governed runtime enough authoritative Kdenlive-native tools to perform real editing rather than special-case demos.

## Timeline editing — LANDED IN SOURCE
- [x] `clip_move`.
- [x] `clip_split`.
- [x] `clip_trim`.
- [x] `clip_ripple_trim`.
- [x] `clip_delete`.
- [x] Stable item selection inspection/set/clear.
- [x] Native group/ungroup.
- [x] Reusable governed `timeline_range_remove` with explicit lift/ripple semantics, native accumulated Undo/redo, locked-track fail-closed behavior and live postcondition verification.
- [x] Repeated-take selection execution through the same governed range-removal transaction path; explicit human keep choice, right-to-left range application, overlap rejection, rollback on failure and one atomic Undo step.
- [ ] Phase 2 compile/runtime verification and golden-fixture proof for `timeline_range_remove` and repeated-take execution.

## Guides and subtitles — LANDED
- [x] Guide/range-guide add/read/remove.
- [x] Subtitle search/read.
- [x] Scope-safe subtitle generation.
- [x] Non-blocking audio export → Whisper → subtitle import.
- [x] Stale-result protection before subtitle import.
- [x] Subtitle edit/delete by stable ID.

## Effects — STRONG BASELINE LANDED IN SOURCE
- [x] Enumerate installed effects instead of inventing IDs.
- [x] Add individual installed effect.
- [x] Inspect effect stack with row, stable effect ID, parameters and Kdenlive XML.
- [x] Edit existing effect parameters with undo/redo and verification.
- [x] Remove effect with duplicate-row-safe identity.
- [x] Effect groups, keyframes and stack-copy paths are present in the current source baseline.
- [ ] Expand quantitative/golden verification across grouped/keyframed/stack-copy mutations.

## Transitions/compositions — STRONG BASELINE LANDED IN SOURCE
- [x] Discover installed transitions/compositions.
- [x] Add transition/composition.
- [x] Move transition/composition.
- [x] Resize transition/composition.
- [x] Remove transition/composition.
- [x] Transition parameter inspection/editing and composition A-track control are present in the current source baseline.
- [ ] Expand quantitative/golden verification across transition parameter and topology cases.

## Same-track mixes — BASELINE LANDED
- [x] `mix_inspect`.
- [x] Conservative previous-neighbor `mix_add_previous`.
- [x] `mix_resize`.
- [x] `mix_remove`.
- [ ] Mix type switching **only where Kdenlive exposes a stable, non-widget backend seam**.
- [ ] Mix parameter editing **only where Kdenlive exposes a stable, non-widget backend seam**.
- [ ] Explicit previous/next-neighbor ownership operations where native invariants can be proven.
- [ ] Mix alignment/cut controls where a safe Kdenlive backend seam exists.

## Titles — SAFE BASELINE LANDED
- [x] Create a real Kdenlive title asset and insert it in timeline.
- [x] Safely update VibeCut-created simple titles.
- [x] Embedded title inspection and indexed text-item editing are present in the current source baseline.
- [ ] Richer title shapes.
- [ ] Title image-element editing.
- [ ] Reusable title templates/styles and lower-third/title-card primitives.
- [ ] Brand packs with explicit asset provenance and reversible application.

## Tracks / mixer — BASELINE LANDED
- [x] List tracks with stable IDs/type/name/order/state/counts.
- [x] Create track.
- [x] Rename track.
- [x] Move track.
- [x] Lock/unlock track.
- [x] Enable/disable track: audio mute / video hide through native UI path.
- [x] Major-risk track delete with affected counts.
- [x] Insertion-target routing status.
- [x] Assign current source audio stream to a target track only when Kdenlive reports it assignable.
- [x] Set/clear video insertion target.
- [ ] Mixer gain editing **only where Kdenlive exposes a safe backend seam**.
- [ ] Mixer pan/balance editing **only where Kdenlive exposes a safe backend seam**.
- [ ] Mixer solo editing **only where Kdenlive exposes a safe backend seam**.
- [ ] More explicit routing state for multi-stream sources and future inserts.

## Bin/media — STRONG SOURCE BASELINE LANDED
- [x] Bin inventory with IDs, paths, type, duration, A/V capability and instance count.
- [x] Local-file import with native undo.
- [x] Insert existing bin asset into timeline.
- [x] Replace source behind an existing file-backed bin asset as undoable MajorEdit.
- [x] Reject generator/title/non-file-backed assets from file replacement.
- [x] Folder inventory/create/move/rename/empty-only delete baseline.
- [x] Missing-media inspection and single/batch relink baseline.
- [x] Relink candidate discovery and proxy lifecycle baseline.
- [x] Project preflight baseline.
- [ ] Golden project-resilience fixtures for relink/proxy/preflight edge cases.

## Render/export — STRONG BASELINE LANDED IN SOURCE
- [x] Discover installed render presets.
- [x] Native `RenderRequest` / `kdenlive_render` execution.
- [x] Shared JobManager progress/cancellation.
- [x] Final output existence/non-empty verification.
- [x] Safer overwrite: do not remove existing approved output until job preparation succeeds.
- [x] Deterministic destination-aware recommendation baseline.
- [x] Named export-policy baseline for common delivery classes.
- [ ] Golden render/preflight fixtures and clean-host package smoke.

---

# PHASE 2 — HARD GATE: compile, tests, smoke and repair — NEXT MERGE/RELEASE BLOCKER

## Goal
Convert “source-implemented” into “known-good application.” No merge to `vibecut`, no upstream PR, and no release claim before this gate.

## Required repository-local gate
- [ ] Run `bash scripts/vibecut-verify.sh` on a host with Kdenlive build dependencies.
- [ ] Fix every compiler error.
- [ ] Fix every linker error.
- [ ] Fix every `vibecut*` test failure.
- [ ] Re-run from a clean build directory.

## Hands-on smoke matrix
- [ ] Launch app and VibeCut dock.
- [ ] Enter/reload model credential through KWallet path.
- [ ] Inspect project and produce a plan.
- [ ] Approve plan and verify edit diff.
- [ ] Undo/redo each native edit family.
- [ ] Clip move/split/trim/ripple/delete.
- [ ] `timeline_range_remove`: lift and ripple, including locked-track refusal and postcondition verification.
- [ ] Repeated-take candidate → review → explicit selection → execute → one-step Undo/redo.
- [ ] Effects inspect/add/edit/remove.
- [ ] Guides.
- [ ] Subtitle search/generate/edit/delete.
- [ ] Titles create/update.
- [ ] Transitions and same-track mixes.
- [ ] Bin import/insert/replace/folders/move/relink/proxy.
- [ ] Track create/rename/move/lock/mute-hide/delete/routing.
- [ ] Render/start/cancel/verify.
- [ ] Whisper/start/cancel/final import.
- [ ] Local pyannote diarization + speaker naming smoke.
- [ ] Local Tesseract OCR + temporal OCR smoke.
- [ ] R128/room-tone/AST audio-event smoke.
- [ ] DETR object + X-CLIP action inference smoke.
- [ ] MiniLM text semantic refresh/search and SigLIP visual refresh/cross-modal search smoke.
- [ ] Hybrid lexical+MiniLM current-only search smoke, including stale text/source-fingerprint refusal.
- [ ] Duplicate-fusion and bounded project-wide duplicate-candidate smoke with missing-evidence disclosure.
- [ ] Rough-cut context/objective-ranking/alternative-comparison smoke proving proposal-only authority and stale-context refusal.
- [ ] Highlight/short proposal smoke proving bounded selection and zero mutation authority.
- [ ] B-roll opportunity/retrieval/placement proposal smoke proving sampled visual anchors never become invented source excerpts.
- [ ] Pacing/narrative descriptive-analysis smoke with semantic fallback and no normative execution thresholds.
- [ ] Editorial agreement/evaluation-case/blinded-review smoke with exact context/proposal binding.

## Governance smoke matrix
- [ ] Review mode asks where required.
- [ ] Auto mode runs safe reversible work while respecting major/external gates.
- [ ] Turbo still honors irreversible confirmation.
- [ ] `.vibecutpolicy.json` deny hides tool from schemas and rejects direct invocation.
- [ ] `always_confirm` overrides mode.
- [ ] `auto_allow` cannot bypass code-defined hard confirmation.
- [ ] Stale plan rejects after user/project mutation.
- [ ] External-only job does not open a project undo macro.
- [ ] Learned evidence cannot self-promote from model prediction/representation into observation or identity.
- [ ] Synthesis rankings/comparisons remain proposal/derived authority and cannot mutate the timeline.
- [ ] Human-review aggregates remain subjective evidence and cannot become an automatic execution gate.
- [ ] `timeline_range_remove` and `repeated_take_selection_execute` remain `MajorEdit`, reversible and project-mutating in the policy surface.

## Acceptance gate
**Only after the entire Phase 2 gate passes:** merge the integration branch to `vibecut`, preserve/update the README lineage and capability section, update operational handoff docs, and decide whether to create an upstream PR.

---

# PHASE 3 — Source health and project resilience — SOURCE BASELINE LANDED; HARDEN NEXT

## Goal
Make VibeCut dependable on real-world messy editing projects.

- [x] Source inspection / missing-media inventory baseline.
- [x] Governed single/batch relink and discovery baseline.
- [x] Proxy lifecycle baseline.
- [x] Project preflight baseline.
- [ ] Bin/project normalization fixtures before long operations.
- [ ] Better checkpoint evidence for source-replacement and relink operations.
- [ ] Adversarial path/type/cycle/dependency fixtures.

### User outcome
A user can open a broken project and ask: “What is missing, where was it expected, and fix everything you can from this folder.”

---

# PHASE 4 — Rich media intelligence — MAJOR SOURCE FOUNDATION LANDED; SEMANTIC RETRIEVAL STRONG SOURCE BASELINE

## Goal
Move from understanding timeline structure/transcripts to understanding audiovisual content. These evidence contracts land before higher-level autonomous editorial synthesis so later features consume one shared, timestamp/range-addressable truth layer.

## Common evidence contract
- [x] `VibeCutMediaIndex` common search/document contract.
- [x] Persistent `.vibecutmedia.json` evidence ledger baseline.
- [x] Source fingerprint + extractor-version freshness baseline.
- [x] Provider-neutral ML extractor registry / constrained evidence sink baseline.
- [x] Capability-specific admission contracts for diarization, OCR, AudioSet events, DETR objects and X-CLIP actions.
- [x] Explicit authority split between observations, model predictions, model representations and derived candidates/summaries.
- [x] Separate bounded `.vibecutembeddings.json` semantic-vector sidecar with exact anchor/source/model/revision/producer provenance and unit-vector validation.
- [x] Atomic producer/model refresh semantics so current refreshes remove superseded source fingerprints rather than accumulating stale vectors.
- [ ] Incremental learned-provider cache accounting and selective refresh across all model-backed extractors.
- [ ] Quantitative evidence-quality/calibration fixtures across OCR, audio, vision and semantic models.

## Audio/speech extractors — SOURCE FOUNDATION LANDED
- [x] Speaker diarization with exact source-frame ranges through provider-neutral `speaker_segment` evidence.
- [x] User-governed speaker naming/identity association in a separate fail-closed sidecar; diarizers cannot assert human identity.
- [x] R128 windowed loudness evidence and derived room-tone candidates that exclude persisted silence/dead-air ranges.
- [x] Local AST AudioSet ranked `audio_event_prediction` evidence with bounded windows/model provenance and derived temporal event summaries.
- [x] Existing silence/loudness evidence is reused rather than rebuilt in parallel.
- [ ] Real-media CPU/GPU/package/cancellation/calibration smoke for learned audio paths.
- [ ] Richer acoustic measurements/event characterization where evidence shows material editorial value.

## Visual extractors — SOURCE FOUNDATION LANDED
- [x] Tesseract OCR/on-screen text with exact sampled-frame geometry/confidence/engine provenance.
- [x] Temporal OCR consolidation that reports observed frames separately from unobserved inferred gaps.
- [x] DETR COCO sampled-frame object predictions with bounded boxes and model/revision provenance.
- [x] Deterministic geometry/provenance-aware object continuity tracks.
- [x] Transparent editorial subject-candidate ranking from persistence/confidence/screen area/center proximity; no identity claim.
- [x] X-CLIP action predictions over versioned `VibeCutActionSet-v1`, exact eight-frame support, fixed action-set hash and explicit fixed-set softmax semantics.
- [x] Temporal action summaries preserve all supporting prediction windows/observed frames and remain derived summaries.
- [ ] Face/person identity evidence remains intentionally unimplemented until privacy, governance and product need justify it.
- [ ] Camera-motion/shot-scale/composition descriptors where they materially improve editing decisions.
- [ ] Real-media package/CPU/GPU/cancellation and quantitative vision-accuracy smoke.

## Semantic retrieval — STRONG SOURCE BASELINE
- [x] MiniLM transcript/OCR text embeddings in a pinned 384-D model space with revision-bound atomic refresh.
- [x] MiniLM asynchronous text semantic search with exact model-space cosine semantics and canonical media-document anchors.
- [x] SigLIP sampled-frame visual embeddings in a pinned shared 768-D image/text space.
- [x] SigLIP text→visual cross-modal semantic search over current source-fingerprint visual anchors.
- [x] Semantic and cross-modal tools are exposed through the normal `VibeCutToolSurface`, not merely compiled as orphan code.
- [x] `media_search_hybrid` combines canonical lexical ranking with MiniLM semantic ranking and excludes stale text **and source ID/fingerprint** semantic anchors before final fusion.
- [x] Hybrid parent/child cancellation and already-terminal child races are handled explicitly.
- [x] Stronger pairwise duplicate/near-duplicate candidate fusion combining current MPEG-7 evidence, SigLIP visual similarity, temporal ordering, MiniLM source-text similarity, transcript/OCR lexical overlap and duration similarity.
- [x] Duplicate-fusion scores disclose available evidence/coverage/missing work and are explicitly derived similarity candidates, not probabilities or duplicate facts.
- [x] `media_duplicate_candidates` performs bounded project-wide discovery over the pairwise fusion primitive without auto-launching missing expensive extractors.
- [ ] Raw MiniLM `semantic_search_text` still annotates rather than pre-filters source-fingerprint staleness; the current-only hybrid surface is the preferred downstream path.
- [ ] Media-search precision/recall and duplicate-ranking fixtures before more autonomous synthesis consumes semantic retrieval automatically.

### User outcome
Requests like “find the cleanest answer from Sarah,” “find every shot of the engine housing,” or “show every on-screen mention of the launch date” now have source implementations for the required evidence/search foundations. Release-quality claims remain gated on Phase 2 runtime verification and quantitative retrieval evaluation.

---

# PHASE 5 — Editorial reasoning and autonomous edit synthesis — PROPOSAL + EVALUATION FOUNDATION LANDED

## Goal
Stop making the user specify frame-level commands and begin solving editorial problems while still producing reviewable deterministic plans.

## Repeated-take execution foundation — LANDED IN SOURCE
- [x] Transcript/subtitle repeated-take candidate grouping.
- [x] Evidence-backed repeated-take review and take-quality context.
- [x] Explicit human-choice selection planning.
- [x] Actual repeated-take selection execution, not merely candidate/review/selection planning.
- [x] Execution resolves to reusable governed `timeline_range_remove`; explicit lift/ripple semantics, overlap rejection, right-to-left application, rollback and one Undo step.
- [x] Source-level golden live/headless mutation fixtures cover success, refusal, stale plan, rollback and exact Undo/Redo fidelity.
- [ ] Phase 2 authoritative compile/runtime verification before release-quality claim.

## Rough-cut synthesis proposal foundation — LANDED IN SOURCE
- [x] `rough_cut_context`: bounded revision-bound candidate universe from current transcript/subtitle documents only.
- [x] Source-backed transcript evidence is preferred over duplicate subtitle representations at the same range/text.
- [x] Context SHA-256 binds candidate IDs, exact ranges/provenance and the **full normalized transcript text hash** even when returned text previews are truncated.
- [x] `rough_cut_proposal_validate`: candidate-ID-only ordered proposals with exact-context identity, duration budget and no caller-supplied raw ranges/paths/edit operations.
- [x] Proposal validation resolves authoritative ranges from context and returns `executable=false`, `mutation_authority=none`.
- [x] `rough_cut_objective_rank`: asynchronous exact-context objective relevance through current-only hybrid retrieval.
- [x] Objective ranking re-hashes current candidate context after the child job completes, refusing transcript/evidence changes even when timeline revision did not move.
- [x] `rough_cut_alternatives_compare`: 2–5 validated alternatives under a fixed disclosed rubric: objective relevance 0.60, retrieval coverage 0.15, chronology 0.10, overlap cleanliness 0.10, provenance coverage 0.05.
- [x] Alternative comparison keeps missing relevance separate as coverage loss, disallows caller-defined weights and remains derived comparison authority only.

## Highlights / shorts — PROPOSAL FOUNDATION LANDED
- [x] `highlight_proposal_build` supports deterministic `highlight_reel`, `short` and `quote` proposals from an exact completed objective ranking.
- [x] Selection is bounded by exact integer segment/frame budgets, relevance threshold, overlap rejection and optional source-order preservation.
- [x] Ranked candidate provenance must match the canonical context and final selections re-run canonical rough-cut proposal validation.
- [x] Highlight output remains proposal-only with `executable=false`, `mutation_authority=none`.

## B-roll — PROPOSAL FOUNDATION LANDED
- [x] `broll_opportunity_validate` binds a bounded visual need to one exact canonical A-roll candidate.
- [x] `broll_candidate_search` delegates to current SigLIP retrieval with revision/context revalidation and cancellation propagation.
- [x] `broll_placement_plan_validate` may select only a visual anchor returned by that exact completed search.
- [x] Retrieved visual frames remain reference/center samples; source excerpt in/out stays unresolved for a later governed resolver.
- [ ] Actual guide placement or timeline mutation remains downstream of evaluation and governed execution translation.

## Pacing / narrative analysis — DESCRIPTIVE FOUNDATION LANDED
- [x] `media_source_pacing`: exact-fingerprint shot/silence/transcript/speaker descriptive measurements.
- [x] `rough_cut_pacing_analyze`: duration/rhythm variability, chronology, overlap and bounded text-density measurements after canonical proposal validation.
- [x] `rough_cut_narrative_analyze`: exact-context adjacency continuity with current MiniLM only when anchor/source/full-text identity matches; lexical fallback otherwise.
- [x] Relative low-continuity adjacency edges are section-boundary candidates; high non-adjacent similarity becomes repetition candidates.
- [x] Pacing/narrative outputs apply no absolute good/bad threshold and remain derived analysis only.
- [ ] Basic continuity warnings beyond relative narrative/repetition analysis.

## Editorial evaluation foundation — LANDED IN SOURCE
- [x] `editorial_selection_evaluate`: explicit-reference precision/recall/F1, exact set/order agreement and common-pair order agreement.
- [x] Deterministic starter corpus `tests/dataset/vibecut/editorial_selection_cases.json` validates metric semantics; it is not a representative editorial benchmark.
- [x] `VibeCutEditorialReview-v1` fixed blinded 1–5 rubric: objective relevance, narrative coherence, pacing fit, source fidelity, overall preference.
- [x] Every blinded review binds exact `context_sha256` + `proposal_id`; aggregates require unique reviewers and exact case/candidate/task/context/proposal identity.
- [x] `editorial_case_validate`: frozen 2–5 candidate evaluation manifests with opaque display labels, exact proposal hashes, deterministic `case_sha256` and optional explicitly sourced `golden`/`human_consensus` structural reference.
- [x] Durable evaluation protocol documented in `VIBECUT_EDITORIAL_EVALUATION.md`.
- [x] Agreement/review/case outputs explicitly deny quality-ground-truth and automatic-execution authority.
- [ ] Build representative rough-cut/highlight/B-roll evaluation cases from real projects.
- [ ] Collect representative blinded human reviews bound to exact proposal/context hashes.
- [ ] Retrieval precision/recall and duplicate-ranking fixtures on representative projects.
- [ ] Provider/version comparison on the same frozen cases/evidence budget.

## Synthesis execution gate — CLOSED
- [ ] Do **not** translate rough-cut/highlight/B-roll proposals into mutations yet.
- [ ] Before opening the gate: representative evaluation data, documented disagreement/failure modes, learned-provider runtime/calibration validation and authoritative Kdenlive compile/test/smoke must exist.
- [ ] If opened later, explicitly approved synthesis must translate into the existing governed revision-bound `EditPlan`/native tool runtime with normal policy, verification and Undo/Redo evaluation. Never add a parallel privileged synthesis executor.

## Remaining editorial synthesis
- [ ] Auto finishing pass: subtitles, richer titles/templates, supported transitions/mixes, audio cleanup and render recommendation.
- [ ] Reference-style matching based on extracted visual/audio style evidence.

## Acceptance principle
The model may propose high-level editorial intent, but final execution still resolves to explicit governed native tool operations with preconditions and evidence. Proposal/ranking/comparison/review authority can never self-elevate into execution authority.

---

# PHASE 6 — Golden fixtures and quantitative evaluation — ACTIVE CROSS-CUTTING RELEASE DISCIPLINE

## Goal
Make VibeCut performance measurable rather than anecdotal. Start this before expanding the mutation surface further and keep it active across every later phase.

- [x] Evaluation harness seam.
- [x] Quantitative mutation evaluator with verified-success, exact Undo-fidelity and Redo-fidelity scores.
- [x] Revision-independent `vibecut_mutation_state_v1` canonical live-state capture.
- [x] Live/headless ripple range-removal success + Undo/Redo fixture.
- [x] Locked-track refusal unchanged-state fixture.
- [x] Real-mutation-then-failure checkpoint rollback fixture.
- [x] Stale-plan refusal after real intervening edit.
- [x] Repeated-take overlap refusal and successful one-command Undo/Redo fixture.
- [x] Pure source fixtures for rough-cut context integrity, objective relevance, alternative comparison, highlights, B-roll, pacing and narrative authority/semantics.
- [x] Structural editorial selection/order agreement evaluator + reusable deterministic golden metric corpus.
- [x] Blinded human-review validation/aggregation harness with exact proposal/context binding and disagreement statistics.
- [x] Frozen editorial evaluation-case manifest validator with deterministic case identity and explicit reference provenance.
- [ ] Representative editorial benchmark/review corpus; current deterministic cases validate plumbing/metrics only.
- [ ] Extend mutation fixtures across effects, transitions, titles, relink/proxy, render and future synthesis execution families.
- [ ] Plan correctness / tool hallucination rate regression suites.
- [ ] Long-job cancellation correctness across learned model/process trees.
- [ ] OCR/diarization/audio/vision accuracy/calibration fixtures.
- [ ] Media-search precision/recall and duplicate-fusion ranking fixtures.
- [ ] Provider comparison using the same tasks/evidence budget.
- [ ] Token/latency/cost accounting by verified editing outcome.
- [ ] Regression scorecard required for releases.

---

# PHASE 7 — Provider and task-routing ecosystem

## Goal
Let users choose intelligence providers without changing the editor architecture, after task/evidence contracts are stable enough that provider breadth does not drive architecture.

- [x] Provider registry seam.
- [x] Built-in Anthropic path.
- [x] Provider-neutral learned-extractor capability registry and shared governed evidence sink.
- [ ] Ollama/local planner-model adapter.
- [ ] OpenAI adapter if desired.
- [ ] Additional hosted-provider adapters.
- [ ] Additional commercially compatible local multimodal adapters only where they satisfy existing task/evidence contracts.
- [ ] Provider capability declaration: text/tool/multimodal/context/streaming/cost.
- [ ] Provider failover policy.
- [ ] User-selectable provider/model settings UI.
- [ ] Per-task provider/model routing: planner vs OCR vs diarization vs visual extraction vs embeddings vs transcription.
- [ ] Routing decisions expose provider/model/version/cost/latency provenance into evidence/evaluation records where applicable.

---

# PHASE 8 — External content and publishing

## Goal
Extend VibeCut from editing local assets to governed acquisition and delivery.

- [ ] Stock footage search/import adapters (e.g. Pexels-class provider) behind explicit network authority.
- [ ] Image generation adapter.
- [ ] Video generation adapter.
- [ ] Music/SFX search providers.
- [ ] YouTube upload/publish adapter.
- [ ] Other publishing destinations.
- [ ] Upload metadata/title/description/thumbnail plan.
- [ ] External-side-effect approval before publication.
- [ ] Credential isolation per provider.
- [ ] Retry/idempotency/job tracking for uploads.

---

# PHASE 9 — Advanced editing systems

## Goal
Expand into areas that are major subsystems rather than simple tools.

- [ ] Richer title shape/image/template/brand-pack system after Phase 1 safe primitives and Phase 6 fixtures are stable.
- [ ] CapCut-style reusable meme/template system.
- [ ] Motion-graphics templates and parameterized title/effect packs.
- [ ] Multicam analysis and switching assistance.
- [ ] Advanced audio mix/ducking/normalization workflows, using gain/pan/solo and mix type/parameters only through proven safe Kdenlive backend seams.
- [ ] Color-match / look-transfer assistance.
- [ ] Proxy/offline workflow management.
- [ ] Sequence/nested-timeline operations.
- [ ] Bulk conform/reframe for landscape/vertical/square variants.

---

# PHASE 10 — Optional long-horizon platforms

These are intentionally not prerequisites for the governed agent editor.

- [ ] Fusion-style node compositor as a separate large subsystem over the same governance/job/provider contracts.
- [ ] TUI/secondary frontend over the same backend/runtime.
- [ ] Remote/headless editing service using the same EditPlan/tool contracts.
- [ ] Collaborative review/approval frontend.

---

# Release sequence

## R0 — Architecture branch
Current state: governed kernel + broad native editing vocabulary + quantitative mutation fixtures + rich audio/vision evidence foundations + semantic retrieval/duplicate understanding + proposal-only rough-cut/highlight/B-roll/pacing/narrative + editorial evaluation infrastructure, all source-landed. Not release-qualified until Phase 2 passes.

## R1 — Verified governed editor
Requires Phase 2 complete, including `timeline_range_remove`, repeated-take execution, learned-runtime, semantic-retrieval and proposal/evaluation smoke. Merge to `vibecut` only after clean compile/tests/smoke.

## R2 — Resilient project editor
Hardens Phase 3 source-health/relink/proxy/preflight with adversarial fixtures.

## R3 — Media-aware editor
Phase 4 source foundations are substantially present; R3 requires runtime verification, retrieval-quality/duplicate-ranking evaluation, stale-index hardening and any remaining useful camera/composition evidence.

## R4 — Editorial copilot
Proposal-only rough-cut/highlight/B-roll plus pacing/narrative/evaluation infrastructure is source-landed. R4 additionally requires representative evaluation cases/blinded reviews and Phase 6 measurement evidence; this does not itself imply autonomous execution.

## R5 — Autonomous governed editor
Requires the synthesis execution gate to be explicitly opened from measured evidence. Approved synthesis must translate through the same normal governed `EditPlan`/native-edit runtime with verification and Undo/Redo fidelity.

## R6+ — Ecosystem
Provider/task-routing breadth, acquisition/publishing, templates, advanced media systems and optional alternate frontends.

---

# Immediate execution order from current branch

1. **Keep the hard release gate authoritative:** compile/link and run all `vibecut*` tests with `scripts/vibecut-verify.sh` on the proper Kdenlive host; repair every failure before merge/release claims.
2. **Build representative evaluation data:** freeze real rough-cut/highlight/B-roll cases with `editorial_case_validate`, collect proposal-bound blinded reviews, and preserve disagreement.
3. **Quantify retrieval quality:** precision/recall fixtures for transcript/OCR semantic search, text→visual search and duplicate ranking; do not let downstream synthesis hide weak/unmeasured retrieval.
4. **Compare proposal/provider versions on identical frozen cases/evidence budgets** using reference agreement + human-review distributions + descriptive pacing/narrative measurements, not one opaque quality score.
5. **Keep synthesis execution CLOSED** until representative data plus host/runtime verification justify an explicit design review.
6. **Only after that evidence:** translate an explicitly approved proposal into the existing governed `EditPlan`/native edit primitives with standard authorization, verification and Undo; never add a parallel mutation path.
7. **Keep mutation evaluation cross-cutting:** every new destructive execution path gets explicit requested-postcondition and Undo/Redo fidelity fixtures.
8. **Presentation/audio breadth:** richer titles/templates/brand packs and mixer/mix editing only where safe Kdenlive backend seams are proven.
9. **Provider scale:** additional adapters and per-task routing only after task/evidence contracts remain stable across measured workloads.
10. **Merge to `vibecut` only after Phase 2 is green**, preserving the halthinks → original VibeCut → Kdenlive README lineage.

---

# Priority principle

The core product is the governed agent runtime plus native editing vocabulary plus media evidence/retrieval plus proposal-governed editorial synthesis. New work should be prioritized by **how much real editing time it removes while preserving inspectability, verification, Undo, truthfulness and human authority**. A capability is not considered finished merely because a model can propose it: consequential edits must have a safe native backend seam, live verification and a measurable recovery/Undo story. Likewise, a model score is not a fact: learned evidence, semantic similarity, synthesis comparison scores, reference agreement and human-review ratings remain explicitly provenance-bound and reviewable until a governed downstream path acts on them.
