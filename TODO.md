# VibeCut — Roadmap

Living implementation roadmap. `VIBECUT_ARCHITECTURE.md` is the authoritative architecture contract, `DESIGN_SPECS.md` contains standing product/behavior rules, and `CLAUDE.md` remains the operational handoff. This file tracks what is actually done versus what is still open.

## Landed on `agent/vibecut-architecture-slices`

- [x] **Subtitle read/search access.** `subtitles_search` returns stable subtitle ids, layers, text and frame ranges without mutating the project.
- [x] **Scope-safe subtitle generation.** `generate_subtitles` prefers explicit clip → selected clip → sole clip and refuses to silently transcribe an ambiguous multi-clip whole project. Whole-project scope must be explicit.
- [x] **Non-blocking subtitle audio export.** The VibeCut subtitle pipeline chains asynchronous MLT audio export → Whisper → import instead of blocking the GUI thread.
- [x] **Stale-result protection for long transcription.** Subtitle import checks captured project state before committing.
- [x] **Contextual next-step suggestions.** The dock offers deterministic follow-up actions instead of a meaningless flat Done state.
- [x] **Plan → authorize → execute-with-checkpoints.** Compound mutations become a revision-bound `EditPlan`; the deterministic runtime executes dependency order only after trust-policy authorization.
- [x] **Review / Auto / Turbo trust modes.** Governance metadata is separate from provider schemas. Reversible edits, major edits, external effects and irreversible work have distinct policies.
- [x] **Project revision / stale-plan gate.** Monotonic revision tracking survives undo → new-edit index reuse.
- [x] **Transactional synchronous checkpoints.** Contiguous project mutations use the Kdenlive undo stack; failed synchronous checkpoints roll back.
- [x] **Project before/after evidence.** Mutating plans capture coarse project snapshots and append a final diff.
- [x] **Shared asynchronous JobManager.** Stable job ids, state, progress, cancellation request and terminal result are available to plan execution and the UI.
- [x] **Whisper setup JobManager bridge.** Legacy setup now returns a trackable job so compound setup → subtitle plans can wait correctly.
- [x] **Bounded conversation context.** Complete model/tool exchanges are compacted without corrupting tool protocol.
- [x] **Project-local rules.** `.vibecutrules` is loaded beside the project with bounded size/error handling and cannot replace the immutable base governance instructions.
- [x] **Composable governed tool surface.** New capabilities live in isolated modules; native tools can be decorated without growing the legacy `vibecuttools.cpp` monolith.
- [x] **Lifecycle/context hooks.** `VibeCutHooks` exposes model/tool/plan/job/trust/error events plus named structured context providers.
- [x] **Model-provider registry seam.** Provider request construction is no longer hardwired into planning/tool logic; Anthropic remains the built-in provider.
- [x] **Media-intelligence index contract.** `media_search` retrieves time-ranged evidence across clip names and subtitle/transcript text; future extractors share the same document contract.
- [x] **Core native timeline edit vocabulary.** Verified `clip_move`, `clip_split`, `clip_trim`, `clip_ripple_trim`, and `clip_delete` use Kdenlive's own undoable APIs.
- [x] **Guides/range guides.** Read/add/remove project guides and range guides for candidate cuts, B-roll, semantic notes and review regions.
- [x] **Subtitle editing.** Verified `subtitle_edit` and `subtitle_delete` by stable subtitle id.
- [x] **Transitions.** Discover actual installed Kdenlive transition ids/names and insert verified compositions through the native controller.
- [x] **Native title creation.** Build a real Kdenlive title document/bin asset, insert it on the timeline, and verify both bin and timeline state.
- [x] **Native render/export baseline.** Discover installed presets and render asynchronously through `RenderRequest` / `kdenlive_render` with JobManager lifecycle and final-file verification.
- [x] **Local zero-CI verification lane.** `scripts/vibecut-verify.sh` configures/builds locally and runs the `vibecut*` tests with `ctest`; no GitHub Actions are required.
- [x] **Architecture/product front door.** README and `VIBECUT_ARCHITECTURE.md` now describe the real agentic editor instead of the original one-tool prototype.

## Immediate hardening before merge / upstream work

- [ ] **Run a clean local Kdenlive compile and VibeCut test gate.** Execute `bash scripts/vibecut-verify.sh` on a machine with Kdenlive build dependencies. Fix every compile/link/test failure before merging the integration branch. GitHub Actions are intentionally not part of this gate.
- [ ] **Hands-on smoke project.** Test inspect → plan → approve → edit → verify → Undo for move/split/trim/delete/guides/title/transition/subtitles.
- [ ] **Long-job smoke tests.** Run Whisper and render while interacting with the editor; test cancellation and final-state evidence.
- [ ] **Trust-mode smoke tests.** Verify Review, Auto and Turbo behavior with reversible edits, major edits, render, and explicitly confirmation-required operations.
- [ ] **Provider event normalization wiring.** `VibeCutModelProvider` exposes `normalizeStreamEvent`; the agent still needs to route parsed provider events through that hook so future providers can truly normalize their own stream format.
- [ ] **Expose job cancellation in the dock/tool surface.** JobManager already supports cancellation requests and render honors them; add the safe user-facing control/API.
- [ ] **Tighten output-overwrite transaction semantics.** `render_start` currently removes an explicitly approved existing output before render preparation; defer destructive replacement until render is actually ready to start where practical.
- [ ] **Update operational handoff.** Reconcile `CLAUDE.md` and any stale DEVLOG/KDENLIVE_INTERNALS notes against this branch after the first successful local build.

## Security / desktop integration

- [ ] **KWallet-backed secret store + settings UI.** Environment variables remain the bootstrap path. Add an optional KDE Wallet secret provider and a user-facing provider/key settings surface without storing credentials in projects/source.
- [ ] **Per-tool allow/deny overrides.** Review/Auto/Turbo exists globally; add user/project overrides for specific governed tools.
- [ ] **Agent-generated project memory.** `.vibecutrules` handles explicit user rules; add durable agent-generated project knowledge separately, with provenance and bounded retrieval rather than silently rewriting rules.

## VibeScript

- [ ] **QJSEngine sandbox.** Build the genuine scriptable escape hatch as an isolated sandbox, not a raw Native-mode shell.
- [ ] **Validated output contract.** VibeScript may inspect explicitly supplied project context and emit proposed plans/artifacts; real project changes must still go through `VibeCutToolSurface` / `VibeCutPlanRuntime`.
- [ ] **Resource/time limits and safe host bindings.** No arbitrary QObject exposure, filesystem/network/process access by default.

## Editing breadth still to deepen

- [ ] Effect parameter discovery/editing beyond the current denoise allowlist.
- [ ] Transition edit/remove and mix-specific operations.
- [ ] Title edit/update after creation, reusable title styles/templates, and richer layout primitives.
- [ ] Bin/media insertion, replacement and relinking through governed native APIs.
- [ ] Group/ungroup and multi-selection operations.
- [ ] Track creation/deletion/move/lock/mute/visibility and audio routing controls.
- [ ] Composition/effect stack introspection so the agent can answer exactly what is already applied.
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

- [ ] Ensure Whisper `turbo` is downloaded on the actual test machine (code already prefers/supports it; repository state cannot guarantee host model cache contents).
- [ ] Remove/retire vestigial `speech_system_python` / `speech_system_python_path` config only after confirming no remaining upstream path depends on them.
- [ ] Remove any one-off manual Whisper test venvs on the host after the VibeCut-owned environment is verified.

## Priority principle

The core product is now the governed agent runtime plus native editing vocabulary. New work should be prioritized by **how much real editing time it removes while preserving inspectability, verification, undo and human authority**, not by how flashy the individual feature sounds.
