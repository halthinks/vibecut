# VibeCut — Roadmap

Living implementation roadmap. `VIBECUT_ARCHITECTURE.md` is the authoritative architecture contract, `DESIGN_SPECS.md` contains standing product/behavior rules, and `CLAUDE.md` remains the operational handoff. This file tracks what is actually done versus what is still open.

## Landed on `agent/vibecut-architecture-slices`

- [x] **Subtitle read/search access.** `subtitles_search` returns stable subtitle ids, layers, text and frame ranges without mutating the project.
- [x] **Scope-safe subtitle generation.** `generate_subtitles` prefers explicit clip → selected clip → sole clip and refuses to silently transcribe an ambiguous multi-clip whole project. Whole-project scope must be explicit.
- [x] **Non-blocking subtitle audio export.** The VibeCut subtitle pipeline chains asynchronous MLT audio export → Whisper → import instead of blocking the GUI thread.
- [x] **Stale-result protection for long transcription.** Subtitle import checks captured project state before committing.
- [x] **Contextual next-step suggestions.** The dock offers deterministic follow-up actions instead of a meaningless flat Done state.
- [x] **Plan → authorize → execute-with-checkpoints.** Compound mutations become a revision-bound `EditPlan`; the deterministic runtime executes dependency order only after trust-policy authorization.
- [x] **Review / Auto / Turbo trust modes.** Reversible edits, major edits, external effects and irreversible work have distinct policies.
- [x] **Per-tool project policy overrides.** `.vibecutpolicy.json` supports `deny`, `always_confirm`, and `auto_allow`; denied tools are hidden from model schemas and rejected again by the execution gate. Irreversible confirmation cannot be bypassed.
- [x] **Project revision / stale-plan gate.** Monotonic revision tracking survives undo → new-edit index reuse.
- [x] **Transactional synchronous checkpoints.** Contiguous project mutations use the Kdenlive undo stack; failed synchronous checkpoints roll back.
- [x] **Project before/after evidence.** Mutating plans capture coarse project snapshots and append a final diff.
- [x] **Shared asynchronous JobManager.** Stable job ids, state, progress, cancellation request and terminal result are available to plan execution and UI/tool surfaces.
- [x] **Governed job cancellation API.** `job_cancel` requests cancellation through JobManager; cancellable implementations such as render honor the request.
- [x] **Whisper setup JobManager bridge.** Legacy setup now returns a trackable job so compound setup → subtitle plans can wait correctly.
- [x] **Bounded conversation context.** Complete model/tool exchanges are compacted without corrupting tool protocol.
- [x] **Project-local rules.** `.vibecutrules` is loaded beside the project with bounded size/error handling and cannot replace immutable base governance instructions.
- [x] **Durable project memory.** `.vibecutmemory.json` stores bounded provenance-labelled project facts separately from rules/chat; malformed or unsupported memory fails closed. `project_memory_list/put/forget` are governed tools.
- [x] **Composable governed tool surface.** New capabilities live in isolated modules; native tools can be decorated without growing the legacy `vibecuttools.cpp` monolith.
- [x] **Lifecycle/context hooks.** `VibeCutHooks` exposes model/tool/plan/job/trust/error events plus named structured context providers.
- [x] **Model-provider registry seam.** Provider request construction and streaming-event normalization are provider-owned; Anthropic remains the built-in provider.
- [x] **Optional KWallet secret backend + dock credential control.** Anthropic credentials load from the environment first and then the VibeCut KWallet folder when KF Wallet is present. The dock can write the key to KWallet and hot-reload the provider without restart; credentials never enter projects, model context or hooks.
- [x] **VibeScript bounded sandbox.** `vibescript_plan` evaluates JavaScript in a no-host-access `QJSEngine`, enforces source/time bounds including infinite-loop interruption, requires a JSON plan result, and submits that result to the same governed plan runtime. Scripts receive no QObject/filesystem/network/process/Kdenlive bindings.
- [x] **Media-intelligence index contract.** `media_search` retrieves time-ranged evidence across clip names and subtitle/transcript text; future extractors share the same document contract.
- [x] **Core native timeline edit vocabulary.** Verified `clip_move`, `clip_split`, `clip_trim`, `clip_ripple_trim`, and `clip_delete` use Kdenlive's own undoable APIs.
- [x] **Effect-stack introspection and editing.** `effects_inspect` exposes the live clip effect stack with stable effect ids, rows, parameters and XML; `effect_remove` and `effect_parameter_set` are verified and undoable, including duplicate-effect row identity.
- [x] **Guides/range guides.** Read/add/remove project guides and range guides for candidate cuts, B-roll, semantic notes and review regions.
- [x] **Subtitle editing.** Verified `subtitle_edit` and `subtitle_delete` by stable subtitle id.
- [x] **Transitions.** Discover actual installed Kdenlive transition ids/names and insert verified compositions through the native controller.
- [x] **Native title creation.** Build a real Kdenlive title document/bin asset, insert it on the timeline, and verify both bin and timeline state.
- [x] **Native render/export baseline.** Discover installed presets and render asynchronously through `RenderRequest` / `kdenlive_render` with JobManager lifecycle and final-file verification.
- [x] **Safer render overwrite semantics.** Existing approved output is not removed before Kdenlive has successfully prepared render jobs.
- [x] **Local zero-CI verification lane.** `scripts/vibecut-verify.sh` configures/builds locally and runs the `vibecut*` tests with `ctest`; no GitHub Actions are required.
- [x] **Architecture/product front door.** README and `VIBECUT_ARCHITECTURE.md` describe the governed agentic editor instead of the original one-tool prototype.
- [x] **Expanded regression harness.** Contracts, planning, trust, revision, context compaction, jobs, hooks, media index, native tool registration, effects, VibeScript watchdog, durable memory, and capability-policy uniqueness are in the local test target.

## Immediate hardening before merge / upstream work

- [ ] **Run a clean local Kdenlive compile and VibeCut test gate.** Execute `bash scripts/vibecut-verify.sh` on a machine with Kdenlive build dependencies. Fix every compile/link/test failure before merging the integration branch. GitHub Actions are intentionally not part of this gate.
- [ ] **Hands-on smoke project.** Test inspect → plan → approve → edit → verify → Undo for move/split/trim/delete/effects/guides/title/transition/subtitles.
- [ ] **Long-job smoke tests.** Run Whisper and render while interacting with the editor; test cancellation and final-state evidence.
- [ ] **Trust/policy smoke tests.** Verify Review, Auto, Turbo and `.vibecutpolicy.json` behavior with reversible edits, major edits, render, deny, auto-allow and always-confirm overrides.
- [ ] **Update operational handoff.** Reconcile `CLAUDE.md` and stale DEVLOG/KDENLIVE_INTERNALS notes against this branch after the first successful local build.

## Editing breadth still to deepen

- [ ] Arbitrary installed-effect discovery/application beyond the current denoise application allowlist. Existing applied effects can already be inspected, parameter-edited and removed.
- [ ] Transition edit/remove and mix-specific operations.
- [ ] Title edit/update after creation, reusable title styles/templates, and richer layout primitives.
- [ ] Bin/media insertion, replacement and relinking through governed native APIs.
- [ ] Group/ungroup and multi-selection operations.
- [ ] Track creation/deletion/move/lock/mute/visibility and audio routing controls.
- [ ] Render output optimization helper that recommends installed presets based on destination constraints rather than just exposing preset selection.

## Media intelligence

The common `VibeCutMediaIndex` exists. The remaining work is extractor depth, not another retrieval architecture.

- [ ] Speaker diarization and speaker-indexed transcript evidence.
- [ ] Scene/shot boundary extraction.
- [ ] Silence, loudness/noise and general audio-event analysis.
- [ ] OCR / on-screen text extraction.
- [ ] Face/subject/object evidence where appropriate and privacy-safe.
- [ ] Visual/CLIP embeddings and reference-style similarity.
- [ ] Semantic transcript/clip embeddings and persistent incremental indexing.
- [ ] Evidence provenance/versioning for derived media-analysis records.

## Higher-level agent editing

- [ ] Interview/repeated-take cleanup from transcript + media evidence.
- [ ] Rough-cut generation with reviewable candidate-cut plans.
- [ ] Shorts/highlight extraction.
- [ ] B-roll opportunity detection and guide placement.
- [ ] Auto color-grade preset selection with before/after review.
- [ ] Reference-style matching using the media-intelligence layer.
- [ ] Finishing passes: titles, transitions, subtitles, loudness/audio cleanup, render recommendation.

## External integrations / feature wishlist

- [ ] Stock footage search/import (for example Pexels adapter) behind explicit external/network authority.
- [ ] Image/video generation provider adapters.
- [ ] Ollama/local-model provider integration using the provider registry.
- [ ] Local WebUI/provider integrations where useful.
- [ ] YouTube/publishing adapters with explicit external-side-effect approval and credential isolation.
- [ ] CapCut-style reusable meme/template system.
- [ ] Fusion-style node compositor — large standalone subsystem, not required for the governed agent kernel.
- [ ] TUI/secondary frontend — another frontend over the same agent/runtime contracts, not a second backend.

## Machine-specific cleanup

- [ ] Ensure Whisper `turbo` is downloaded on the actual test machine (code prefers/supports it; repository state cannot guarantee host model cache contents).
- [ ] Remove/retire vestigial `speech_system_python` / `speech_system_python_path` config only after confirming no remaining upstream path depends on them.
- [ ] Remove one-off manual Whisper test venvs on the host after the VibeCut-owned environment is verified.

## Priority principle

The core product is the governed agent runtime plus native editing vocabulary. New work should be prioritized by **how much real editing time it removes while preserving inspectability, verification, undo and human authority**, not by how flashy the individual feature sounds.
