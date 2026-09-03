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
- [ ] Duplicate-fusion candidate smoke with missing-evidence disclosure.

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

# PHASE 4 — Rich media intelligence — MAJOR SOURCE FOUNDATION LANDED; SEMANTIC RETRIEVAL ACTIVE

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

## Semantic retrieval — ACTIVE SOURCE BASELINE
- [x] MiniLM transcript/OCR text embeddings in a pinned 384-D model space with revision-bound atomic refresh.
- [x] MiniLM asynchronous text semantic search with exact model-space cosine semantics and canonical media-document anchors.
- [x] SigLIP sampled-frame visual embeddings in a pinned shared 768-D image/text space.
- [x] SigLIP text→visual cross-modal semantic search over current source-fingerprint visual anchors.
- [x] Semantic and cross-modal tools are exposed through the normal `VibeCutToolSurface`, not merely compiled as orphan code.
- [x] Stronger pairwise duplicate/near-duplicate candidate fusion combining current MPEG-7 evidence, SigLIP visual similarity, temporal ordering, MiniLM source-text similarity, transcript/OCR lexical overlap and duration similarity.
- [x] Duplicate-fusion scores disclose available evidence/coverage/missing work and are explicitly derived similarity candidates, not probabilities or duplicate facts.
- [ ] MiniLM source-backed search pre-filtering should exclude stale source fingerprints before ranking; current output identifies stale anchors with `anchor_current` and project revision guards, but pre-filtering remains a hardening item.
- [ ] Hybrid lexical + MiniLM + SigLIP + editorial relevance/freshness ranking in one reviewable search surface.
- [ ] Project-wide bounded duplicate-candidate discovery over the pairwise fusion primitive.
- [ ] Media-search precision/recall and duplicate-ranking fixtures before editorial synthesis consumes semantic retrieval automatically.

### User outcome
Requests like “find the cleanest answer from Sarah,” “find every shot of the engine housing,” or “show every on-screen mention of the launch date” now have source implementations for the required evidence/search foundations. Release-quality claims remain gated on Phase 2 runtime verification and quantitative retrieval evaluation.

---

# PHASE 5 — Editorial reasoning and autonomous edit synthesis — NEXT PRODUCT EDGE AFTER RETRIEVAL HARDENING

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

## Editorial synthesis — BUILD IN THIS ORDER
- [ ] Rough-cut synthesis from transcript + scene + audiovisual quality + semantic retrieval evidence.
- [ ] Highlights/shorts extraction with explicit objective/rubric and source-range provenance.
- [ ] B-roll opportunity detection and candidate retrieval.
- [ ] B-roll planning/guide placement before mutation; execution remains a separate governed plan.
- [ ] Pacing analysis: shot length, silence, speaker balance, rhythm and density.
- [ ] Narrative analysis: sections, topic progression, repetition, continuity and alternative orderings.
- [ ] Candidate cut scoring and alternative reviewable plans.
- [ ] Basic continuity warnings.
- [ ] Auto finishing pass: subtitles, richer titles/templates, supported transitions/mixes, audio cleanup and render recommendation.
- [ ] Reference-style matching based on extracted visual/audio style evidence.

## Acceptance principle
The model may propose high-level editorial intent, but final execution still resolves to explicit governed native tool operations with preconditions and evidence.

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
- [ ] Extend mutation fixtures across effects, transitions, titles, relink/proxy, render and future synthesis families.
- [ ] Plan correctness / tool hallucination rate regression suites.
- [ ] Long-job cancellation correctness across learned model/process trees.
- [ ] OCR/diarization/audio/vision accuracy/calibration fixtures.
- [ ] Media-search precision/recall and duplicate-fusion ranking fixtures.
- [ ] Rough-cut/highlight quality rubrics and blinded human review harness.
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
Current state: governed kernel + broad native editing vocabulary + quantitative mutation fixtures + rich audio/vision evidence foundations + semantic text/visual retrieval + pairwise duplicate fusion, all source-landed. Not release-qualified until Phase 2 passes.

## R1 — Verified governed editor
Requires Phase 2 complete, including `timeline_range_remove`, repeated-take execution, learned-runtime and semantic-retrieval smoke. Merge to `vibecut` only after clean compile/tests/smoke.

## R2 — Resilient project editor
Hardens Phase 3 source-health/relink/proxy/preflight with adversarial fixtures.

## R3 — Media-aware editor
Phase 4 source foundations are substantially present; R3 requires runtime verification, retrieval-quality/duplicate-ranking evaluation, stale-index hardening and any remaining useful camera/composition evidence.

## R4 — Editorial copilot
Adds first Phase 5 rough-cut/highlight/B-roll/pacing/narrative workflows with Phase 6 evaluation gates.

## R5 — Autonomous governed editor
Large multi-step editorial plans, finishing passes and evidence-backed review.

## R6+ — Ecosystem
Provider/task-routing breadth, acquisition/publishing, templates, advanced media systems and optional alternate frontends.

---

# Immediate execution order from current branch

1. **Keep the hard release gate authoritative:** compile/link and run all `vibecut*` tests with `scripts/vibecut-verify.sh` on the proper Kdenlive host; repair every failure before merge/release claims.
2. **Finish semantic retrieval hardening:** pre-filter stale MiniLM source fingerprints, add hybrid lexical/semantic/freshness ranking, and add bounded project-wide duplicate-candidate discovery over `media_duplicate_fusion`.
3. **Quantify retrieval quality:** precision/recall fixtures for transcript/OCR semantic search, text→visual search and duplicate ranking; do not let editorial synthesis consume weak/unmeasured retrieval automatically.
4. **Begin editorial synthesis on reviewable plans:** rough-cut candidate synthesis → highlights/shorts → B-roll opportunities/retrieval/planning → pacing/narrative analysis.
5. **Keep mutation evaluation cross-cutting:** every new destructive/synthesizing execution path gets explicit requested-postcondition and Undo/Redo fidelity fixtures.
6. **Presentation/audio breadth:** richer titles/templates/brand packs and mixer/mix editing only where safe Kdenlive backend seams are proven.
7. **Provider scale:** additional adapters and per-task routing only after task/evidence contracts remain stable across measured workloads.
8. **Merge to `vibecut` only after Phase 2 is green**, preserving the halthinks → original VibeCut → Kdenlive README lineage.

---

# Priority principle

The core product is the governed agent runtime plus native editing vocabulary plus media evidence and retrieval. New work should be prioritized by **how much real editing time it removes while preserving inspectability, verification, Undo, truthfulness and human authority**. A capability is not considered finished merely because a model can propose it: consequential edits must have a safe native backend seam, live verification and a measurable recovery/Undo story. Likewise, a model score is not a fact: learned evidence and semantic similarity remain explicitly calibrated, source-bound and reviewable until a governed downstream path acts on them.
