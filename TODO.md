# VibeCut — Full Product Roadmap

Living implementation roadmap for the governed agentic video editor. `VIBECUT_ARCHITECTURE.md` is the architecture contract, `DESIGN_SPECS.md` contains product/behavior rules, and `CLAUDE.md` remains the operational handoff.

This roadmap is ordered by dependency and product value. A later phase may be researched before an earlier one is complete, but **merge/release authority follows the gates below**. GitHub Actions are intentionally not part of the validation path; verification is repository-local.

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
- [x] Local zero-CI verification script: `scripts/vibecut-verify.sh`.

## User outcome
The AI is no longer trusted just because it generated text. Plans are state-bound, governed, undo-aware and verifiable.

---

# PHASE 1 — Native professional editing vocabulary — MOSTLY LANDED

## Goal
Give the governed runtime enough authoritative Kdenlive-native tools to perform real editing rather than special-case demos.

## Timeline editing — LANDED
- [x] `clip_move`.
- [x] `clip_split`.
- [x] `clip_trim`.
- [x] `clip_ripple_trim`.
- [x] `clip_delete`.
- [x] Stable item selection inspection/set/clear.
- [x] Native group/ungroup.

## Guides and subtitles — LANDED
- [x] Guide/range-guide add/read/remove.
- [x] Subtitle search/read.
- [x] Scope-safe subtitle generation.
- [x] Non-blocking audio export → Whisper → subtitle import.
- [x] Stale-result protection before subtitle import.
- [x] Subtitle edit/delete by stable ID.

## Effects — BASELINE LANDED
- [x] Enumerate installed effects instead of inventing IDs.
- [x] Add individual installed effect.
- [x] Inspect effect stack with row, stable effect ID, parameters and Kdenlive XML.
- [x] Edit existing effect parameters with undo/redo and verification.
- [x] Remove effect with duplicate-row-safe identity.
- [ ] Expand/apply effect groups with child-by-child verification.
- [ ] Keyframe-aware parameter editing as a first-class governed tool set.
- [ ] Effect-stack copy/paste presets across selected clips with evidence of every child operation.

## Transitions/compositions — BASELINE LANDED
- [x] Discover installed transitions/compositions.
- [x] Add transition/composition.
- [x] Move transition/composition.
- [x] Resize transition/composition.
- [x] Remove transition/composition.
- [ ] Inspect/edit transition parameter models.
- [ ] A-track / composition-track assignment controls.

## Same-track mixes — BASELINE LANDED
- [x] `mix_inspect`.
- [x] Conservative previous-neighbor `mix_add_previous`.
- [x] `mix_resize`.
- [x] `mix_remove`.
- [ ] Mix type switching.
- [ ] Mix parameter editing.
- [ ] Explicit previous/next-neighbor ownership operations.
- [ ] Mix alignment/cut controls exposed directly rather than only through resize.

## Titles — SAFE BASELINE LANDED
- [x] Create a real Kdenlive title asset and insert it in timeline.
- [x] Safely update VibeCut-created simple titles.
- [ ] Inspect arbitrary title document elements.
- [ ] Edit specific text/shape/image elements without replacing the whole title.
- [ ] Reusable title styles/templates.
- [ ] Lower-third/title-card primitives.
- [ ] Brand/style packs.

## Tracks — BASELINE LANDED
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
- [ ] Rich mixer controls: gain, pan/balance, monitor/record state, master/track effects.
- [ ] More explicit routing state for multi-stream sources and future inserts.

## Bin/media — STRONG BASELINE LANDED
- [x] Bin inventory with IDs, paths, type, duration, A/V capability and instance count.
- [x] Local-file import with native undo.
- [x] Insert existing bin asset into timeline.
- [x] Replace source behind an existing file-backed bin asset as undoable MajorEdit.
- [x] Reject generator/title/non-file-backed assets from file replacement.
- [x] Folder inventory.
- [x] Folder creation with native undo.
- [x] Move existing clip between folders with `MoveBinClipCommand` and parent verification.
- [ ] Missing-media/source-state inspection with actionable diagnostics.
- [ ] Missing-media recovery/relink workflow supporting one clip and batches.
- [ ] Proxy/source-state inspection and governed proxy actions.
- [ ] Bin clip rename/description/tag/rating operations where useful to editorial search.
- [ ] Folder move/delete with cycle/dependency checks.

## Render/export — BASELINE LANDED
- [x] Discover installed render presets.
- [x] Native `RenderRequest` / `kdenlive_render` execution.
- [x] Shared JobManager progress/cancellation.
- [x] Final output existence/non-empty verification.
- [x] Safer overwrite: do not remove existing approved output until job preparation succeeds.
- [ ] Destination-aware render recommendation.
- [ ] Preflight checks: missing media, unsupported preset, insufficient output path, project warnings.
- [ ] Export profile policy: YouTube, review proxy, archive master, social vertical, audio-only, etc.

---

# PHASE 2 — HARD GATE: compile, tests, smoke and repair — NEXT RELEASE BLOCKER

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
- [ ] Effects inspect/add/edit/remove.
- [ ] Guides.
- [ ] Subtitle search/generate/edit/delete.
- [ ] Titles create/update.
- [ ] Transitions and same-track mixes.
- [ ] Bin import/insert/replace/folders/move.
- [ ] Track create/rename/move/lock/mute-hide/delete/routing.
- [ ] Render/start/cancel/verify.
- [ ] Whisper/start/cancel/final import.

## Governance smoke matrix
- [ ] Review mode asks where required.
- [ ] Auto mode runs safe reversible work while respecting major/external gates.
- [ ] Turbo still honors irreversible confirmation.
- [ ] `.vibecutpolicy.json` deny hides tool from schemas and rejects direct invocation.
- [ ] `always_confirm` overrides mode.
- [ ] `auto_allow` cannot bypass irreversible safety.
- [ ] Stale plan rejects after user/project mutation.
- [ ] External-only job does not open a project undo macro.

## Acceptance gate
**Only after the entire Phase 2 gate passes:** merge the integration branch to `vibecut`, update operational handoff docs, and decide whether to create an upstream PR.

---

# PHASE 3 — Source health and project resilience

## Goal
Make VibeCut dependable on real-world messy editing projects.

- [ ] `bin_source_inspect`: source exists, canonical path, original/proxy path, status, proxy state, A/V capabilities, duration and timeline use.
- [ ] `bin_missing_list`: enumerate missing/offline assets.
- [ ] `bin_relink_source`: governed relink for missing assets with type/path validation.
- [ ] Batch relink by directory/path mapping with reviewable plan.
- [ ] Project preflight: missing media, empty output dir, unsupported render, stale proxy, offline generated asset, unrenderable state.
- [ ] Bin/project normalization checks before long operations.
- [ ] Better checkpoint evidence for source-replacement and relink operations.

### User outcome
A user can open a broken project and ask: “What is missing, where was it expected, and fix everything you can from this folder.”

---

# PHASE 4 — Rich media intelligence

## Goal
Move from understanding timeline structure/transcripts to understanding audiovisual content.

## Common evidence contract
- [x] `VibeCutMediaIndex` common search/document contract.
- [ ] Evidence provenance/version/producer model.
- [ ] Incremental extractor refresh keyed by media hash + extractor version.
- [ ] Derived-evidence confidence/quality fields.
- [ ] Persistent media intelligence cache separate from conversational memory.

## Audio/speech extractors
- [ ] Speaker diarization.
- [ ] Speaker naming/identity association when user supplies labels.
- [ ] Silence/dead-air detection.
- [ ] Loudness measurements and clipping detection.
- [ ] Noise/room-tone characterization.
- [ ] Music/speech/audio-event segmentation.
- [ ] Repeated-take similarity from transcript/audio evidence.

## Visual extractors
- [ ] Shot/scene boundary detection.
- [ ] OCR/on-screen text.
- [ ] Face/person/subject evidence where appropriate and privacy-safe.
- [ ] Object/location/action tags.
- [ ] Camera-motion/shot-scale/composition descriptors.
- [ ] Visual embeddings / CLIP-style similarity.
- [ ] Duplicate/near-duplicate shot detection.
- [ ] Blur/black-frame/flash/freeze/error-frame detection.

## Semantic retrieval
- [ ] Transcript embeddings.
- [ ] Visual-semantic embeddings.
- [ ] Cross-modal search.
- [ ] Search ranking by exact text + semantic similarity + editorial relevance + recency/quality.

### User outcome
Requests like “find the cleanest answer from Sarah,” “find every shot of the engine housing,” or “show dead air longer than 600 ms” become evidence-backed operations.

---

# PHASE 5 — Editorial reasoning and autonomous edit synthesis

## Goal
Stop making the user specify frame-level commands and begin solving editorial problems while still producing reviewable deterministic plans.

- [ ] Interview/repeated-take cleanup.
- [ ] Silence/dead-air cleanup with configurable editorial cadence.
- [ ] Rough-cut generation from transcript + scene + quality evidence.
- [ ] Highlight/shorts extraction.
- [ ] B-roll opportunity detection.
- [ ] B-roll guide placement before mutation.
- [ ] Candidate cut scoring and alternative plans.
- [ ] Narrative ordering/sectioning suggestions.
- [ ] Speaker balance and pacing diagnostics.
- [ ] Basic continuity warnings.
- [ ] Auto finishing pass: subtitles, titles, transitions, audio cleanup, render recommendation.
- [ ] Reference-style matching based on extracted visual/audio style evidence.

## Acceptance principle
The model may propose high-level editorial intent, but final execution still resolves to explicit governed native tool operations with preconditions and evidence.

---

# PHASE 6 — Evaluation and quality assurance

## Goal
Make VibeCut performance measurable rather than anecdotal.

- [x] Evaluation harness seam.
- [ ] Golden editing fixtures/projects.
- [ ] Plan correctness tests.
- [ ] Stale-plan regression tests.
- [ ] Undo fidelity tests.
- [ ] Tool hallucination rate tests.
- [ ] “Claimed success vs verified success” metric.
- [ ] Long-job cancellation correctness.
- [ ] Media-search precision/recall fixtures.
- [ ] Rough-cut quality rubric and human review harness.
- [ ] Provider comparison using same tasks/evidence budget.
- [ ] Token/latency/cost accounting by editing outcome.
- [ ] Regression scorecard required for releases.

---

# PHASE 7 — Provider and deployment ecosystem

## Goal
Let users choose intelligence providers without changing the editor architecture.

- [x] Provider registry seam.
- [x] Built-in Anthropic path.
- [ ] Ollama/local model adapter.
- [ ] OpenAI adapter if desired.
- [ ] Other hosted-provider adapters.
- [ ] Local multimodal model adapter for extraction tasks.
- [ ] Provider capability declaration: text/tool/multimodal/context/streaming/cost.
- [ ] Provider failover policy.
- [ ] User-selectable provider/model settings UI.
- [ ] Per-task provider routing: planner vs media extractor vs transcription.

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

- [ ] CapCut-style reusable meme/template system.
- [ ] Motion-graphics templates and parameterized title/effect packs.
- [ ] Multicam analysis and switching assistance.
- [ ] Advanced audio mix/ducking/normalization workflows.
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
Current state: governed kernel + broad native editing vocabulary. Not release-qualified until Phase 2 passes.

## R1 — Verified governed editor
Requires Phase 2 complete. Merge to `vibecut` only after clean compile/tests/smoke.

## R2 — Resilient project editor
Adds Phase 3 missing-media/source-health/preflight.

## R3 — Media-aware editor
Adds Phase 4 extractors and persistent evidence.

## R4 — Editorial copilot
Adds first Phase 5 rough-cut/cleanup/highlight workflows with evaluation gates.

## R5 — Autonomous governed editor
Large multi-step editorial plans, finishing passes and evidence-backed review.

## R6+ — Ecosystem
Provider breadth, acquisition/publishing, templates, advanced media systems and optional alternate frontends.

---

# Immediate execution order from current branch

1. **Source health:** implement source inspection / missing-media inventory first because it is low-risk, directly useful, and feeds render/preflight reliability.
2. **Local compile/test gate:** run `scripts/vibecut-verify.sh`; repair every compile/link/test failure before increasing C++ breadth further.
3. **Hands-on smoke matrix** across every native tool family.
4. **Operational docs** (`CLAUDE.md`, DEVLOG/KDENLIVE_INTERNALS) reconciled to the verified build.
5. **Merge to `vibecut`** only after the gate passes.
6. Begin **media intelligence** with evidence/versioning + silence/shot/transcript-derived features before higher-level autonomous editing.

---

# Priority principle

The core product is the governed agent runtime plus native editing vocabulary plus media evidence. New work should be prioritized by **how much real editing time it removes while preserving inspectability, verification, undo, truthfulness and human authority**.